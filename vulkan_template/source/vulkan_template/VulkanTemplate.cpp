#include "vulkan_template/VulkanTemplate.hpp"

#include "vulkan_template/app/FrameBuffer.hpp"
#include "vulkan_template/app/GraphicsContext.hpp"
#include "vulkan_template/app/PlatformWindow.hpp"
#include "vulkan_template/app/PostProcess.hpp"
#include "vulkan_template/app/Renderer.hpp"
#include "vulkan_template/app/Swapchain.hpp"
#include "vulkan_template/app/UILayer.hpp"
#include "vulkan_template/core/Log.hpp"
#include "vulkan_template/vulkan/VulkanMacros.hpp"
#include "vulkan_template/vulkan/VulkanUsage.hpp"
#include <GLFW/glfw3.h>
#include <chrono>
#include <functional>
#include <glm/vec2.hpp>
#include <optional>
#include <thread>
#include <utility>

namespace vkt
{
struct RenderTarget;
} // namespace vkt

namespace detail
{
struct Resources
{
    vkt::PlatformWindow window;
    vkt::GraphicsContext graphics;
    vkt::Swapchain swapchain;
    vkt::FrameBuffer frameBuffer;
    vkt::UILayer uiLayer;
    vkt::Renderer renderer;
    vkt::PostProcess postProcess;
};
struct Config
{
    // As a post process step, encode main render target to sRGB
    bool postProcessLinearToSRGB{true};
};

auto initialize() -> std::optional<Resources>
{
    VkExtent2D constexpr TEXTURE_MAX{4096, 4096};

    VKT_INFO("Initializing Editor resources...");

    VKT_INFO("Creating window...");

    glm::u16vec2 constexpr DEFAULT_WINDOW_EXTENT{1920, 1080};
    std::optional<vkt::PlatformWindow> windowResult{
        vkt::PlatformWindow::create(DEFAULT_WINDOW_EXTENT)
    };
    if (!windowResult.has_value())
    {
        VKT_ERROR("Failed to create window.");
        return std::nullopt;
    }

    VKT_INFO("Creating Graphics Context...");

    std::optional<vkt::GraphicsContext> graphicsResult{
        vkt::GraphicsContext::create(windowResult.value())
    };
    if (!graphicsResult.has_value())
    {
        VKT_ERROR("Failed to create graphics context.");
        return std::nullopt;
    }
    vkt::GraphicsContext& graphicsContext{graphicsResult.value()};

    VKT_INFO("Creating Swapchain...");

    std::optional<vkt::Swapchain> swapchainResult{vkt::Swapchain::create(
        graphicsContext.physicalDevice(),
        graphicsContext.device(),
        graphicsContext.surface(),
        windowResult.value().extent(),
        std::optional<VkSwapchainKHR>{}
    )};
    if (!swapchainResult.has_value())
    {
        VKT_ERROR("Failed to create swapchain.");
        return std::nullopt;
    }

    VKT_INFO("Creating Frame Buffer...");

    std::optional<vkt::FrameBuffer> frameBufferResult{vkt::FrameBuffer::create(
        graphicsContext.device(), graphicsContext.universalQueueFamily()
    )};
    if (!frameBufferResult.has_value())
    {
        VKT_ERROR("Failed to create FrameBuffer.");
        return std::nullopt;
    }

    VKT_INFO("Creating UI Layer...");

    std::optional<vkt::UILayer> uiLayerResult{vkt::UILayer::create(
        graphicsContext.instance(),
        graphicsContext.physicalDevice(),
        graphicsContext.device(),
        graphicsContext.allocator(),
        TEXTURE_MAX,
        graphicsContext.universalQueueFamily(),
        graphicsContext.universalQueue(),
        windowResult.value(),
        vkt::UIPreferences{}
    )};
    if (!uiLayerResult.has_value())
    {
        VKT_ERROR("Failed to create UI Layer.");
        return std::nullopt;
    }

    VKT_INFO("Creating Renderer...");

    std::optional<vkt::Renderer> rendererResult{
        vkt::Renderer::create(graphicsContext.device())
    };
    if (!rendererResult.has_value())
    {
        VKT_ERROR("Failed to create renderer.");
        return std::nullopt;
    }

    VKT_INFO("Creating Post Processor...");

    std::optional<vkt::PostProcess> postProcessResult{
        vkt::PostProcess::create(graphicsContext.device())
    };
    if (!postProcessResult.has_value())
    {
        VKT_ERROR("Failed to create post process instance.");
        return std::nullopt;
    }

    VKT_INFO("Successfully initialized Editor resources.");

    return Resources{
        .window = std::move(windowResult).value(),
        .graphics = std::move(graphicsResult).value(),
        .swapchain = std::move(swapchainResult).value(),
        .frameBuffer = std::move(frameBufferResult).value(),
        .uiLayer = std::move(uiLayerResult).value(),
        .renderer = std::move(rendererResult).value(),
        .postProcess = std::move(postProcessResult).value(),
    };
}

enum class LoopResult
{
    CONTINUE,
    FATAL_ERROR
};

auto mainLoop(Resources& resources, Config& config) -> LoopResult
{
    vkt::GraphicsContext& graphicsContext{resources.graphics};
    vkt::Swapchain& swapchain{resources.swapchain};
    vkt::FrameBuffer& frameBuffer{resources.frameBuffer};
    vkt::UILayer& uiLayer{resources.uiLayer};
    vkt::Renderer const& renderer{resources.renderer};
    vkt::PostProcess& postProcess{resources.postProcess};

    if (VkResult const beginFrameResult{frameBuffer.beginNewFrame()};
        beginFrameResult != VK_SUCCESS)
    {
        VKT_LOG_VK(beginFrameResult, "Failed to begin frame.");
        return LoopResult::FATAL_ERROR;
    }
    VkCommandBuffer const cmd{frameBuffer.currentFrame().mainCommandBuffer};

    {
        vkt::DockingLayout const& dockingLayout{uiLayer.begin()};

        uiLayer.HUDMenuToggle(
            "Display",
            "Post-Process Linear to sRGB",
            config.postProcessLinearToSRGB
        );

        std::optional<vkt::SceneViewport> sceneViewport{uiLayer.sceneViewport()
        };

        if (sceneViewport.has_value())
        {
            renderer.recordDraw(cmd, sceneViewport.value().texture);
        }

        uiLayer.end();
    }

    std::optional<std::reference_wrapper<vkt::RenderTarget>> uiOutput{
        uiLayer.recordDraw(cmd)
    };

    if (!uiOutput.has_value())
    {
        // TODO: make this not a fatal error, but that requires better
        // handling on frame resources like the open command buffer
        VKT_ERROR("UI Layer did not have output image.");
        return LoopResult::FATAL_ERROR;
    }

    if (config.postProcessLinearToSRGB)
    {
        postProcess.recordLinearToSRGB(cmd, uiOutput.value());
    }

    if (VkResult const endFrameResult{frameBuffer.finishFrameWithPresent(
            swapchain, graphicsContext.universalQueue(), uiOutput.value()
        )};
        endFrameResult != VK_SUCCESS)
    {
        if (endFrameResult != VK_ERROR_OUT_OF_DATE_KHR)
        {
            VKT_LOG_VK(
                endFrameResult,
                "Failed to end frame, due to non-out-of-date error."
            );
            return LoopResult::FATAL_ERROR;
        }

        if (VkResult const rebuildResult{swapchain.rebuild()};
            rebuildResult != VK_SUCCESS)
        {
            VKT_LOG_VK(
                rebuildResult, "Failed to rebuild swapchain for resizing."
            );
            return LoopResult::FATAL_ERROR;
        }
    }

    return LoopResult::CONTINUE;
}

auto runApp() -> vkt::RunResult
{
    // For usage of time suffixes i.e. 1ms
    using namespace std::chrono_literals;

    std::optional<Resources> resourcesResult{detail::initialize()};
    if (!resourcesResult.has_value())
    {
        VKT_ERROR("Failed to initialize application resources.");
        return vkt::RunResult::FAILURE;
    }
    Resources& resources{resourcesResult.value()};

    Config config{};

    glfwShowWindow(resources.window.handle());

    vkt::RunResult runResult{vkt::RunResult::SUCCESS};

    while (glfwWindowShouldClose(resources.window.handle()) == GLFW_FALSE)
    {
        glfwPollEvents();

        if (glfwGetWindowAttrib(resources.window.handle(), GLFW_ICONIFIED)
            == GLFW_TRUE)
        {
            std::this_thread::sleep_for(1ms);
            continue;
        }

        LoopResult const loopResult{mainLoop(resourcesResult.value(), config)};
        switch (loopResult)
        {
        case LoopResult::CONTINUE:
            continue;
        case LoopResult::FATAL_ERROR:
            runResult = vkt::RunResult::FAILURE;
            break;
        }
    }

    vkDeviceWaitIdle(resources.graphics.device());

    return runResult;
}

} // namespace detail

namespace vkt
{
auto run() -> RunResult
{
    vkt::Logger::initLogging();
    VKT_INFO("Logging initialized.");

    if (glfwInit() != GLFW_TRUE)
    {
        VKT_ERROR("Failed to initialize GLFW.");
        return RunResult::FAILURE;
    }

    RunResult const result{detail::runApp()};

    glfwTerminate();

    return result;
}
} // namespace vkt