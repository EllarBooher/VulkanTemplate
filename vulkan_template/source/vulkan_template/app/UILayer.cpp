#include "UILayer.hpp"

#include "vulkan_template/app/PlatformWindow.hpp"
#include "vulkan_template/app/SceneTexture.hpp"
#include "vulkan_template/core/Log.hpp"
#include "vulkan_template/core/UIRectangle.hpp"
#include "vulkan_template/core/UIWindowScope.hpp"
#include "vulkan_template/vulkan/Image.hpp"
#include "vulkan_template/vulkan/ImageView.hpp"
#include "vulkan_template/vulkan/VulkanMacros.hpp"
#include "vulkan_template/vulkan/VulkanStructs.hpp"
#include <algorithm>
#include <glm/common.hpp>
#include <glm/exponential.hpp>
#include <glm/ext/vector_relational.hpp>
#include <glm/vec3.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <imgui_internal.h>
#include <implot.h>
#include <span>
#include <utility>
#include <vector>

namespace detail
{
auto buildDefaultMultiWindowLayout(
    vkt::UIRectangle workArea, ImGuiID parentNode
) -> vkt::DockingLayout
{
    ImGui::DockBuilderAddNode(parentNode);

    // Set the size and position:
    ImGui::DockBuilderSetNodeSize(parentNode, workArea.size());
    ImGui::DockBuilderSetNodePos(parentNode, workArea.pos());

    ImGuiID parentID{parentNode};

    ImGuiID const leftID{ImGui::DockBuilderSplitNode(
        parentID, ImGuiDir_Left, 0.3, nullptr, &parentID
    )};

    ImGuiID const rightID{ImGui::DockBuilderSplitNode(
        parentID, ImGuiDir_Right, 0.2 / (1.0 - 0.3), nullptr, &parentID
    )};

    ImGuiID const centerBottomID{ImGui::DockBuilderSplitNode(
        parentID, ImGuiDir_Down, 0.2, nullptr, &parentID
    )};

    ImGuiID const centerTopID{parentID};

    ImGui::DockBuilderFinish(parentNode);

    return vkt::DockingLayout{
        .left = leftID,
        .right = rightID,
        .centerBottom = centerBottomID,
        .centerTop = centerTopID,
    };
}

void renderPreferences(
    bool& open, vkt::UIPreferences& preferences, vkt::HUDState& hud
)
{
    if (ImGui::Begin("Preferences", &open))
    {
        float constexpr DPI_SPEED{0.05F};
        float constexpr DPI_MIN{0.5F};
        float constexpr DPI_MAX{4.0F};

        ImGui::DragFloat(
            "DPI Scale", &preferences.dpiScale, DPI_SPEED, DPI_MIN, DPI_MAX
        );

        ImGui::TextWrapped("Some DPI Scale values will produce blurry fonts, "
                           "so consider using an integer value.");

        if (ImGui::Button("Apply"))
        {
            hud.applyPreferencesRequested = true;
        }
        if (ImGui::Button("Reset"))
        {
            hud.resetPreferencesRequested = true;
        }
    }
    ImGui::End();
}

auto renderHUD(vkt::UIPreferences& preferences) -> vkt::HUDState
{
    vkt::HUDState hud{};

    ImGuiViewport const& viewport{*ImGui::GetMainViewport()};
    { // Create background windw, as a target for docking

        ImGuiWindowFlags constexpr WINDOW_INVISIBLE_FLAGS{
            ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking
            | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoBackground
            | ImGuiWindowFlags_NoBringToFrontOnFocus
            | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoNavFocus
        };

        ImGui::SetNextWindowPos(viewport.WorkPos);
        ImGui::SetNextWindowSize(viewport.WorkSize);
        ImGui::SetNextWindowViewport(viewport.ID);

        bool resetLayoutRequested{false};

        static bool maximizeSceneViewport{false};
        bool const maximizeSceneViewportLastValue{maximizeSceneViewport};

        static bool showPreferences{false};
        static bool showUIDemoWindow{false};
        static bool showImGuiDemoWindow{false};

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));

