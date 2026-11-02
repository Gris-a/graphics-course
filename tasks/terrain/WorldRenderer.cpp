#include "WorldRenderer.hpp"

#include <etna/Etna.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/RenderTargetStates.hpp>

#include <glm/ext.hpp>

#include <span>
#include <vector>

void WorldRenderer::allocateResources(glm::uvec2 swapchain_resolution)
{
  resolution = swapchain_resolution;
  auto& context = etna::get_context();

  mainViewDepth = context.createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{resolution.x, resolution.y, 1},
    .name = "main_view_depth",
    .format = vk::Format::eD32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
  });

  heightmapGenerator.allocateResources();

  std::vector<glm::vec2> vertices;
  vertices.reserve((GRID_CELLS + 1) * (GRID_CELLS + 1));
  for (uint32_t y = 0; y <= GRID_CELLS; ++y)
  {
    for (uint32_t x = 0; x <= GRID_CELLS; ++x)
    {
      vertices.emplace_back(static_cast<float>(x) / GRID_CELLS, static_cast<float>(y) / GRID_CELLS);
    }
  }

  std::vector<uint32_t> indices;
  indices.reserve(GRID_CELLS * GRID_CELLS * 6);
  for (uint32_t y = 0; y < GRID_CELLS; ++y)
  {
    for (uint32_t x = 0; x < GRID_CELLS; ++x)
    {
      const uint32_t topLeft = y * (GRID_CELLS + 1) + x;
      const uint32_t topRight = topLeft + 1;
      const uint32_t bottomLeft = topLeft + GRID_CELLS + 1;
      const uint32_t bottomRight = bottomLeft + 1;

      indices.insert(
        indices.end(), {topLeft, bottomLeft, topRight, topRight, bottomLeft, bottomRight});
    }
  }

  gridIndexCount = static_cast<uint32_t>(indices.size());
  gridVertices = context.createBuffer(etna::Buffer::CreateInfo{
    .size = vertices.size() * sizeof(glm::vec2),
    .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
    .name = "terrain_grid_vertices",
  });
  gridIndices = context.createBuffer(etna::Buffer::CreateInfo{
    .size = indices.size() * sizeof(uint32_t),
    .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer,
    .name = "terrain_grid_indices",
  });

  transferHelper = std::make_unique<etna::BlockingTransferHelper>(
    etna::BlockingTransferHelper::CreateInfo{.stagingSize = TerrainTextures::UPLOAD_SIZE});
  uploadCommandManager = context.createOneShotCmdMgr();
  transferHelper->uploadBuffer(
    *uploadCommandManager, gridVertices, 0, std::span<const glm::vec2>{vertices});
  transferHelper->uploadBuffer(
    *uploadCommandManager, gridIndices, 0, std::span<const uint32_t>{indices});

  terrainTextures.load(*transferHelper, *uploadCommandManager);
}

void WorldRenderer::loadShaders()
{
  etna::create_program(
    "terrain_grid",
    {TERRAIN_SHADERS_ROOT "static_mesh.frag.spv", TERRAIN_SHADERS_ROOT "static_mesh.vert.spv"});
  heightmapGenerator.loadShaders();
}

void WorldRenderer::setupPipelines(vk::Format swapchain_format)
{
  etna::VertexByteStreamFormatDescription vertexFormat{
    .stride = sizeof(glm::vec2),
    .attributes = {{.format = vk::Format::eR32G32Sfloat, .offset = 0}},
  };
  etna::VertexShaderInputDescription sceneVertexInputDescription{
    .bindings = {etna::VertexShaderInputDescription::Binding{
      .byteStreamDescription = vertexFormat}},
  };

  staticMeshPipeline = etna::get_context().getPipelineManager().createGraphicsPipeline(
    "terrain_grid",
    etna::GraphicsPipeline::CreateInfo{
      .vertexShaderInput = sceneVertexInputDescription,
      .rasterizationConfig =
        vk::PipelineRasterizationStateCreateInfo{
          .polygonMode = vk::PolygonMode::eFill,
          .cullMode = vk::CullModeFlagBits::eNone,
          .frontFace = vk::FrontFace::eCounterClockwise,
          .lineWidth = 1.0f,
        },
      .fragmentShaderOutput =
        {
          .colorAttachmentFormats = {swapchain_format},
          .depthAttachmentFormat = vk::Format::eD32Sfloat,
        },
    });

  heightmapGenerator.setupPipeline();
  heightmapGenerator.generate();
}

void WorldRenderer::update(const Camera& camera)
{
  cameraPosition = camera.position;
  const float aspect = static_cast<float>(resolution.x) / static_cast<float>(resolution.y);
  worldViewProj = camera.projTm(aspect) * camera.viewTm();
}

void WorldRenderer::drawGui()
{
  terrainGui.draw(heightmapGenerator, lightingSettings);
}

void WorldRenderer::renderWorld(
  vk::CommandBuffer command_buffer, vk::Image target_image, vk::ImageView target_image_view)
{
  const auto program = etna::get_shader_program("terrain_grid");
  std::vector<etna::Binding> bindings;
  bindings.reserve(1 + TerrainTextures::MATERIAL_COUNT * 3);
  bindings.emplace_back(0, heightmapGenerator.sampledBinding());
  terrainTextures.appendBindings(bindings);

  auto descriptorSet = etna::create_descriptor_set(
    program.getDescriptorLayoutId(0), command_buffer, std::move(bindings));
  etna::flush_barriers(command_buffer);

  etna::RenderTargetState renderTargets(
    command_buffer,
    {{0, 0}, {resolution.x, resolution.y}},
    {{.image = target_image, .view = target_image_view}},
    {.image = mainViewDepth.get(), .view = mainViewDepth.getView({})});

  command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, staticMeshPipeline.getVkPipeline());
  command_buffer.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics,
    staticMeshPipeline.getVkPipelineLayout(),
    0,
    {descriptorSet.getVkSet()},
    {});
  command_buffer.bindVertexBuffers(0, {gridVertices.get()}, {0});
  command_buffer.bindIndexBuffer(gridIndices.get(), 0, vk::IndexType::eUint32);

  const TerrainPushConstants pushConstants{
    worldViewProj,
    glm::vec4{
      WORLD_SIZE, HEIGHT_SCALE, lightingSettings.textureScale, lightingSettings.bumpStrength},
    glm::vec4{
      glm::radians(lightingSettings.lightAzimuth),
      glm::radians(lightingSettings.lightElevation),
      lightingSettings.lightIntensity,
      0.0f},
    glm::vec4{cameraPosition, 1.0f}};
  command_buffer.pushConstants<TerrainPushConstants>(
    staticMeshPipeline.getVkPipelineLayout(),
    vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
    0,
    {pushConstants});
  command_buffer.drawIndexed(gridIndexCount, 1, 0, 0, 0);
}
