#pragma once

#include <etna/BindingItems.hpp>
#include <etna/BlockingTransferHelper.hpp>
#include <etna/Image.hpp>
#include <etna/OneShotCmdMgr.hpp>
#include <etna/Sampler.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

class TerrainTextures
{
public:
  static constexpr std::size_t MATERIAL_COUNT = 4;
  static constexpr uint32_t TEXTURE_SIZE = 1024;
  static constexpr std::size_t UPLOAD_SIZE = TEXTURE_SIZE * TEXTURE_SIZE * 4;

  void load(etna::BlockingTransferHelper& transfer_helper, etna::OneShotCmdMgr& command_manager);
  void appendBindings(std::vector<etna::Binding>& bindings) const;

private:
  std::array<etna::Image, MATERIAL_COUNT> albedoImages;
  std::array<etna::Image, MATERIAL_COUNT> roughnessImages;
  std::array<etna::Image, MATERIAL_COUNT> heightImages;
  etna::Sampler materialSampler;
};