        ImGui::Begin("BackgroundWindow", nullptr, WINDOW_INVISIBLE_FLAGS);

        ImGui::PopStyleVar(3);

        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("Tools"))
            {
                ImGui::MenuItem("Preferences", nullptr, &showPreferences);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Window"))
            {
                ImGui::MenuItem(
                    "Maximize Scene Viewport", nullptr, &maximizeSceneViewport
                );
                ImGui::MenuItem("UI Demo Window", nullptr, &showUIDemoWindow);
                ImGui::MenuItem(
                    "ImGui Demo Window", nullptr, &showImGuiDemoWindow
                );
                ImGui::MenuItem(
                    "Reset Window Layout", nullptr, &resetLayoutRequested
                );
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        bool const maximizeEnded{
            maximizeSceneViewportLastValue && !maximizeSceneViewport
        };

        if (resetLayoutRequested || maximizeEnded)
        {
            hud.rebuildLayoutRequested = true;

            maximizeSceneViewport = false;
        }

        hud.maximizeSceneViewport = maximizeSceneViewport;

        hud.workArea = vkt::UIRectangle::fromPosSize(
            ImGui::GetCursorPos(), ImGui::GetContentRegionAvail()
        );
        hud.dockspaceID = ImGui::DockSpace(ImGui::GetID("BackgroundDockSpace"));

        ImGui::End();

        if (showPreferences)
        {
            renderPreferences(showPreferences, preferences, hud);
        }

        if (showImGuiDemoWindow)
        {
            ImGui::ShowDemoWindow(&showImGuiDemoWindow);
        }
    }

    static bool firstLoop{true};
    if (firstLoop)
    {
        hud.rebuildLayoutRequested = true;
        firstLoop = false;
    }

    return hud;
}

struct SceneViewportResult
{
    std::optional<vkt::UIRectangle> screenPixels;
    bool focused;
};

auto sceneViewportWindow(
    std::optional<ImGuiID> dockNode,
    std::optional<vkt::UIRectangle> maximizeArea,
    ImTextureID const sceneTexture,
    ImVec2 const sceneTextureMax,
    bool const focused
) -> SceneViewportResult
{
    uint16_t pushedStyleColors{0};
    if (focused)
    {
        ImVec4 const activeTitleColor{
            ImGui::GetStyleColorVec4(ImGuiCol_TitleBgActive)
        };
        ImGui::PushStyleColor(ImGuiCol_WindowBg, activeTitleColor);
        pushedStyleColors += 1;
    }

    char const* const WINDOW_TITLE{"Scene Viewport"};

    vkt::UIWindowScope sceneViewport{
        maximizeArea.has_value()
            ? vkt::UIWindowScope::beginMaximized(
                WINDOW_TITLE, maximizeArea.value()
            )
            : vkt::UIWindowScope::beginDockable(WINDOW_TITLE, dockNode)
    };

    if (!sceneViewport.isOpen())
    {
        return {
            .screenPixels = std::nullopt,
            .focused = false,
        };
    }

    glm::vec2 const contentExtent{sceneViewport.screenRectangle().size()};
    float const textHeight{
        ImGui::CalcTextSize("").y + ImGui::GetStyle().ItemSpacing.y
    };

    glm::vec2 const imageMin{0.0F};
    glm::vec2 const imageMax{contentExtent - glm::vec2{0.0F, textHeight}};

    ImVec2 const uvMin{imageMin / glm::vec2{sceneTextureMax}};
    ImVec2 const uvMax{imageMax / glm::vec2{sceneTextureMax}};

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{});

    ImVec2 const imageStartScreenPos{ImGui::GetCursorScreenPos()};
    bool const clicked = ImGui::ImageButton(
        "##viewport",
        sceneTexture,
        (imageMax - imageMin),
        uvMin,
        uvMax,
        ImVec4{0.0F, 0.0F, 0.0F, 1.0F},
        ImVec4{1.0F, 1.0F, 1.0F, 1.0F}
    );

    ImGui::PopStyleVar();

    sceneViewport.end();

    ImGui::PopStyleColor(pushedStyleColors);

    return {
        .screenPixels = vkt::UIRectangle::fromPosSize(
            imageStartScreenPos, imageMax - imageMin
        ),
        .focused = clicked,
    };
}
} // namespace detail

