#include "TerrainTextures.hpp"

#include <etna/GlobalContext.hpp>

#include <tiny_gltf.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

constexpr std::array<std::string_view, TerrainTextures::MATERIAL_COUNT> MATERIAL_NAMES{{
  "sand_03",
  "sparse_grass",
  "rock_06",
  "snow_01",
}};

std::vector<unsigned char> loadTexture(const std::filesystem::path& path)
{
  std::ifstream file(path, std::ios::binary);
  if (!file)
    throw std::runtime_error("Failed to open terrain texture: " + path.string());

  const std::vector<unsigned char> encoded{
    std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
  if (
    file.bad() || encoded.empty() ||
    encoded.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
  {
    throw std::runtime_error("Failed to read terrain texture: " + path.string());
  }

  tinygltf::Image image;
  std::string error;
  if (!tinygltf::LoadImageData(
        &image,
        0,
        &error,
        nullptr,
        0,
        0,
        encoded.data(),
        static_cast<int>(encoded.size()),
        nullptr))
  {
    throw std::runtime_error("Failed to decode terrain texture '" + path.string() + "': " + error);
  }

  return std::move(image.image);
}

void createTexture(
  etna::Image& texture,
  std::span<const unsigned char> pixels,
  vk::Format format,
  const std::string& name,
  etna::BlockingTransferHelper& transfer_helper,
  etna::OneShotCmdMgr& command_manager)
{
  texture = etna::get_context().createImage(
    etna::Image::CreateInfo{
      .extent = vk::Extent3D{TerrainTextures::TEXTURE_SIZE, TerrainTextures::TEXTURE_SIZE, 1},
      .name = name,
      .format = format,
      .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
    });

  transfer_helper.uploadImage(command_manager, texture, 0, 0, std::as_bytes(pixels));
}

} // namespace

void TerrainTextures::load(
  etna::BlockingTransferHelper& transfer_helper, etna::OneShotCmdMgr& command_manager)
{
  materialSampler = etna::Sampler(
    etna::Sampler::CreateInfo{
      .filter = vk::Filter::eLinear,
      .addressMode = vk::SamplerAddressMode::eRepeat,
      .name = "terrain_material_sampler",
    });

  const std::filesystem::path textureRoot{TERRAIN_TEXTURES_ROOT};
  for (std::size_t i = 0; i < MATERIAL_COUNT; ++i)
  {
    const auto directory = textureRoot / (std::string{MATERIAL_NAMES[i]} + "_1k") / "textures";
    const std::string prefix{MATERIAL_NAMES[i]};

    const auto albedo = loadTexture(directory / (prefix + "_diff_1k.jpg"));
    const auto roughness = loadTexture(directory / (prefix + "_rough_1k.jpg"));
    const auto height = loadTexture(directory / (prefix + "_disp_1k.jpg"));

    createTexture(
      albedoImages[i],
      albedo,
      vk::Format::eR8G8B8A8Srgb,
      "terrain_albedo_" + prefix,
      transfer_helper,
      command_manager);
    createTexture(
      roughnessImages[i],
      roughness,
      vk::Format::eR8G8B8A8Unorm,
      "terrain_roughness_" + prefix,
      transfer_helper,
      command_manager);
    createTexture(
      heightImages[i],
      height,
      vk::Format::eR8G8B8A8Unorm,
      "terrain_height_" + prefix,
      transfer_helper,
      command_manager);
  }
}

void TerrainTextures::appendBindings(std::vector<etna::Binding>& bindings) const
{
  for (std::size_t i = 0; i < MATERIAL_COUNT; ++i)
  {
    bindings.emplace_back(
      static_cast<uint32_t>(1 + i),
      albedoImages[i].genBinding(materialSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal));
    bindings.emplace_back(
      static_cast<uint32_t>(5 + i),
      roughnessImages[i].genBinding(
        materialSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal));
    bindings.emplace_back(
      static_cast<uint32_t>(9 + i),
      heightImages[i].genBinding(materialSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal));
  }
}
