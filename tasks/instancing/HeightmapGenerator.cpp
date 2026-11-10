#include "HeightmapGenerator.hpp"

#include <etna/Etna.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>

#include <stdexcept>
#include <cstring>
#include <algorithm>

namespace
{

constexpr uint32_t WORKGROUP_SIZE = 8;

}

void HeightmapGenerator::loadShaders()
{
  etna::create_program("terrain_heightmap", {INSTANCING_SHADERS_ROOT "heightmap.comp.spv"});
}

void HeightmapGenerator::allocateResources(glm::uvec2 size)
{
  if (size.x < 2 || size.y < 2)
    throw std::invalid_argument("Heightmap dimensions must be at least 2x2");

  auto& context = etna::get_context();
  const auto limits = context.getPhysicalDevice().getProperties().limits;
  if (size.x > limits.maxImageDimension2D || size.y > limits.maxImageDimension2D)
  {
    throw std::invalid_argument("Heightmap dimensions exceed the device's maximum 2D image size");
  }

  const glm::uvec2 workgroupCount{
    (size.x - 1) / WORKGROUP_SIZE + 1, (size.y - 1) / WORKGROUP_SIZE + 1};
  if (
    workgroupCount.x > limits.maxComputeWorkGroupCount[0] ||
    workgroupCount.y > limits.maxComputeWorkGroupCount[1])
  {
    throw std::invalid_argument("Heightmap dimensions exceed the device's compute dispatch limits");
  }
  heightmapSize = size;

  heightmapImage = context.createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{size.x, size.y, 1},
    .name = "island_heightmap",
    .format = vk::Format::eR32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage |
      vk::ImageUsageFlagBits::eTransferSrc,
  });

  heightmapSampler = etna::Sampler(etna::Sampler::CreateInfo{
    .filter = vk::Filter::eLinear,
    .addressMode = vk::SamplerAddressMode::eClampToEdge,
    .name = "heightmap_sampler",
  });

  maxHeightBuffer = context.createBuffer(etna::Buffer::CreateInfo{
    .size = sizeof(uint32_t),
    .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
    .name = "heightmap_max_value",
  });

  commandManager = context.createOneShotCmdMgr();
}

void HeightmapGenerator::setupPipeline()
{
  heightmapPipeline =
    etna::get_context().getPipelineManager().createComputePipeline("terrain_heightmap", {});
}

