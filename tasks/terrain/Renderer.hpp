#pragma once

#include <etna/GlobalContext.hpp>
#include <etna/PerFrameCmdMgr.hpp>
#include <glm/glm.hpp>
#include <function2/function2.hpp>

#include <memory>
#include <span>

#include "scene/Camera.hpp"
#include "WorldRenderer.hpp"

class ImGuiRenderer;


using ResolutionProvider = fu2::unique_function<glm::uvec2() const>;

class Renderer
{
public:
  explicit Renderer(glm::uvec2 resolution);
  ~Renderer();

  void initVulkan(std::span<const char*> instance_extensions);
  void initFrameDelivery(vk::UniqueSurfaceKHR surface, ResolutionProvider res_provider);

  void update(const Camera& camera);
  void drawFrame();

private:
  ResolutionProvider resolutionProvider;

  std::unique_ptr<etna::Window> window;
  std::unique_ptr<etna::PerFrameCmdMgr> commandManager;
  std::unique_ptr<ImGuiRenderer> guiRenderer;

  glm::uvec2 resolution;
  std::unique_ptr<WorldRenderer> worldRenderer;
};
