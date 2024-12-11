#pragma once

#include "vulkan_template/core/Integer.hpp"
#include "vulkan_template/core/UIRectangle.hpp"
#include <imgui.h>
#include <optional>
#include <string>

namespace vkt
{
// Opens a window on the ImGui stack. Further ImGui backend calls until ::end()
// or this object is destructed will render to that window.
struct UIWindowScope
{
    static auto beginMaximized(std::string const& name, UIRectangle workArea)
        -> UIWindowScope;

    static auto
    beginDockable(std::string const& name, std::optional<ImGuiID> dockspace)
        -> UIWindowScope;

    static auto beginDockable(
        std::string const& name, bool& open, std::optional<ImGuiID> dockspace
    ) -> UIWindowScope;

    auto operator=(UIWindowScope const& other) -> UIWindowScope& = delete;
    auto operator=(UIWindowScope&& other) -> UIWindowScope& = delete;
    UIWindowScope(UIWindowScope&& other) noexcept;

    ~UIWindowScope();
    void end();

    // Returns whether this window is open, i.e. active in the ImGui stack
    [[nodiscard]] auto isOpen() const -> bool;
    // Gives the rectangle this window occupies on the screen, in pixel units
    [[nodiscard]] auto screenRectangle() const -> UIRectangle const&;

private:
    UIWindowScope(
        UIRectangle screenRectangle, bool open, uint16_t styleVariables
    )
        : m_screenRectangle{screenRectangle}
        , m_open{open}
        , m_styleVariables{styleVariables}
        , m_initialized{true}
    {
    }

    UIRectangle m_screenRectangle{};
    bool m_open{false};
    uint16_t m_styleVariables{0};
    bool m_initialized{false};
};
} // namespace vkt