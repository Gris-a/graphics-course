#pragma once

#include <etna/BlockingTransferHelper.hpp>
#include <etna/Buffer.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/OneShotCmdMgr.hpp>
#include <etna/RenderTargetStates.hpp>

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>

#include "HeightmapGenerator.hpp"
#include "TerrainGui.hpp"
#include "TerrainTextures.hpp"
#include "scene/Camera.hpp"

struct alignas(16) TerrainPushConstants
{
  glm::mat4x4 projView;
  glm::vec4 terrainParams;
  glm::vec4 lightingParams;
  glm::vec4 cameraPosition;
};

static_assert(sizeof(TerrainPushConstants) == 112);
static_assert(offsetof(TerrainPushConstants, terrainParams) == 64);
static_assert(offsetof(TerrainPushConstants, lightingParams) == 80);
static_assert(offsetof(TerrainPushConstants, cameraPosition) == 96);

class WorldRenderer
{
public:
  void loadShaders();
  void allocateResources(glm::uvec2 swapchain_resolution);
  void setupPipelines(vk::Format swapchain_format);

  void update(const Camera& camera);
  void drawGui();
  void renderWorld(
    vk::CommandBuffer command_buffer, vk::Image target_image, vk::ImageView target_image_view);

private:
  static constexpr uint32_t GRID_CELLS = 256;
  static constexpr float WORLD_SIZE = 20.0f;
  static constexpr float HEIGHT_SCALE = 6.0f;

  HeightmapGenerator heightmapGenerator;
  TerrainTextures terrainTextures;
  TerrainGui terrainGui;
  TerrainLightingSettings lightingSettings;

  etna::Buffer gridVertices;
  etna::Buffer gridIndices;
  uint32_t gridIndexCount = 0;

  std::unique_ptr<etna::BlockingTransferHelper> transferHelper;
  std::unique_ptr<etna::OneShotCmdMgr> uploadCommandManager;
  etna::Image mainViewDepth;
  etna::GraphicsPipeline staticMeshPipeline;

  glm::mat4x4 worldViewProj;
  glm::vec3 cameraPosition{};
  glm::uvec2 resolution{};
};