void HeightmapGenerator::generate()
{
  auto commandBuffer = commandManager->start();
  ETNA_CHECK_VK_RESULT(commandBuffer.begin(vk::CommandBufferBeginInfo{}));

  etna::set_state(
    commandBuffer,
    maxHeightBuffer.get(),
    vk::PipelineStageFlagBits2::eTransfer,
    vk::AccessFlagBits2::eTransferWrite);
  etna::flush_barriers(commandBuffer);
  commandBuffer.fillBuffer(maxHeightBuffer.get(), 0, sizeof(uint32_t), 0);

  etna::set_state(
    commandBuffer,
    heightmapImage.get(),
    vk::PipelineStageFlagBits2::eComputeShader,
    vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
    vk::ImageLayout::eGeneral,
    vk::ImageAspectFlagBits::eColor,
    ForceSetState::eTrue);
  etna::set_state(
    commandBuffer,
    maxHeightBuffer.get(),
    vk::PipelineStageFlagBits2::eComputeShader,
    vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite);
  etna::flush_barriers(commandBuffer);

  auto program = etna::get_shader_program("terrain_heightmap");
  auto descriptorSet = etna::create_descriptor_set(
    program.getDescriptorLayoutId(0),
    commandBuffer,
    {
      etna::Binding{0, heightmapImage.genBinding(vk::Sampler{}, vk::ImageLayout::eGeneral)},
      etna::Binding{1, maxHeightBuffer.genBinding()},
    });
  etna::flush_barriers(commandBuffer);

  commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, heightmapPipeline.getVkPipeline());
  commandBuffer.bindDescriptorSets(
    vk::PipelineBindPoint::eCompute,
    heightmapPipeline.getVkPipelineLayout(),
    0,
    {descriptorSet.getVkSet()},
    {});

  const PushConstants settings{
    .seed = noiseSettings.seed,
    .octaves = noiseSettings.octaves,
    .frequency = noiseSettings.frequency,
    .persistence = noiseSettings.persistence,
    .lacunarity = noiseSettings.lacunarity,
    .islandFalloff = noiseSettings.islandFalloff,
    .normalize = 0,
  };
  commandBuffer.pushConstants<PushConstants>(
    heightmapPipeline.getVkPipelineLayout(), vk::ShaderStageFlagBits::eCompute, 0, {settings});
  commandBuffer.dispatch(
    (heightmapSize.x - 1) / WORKGROUP_SIZE + 1, (heightmapSize.y - 1) / WORKGROUP_SIZE + 1, 1);

  etna::set_state(
    commandBuffer,
    heightmapImage.get(),
    vk::PipelineStageFlagBits2::eComputeShader,
    vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite,
    vk::ImageLayout::eGeneral,
    vk::ImageAspectFlagBits::eColor,
    ForceSetState::eTrue);
  etna::set_state(
    commandBuffer,
    maxHeightBuffer.get(),
    vk::PipelineStageFlagBits2::eComputeShader,
    vk::AccessFlagBits2::eShaderRead);
  etna::flush_barriers(commandBuffer);

  const PushConstants normalizeSettings{
    .seed = noiseSettings.seed,
    .octaves = noiseSettings.octaves,
    .frequency = noiseSettings.frequency,
    .persistence = noiseSettings.persistence,
    .lacunarity = noiseSettings.lacunarity,
    .islandFalloff = noiseSettings.islandFalloff,
    .normalize = 1,
  };
  commandBuffer.pushConstants<PushConstants>(
    heightmapPipeline.getVkPipelineLayout(),
    vk::ShaderStageFlagBits::eCompute,
    0,
    {normalizeSettings});
  commandBuffer.dispatch(
    (heightmapSize.x - 1) / WORKGROUP_SIZE + 1, (heightmapSize.y - 1) / WORKGROUP_SIZE + 1, 1);

  auto& context = etna::get_context();
  auto device = context.getDevice();
  const vk::DeviceSize bytes = heightmapSize.x * heightmapSize.y * sizeof(float);
  auto readback = etna::unwrap_vk_result(device.createBufferUnique(
    vk::BufferCreateInfo{.size = bytes, .usage = vk::BufferUsageFlagBits::eTransferDst}));
  const auto requirements = device.getBufferMemoryRequirements(readback.get());
  const auto properties = context.getPhysicalDevice().getMemoryProperties();
  uint32_t memoryType = properties.memoryTypeCount;
  const auto flags =
    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
  for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
    if (
      (requirements.memoryTypeBits & (1u << i)) != 0 &&
      (properties.memoryTypes[i].propertyFlags & flags) == flags)
    {
      memoryType = i;
      break;
    }
  if (memoryType == properties.memoryTypeCount)
    throw std::runtime_error("No coherent host-visible memory for heightmap readback");
  auto memory = etna::unwrap_vk_result(device.allocateMemoryUnique(
    vk::MemoryAllocateInfo{.allocationSize = requirements.size, .memoryTypeIndex = memoryType}));
  ETNA_CHECK_VK_RESULT(device.bindBufferMemory(readback.get(), memory.get(), 0));
  etna::set_state(
    commandBuffer,
    heightmapImage.get(),
    vk::PipelineStageFlagBits2::eTransfer,
    vk::AccessFlagBits2::eTransferRead,
    vk::ImageLayout::eTransferSrcOptimal,
    vk::ImageAspectFlagBits::eColor);
  etna::flush_barriers(commandBuffer);
  commandBuffer.copyImageToBuffer(
    heightmapImage.get(),
    vk::ImageLayout::eTransferSrcOptimal,
    readback.get(),
    {vk::BufferImageCopy{
      .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
      .imageExtent = {heightmapSize.x, heightmapSize.y, 1}}});
  ETNA_CHECK_VK_RESULT(commandBuffer.end());
  commandManager->submitAndWait(std::move(commandBuffer));
  heights.resize(heightmapSize.x * heightmapSize.y);
  const auto* mapped = etna::unwrap_vk_result(device.mapMemory(memory.get(), 0, bytes));
  std::memcpy(heights.data(), mapped, static_cast<std::size_t>(bytes));
  device.unmapMemory(memory.get());
  readback.reset();
}

etna::ImageBinding HeightmapGenerator::sampledBinding() const
{
  return heightmapImage.genBinding(heightmapSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal);
}

float HeightmapGenerator::sample(glm::vec2 uv) const
{
  const glm::vec2 pixel =
    glm::clamp(uv, glm::vec2{0}, glm::vec2{1}) * glm::vec2{heightmapSize - 1u};
  const auto x = static_cast<uint32_t>(pixel.x);
  const auto y = static_cast<uint32_t>(pixel.y);
  const auto right = std::min(x + 1, heightmapSize.x - 1);
  const auto top = std::min(y + 1, heightmapSize.y - 1);
  return glm::mix(
    glm::mix(heights[y * heightmapSize.x + x], heights[y * heightmapSize.x + right], pixel.x - x),
    glm::mix(
      heights[top * heightmapSize.x + x], heights[top * heightmapSize.x + right], pixel.x - x),
    pixel.y - y);
}
