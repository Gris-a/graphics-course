#include "RockRenderer.hpp"

#include <etna/Etna.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <glm/ext.hpp>
#include <imgui.h>
#include <tiny_gltf.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <span>
#include <stdexcept>

namespace
{
constexpr float WORLD_SIZE = 20.0f;
constexpr float HEIGHT_SCALE = 6.0f;
constexpr float CELL_SIZE = 0.1f;
constexpr int CELL_COUNT = 200;

uint32_t hash_cell(uint32_t value)
{
  value ^= value >> 16;
  value *= 0x7feb352du;
  value ^= value >> 15;
  value *= 0x846ca68bu;
  return value ^ (value >> 16);
}

float random_value(uint32_t& state)
{
  state = hash_cell(state + 0x9e3779b9u);
  return static_cast<float>(state >> 8) / 16777216.0f;
}

template <typename Value>
Value read_accessor(const tinygltf::Model& model, int accessor_index, std::size_t index)
{
  const auto& accessor = model.accessors.at(accessor_index);
  const auto& view = model.bufferViews.at(accessor.bufferView);
  const auto stride = accessor.ByteStride(view);
  if (stride < static_cast<int>(sizeof(Value)) || index >= accessor.count)
    throw std::runtime_error("Invalid rock accessor");
  const auto offset =
    view.byteOffset + accessor.byteOffset + index * static_cast<std::size_t>(stride);
  const auto& bytes = model.buffers.at(view.buffer).data;
  if (offset + sizeof(Value) > bytes.size())
    throw std::runtime_error("Rock accessor exceeds buffer");
  Value result;
  std::memcpy(&result, bytes.data() + offset, sizeof(Value));
  return result;
}

float terrain_height(const HeightmapGenerator& heightmap, glm::vec2 position)
{
  const glm::vec2 grid = (position / WORLD_SIZE + 0.5f) * 256.0f;
  const glm::vec2 base = glm::floor(grid);
  const glm::vec2 local = grid - base;
  const float h00 = heightmap.sample(base / 256.0f);
  const float h10 = heightmap.sample((base + glm::vec2{1, 0}) / 256.0f);
  const float h01 = heightmap.sample((base + glm::vec2{0, 1}) / 256.0f);
  const float h11 = heightmap.sample((base + glm::vec2{1, 1}) / 256.0f);
  if (local.x + local.y <= 1.0f)
    return h00 + local.x * (h10 - h00) + local.y * (h01 - h00);
  return h11 + (1.0f - local.x) * (h01 - h11) + (1.0f - local.y) * (h10 - h11);
}
}

