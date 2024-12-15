#include "vulkan_template/VulkanTemplate.hpp"

#include "vulkan_template/app/FrameBuffer.hpp"
#include "vulkan_template/app/GBuffer.hpp"
#include "vulkan_template/app/GraphicsContext.hpp"
#include "vulkan_template/app/LightingPass.hpp"
#include "vulkan_template/app/Mesh.hpp"
#include "vulkan_template/app/PlatformWindow.hpp"
#include "vulkan_template/app/PostProcess.hpp"
#include "vulkan_template/app/RenderTarget.hpp"
#include "vulkan_template/app/Renderer.hpp"
#include "vulkan_template/app/Scene.hpp"
#include "vulkan_template/app/Swapchain.hpp"
#include "vulkan_template/app/UILayer.hpp"
#include "vulkan_template/core/Log.hpp"
#include "vulkan_template/platform/PlatformUtils.hpp"
#include "vulkan_template/vulkan/Image.hpp"
#include "vulkan_template/vulkan/ImageView.hpp"
#include "vulkan_template/vulkan/Immediate.hpp"
#include "vulkan_template/vulkan/VulkanMacros.hpp"
#include "vulkan_template/vulkan/VulkanUsage.hpp"
#include <GLFW/glfw3.h>
#include <chrono>
#include <filesystem>
#include <functional>
#include <glm/vec2.hpp>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace
{
struct Resources
{
    vkt::PlatformWindow window;
    vkt::GraphicsContext graphics;
    vkt::ImmediateSubmissionQueue submissionQueue;
    vkt::Swapchain swapchain;
    vkt::FrameBuffer frameBuffer;
    vkt::UILayer uiLayer;
    vkt::Renderer renderer;
    vkt::GBufferPipeline gbufferPipeline;
    vkt::LightingPass lightingPass;
    vkt::PostProcess postProcess;
    vkt::GBuffer gbuffer;
    vkt::Scene scene;
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

    VKT_INFO("Creating Immediate Submission Queue...");

    std::optional<vkt::ImmediateSubmissionQueue> queueResult{
        vkt::ImmediateSubmissionQueue::create(
            graphicsContext.device(),
            graphicsContext.universalQueue(),
            graphicsContext.universalQueueFamily()
        )
    };
    if (!queueResult.has_value())
    {
        VKT_ERROR("Failed to create immediate submission queue.");
        return std::nullopt;
    }
    vkt::ImmediateSubmissionQueue& submissionQueue{queueResult.value()};

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

    std::optional<vkt::Renderer> rendererResult{vkt::Renderer::create(
        graphicsContext.device(),
        graphicsContext.allocator(),
        submissionQueue,
        vkt::Renderer::RendererArguments{
            .color =
                uiLayerResult.value().sceneTexture().color().image().format(),
            .depth =
                uiLayerResult.value().sceneTexture().depth().image().format(),
            .reverseZ = true
        }
    )};
    if (!rendererResult.has_value())
    {
        VKT_ERROR("Failed to create renderer.");
        return std::nullopt;
    }

    VKT_INFO("Creating GBuffer Pipeline...");

    std::optional<vkt::GBufferPipeline> gbufferPipelineResult{
        vkt::GBufferPipeline::create(
            graphicsContext.device(),
            vkt::GBufferPipeline::RendererArguments{
                .color =
                    uiLayerResult.value().sceneTexture().color().image().format(
                    ),
                .depth =
                    uiLayerResult.value().sceneTexture().depth().image().format(
                    ),
                .reverseZ = true
            }
        )
    };
    if (!gbufferPipelineResult.has_value())
    {
        VKT_ERROR("Failed to create GBuffer Pipeline.");
        return std::nullopt;
    }

    VKT_INFO("Creating Lighting Pass...");

    std::optional<vkt::LightingPass> lightingPassResult{
        vkt::LightingPass::create(graphicsContext.device())
    };
    if (!lightingPassResult.has_value())
    {
        VKT_ERROR("Failed to create Lighting Pass pipeline.");
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

    VKT_INFO("Creating GBuffer...");

    std::optional<vkt::GBuffer> gbufferResult{vkt::GBuffer::create(
        graphicsContext.device(), graphicsContext.allocator(), TEXTURE_MAX
    )};
    if (!gbufferResult.has_value())
    {
        VKT_ERROR("Failed to create GBuffer Pipeline.");
        return std::nullopt;
    }

    VKT_INFO("Loading Meshes from disk and creating Scene...");

    std::optional<std::filesystem::path> meshPathResult{
        vkt::openFile("Load Mesh", windowResult.value())
    };
    if (!meshPathResult.has_value())
    {
        VKT_ERROR("No path loaded, exiting.");
        return std::nullopt;
    }

    std::vector<vkt::Mesh> meshes{vkt::Mesh::fromPath(
        graphicsContext.device(),
        graphicsContext.allocator(),
        submissionQueue,
        meshPathResult.value()
    )};
    if (meshes.empty())
    {
        VKT_ERROR("Failed to load any meshes.");
        return std::nullopt;
    }

    std::optional<vkt::Scene> sceneResult{vkt::Scene::create(
        graphicsContext.device(), graphicsContext.allocator(), submissionQueue
    )};
    if (!sceneResult.has_value())
    {
        VKT_ERROR("Faield to create scene.");
        return std::nullopt;
    }
    sceneResult.value().setMesh(std::make_unique<vkt::Mesh>(std::move(meshes[0])
    ));

    VKT_INFO("Successfully initialized Application resources.");

    return Resources{
        .window = std::move(windowResult).value(),
        .graphics = std::move(graphicsResult).value(),
        .submissionQueue = std::move(queueResult).value(),
        .swapchain = std::move(swapchainResult).value(),
        .frameBuffer = std::move(frameBufferResult).value(),
        .uiLayer = std::move(uiLayerResult).value(),
        .renderer = std::move(rendererResult).value(),
        .gbufferPipeline = std::move(gbufferPipelineResult).value(),
        .lightingPass = std::move(lightingPassResult).value(),
        .postProcess = std::move(postProcessResult).value(),
        .gbuffer = std::move(gbufferResult).value(),
        .scene = std::move(sceneResult).value()
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
    // vkt::Renderer& renderer{resources.renderer};
    vkt::GBuffer& gbuffer{resources.gbuffer};
    vkt::GBufferPipeline& gbufferPipeline{resources.gbufferPipeline};
    vkt::LightingPass& lightingPass{resources.lightingPass};
    vkt::PostProcess& postProcess{resources.postProcess};
    vkt::Scene& scene{resources.scene};

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

        scene.controlsWindow(dockingLayout.right);
        lightingPass.controlsWindow(dockingLayout.right);

        if (sceneViewport.has_value())
        {
            // renderer.recordDraw(
            //     cmd, sceneViewport.value().texture, resources.meshes[0]
            //);
            gbufferPipeline.recordDraw(
                cmd, sceneViewport.value().texture, gbuffer, scene
            );
            lightingPass.recordDraw(
                cmd, sceneViewport.value().texture, gbuffer, scene
            );
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

    std::optional<Resources> resourcesResult{::initialize()};
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

} // namespace

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

    RunResult const result{::runApp()};

    glfwTerminate();

    return result;
}
} // namespace vkt