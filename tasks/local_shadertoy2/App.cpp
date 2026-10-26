#include "App.hpp"

#include <etna/Etna.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/RenderTargetStates.hpp>
#include <GLFW/glfw3.h>

App::App()
  : resolution{1920, 1080}
  , useVsync{true}
{
  // First, we need to initialize Vulkan, which is not trivial because
  // extensions are required for just about anything.
  {
    // GLFW tells us which extensions it needs to present frames to the OS window.
    // Actually rendering anything to a screen is optional in Vulkan, you can
    // alternatively save rendered frames into files, send them over network, etc.
    // Instance extensions do not depend on the actual GPU, only on the OS.
    auto glfwInstExts = windowing.getRequiredVulkanInstanceExtensions();

    std::vector<const char*> instanceExtensions{glfwInstExts.begin(), glfwInstExts.end()};

    // We also need the swapchain device extension to get access to the OS
    // window from inside of Vulkan on the GPU.
    // Device extensions require HW support from the GPU.
    // Generally, in Vulkan, we call the GPU a "device" and the CPU/OS combination a "host."
    std::vector<const char*> deviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    // Etna does all of the Vulkan initialization heavy lifting.
    // You can skip figuring out how it works for now.
    etna::initialize(etna::InitParams{
      .applicationName = "Local Shadertoy",
      .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
      .instanceExtensions = instanceExtensions,
      .deviceExtensions = deviceExtensions,
      // Replace with an index if etna detects your preferred GPU incorrectly
      .physicalDeviceIndexOverride = {},
      .numFramesInFlight = 1,
    });
  }

  // Next, we need a magical Etna helper to send commands to the GPU.
  // How it is actually performed is not trivial, but we can skip this for now.
  commandManager = etna::get_context().createPerFrameCmdMgr();

  // Now we can create an OS window
  osWindow = windowing.createWindow(OsWindow::CreateInfo{
    .resolution = resolution,
  });

  // But we also need to hook the OS window up to Vulkan manually!
  {
    // First, we ask GLFW to provide a "surface" for the window,
    // which is an opaque description of the area where we can actually render.
    auto surface = osWindow->createVkSurface(etna::get_context().getInstance());

    // Then we pass it to Etna to do the complicated work for us
    vkWindow = etna::get_context().createWindow(etna::Window::CreateInfo{
      .surface = std::move(surface),
    });

    // And finally ask Etna to create the actual swapchain so that we can
    // get (different) images each frame to render stuff into.
    // Here, we do not support window resizing, so we only need to call this once.
    auto [w, h] = vkWindow->recreateSwapchain(etna::Window::DesiredProperties{
      .resolution = {resolution.x, resolution.y},
      .vsync = useVsync,
      .numFramesInFlight = static_cast<uint32_t>(commandManager->getCmdBufferCount()),
    });

    // Technically, Vulkan might fail to initialize a swapchain with the requested
    // resolution and pick a different one. This, however, does not occur on platforms
    // we support. Still, it's better to follow the "intended" path.
    resolution = {w, h};
  }

  // TODO: Initialize any additional resources you require here!

  etna::create_program(
    "buffer_a",
    {LOCAL_SHADERTOY2_SHADERS_ROOT "buffer_a.frag.spv",
     LOCAL_SHADERTOY2_SHADERS_ROOT "fullscreen.vert.spv"});

  etna::create_program(
    "buffer_b",
    {LOCAL_SHADERTOY2_SHADERS_ROOT "buffer_b.frag.spv",
     LOCAL_SHADERTOY2_SHADERS_ROOT "fullscreen.vert.spv"});

  etna::create_program(
    "image",
    {LOCAL_SHADERTOY2_SHADERS_ROOT "image.frag.spv",
     LOCAL_SHADERTOY2_SHADERS_ROOT "fullscreen.vert.spv"});

  auto& ctx = etna::get_context();

  const auto textureUsage =
    vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;

  bufferAImage = ctx.createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{resolution.x, resolution.y, 1},
    .name = "ProceduralBufferA",
    .format = vk::Format::eR8G8B8A8Unorm,
    .imageUsage = textureUsage,
  });

  bufferBImage = ctx.createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{resolution.x, resolution.y, 1},
    .name = "ProceduralBufferB",
    .format = vk::Format::eR8G8B8A8Unorm,
    .imageUsage = textureUsage,
  });

  proceduralSampler = etna::Sampler(etna::Sampler::CreateInfo{.name = "procedural_sampler"});

  auto& pipelineManager = ctx.getPipelineManager();

  auto makeFullscreenPipeline =
    [&pipelineManager](const char* program_name, vk::Format color_format) {
      return pipelineManager.createGraphicsPipeline(
        program_name,
        etna::GraphicsPipeline::CreateInfo{
          .vertexShaderInput = {},
          .rasterizationConfig =
            vk::PipelineRasterizationStateCreateInfo{
              .polygonMode = vk::PolygonMode::eFill,
              .cullMode = vk::CullModeFlagBits::eNone,
              .frontFace = vk::FrontFace::eCounterClockwise,
              .lineWidth = 1.0f,
            },
          .fragmentShaderOutput =
            {
              .colorAttachmentFormats = {color_format},
            },
        });
    };

  const auto textureFormat = vk::Format::eR8G8B8A8Unorm;

  bufferAPipeline = makeFullscreenPipeline("buffer_a", textureFormat);
  bufferBPipeline = makeFullscreenPipeline("buffer_b", textureFormat);

  imagePipeline = makeFullscreenPipeline("image", vkWindow->getCurrentFormat());
}