void RockRenderer::load(
  etna::BlockingTransferHelper& transfer_helper, etna::OneShotCmdMgr& command_manager)
{
  auto& context = etna::get_context();
  sampler = etna::Sampler(
    etna::Sampler::CreateInfo{
      .filter = vk::Filter::eLinear,
      .addressMode = vk::SamplerAddressMode::eRepeat,
      .name = "rock_sampler"});
  const std::array<std::string, 2> names{"boulder_01", "rock_09"};
  for (std::size_t m = 0; m < models.size(); ++m)
  {
    auto& target = models[m];
    tinygltf::Model source;
    tinygltf::TinyGLTF loader;
    std::string error;
    std::string warning;
    const auto path = std::filesystem::path{INSTANCING_TEXTURES_ROOT} / (names[m] + "_1k") /
      (names[m] + "_1k.gltf");
    if (!loader.LoadASCIIFromFile(&source, &error, &warning, path.string()))
      throw std::runtime_error("Cannot load rock: " + path.string() + ": " + error);
    const auto& primitive = source.meshes.at(0).primitives.at(0);
    const int positions = primitive.attributes.at("POSITION");
    const int normals = primitive.attributes.at("NORMAL");
    const int uvs = primitive.attributes.at("TEXCOORD_0");
    if (
      primitive.mode != TINYGLTF_MODE_TRIANGLES ||
      source.accessors.at(positions).componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
      source.accessors.at(normals).componentType != TINYGLTF_COMPONENT_TYPE_FLOAT ||
      source.accessors.at(uvs).componentType != TINYGLTF_COMPONENT_TYPE_FLOAT)
      throw std::runtime_error("Unsupported rock vertex format");
    std::vector<RockVertex> vertices(source.accessors.at(positions).count);
    glm::vec3 lower{std::numeric_limits<float>::max()};
    glm::vec3 upper{std::numeric_limits<float>::lowest()};
    for (std::size_t i = 0; i < vertices.size(); ++i)
    {
      vertices[i] = {
        read_accessor<glm::vec3>(source, positions, i),
        read_accessor<glm::vec3>(source, normals, i),
        read_accessor<glm::vec2>(source, uvs, i)};
      lower = glm::min(lower, vertices[i].position);
      upper = glm::max(upper, vertices[i].position);
    }
    const glm::vec3 center = (lower + upper) * 0.5f;
    const float diameter = std::max({upper.x - lower.x, upper.y - lower.y, upper.z - lower.z});
    if (diameter <= 0.0f)
      throw std::runtime_error("Degenerate rock mesh");
    for (auto& vertex : vertices)
      vertex.position = (vertex.position - center) / diameter;
    const auto& accessor = source.accessors.at(primitive.indices);
    std::vector<uint32_t> indices(accessor.count);
    for (std::size_t i = 0; i < indices.size(); ++i)
    {
      switch (accessor.componentType)
      {
      case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        indices[i] = read_accessor<uint8_t>(source, primitive.indices, i);
        break;
      case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        indices[i] = read_accessor<uint16_t>(source, primitive.indices, i);
        break;
      case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        indices[i] = read_accessor<uint32_t>(source, primitive.indices, i);
        break;
      default:
        throw std::runtime_error("Unsupported rock index format");
      }
      if (indices[i] >= vertices.size())
        throw std::runtime_error("Rock index exceeds vertex count");
    }
    target.indexCount = static_cast<uint32_t>(indices.size());
    target.vertices = context.createBuffer(
      etna::Buffer::CreateInfo{
        .size = vertices.size() * sizeof(RockVertex),
        .bufferUsage =
          vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
        .name = "rock_vertices"});
    target.indices = context.createBuffer(
      etna::Buffer::CreateInfo{
        .size = indices.size() * sizeof(uint32_t),
        .bufferUsage =
          vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
        .name = "rock_indices"});
    target.instances = context.createBuffer(
      etna::Buffer::CreateInfo{
        .size = MAX_INSTANCES * sizeof(RockInstance),
        .bufferUsage =
          vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
        .name = "rock_instances"});
    transfer_helper.uploadBuffer(
      command_manager, target.vertices, 0, std::span<const RockVertex>{vertices});
    transfer_helper.uploadBuffer(
      command_manager, target.indices, 0, std::span<const uint32_t>{indices});
    const auto& material = source.materials.at(primitive.material);
    const std::array<int, 3> textureIndices{
      material.pbrMetallicRoughness.baseColorTexture.index,
      material.normalTexture.index,
      material.pbrMetallicRoughness.metallicRoughnessTexture.index};
    for (std::size_t t = 0; t < textureIndices.size(); ++t)
    {
      const auto& image = source.images.at(source.textures.at(textureIndices[t]).source);
      if (image.component != 4 || image.bits != 8 || image.width <= 0 || image.height <= 0)
        throw std::runtime_error("Unsupported rock texture format");
      target.textures[t] = context.createImage(
        etna::Image::CreateInfo{
          .extent = {static_cast<uint32_t>(image.width), static_cast<uint32_t>(image.height), 1},
          .name = "rock_material",
          .format = t == 0 ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm,
          .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst});
      transfer_helper.uploadImage(
        command_manager, target.textures[t], 0, 0, std::as_bytes(std::span{image.image}));
    }
  }
}

