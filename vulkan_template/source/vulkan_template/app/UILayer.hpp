#pragma once

#include "vulkan_template/core/Integer.hpp"
#include "vulkan_template/core/UIRectangle.hpp"
#include "vulkan_template/vulkan/VulkanUsage.hpp"
#include <functional>
#include <imgui.h>
#include <memory>
#include <optional>
#include <string>

namespace vkt
{
struct PlatformWindow;
struct SceneTexture;
} // namespace vkt

namespace vkt
{
struct SceneViewport
{
    bool focused;
    std::reference_wrapper<SceneTexture> texture;

    // The screen-space pixels that the viewport takes up in the UI, useful for
    // transforming application window coordinates into scene world coordinates,
    // such as when raycasting in mouse events.
    UIRectangle windowExtent;
};

struct UIPreferences
{
    static float constexpr DEFAULT_DPI_SCALE{2.0F};

    float dpiScale{DEFAULT_DPI_SCALE};
};

struct HUDState
{
    UIRectangle workArea{};

    // The background window that acts as the parent of all the laid out windows
    ImGuiID dockspaceID{};

    bool maximizeSceneViewport{false};

    bool rebuildLayoutRequested{false};

    bool resetPreferencesRequested{false};
    bool applyPreferencesRequested{false};
};

struct DockingLayout
{
    std::optional<ImGuiID> left{};
    std::optional<ImGuiID> right{};
    std::optional<ImGuiID> centerBottom{};
    std::optional<ImGuiID> centerTop{};
};

struct UILayer
{
public:
    UILayer(UILayer const&) = delete;
    auto operator=(UILayer const&) -> UILayer& = delete;

    UILayer(UILayer&&) noexcept;
    auto operator=(UILayer&&) noexcept -> UILayer&;
    ~UILayer();

private:
    UILayer();
    void destroy();

public:
    // GLFW Detail: the backend installs any callbacks, so this can be called
    // after window callbacks are set (Such as cursor position/key event
    // callbacks).
    static auto create(
        VkInstance,
        VkPhysicalDevice,
        VkDevice,
        VmaAllocator,
        VkExtent2D textureCapacity,
        uint32_t graphicsQueueFamily,
        VkQueue graphicsQueue,
        PlatformWindow& mainWindow,
        UIPreferences defaultPreferences
    ) -> std::optional<UILayer>;

    auto begin() -> DockingLayout const&;

    [[nodiscard]] auto
    HUDMenuItem(std::string const& menu, std::string const& item) const -> bool;
    void HUDMenuToggle(
        std::string const& menu, std::string const& item, bool& value
    ) const;

    [[nodiscard]] auto sceneTextureLayout() const
        -> std::optional<VkDescriptorSetLayout>;

    auto sceneViewport(bool forceFocus = false) -> std::optional<SceneViewport>;

    // TODO: remove this once able to, to encapsulate scene texture and defer
    // exposing it until rendering time
    [[nodiscard]] auto sceneTexture() -> SceneTexture const&;

    void end();

    // Returns the final output image that should be presented.
    auto recordDraw(VkCommandBuffer)
        -> std::optional<std::reference_wrapper<SceneTexture>>;

private:
    bool m_backendInitialized{false};

    bool m_reloadNecessary{false};
    UIPreferences m_currentPreferences{};
    UIPreferences m_defaultPreferences{};

    VkDevice m_device{VK_NULL_HANDLE};

    VkDescriptorPool m_imguiPool{VK_NULL_HANDLE};

    bool m_open{false};
    HUDState m_currentHUD{};
    DockingLayout m_currentDockingLayout{};

    // A sub-texture used by the UI backend to render a scene viewport.
    std::unique_ptr<SceneTexture> m_sceneTexture;
    // An opaque handle from the Vulkan backend that contains the scene texture
    ImTextureID m_imguiSceneTextureHandle{nullptr};

    // The final output of the application viewport, with all geometry and UI
    // rendered
    std::unique_ptr<SceneTexture> m_outputTexture;
};
} // namespace vkt