namespace vkt
{
void uiReload(VkDevice const device, UIPreferences const preferences)
{
    float constexpr FONT_BASE_SIZE{13.0F};

    ImFontConfig fontConfig{};
    fontConfig.SizePixels = FONT_BASE_SIZE * preferences.dpiScale;
    fontConfig.OversampleH = 1;
    fontConfig.OversampleV = 1;
    fontConfig.PixelSnapH = true;

    ImGui::GetIO().Fonts->Clear();
    ImGui::GetIO().Fonts->AddFontDefault(&fontConfig);

    // Wait for idle since we are modifying backend resources
    vkDeviceWaitIdle(device);
    // We destroy this to later force a rebuild when the fonts are needed.
    ImGui_ImplVulkan_DestroyFontsTexture();

    // TODO: is rebuilding the font with a specific scale good?
    // ImGui recommends building fonts at various sizes then just
    // selecting them

    // Reset style so further scaling works off the base "1.0x" scaling
    // TODO: Resetting is problematic since not all fields are sizes, changes we
    // have made might be overwritten. A more comprehensive fix is needed.
    ImGuiStyle newStyle{};
    std::copy(
        std::begin(ImGui::GetStyle().Colors),
        std::end(ImGui::GetStyle().Colors),
        std::begin(newStyle.Colors)
    );
    newStyle.ScaleAllSizes(preferences.dpiScale);
    ImGui::GetStyle() = newStyle;
}

UILayer::UILayer(UILayer&& other) noexcept { *this = std::move(other); }

auto UILayer::operator=(UILayer&& other) noexcept -> UILayer&
{
    m_backendInitialized = std::exchange(other.m_backendInitialized, false);

    m_reloadNecessary = std::exchange(other.m_reloadNecessary, false);
    m_currentPreferences =
        std::exchange(other.m_currentPreferences, UIPreferences{});
    m_defaultPreferences =
        std::exchange(other.m_defaultPreferences, UIPreferences{});

    m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
    m_imguiPool = std::exchange(other.m_imguiPool, VK_NULL_HANDLE);

    m_currentHUD = std::exchange(other.m_currentHUD, HUDState{});
    m_currentDockingLayout =
        std::exchange(other.m_currentDockingLayout, DockingLayout{});

    m_sceneTexture = std::move(other.m_sceneTexture);
    m_imguiSceneTextureHandle =
        std::exchange(other.m_imguiSceneTextureHandle, nullptr);
    m_outputTexture = std::move(other.m_outputTexture);

    return *this;
}

UILayer::~UILayer() { destroy(); }
UILayer::UILayer() = default;

void UILayer::destroy()
{
    if (m_backendInitialized)
    {
        ImPlot::DestroyContext();

        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        m_backendInitialized = false;
    }

    if (m_device != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(m_device, m_imguiPool, nullptr);
        // m_imguiSceneTextureHandle is freed here
    }
    else if (m_imguiPool != VK_NULL_HANDLE)
    {
        VKT_WARNING("UILayer: Device was NULL when pool was not. The pool was "
                    "likely leaked.");
    }

    m_sceneTexture.reset();
    m_outputTexture.reset();

    m_device = VK_NULL_HANDLE;

    m_reloadNecessary = false;
    m_currentPreferences = {};
    m_defaultPreferences = {};

    m_currentHUD = {};
    m_currentDockingLayout = {};
}

auto UILayer::create(
    VkInstance const instance,
    VkPhysicalDevice const physicalDevice,
    VkDevice const device,
    VmaAllocator const allocator,
    VkExtent2D textureCapacity,
    uint32_t const graphicsQueueFamily,
    VkQueue const graphicsQueue,
    PlatformWindow& mainWindow,
    UIPreferences const defaultPreferences
) -> std::optional<UILayer>
{
    std::optional<UILayer> layerResult{UILayer{}};
    UILayer& layer{layerResult.value()};

    std::vector<VkDescriptorPoolSize> const poolSizes{
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}
    };