App::~App()
{
  ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
}

void App::run()
{
  while (!osWindow->isBeingClosed())
  {
    windowing.poll();

    drawFrame();
  }

  // We need to wait for the GPU to execute the last frame before destroying
  // all resources and closing the application.
  ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
}

void App::drawFrame()
{
  auto currentCmdBuf = commandManager->acquireNext();

  etna::begin_frame();

  auto nextSwapchainImage = vkWindow->acquireNext();

  if (nextSwapchainImage)
  {
    auto [backbuffer, backbufferView, backbufferAvailableSem, backbufferReadyForPresentSem] =
      *nextSwapchainImage;

    ETNA_CHECK_VK_RESULT(currentCmdBuf.begin(vk::CommandBufferBeginInfo{}));

    {
      PushConstants pc{
        .iResolution = glm::vec2(resolution),
        .iTime = static_cast<float>(glfwGetTime()),
      };

      auto bindFullscreenPipeline = [&](etna::GraphicsPipeline& pipeline) {
        currentCmdBuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.getVkPipeline());

        currentCmdBuf.pushConstants<PushConstants>(
          pipeline.getVkPipelineLayout(), vk::ShaderStageFlagBits::eFragment, 0, {pc});
      };

      {
        etna::RenderTargetState target(
          currentCmdBuf,
          {{0, 0}, {resolution.x, resolution.y}},
          {{
            .image = bufferAImage.get(),
            .view = bufferAImage.getView({}),
          }},
          {});

        bindFullscreenPipeline(bufferAPipeline);
        currentCmdBuf.draw(3, 1, 0, 0);
      }

      {
        etna::RenderTargetState target(
          currentCmdBuf,
          {{0, 0}, {resolution.x, resolution.y}},
          {{
            .image = bufferBImage.get(),
            .view = bufferBImage.getView({}),
          }},
          {});

        bindFullscreenPipeline(bufferBPipeline);
        currentCmdBuf.draw(3, 1, 0, 0);
      }

      auto imageInfo = etna::get_shader_program("image");

      auto set = etna::create_descriptor_set(
        imageInfo.getDescriptorLayoutId(0),
        currentCmdBuf,
        {
          etna::Binding{
            0,
            bufferAImage.genBinding(
              proceduralSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
          etna::Binding{
            1,
            bufferBImage.genBinding(
              proceduralSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
        });

      {
        etna::RenderTargetState target(
          currentCmdBuf,
          {{0, 0}, {resolution.x, resolution.y}},
          {{
            .image = backbuffer,
            .view = backbufferView,
          }},
          {});

        bindFullscreenPipeline(imagePipeline);

        currentCmdBuf.bindDescriptorSets(
          vk::PipelineBindPoint::eGraphics,
          imagePipeline.getVkPipelineLayout(),
          0,
          {set.getVkSet()},
          {});

        currentCmdBuf.draw(3, 1, 0, 0);
      }

      etna::set_state(
        currentCmdBuf,
        backbuffer,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        {},
        vk::ImageLayout::ePresentSrcKHR,
        vk::ImageAspectFlagBits::eColor);

      etna::flush_barriers(currentCmdBuf);
    }

    ETNA_CHECK_VK_RESULT(currentCmdBuf.end());

    auto renderingDone = commandManager->submit(
      std::move(currentCmdBuf),
      std::move(backbufferAvailableSem),
      std::move(backbufferReadyForPresentSem));

    const bool presented = vkWindow->present(std::move(renderingDone), backbufferView);

    if (!presented)
      nextSwapchainImage = std::nullopt;
  }

  etna::end_frame();

  // Восстанавливаем swapchain после сворачивания окна.
  if (!nextSwapchainImage && osWindow->getResolution() != glm::uvec2{0, 0})
  {
    auto [w, h] = vkWindow->recreateSwapchain(etna::Window::DesiredProperties{
      .resolution = {resolution.x, resolution.y},
      .vsync = useVsync,
      .numFramesInFlight = static_cast<uint32_t>(commandManager->getCmdBufferCount()),
    });

    ETNA_VERIFY((resolution == glm::uvec2{w, h}));
  }
}