void RockRenderer::setupPipeline(vk::Format format)
{
  etna::create_program(
    "instancing_rocks",
    {INSTANCING_SHADERS_ROOT "rocks.vert.spv", INSTANCING_SHADERS_ROOT "rocks.frag.spv"});
  pipeline = etna::get_context().getPipelineManager().createGraphicsPipeline(
    "instancing_rocks",
    etna::GraphicsPipeline::CreateInfo{
      .vertexShaderInput =
        {.bindings = {etna::VertexShaderInputDescription::Binding{
           .byteStreamDescription =
             {.stride = sizeof(RockVertex),
              .attributes =
                {{.format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(RockVertex, position)},
                 {.format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(RockVertex, normal)},
                 {.format = vk::Format::eR32G32Sfloat, .offset = offsetof(RockVertex, uv)}}}}}},
      .rasterizationConfig =
        vk::PipelineRasterizationStateCreateInfo{
          .polygonMode = vk::PolygonMode::eFill,
          .cullMode = vk::CullModeFlagBits::eNone,
          .frontFace = vk::FrontFace::eCounterClockwise,
          .lineWidth = 1.0f},
      .fragmentShaderOutput = {
        .colorAttachmentFormats = {format}, .depthAttachmentFormat = vk::Format::eD32Sfloat}});
}

void RockRenderer::drawGui()
{
  ImGui::Begin("Rock instancing");
  ImGui::SliderFloat("Density", &density, 0.0f, 1.0f);
  ImGui::SliderFloat("Draw distance", &drawDistance, 1.0f, 30.0f);
  ImGui::SliderFloat("Minimum size", &minSize, 0.03f, 1.0f);
  ImGui::SliderFloat("Maximum size", &maxSize, minSize, 1.5f);
  maxSize = std::max(minSize, maxSize);
  ImGui::Text(
    "Visible: %zu boulders, %zu rocks", models[0].visible.size(), models[1].visible.size());
  ImGui::End();
}

