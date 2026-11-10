#include "App.hpp"

#include <etna/GlobalContext.hpp>
#include <imgui.h>

#include "gui/ImGuiRenderer.hpp"


App::App()
{
  glm::uvec2 initialRes = {1280, 720};
  mainWindow = windowing.createWindow(
    OsWindow::CreateInfo{
      .resolution = initialRes,
    });

  renderer = std::make_unique<Renderer>(initialRes);

  auto instExts = windowing.getRequiredVulkanInstanceExtensions();
  renderer->initVulkan(instExts);

  auto surface = mainWindow->createVkSurface(etna::get_context().getInstance());

  renderer->initFrameDelivery(std::move(surface), [this]() { return mainWindow->getResolution(); });
  ImGuiRenderer::enableImGuiForWindow(mainWindow->native());

  mainCam.lookAt({0, 10, 10}, {0, 0, 0}, {0, 1, 0});
}

void App::run()
{
  double lastTime = windowing.getTime();
  while (!mainWindow->isBeingClosed())
  {
    const double currTime = windowing.getTime();
    const float diffTime = static_cast<float>(currTime - lastTime);
    lastTime = currTime;

    windowing.poll();

    processInput(diffTime);

    drawFrame();
  }
}

void App::processInput(float dt)
{
  if (mainWindow->keyboard[KeyboardKey::kEscape] == ButtonState::Falling)
    mainWindow->askToClose();

  if (is_held_down(mainWindow->keyboard[KeyboardKey::kLeftShift]))
    camMoveSpeed = 10;
  else
    camMoveSpeed = 1;

  const auto& imguiIo = ImGui::GetIO();
  if (!imguiIo.WantCaptureMouse && mainWindow->mouse[MouseButton::mbRight] == ButtonState::Rising)
    mainWindow->captureMouse = !mainWindow->captureMouse;

  if (!imguiIo.WantCaptureKeyboard)
    moveCam(mainCam, mainWindow->keyboard, dt);
  if (mainWindow->captureMouse && !imguiIo.WantCaptureMouse)
    rotateCam(mainCam, mainWindow->mouse);
}

void App::drawFrame()
{
  renderer->update(mainCam);
  renderer->drawFrame();
}

void App::moveCam(Camera& cam, const Keyboard& kb, float dt)
{

  glm::vec3 dir = {0, 0, 0};

  if (is_held_down(kb[KeyboardKey::kS]))
    dir -= cam.forward();

  if (is_held_down(kb[KeyboardKey::kW]))
    dir += cam.forward();

  if (is_held_down(kb[KeyboardKey::kA]))
    dir -= cam.right();

  if (is_held_down(kb[KeyboardKey::kD]))
    dir += cam.right();

  if (is_held_down(kb[KeyboardKey::kF]))
    dir -= cam.up();

  if (is_held_down(kb[KeyboardKey::kR]))
    dir += cam.up();

  cam.move(dt * camMoveSpeed * (length(dir) > 1e-9 ? normalize(dir) : dir));
}

void App::rotateCam(Camera& cam, const Mouse& ms)
{
  cam.rotate(-camRotateSpeed * ms.capturedPosDelta.y, -camRotateSpeed * ms.capturedPosDelta.x);

  cam.fov -= zoomSensitivity * ms.scrollDelta.y;
  if (cam.fov < 1.0f)
    cam.fov = 1.0f;
  if (cam.fov > 120.0f)
    cam.fov = 120.0f;
}