    VkDescriptorPoolCreateInfo const poolInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,

        .maxSets = 1000,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data(),
    };

    VKT_TRY_VK(
        vkCreateDescriptorPool(device, &poolInfo, nullptr, &layer.m_imguiPool),
        "Failed to create descriptor pool for Dear ImGui",
        std::nullopt
    );

    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGui::StyleColorsDark();
    for (ImVec4& styleColor : ImGui::GetStyle().Colors)
    {
        // We linearize the colors, since ImGui seems to have picked its colors
        // such that they look best when interpreted as non-linear

        // Transfer implementation as defined in
        // https://www.color.org/chardata/rgb/srgb.xalter

        glm::vec3 const rgb{styleColor.x, styleColor.y, styleColor.z};
        glm::bvec3 const linearCutoff{
            glm::lessThan(rgb, glm::vec3(0.0031308F * 12.92F))
        };
        glm::vec3 const linear = rgb / glm::vec3(12.92F);
        glm::vec3 const nonlinear = glm::pow(
            (rgb + glm::vec3(0.055F)) / glm::vec3(1.055F), glm::vec3(2.4F)
        );

        glm::vec3 const converted{
            0.95F * glm::mix(nonlinear, linear, linearCutoff)
        };

        styleColor.x = converted.x;
        styleColor.y = converted.y;
        styleColor.z = converted.z;
    }

    ImVec4 constexpr MODAL_BACKGROUND_DIM{0.0F, 0.0F, 0.0F, 0.8F};
    ImGui::GetStyle().Colors[ImGuiCol_ModalWindowDimBg] = MODAL_BACKGROUND_DIM;

    ImGui_ImplGlfw_InitForVulkan(mainWindow.handle(), true);

    // Load functions since we are using volk,
    // and not the built-in vulkan loader
    ImGui_ImplVulkan_LoadFunctions(
        [](char const* functionName, void* vkInstance)
    {
        return vkGetInstanceProcAddr(
            *(reinterpret_cast<VkInstance*>(vkInstance)), functionName
        );
    },
        const_cast<VkInstance*>(&instance)
    );

    // This amount is recommended by ImGui to satisfy validation layers, even if
    // a little wasteful
    VkDeviceSize constexpr IMGUI_MIN_ALLOCATION_SIZE{1024ULL * 1024ULL};

    auto const checkVkResult_imgui{
        [](VkResult const result)
    {
        if (result == VK_SUCCESS)
        {
            return;
        }

        VKT_ERROR(
            "Dear ImGui Detected Vulkan Error : {}", string_VkResult(result)
        );
    },
    };

    std::vector<VkFormat> const colorAttachmentFormats{
        VK_FORMAT_R16G16B16A16_UNORM
    };
    VkPipelineRenderingCreateInfo const dynamicRenderingInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .pNext = nullptr,

        .viewMask = 0, // Not sure on this value
        .colorAttachmentCount =
            static_cast<uint32_t>(colorAttachmentFormats.size()),
        .pColorAttachmentFormats = colorAttachmentFormats.data(),

        .depthAttachmentFormat = VK_FORMAT_UNDEFINED,
        .stencilAttachmentFormat = VK_FORMAT_UNDEFINED,
    };

    ImGui_ImplVulkan_InitInfo initInfo{
        .Instance = instance,
        .PhysicalDevice = physicalDevice,
        .Device = device,

        .QueueFamily = graphicsQueueFamily,
        .Queue = graphicsQueue,

        .DescriptorPool = layer.m_imguiPool,

        .MinImageCount = 3,
        .ImageCount = 3,
        .MSAASamples = VK_SAMPLE_COUNT_1_BIT, // No MSAA

        // Dynamic rendering
        .UseDynamicRendering = true,
        .PipelineRenderingCreateInfo = dynamicRenderingInfo,

        // Allocation/Debug
        .Allocator = nullptr,
        .CheckVkResultFn = checkVkResult_imgui,
        .MinAllocationSize = IMGUI_MIN_ALLOCATION_SIZE,
    };

    ImGui_ImplVulkan_Init(&initInfo);

    layer.m_backendInitialized = true;

    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    if (std::optional<SceneTexture> outputTextureResult{SceneTexture::create(
            device,
            allocator,
            SceneTexture::CreateParameters{
                .max = textureCapacity,
                .color = VK_FORMAT_R16G16B16A16_UNORM,
                .depth = VK_FORMAT_D32_SFLOAT
            }
        )};
        outputTextureResult.has_value())
    {
        layer.m_outputTexture = std::make_unique<SceneTexture>(
            std::move(outputTextureResult).value()
        );
    }
    else
    {
        VKT_ERROR("Failed to allocate UI Layer output texture.");
        return std::nullopt;
    }
    if (std::optional<SceneTexture> sceneTextureResult{SceneTexture::create(
            device,
            allocator,
            SceneTexture::CreateParameters{
                .max = textureCapacity,
                .color = VK_FORMAT_R16G16B16A16_UNORM,
                .depth = VK_FORMAT_D32_SFLOAT
            }
        )};
        sceneTextureResult.has_value())
    {
        layer.m_sceneTexture =
            std::make_unique<SceneTexture>(std::move(sceneTextureResult).value()
            );

        SceneTexture& sceneTexture{*layer.m_sceneTexture};

        layer.m_imguiSceneTextureHandle = ImGui_ImplVulkan_AddTexture(
            sceneTexture.colorSampler(),
            sceneTexture.color().view(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );
    }
    else
    {
        VKT_ERROR("Failed to allocate UI Layer scene texture.");
        return std::nullopt;
    }

    layer.m_defaultPreferences = defaultPreferences;
    layer.m_currentPreferences = defaultPreferences;
    layer.m_device = device;

    uiReload(device, layer.m_currentPreferences);

    return layerResult;
}
auto UILayer::begin() -> DockingLayout const&
{
    if (m_reloadNecessary)
    {
        uiReload(m_device, m_currentPreferences);

        m_reloadNecessary = false;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    m_open = true;

    m_currentHUD = detail::renderHUD(m_currentPreferences);

    m_reloadNecessary = m_currentHUD.applyPreferencesRequested
                     || m_currentHUD.resetPreferencesRequested;
    if (m_currentHUD.resetPreferencesRequested)
    {
        m_currentPreferences = m_defaultPreferences;
    }

    m_currentDockingLayout = {};
    if (m_currentHUD.rebuildLayoutRequested && m_currentHUD.dockspaceID != 0)
    {
        m_currentDockingLayout = detail::buildDefaultMultiWindowLayout(
            m_currentHUD.workArea, m_currentHUD.dockspaceID
        );
    }

    return m_currentDockingLayout;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
auto UILayer::HUDMenuItem(std::string const& menu, std::string const& item)
    const -> bool
{
    if (!m_open)
    {
        VKT_WARNING("UILayer method called while UI frame is not open.");
        return false;
    }

    bool clicked{};

    ImGui::Begin("BackgroundWindow");

    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu(menu.c_str()))
        {
            clicked = ImGui::MenuItem(item.c_str());
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    ImGui::End();

    return clicked;
}

void UILayer::HUDMenuToggle(
    std::string const& menu, std::string const& item, bool& value
) const
{
    if (!m_open)
    {
        VKT_WARNING("UILayer method called while UI frame is not open.");
        return;
    }

    ImGui::Begin("BackgroundWindow");

    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu(menu.c_str()))
        {
            ImGui::MenuItem(item.c_str(), nullptr, &value);
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    ImGui::End();
}

auto UILayer::sceneTextureLayout() const -> std::optional<VkDescriptorSetLayout>
{
    if (m_sceneTexture == nullptr)
    {
        return std::nullopt;
    }

    return m_sceneTexture->singletonLayout();
}

auto UILayer::sceneViewport(bool const forceFocus)
    -> std::optional<SceneViewport>
{
    if (m_sceneTexture == nullptr)
    {
        VKT_WARNING("No scene texture to draw into.");
        return std::nullopt;
    }

    VkExtent2D const sceneTextureMax{m_sceneTexture->color().image().extent2D()
    };
    detail::SceneViewportResult const windowResult{detail::sceneViewportWindow(
        m_currentDockingLayout.centerTop,
        m_currentHUD.maximizeSceneViewport ? m_currentHUD.workArea
                                           : std::optional<UIRectangle>{},
        m_imguiSceneTextureHandle,
        ImVec2{
            static_cast<float>(sceneTextureMax.width),
            static_cast<float>(sceneTextureMax.height)
        },
        forceFocus
    )};

    if (!windowResult.screenPixels.has_value())
    {
        // Widget did not render any area, there is no viewport to render the
        // scene into
        return std::nullopt;
    }

    // This value should be returned by the method sceneViewportWindow above
    // that actually gives the image to the UI backend, but this works for now.
    VkRect2D const sceneTextureSubregion{
        .extent =
            VkExtent2D{
                .width = static_cast<uint32_t>(
                    windowResult.screenPixels.value().size().x
                ),
                .height = static_cast<uint32_t>(
                    windowResult.screenPixels.value().size().y
                )
            }
    };
    m_sceneTexture->setSize(sceneTextureSubregion);

    return SceneViewport{
        .focused = windowResult.focused,
        .texture = *m_sceneTexture,
        .windowExtent = windowResult.screenPixels.value()
    };
}

auto UILayer::sceneTexture() -> SceneTexture const& { return *m_sceneTexture; }

void UILayer::end()
{
    if (!m_open)
    {
        VKT_ERROR("UILayer::end() called without matching UILayer::open().");
        return;
    };

    ImGui::Render();

    m_open = false;
}
auto UILayer::recordDraw(VkCommandBuffer const cmd)
    -> std::optional<std::reference_wrapper<SceneTexture>>
{
    if (m_outputTexture == nullptr)
    {
        VKT_ERROR("UI Layer had no texture to render to.");
        return std::nullopt;
    }

    if (m_sceneTexture != nullptr)
    {
        m_sceneTexture->color().recordTransitionBarriered(
            cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );
    }

    m_outputTexture->color().recordTransitionBarriered(
        cmd, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    );

    ImDrawData* const drawData{ImGui::GetDrawData()};

    // TODO: when is this offset nonzero?
    // TODO: Is this offset synced with what imgui will render into?
    VkRect2D const renderedArea{
        .offset{VkOffset2D{
            .x = static_cast<int32_t>(drawData->DisplayPos.x),
            .y = static_cast<int32_t>(drawData->DisplayPos.y),
        }},
        .extent{VkExtent2D{
            .width = static_cast<uint32_t>(drawData->DisplaySize.x),
            .height = static_cast<uint32_t>(drawData->DisplaySize.y),
        }},
    };
    m_outputTexture->setSize(renderedArea);

    VkRenderingAttachmentInfo const colorAttachmentInfo{renderingAttachmentInfo(
        m_outputTexture->color().view(),
        VK_IMAGE_LAYOUT_GENERAL,
        VkClearValue{
            .color = VkClearColorValue{.float32 = {0.0F, 0.0F, 0.0F, 1.0F}}
        }
    )};
    std::vector<VkRenderingAttachmentInfo> const colorAttachments{
        colorAttachmentInfo
    };
    VkRenderingInfo const renderInfo{
        renderingInfo(renderedArea, colorAttachments, nullptr)
    };
    vkCmdBeginRendering(cmd, &renderInfo);

    ImGui_ImplVulkan_RenderDrawData(drawData, cmd);

    vkCmdEndRendering(cmd);

    return *m_outputTexture;
}
} // namespace vkt