void RockRenderer::prepare(
  vk::CommandBuffer command_buffer,
  const HeightmapGenerator& heightmap,
  const glm::mat4& proj_view,
  glm::vec3 camera_position)
{
  const auto rows = glm::transpose(proj_view);
  std::array<glm::vec4, 6> planes{
    rows[3] + rows[0],
    rows[3] - rows[0],
    rows[3] + rows[1],
    rows[3] - rows[1],
    rows[2],
    rows[3] - rows[2]};
  for (auto& plane : planes)
    plane /= glm::length(glm::vec3{plane});
  for (auto& model : models)
    model.visible.clear();
  const float margin = maxSize;
  const auto cell = [](float coordinate) {
    return static_cast<int>(std::floor((coordinate + WORLD_SIZE * 0.5f) / CELL_SIZE));
  };
  const int minX = std::max(0, cell(camera_position.x - drawDistance - margin));
  const int maxX = std::min(CELL_COUNT - 1, cell(camera_position.x + drawDistance + margin));
  const int minZ = std::max(0, cell(camera_position.z - drawDistance - margin));
  const int maxZ = std::min(CELL_COUNT - 1, cell(camera_position.z + drawDistance + margin));
  for (int z = minZ; z <= maxZ; ++z)
    for (int x = minX; x <= maxX; ++x)
    {
      uint32_t state = hash_cell(static_cast<uint32_t>(z * CELL_COUNT + x) ^ 0x43a291d7u);
      const glm::vec2 horizontal =
        (glm::vec2{x, z} + glm::vec2{random_value(state), random_value(state)}) * CELL_SIZE -
        WORLD_SIZE * 0.5f;
      const float height = terrain_height(heightmap, horizontal);
      const float probability = density * glm::smoothstep(0.18f, 0.35f, height) *
        (1.0f - glm::smoothstep(0.68f, 0.85f, height));
      if (random_value(state) >= probability)
        continue;
      const std::size_t modelIndex = hash_cell(state) % models.size();
      const float size = glm::mix(minSize, maxSize, random_value(state));
      const float radius = size * 0.866026f;
      const glm::vec3 position{horizontal.x, height * HEIGHT_SCALE, horizontal.y};
      if (glm::distance(position, camera_position) > drawDistance + radius)
        continue;
      bool visible = true;
      for (const auto& plane : planes)
        if (glm::dot(glm::vec3{plane}, position) + plane.w < -radius)
          visible = false;
      if (!visible)
        continue;
      auto transform = glm::translate(glm::mat4{1.0f}, position);
      transform =
        glm::rotate(transform, random_value(state) * glm::two_pi<float>(), glm::vec3{0, 1, 0});
      transform = glm::rotate(transform, (random_value(state) - 0.5f) * 0.5f, glm::vec3{1, 0, 0});
      transform = glm::scale(transform, glm::vec3{size});
      const float tint = glm::mix(0.8f, 1.15f, random_value(state));
      models[modelIndex].visible.push_back(
        {transform, glm::vec4{tint, tint * 0.98f, tint * 0.94f, 1.0f}});
    }
  for (auto& model : models)
  {
    if (model.visible.empty())
      continue;
    etna::set_state(
      command_buffer,
      model.instances.get(),
      vk::PipelineStageFlagBits2::eTransfer,
      vk::AccessFlagBits2::eTransferWrite);
    etna::flush_barriers(command_buffer);
    const auto bytes = std::as_bytes(std::span{model.visible});
    constexpr std::size_t CHUNK_SIZE = 65536;
    for (std::size_t offset = 0; offset < bytes.size(); offset += CHUNK_SIZE)
    {
      const auto count = std::min(CHUNK_SIZE, bytes.size() - offset);
      command_buffer.updateBuffer(model.instances.get(), offset, count, bytes.data() + offset);
    }
    etna::set_state(
      command_buffer,
      model.instances.get(),
      vk::PipelineStageFlagBits2::eVertexShader,
      vk::AccessFlagBits2::eShaderStorageRead);
    etna::set_state(
      command_buffer,
      model.vertices.get(),
      vk::PipelineStageFlagBits2::eVertexInput,
      vk::AccessFlagBits2::eVertexAttributeRead);
    etna::set_state(
      command_buffer,
      model.indices.get(),
      vk::PipelineStageFlagBits2::eVertexInput,
      vk::AccessFlagBits2::eIndexRead);
    std::vector<etna::Binding> bindings;
    bindings.emplace_back(0, model.instances.genBinding());
    for (uint32_t t = 0; t < model.textures.size(); ++t)
      bindings.emplace_back(
        t + 1,
        model.textures[t].genBinding(sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal));
    model.descriptor = etna::create_descriptor_set(
      etna::get_shader_program("instancing_rocks").getDescriptorLayoutId(0),
      command_buffer,
      std::move(bindings));
  }
  etna::flush_barriers(command_buffer);
}

void RockRenderer::render(
  vk::CommandBuffer command_buffer,
  const glm::mat4& proj_view,
  glm::vec3 camera_position,
  glm::vec3 light)
{
  struct PushConstants
  {
    glm::mat4 projView;
    glm::vec4 cameraPosition;
    glm::vec4 light;
  };
  const PushConstants constants{proj_view, glm::vec4{camera_position, 1}, glm::vec4{light, 0}};
  command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.getVkPipeline());
  command_buffer.pushConstants<PushConstants>(
    pipeline.getVkPipelineLayout(),
    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
    0,
    {constants});
  for (auto& model : models)
  {
    if (model.visible.empty())
      continue;
    command_buffer.bindDescriptorSets(
      vk::PipelineBindPoint::eGraphics,
      pipeline.getVkPipelineLayout(),
      0,
      {model.descriptor.getVkSet()},
      {});
    command_buffer.bindVertexBuffers(0, {model.vertices.get()}, {0});
    command_buffer.bindIndexBuffer(model.indices.get(), 0, vk::IndexType::eUint32);
    command_buffer.drawIndexed(
      model.indexCount, static_cast<uint32_t>(model.visible.size()), 0, 0, 0);
  }
}
