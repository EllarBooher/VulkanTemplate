#pragma once

#include "vulkan_template/core/Integer.hpp"
#include <cassert>
#include <functional>
#include <glm/vec3.hpp>
#include <imgui.h>
#include <limits>
#include <optional>
#include <span>
#include <string>

namespace vkt
{
struct FloatBounds
{
    float min = std::numeric_limits<float>::lowest();
    float max = std::numeric_limits<float>::max();
};

struct PropertySliderBehavior
{
    float const speed{0.0F};
    ImGuiSliderFlags const flags{ImGuiSliderFlags_None};
    FloatBounds bounds{};
};

struct PropertyTableRowTexts
{
    std::string name{};
    std::string tooltip{};
};

struct PropertyTable
{
private:
    PropertyTable();
    using Self = PropertyTable;

    // We use 16 bit unsized integers that fit inside ImGui's expected
    // 32 bit signed integers.
    static uint16_t constexpr PROPERTY_INDEX{0};
    static uint16_t constexpr VALUE_INDEX{1};
    static uint16_t constexpr RESET_INDEX{2};

    uint16_t const m_styleVariablesCount{0};

    // Used to avoid name collision, by incrementing and salting names passed
    // to ImGui.
    size_t m_propertyCount{0};

    bool m_open{false};
    bool m_rowOpen{false};

    // TODO: sizing and bounds issues: this should maybe be an int32. Depth
    // could conceivably be negative and cause negative indenting.
    // Also, ImGui IDs use an int32, so this should probably be int32
    // or uint16 alongside m_propertyCount.
    size_t m_childPropertyDepth{0};

    // The depth at which we first collapsed. If no value is set,
    // we are not collapsed. We track this so when nesting child properties
    // within a collapsed child, we can see when to stop being collapsed.
    std::optional<size_t> m_childPropertyFirstCollapse{std::nullopt};

    explicit PropertyTable(uint16_t styleVariables)
        : m_styleVariablesCount(styleVariables)
        , m_open(true)
    {
    }

    static void nameColumn(PropertyTableRowTexts const& rowTexts);

    static auto resetColumn(std::string const& name, bool visible) -> bool;

    static auto collapseButtonWidth() -> float
    {
        return ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
    }

    void checkInvariant() const
    {
        // If we are collapsed, it must have occured at the current or an
        // earlier depth. Violation of this invariant likely means
        // m_childPropertyDepth was decremented without updating collapse
        // status.
        assert(
            !m_childPropertyFirstCollapse.has_value()
            || m_childPropertyFirstCollapse.value() <= m_childPropertyDepth
                   && "PropertyTable collapse depth invariant violated."
        );
    }

    [[nodiscard]] [[nodiscard]] auto hideNextRow() const -> bool
    {
        return m_childPropertyFirstCollapse.has_value()
            && m_childPropertyDepth > m_childPropertyFirstCollapse.value();
    }

    // If this returns false, the row should not be modified further.
    // Do NOT call rowEnd if this returns false.
    auto rowBegin(PropertyTableRowTexts const& rowTexts) -> bool;
    void rowEnd();

public:
    // Creates a separate window, that demonstrates PropertyTable usage.
    static void demoWindow(bool& open);

    // Using the default name synchronizes many of the table's properties
    // across the window.
    static auto begin(std::string const& name = "PropertyTable")
        -> PropertyTable;

    [[nodiscard]] auto open() const -> bool;

    void end();

    // Adds an arrow button to the previous row, and enters a collapsible
    // section. Further calls to row drawing methods will be skipped until
    // childPropertyEnd is called, depending on if this rows button is
    // collapsed or not. This is tracked internally.
    auto childPropertyBegin(bool startCollapsed = true) -> PropertyTable&;

    // This adds a new row for the collapsing button.
    // See PropertyTable::childPropertyBegin.
    auto rowChildPropertyBegin(
        PropertyTableRowTexts const& rowTexts, bool startCollapsed = true
    ) -> PropertyTable&;

    // A corresponding PropertyTable::rowChildPropertyBegin
    // or PropertyTable::childPropertyBegin must have been called.
    auto childPropertyEnd() -> PropertyTable&;

