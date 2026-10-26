#pragma once

#include <etna/Window.hpp>
#include <etna/PerFrameCmdMgr.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/Image.hpp>
#include <etna/Sampler.hpp>

#include <glm/glm.hpp>

#include "wsi/OsWindowingManager.hpp"

class App
{
public:
    App();
    ~App();

    void run();

private:
    void drawFrame();

    struct PushConstants
    {
        glm::vec2 iResolution;
        float iTime;
    };

    OsWindowingManager windowing;
    std::unique_ptr<OsWindow> osWindow;

    glm::uvec2 resolution;
    bool useVsync;

    std::unique_ptr<etna::Window> vkWindow;
    std::unique_ptr<etna::PerFrameCmdMgr> commandManager;

    etna::Image bufferAImage;
    etna::Image bufferBImage;
    etna::Sampler proceduralSampler;

    etna::GraphicsPipeline bufferAPipeline;
    etna::GraphicsPipeline bufferBPipeline;
    etna::GraphicsPipeline imagePipeline;
};