#include "TerrainGui.hpp"

#include "HeightmapGenerator.hpp"

#include <imgui.h>

void TerrainGui::draw(
  HeightmapGenerator& heightmap_generator, TerrainLightingSettings& lighting_settings)
{
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(
    ImVec2{viewport->WorkPos.x + viewport->WorkSize.x - 10.0f, viewport->WorkPos.y + 10.0f},
    ImGuiCond_FirstUseEver,
    ImVec2{1.0f, 0.0f});
  ImGui::Begin("Island heightmap");
  const auto heightmapSize = heightmap_generator.size();
  ImGui::Text("%u x %u, R32_SFLOAT", heightmapSize.x, heightmapSize.y);

  if (!settingsInitialized)
  {
    pendingNoiseSettings = heightmap_generator.settings();
    settingsInitialized = true;
  }

  ImGui::InputInt("Seed", &pendingNoiseSettings.seed);
  ImGui::SliderInt("Octaves", &pendingNoiseSettings.octaves, 1, 12);
  ImGui::SliderFloat("Frequency", &pendingNoiseSettings.frequency, 0.1f, 10.0f);
  ImGui::SliderFloat("Gain", &pendingNoiseSettings.persistence, 0.0f, 1.0f);
  ImGui::SliderFloat("Lacunarity", &pendingNoiseSettings.lacunarity, 0.1f, 10.0f);
  ImGui::SliderFloat("Mask", &pendingNoiseSettings.islandFalloff, 0.1f, 10.0f);

  if (ImGui::Button("Regenerate"))
  {
    heightmap_generator.settings() = pendingNoiseSettings;
    heightmap_generator.generate();
  }

  ImGui::Separator();
  ImGui::Text("Lighting and materials");
  ImGui::SliderFloat("Light azimuth", &lighting_settings.lightAzimuth, -180.0f, 180.0f);
  ImGui::SliderFloat("Light elevation", &lighting_settings.lightElevation, 5.0f, 90.0f);
  ImGui::SliderFloat("Light intensity", &lighting_settings.lightIntensity, 0.0f, 10.0f);
  ImGui::SliderFloat("Texture scale", &lighting_settings.textureScale, 0.1f, 8.0f);
  ImGui::SliderFloat("Bump strength", &lighting_settings.bumpStrength, 0.0f, 0.1f);

  ImGui::End();
}
