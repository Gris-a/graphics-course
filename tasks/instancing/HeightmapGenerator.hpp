#pragma once

#include <etna/Buffer.hpp>
#include <etna/ComputePipeline.hpp>
#include <etna/Image.hpp>
#include <etna/OneShotCmdMgr.hpp>
#include <etna/Sampler.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

class HeightmapGenerator
{
public:
  static constexpr uint32_t DEFAULT_TEXTURE_SIZE = 1024;

  struct Settings
  {
    int seed = 1;
    int octaves = 6;
    float frequency = 2.0f;
    float persistence = 0.5f;
    float lacunarity = 2.0f;
    float islandFalloff = 1.5f;
  };

  void loadShaders();
  void allocateResources(glm::uvec2 size = glm::uvec2{DEFAULT_TEXTURE_SIZE});
  void setupPipeline();
  void generate();

  float sample(glm::vec2 uv) const;

  Settings& settings() { return noiseSettings; }
  glm::uvec2 size() const { return heightmapSize; }
  etna::ImageBinding sampledBinding() const;

private:
  struct PushConstants
  {
    int32_t seed;
    int32_t octaves;
    float frequency;
    float persistence;
    float lacunarity;
    float islandFalloff;
    int32_t normalize;
    int32_t padding = 0;
  };

  std::vector<float> heights;
  Settings noiseSettings;
  glm::uvec2 heightmapSize{DEFAULT_TEXTURE_SIZE};
  etna::Image heightmapImage;
  etna::Sampler heightmapSampler;
  etna::Buffer maxHeightBuffer;
  etna::ComputePipeline heightmapPipeline;
  std::unique_ptr<etna::OneShotCmdMgr> commandManager;
};
