#pragma once

#include "HeightmapGenerator.hpp"
#include <etna/BlockingTransferHelper.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/Sampler.hpp>
#include <array>
#include <vector>

struct RockVertex
{
  glm::vec3 position;
  glm::vec3 normal;
  glm::vec2 uv;
};

struct alignas(16) RockInstance
{
  glm::mat4 transform;
  glm::vec4 color;
};
static_assert(sizeof(RockInstance) == 80);

class RockRenderer
{
public:
  void load(etna::BlockingTransferHelper& transfer_helper, etna::OneShotCmdMgr& command_manager);
  void setupPipeline(vk::Format format);
  void drawGui();
  void prepare(
    vk::CommandBuffer command_buffer,
    const HeightmapGenerator& heightmap,
    const glm::mat4& proj_view,
    glm::vec3 camera_position);
  void render(
    vk::CommandBuffer command_buffer,
    const glm::mat4& proj_view,
    glm::vec3 camera_position,
    glm::vec3 light);

private:
  struct Model
  {
    etna::Buffer vertices;
    etna::Buffer indices;
    etna::Buffer instances;
    std::array<etna::Image, 3> textures;
    etna::DescriptorSet descriptor;
    uint32_t indexCount = 0;
    std::vector<RockInstance> visible;
  };
  static constexpr uint32_t MAX_INSTANCES = 40000;
  std::array<Model, 2> models;
  etna::Sampler sampler;
  etna::GraphicsPipeline pipeline;
  float density = 0.5f;
  float drawDistance = 12.0f;
  float minSize = 0.12f;
  float maxSize = 0.45f;
};
