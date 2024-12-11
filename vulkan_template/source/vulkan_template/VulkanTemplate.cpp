#include "vulkan_template/VulkanTemplate.hpp"

#include "vulkan_template/app_resources/DescriptorAllocator.hpp"
#include "vulkan_template/app_resources/FrameBuffer.hpp"
#include "vulkan_template/app_resources/GraphicsContext.hpp"
#include "vulkan_template/app_resources/PlatformWindow.hpp"
#include "vulkan_template/app_resources/Renderer.hpp"
#include "vulkan_template/app_resources/Swapchain.hpp"
#include "vulkan_template/app_resources/UILayer.hpp"
#include "vulkan_template/core/log.hpp"
#include <GLFW/glfw3.h>
#include <chrono>

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
};

auto initialize() -> std::optional<Resources>
{
    VKT_INFO("Initializing Editor resources...");

    VKT_INFO("Creating window...");

    glm::u16vec2 constexpr DEFAULT_WINDOW_EXTENT{1920, 1080};
    VkExtent2D constexpr TEXTURE_MAX{4096, 4096};

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

    VKT_INFO("Successfully initialized Editor resources.");

    return Resources{
        .window = std::move(windowResult).value(),
        .graphics = std::move(graphicsResult).value(),
        .swapchain = std::move(swapchainResult).value(),
        .frameBuffer = std::move(frameBufferResult).value(),
        .uiLayer = std::move(uiLayerResult).value(),
        .renderer = std::move(rendererResult).value(),
    };
}

void render(VkCommandBuffer const cmd, vkt::SceneTexture& sceneTexture) {}

auto mainLoop() -> vkt::RunResult
{
    // For usage of time suffixes i.e. 1ms
    using namespace std::chrono_literals;

    std::optional<Resources> resourcesResult{detail::initialize()};
    if (!resourcesResult.has_value())
    {
        VKT_ERROR("Failed to initialize application resources.");
        return vkt::RunResult::FAILURE;
    }
    vkt::PlatformWindow const& mainWindow{resourcesResult.value().window};
    vkt::GraphicsContext& graphicsContext{resourcesResult.value().graphics};
    vkt::Swapchain& swapchain{resourcesResult.value().swapchain};
    vkt::FrameBuffer& frameBuffer{resourcesResult.value().frameBuffer};
    vkt::UILayer& uiLayer{resourcesResult.value().uiLayer};
    vkt::Renderer& renderer{resourcesResult.value().renderer};

    glfwShowWindow(mainWindow.handle());

    while (glfwWindowShouldClose(mainWindow.handle()) == GLFW_FALSE)
    {
        glfwPollEvents();

        if (glfwGetWindowAttrib(mainWindow.handle(), GLFW_ICONIFIED)
            == GLFW_TRUE)
        {
            std::this_thread::sleep_for(1ms);
            continue;
        }

        if (VkResult const beginFrameResult{frameBuffer.beginNewFrame()};
            beginFrameResult != VK_SUCCESS)
        {
            VKT_LOG_VK(beginFrameResult, "Failed to begin frame.");
            return vkt::RunResult::FAILURE;
        }
        VkCommandBuffer const cmd{frameBuffer.currentFrame().mainCommandBuffer};

        {
            vkt::DockingLayout const& dockingLayout{uiLayer.begin()};

            std::optional<vkt::SceneViewport> sceneViewport{
                uiLayer.sceneViewport()
            };

            if (sceneViewport.has_value())
            {
                renderer.recordDraw(cmd, sceneViewport.value().texture);
            }

            uiLayer.end();
        }

        std::optional<std::reference_wrapper<vkt::SceneTexture>> uiOutput{
            uiLayer.recordDraw(cmd)
        };

        if (!uiOutput.has_value())
        {
            // TODO: make this not a fatal error, but that requires better
            // handling on frame resources like the open command buffer
            VKT_ERROR("UI Layer did not have output image.");
            return vkt::RunResult::FAILURE;
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
                return vkt::RunResult::FAILURE;
            }

            if (VkResult rebuildResult{swapchain.rebuild()};
                rebuildResult != VK_SUCCESS)
            {
                VKT_LOG_VK(
                    rebuildResult, "Failed to rebuild swapchain for resizing."
                );
                return vkt::RunResult::FAILURE;
            }
        }
    }

    vkDeviceWaitIdle(graphicsContext.device());

    return vkt::RunResult::SUCCESS;
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

    RunResult const result{detail::mainLoop()};

    glfwTerminate();

    return result;
}
} // namespace vkt