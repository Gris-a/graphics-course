#pragma once

#include "HeightmapGenerator.hpp"

struct TerrainLightingSettings
{
  float textureScale = 1.0f;
  float bumpStrength = 0.015f;
  float lightAzimuth = 35.0f;
  float lightElevation = 45.0f;
  float lightIntensity = 3.0f;
};

class TerrainGui
{
public:
  void draw(HeightmapGenerator& heightmap_generator, TerrainLightingSettings& lighting_settings);

private:
  HeightmapGenerator::Settings pendingNoiseSettings;
  bool settingsInitialized = false;
};