    // Adds a row that contains a dropdown, containing a list of values,
    // alongside a reset button.
    auto rowDropdown(
        PropertyTableRowTexts const& rowTexts,
        size_t& selectedIndex,
        size_t const& defaultIndex,
        std::span<std::string const> displayValues
    ) -> PropertyTable&;

    // Adds a row that runs a callback for the content column. Useful to render
    // custom UI.
    auto rowCustom(
        PropertyTableRowTexts const& rowTexts,
        std::function<void()> const& contentCallback
    ) -> PropertyTable&;

    // Adds a row that runs a callback for the content column + reset column.
    // Useful to render custom UI.
    auto rowCustom(
        PropertyTableRowTexts const& rowTexts,
        std::function<void()> const& contentCallback,
        bool resetVisible,
        std::function<void()> const& resetCallback
    ) -> PropertyTable&;

    auto rowButton(
        PropertyTableRowTexts const& rowTexts,
        std::function<void()> const& clickedCallback,
        std::string const& label
    ) -> PropertyTable&;

    // Adds a row that contains an interactable text entry,
    // alongside a reset button.
    auto rowTextInput(
        PropertyTableRowTexts const& rowTexts,
        std::string& value,
        std::string const& resetValue
    ) -> PropertyTable&;

    // Adds a row that contains text that is interactable, but readonly.
    auto rowReadOnlyTextInput(
        PropertyTableRowTexts const& rowTexts,
        std::string const& value,
        bool multiline
    ) -> PropertyTable&;

    // Adds a row that contains some read only text.
    auto rowTextLabel(
        PropertyTableRowTexts const& rowTexts, std::string const& value
    ) -> PropertyTable&;

    // Adds a row that contains an interactable 32-bit signed integer entry,
    // alongside a reset button.
    auto rowInteger(
        PropertyTableRowTexts const& rowTexts,
        int32_t& value,
        int32_t const& resetValue,
        PropertySliderBehavior behavior
    ) -> PropertyTable&;

    // Adds a row that contains a read only integer.
    // TODO: more generic row types for all integer widths and types
    auto rowReadOnlyInteger(
        PropertyTableRowTexts const& rowTexts, int32_t const& value
    ) -> PropertyTable&;

    // Adds a row that contains an interactable three-float vector entry,
    // alongside a reset button.
    auto rowVec3(
        PropertyTableRowTexts const& rowTexts,
        glm::vec3& value,
        glm::vec3 const& resetValue,
        PropertySliderBehavior behavior
    ) -> PropertyTable&;

    // Adds a row with a variable number of floats displayed side by side
    auto rowVec2(
        PropertyTableRowTexts const& rowTexts,
        glm::vec2& value,
        glm::vec2 const& resetValue,
        PropertySliderBehavior behavior
    ) -> PropertyTable&;

    auto rowColor(
        PropertyTableRowTexts const& rowTexts,
        glm::vec3& value,
        glm::vec3 const& resetValue,
        PropertySliderBehavior behavior,
        size_t digits = 4
    ) -> PropertyTable&;

    // Adds a row that contains a non-interactable three-float vector entry.
    auto rowReadOnlyVec3(
        PropertyTableRowTexts const& rowTexts, glm::vec3 const& value
    ) -> PropertyTable&;

    // Adds a row that contains an interactable float entry,
    // alongside a reset button.
    auto rowFloat(
        PropertyTableRowTexts const& rowTexts,
        float& value,
        float const& resetValue,
        PropertySliderBehavior behavior
    ) -> PropertyTable&;

    // Adds a row that contains a non-interactable float entry.
    auto
    rowReadOnlyFloat(PropertyTableRowTexts const& rowTexts, float const& value)
        -> PropertyTable&;

    // Adds a row that contains an interactable boolean entry,
    // alongside a reset button.
    auto rowBoolean(
        PropertyTableRowTexts const& rowTexts,
        bool& value,
        bool const& resetValue
    ) -> PropertyTable&;

    // Adds a row that contains a non-interactable float entry.
    auto
    rowReadOnlyBoolean(PropertyTableRowTexts const& rowTexts, bool const& value)
        -> PropertyTable&;
};
} // namespace vkt