#include "PropertyTable.hpp"

#include <array>
#include <cmath>
#include <imgui_internal.h>
#include <limits>
#include <misc/cpp/imgui_stdlib.h>
#include <spdlog/fmt/bundled/core.h>
#include <unordered_map>

namespace vkt
{
PropertyTable::PropertyTable() = default;

void PropertyTable::nameColumn(PropertyTableRowTexts const& rowTexts)
{
    ImGui::TableSetColumnIndex(PROPERTY_INDEX);
    ImGui::Text("%s", rowTexts.name.c_str());

    if (!rowTexts.tooltip.empty())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetNextWindowBgAlpha(1.0F);
            if (ImGui::BeginTooltip())
            {
                float constexpr TOOLTIP_WIDTH_EM{40.0F};
                ImGui::PushTextWrapPos(
                    TOOLTIP_WIDTH_EM * ImGui::GetTextLineHeight()
                );
                ImGui::Text("%s", rowTexts.tooltip.c_str());
                ImGui::EndTooltip();
            }
        }
    }
}

auto PropertyTable::resetColumn(std::string const& name, bool const visible)
    -> bool
{
    ImGui::TableSetColumnIndex(RESET_INDEX);

    if (!visible)
    {
        return false;
    }

    float const width{ImGui::GetColumnWidth(RESET_INDEX)};
    ImGui::SetNextItemWidth(width);

    bool const clicked{ImGui::Button(
        fmt::format("<-##{}reset", name).c_str(), ImVec2{-1.0F, 0.0F}
    )};

    return clicked;
}

auto PropertyTable::begin(std::string const& name) -> PropertyTable
{
    if (!ImGui::BeginTable(
            name.c_str(),
            3,
            ImGuiTableFlags_None | ImGuiTableFlags_BordersInner
                | ImGuiTableFlags_Resizable
        ))
    {
        return PropertyTable{};
    }

    ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(
        "Value",
        ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoResize
    );
    ImGui::TableSetupColumn(
        "Reset",
        ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize,
        ImGui::GetStyle().FramePadding.x * 2 + ImGui::CalcTextSize("<-").x
    );

    ImGui::Indent(Self::collapseButtonWidth());

    ImVec2 constexpr PROPERTY_TABLE_CELL_PADDING{0.0F, 6.0F};

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, PROPERTY_TABLE_CELL_PADDING);

    uint16_t const styleVariableCount{1};

    return PropertyTable(styleVariableCount);
}

auto PropertyTable::open() const -> bool { return m_open; }

void PropertyTable::end()
{
    assert(!m_rowOpen && "end() called on PropertyTable with an open row.");
    assert(m_open && "end() called on PropertyTable that was not open.");
    assert(
        m_childPropertyDepth == 0
        && "end() called on PropertyTable without ending all nested child "
           "properties."
    );

    m_open = false;

    ImGui::PopStyleVar(m_styleVariablesCount);
    ImGui::Unindent(Self::collapseButtonWidth());

    ImGui::EndTable();
}

auto PropertyTable::childPropertyBegin(bool const startCollapsed)
    -> PropertyTable&
{
    static std::unordered_map<ImGuiID, bool> collapseStatus{};
    checkInvariant();

    bool const hideRow{hideNextRow()};
    if (!hideRow)
    {
        ImGui::TableSetColumnIndex(PROPERTY_INDEX);

        std::string const arrowButtonName{
            fmt::format("##arrowButton{}", m_propertyCount)
        };
        ImGuiID const arrowButtonID{ImGui::GetID(arrowButtonName.c_str())};

        if (!collapseStatus.contains(arrowButtonID))
        {
            collapseStatus.insert({arrowButtonID, startCollapsed});
        }

        bool& collapsed{collapseStatus.at(arrowButtonID)};
        ImGuiDir const direction{collapsed ? ImGuiDir_Right : ImGuiDir_Down};

        // We find the beginning of the previous column WITHOUT indents,
        // by passing a minimal float. Passing 0.0 to SameLine puts the button
        // after the column's text, which is not what we want.
        // There might be a better way to do this.
        auto const maxX{ImGui::GetContentRegionMax().x};
        auto const columnWidth{ImGui::GetColumnWidth()};
        auto const buttonWidth{Self::collapseButtonWidth()};

        // We must precompute the above values since
        // this line would modify them.
        ImGui::SameLine(std::numeric_limits<float>::min());

        auto const cursorX{ImGui::GetCursorPosX()};

        ImGui::SetCursorPosX(maxX - cursorX - columnWidth - buttonWidth);

        if (ImGui::ArrowButton(arrowButtonName.c_str(), direction))
        {
            collapsed = !collapsed;
        }

        if (!m_childPropertyFirstCollapse.has_value() && collapsed)
        {
            m_childPropertyFirstCollapse = m_childPropertyDepth;
        }
    }

    ImGui::PushID(static_cast<int32_t>(m_childPropertyDepth));
    m_childPropertyDepth += 1;
    ImGui::Indent(ImGui::GetStyle().IndentSpacing);

    return *this;
}

auto PropertyTable::rowChildPropertyBegin(
    PropertyTableRowTexts const& rowTexts, bool const startCollapsed
) -> PropertyTable&
{
    if (Self::rowBegin(rowTexts))
    {
        Self::rowEnd();
    }

    return childPropertyBegin(startCollapsed);
}

auto PropertyTable::childPropertyEnd() -> PropertyTable&
{
    checkInvariant();
    assert(
        m_childPropertyDepth > 0
        && "(rowChildPropertyEnd() called on PropertyTable with not enough"
           "matching rowChildPropertyBegin())"
    );

    ImGui::Unindent(ImGui::GetStyle().IndentSpacing);
    m_childPropertyDepth -= 1;
    ImGui::PopID();

    if (m_childPropertyFirstCollapse.has_value()
        && m_childPropertyFirstCollapse.value() >= m_childPropertyDepth)
    {
        m_childPropertyFirstCollapse = std::nullopt;
    }

    return *this;
}

auto PropertyTable::rowBegin(PropertyTableRowTexts const& rowTexts) -> bool
{
    assert(!m_rowOpen && "Row opened without ending the previous one.");

    checkInvariant();

    m_propertyCount += 1;

    if (hideNextRow())
    {
        return false;
    }

    m_rowOpen = true;

    ImGui::PushID(static_cast<int32_t>(m_propertyCount));
    ImGui::PushID(rowTexts.name.c_str());

    ImGui::TableNextRow();

    Self::nameColumn(rowTexts);

    return true;
}

void PropertyTable::rowEnd()
{
    m_rowOpen = false;
    ImGui::PopID(); // name
    ImGui::PopID(); // m_propertyCount
}

auto PropertyTable::rowDropdown(
    PropertyTableRowTexts const& rowTexts,
    size_t& selectedIndex,
    size_t const& defaultIndex,
    std::span<std::string const> const displayValues
) -> PropertyTable&
{
    if (!Self::rowBegin(rowTexts))
    {
        return *this;
    }

    if (selectedIndex >= displayValues.size())
    {
        selectedIndex = 0;
    }

    std::string const& previewValue{
        displayValues.empty() ? "No Possible Values."
                              : displayValues[selectedIndex]
    };

    ImGui::TableSetColumnIndex(VALUE_INDEX);

    ImGui::BeginDisabled(displayValues.empty());
    if (ImGui::BeginCombo("##combo", previewValue.c_str()))
    {
        size_t index{0};
        for (std::string const& displayValue : displayValues)
        {
            bool const isSelected{index == selectedIndex};
            if (ImGui::Selectable(displayValue.c_str(), isSelected))
            {
                selectedIndex = index;
            }

            if (isSelected)
            {
                ImGui::SetItemDefaultFocus();
            }

            index += 1;
        }
        ImGui::EndCombo();
    }

    ImGui::EndDisabled();

    if (Self::resetColumn(rowTexts.name, selectedIndex != defaultIndex))
    {
        selectedIndex = defaultIndex;
    }

    Self::rowEnd();

    return *this;
}

auto PropertyTable::rowCustom(
    PropertyTableRowTexts const& rowTexts,
    std::function<void()> const& contentCallback
) -> PropertyTable&
{
    if (!Self::rowBegin(rowTexts))
    {
        return *this;
    }

    ImGui::TableSetColumnIndex(VALUE_INDEX);

    contentCallback();

    Self::rowEnd();

    return *this;
}

auto PropertyTable::rowCustom(
    PropertyTableRowTexts const& rowTexts,
    std::function<void()> const& contentCallback,
    bool const resetVisible,
    std::function<void()> const& resetCallback
) -> PropertyTable&
{
    if (!Self::rowBegin(rowTexts))
    {
        return *this;
    }

    ImGui::TableSetColumnIndex(VALUE_INDEX);

    contentCallback();

    if (Self::resetColumn(rowTexts.name, resetVisible))
    {
        resetCallback();
    }

    Self::rowEnd();

    return *this;
}

auto PropertyTable::rowButton(
    PropertyTableRowTexts const& rowTexts,
    std::function<void()> const& clickedCallback,
    std::string const& label
) -> PropertyTable&
{
    if (!Self::rowBegin(rowTexts))
    {
        return *this;
    }

    ImGui::TableSetColumnIndex(VALUE_INDEX);

    if (ImGui::Button(label.c_str()))
    {
        clickedCallback();
    }

    Self::rowEnd();

    return *this;
}

auto PropertyTable::rowTextInput(
    PropertyTableRowTexts const& rowTexts,
    std::string& value,
    std::string const& resetValue
) -> PropertyTable&
{
    if (!Self::rowBegin(rowTexts))
    {
        return *this;
    }

    ImGui::TableSetColumnIndex(VALUE_INDEX);

    ImGui::InputText(
        fmt::format("##{}{}", rowTexts.name, m_propertyCount).c_str(), &value
    );

    if (Self::resetColumn(rowTexts.name, value != resetValue))
    {
        value = resetValue;
    }

    Self::rowEnd();

    return *this;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters): It seems unlikely to mix
// up the parameters, since name is first across all Property table row
// methods.

auto PropertyTable::rowReadOnlyTextInput(
    PropertyTableRowTexts const& rowTexts,
    std::string const& value,
    bool const multiline
) -> PropertyTable&
{
    if (!Self::rowBegin(rowTexts))
    {
        return *this;
    }

    ImGui::TableSetColumnIndex(VALUE_INDEX);
    ImGui::SetNextItemWidth(ImGui::GetColumnWidth(VALUE_INDEX));

    ImGuiInputTextFlags const flags{ImGuiInputTextFlags_ReadOnly};

    std::string local{value};
    std::string const multilineLabel{
        fmt::format("##{}{}", rowTexts.name, m_propertyCount)
    };

    ImGuiStyle const& style{ImGui::GetStyle()};
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style.Alpha * style.DisabledAlpha);
    if (multiline)
    {
        ImGui::InputTextMultiline(multilineLabel.c_str(), &local, {}, flags);
    }
    else
    {
        ImGui::InputText(multilineLabel.c_str(), &local, flags);
    }
    ImGui::PopStyleVar();

    Self::rowEnd();

    return *this;
}

auto PropertyTable::rowTextLabel(
    PropertyTableRowTexts const& rowTexts, std::string const& value
) -> PropertyTable&
{
    if (!Self::rowBegin(rowTexts))
    {
        return *this;
    }

    ImGui::TableSetColumnIndex(VALUE_INDEX);
    ImGui::SetNextItemWidth(ImGui::GetColumnWidth(VALUE_INDEX));
    ImGui::TextWrapped("%s", value.c_str());

    Self::rowEnd();

    return *this;
}

// NOLINTEND(bugprone-easily-swappable-parameters)

auto PropertyTable::rowInteger(
    PropertyTableRowTexts const& rowTexts,
    int32_t& value,
    int32_t const& resetValue,
    PropertySliderBehavior const behavior
) -> PropertyTable&
{
    if (!Self::rowBegin(rowTexts))
    {
        return *this;
    }

    ImGui::TableSetColumnIndex(VALUE_INDEX);
    ImGui::PushItemWidth(ImGui::GetColumnWidth(VALUE_INDEX));

    ImGui::DragInt(
        fmt::format("##{}{}", rowTexts.name, m_propertyCount).c_str(),
        &value,
        behavior.speed,
        std::ceil(behavior.bounds.min),
        std::floor(behavior.bounds.max),
        "%i",
        behavior.flags
    );

    ImGui::PopItemWidth();

    if (Self::resetColumn(rowTexts.name, value != resetValue))
    {
        value = resetValue;
    }

    Self::rowEnd();

    return *this;
}

auto PropertyTable::rowReadOnlyInteger(
    PropertyTableRowTexts const& rowTexts, int32_t const& value
) -> PropertyTable&
{
    if (!Self::rowBegin(rowTexts))
    {
        return *this;
    }

    ImGui::TableSetColumnIndex(VALUE_INDEX);
    ImGui::PushItemWidth(ImGui::GetColumnWidth(VALUE_INDEX));

    ImGui::BeginDisabled();

    int32_t valueCopy{value};
    ImGui::DragInt(
        fmt::format("##{}{}", rowTexts.name, m_propertyCount).c_str(),
        &valueCopy,
        0,
        0,
        0,
        "%i",
        ImGuiSliderFlags_None
    );

    ImGui::EndDisabled();

    ImGui::PopItemWidth();

    Self::rowEnd();

    return *this;
}

auto PropertyTable::rowVec3(
    PropertyTableRowTexts const& rowTexts,
    glm::vec3& value,
    glm::vec3 const& resetValue,
    PropertySliderBehavior const behavior
) -> PropertyTable&
{
    if (!Self::rowBegin(rowTexts))
    {
        return *this;
    }

    ImGui::TableSetColumnIndex(VALUE_INDEX);
    ImGui::PushMultiItemsWidths(3, ImGui::GetColumnWidth(VALUE_INDEX));
    for (size_t component{0}; component < 3; component++)
    {
        float const spacing{ImGui::GetStyle().ItemInnerSpacing.x};
        if (component > 0)
        {
            ImGui::SameLine(0.0F, spacing);
        }

        ImGui::DragFloat(
            fmt::format("##{}{}{}", rowTexts.name, m_propertyCount, component)
                .c_str(),
            &value[component],
            behavior.speed,
            behavior.bounds.min,
            behavior.bounds.max,
            "%.4f",
            behavior.flags
        );
        ImGui::PopItemWidth();
    }

    if (Self::resetColumn(rowTexts.name, value != resetValue))
    {
        value = resetValue;
    }

    Self::rowEnd();

    return *this;
}

auto PropertyTable::rowVec2(
    PropertyTableRowTexts const& rowTexts,
    glm::vec2& value,
    glm::vec2 const& resetValue,
    PropertySliderBehavior behavior
) -> PropertyTable&
{
    if (!Self::rowBegin(rowTexts))
    {
        return *this;
    }

    size_t constexpr COMPONENT_COUNT{glm::vec2::length()};

    ImGui::TableSetColumnIndex(VALUE_INDEX);
    ImGui::PushMultiItemsWidths(
        COMPONENT_COUNT, ImGui::GetColumnWidth(VALUE_INDEX)
    );
    for (size_t component{0}; component < COMPONENT_COUNT; component++)
    {
        float const spacing{ImGui::GetStyle().ItemInnerSpacing.x};
        if (component > 0)
        {
            ImGui::SameLine(0.0F, spacing);
        }

        ImGui::DragFloat(
            fmt::format("##{}{}{}", rowTexts.name, m_propertyCount, component)
                .c_str(),
            &value[component],
            behavior.speed,
            behavior.bounds.min,
            behavior.bounds.max,
            "%.4f",
            behavior.flags
        );
        ImGui::PopItemWidth();
    }

    if (Self::resetColumn(rowTexts.name, value != resetValue))
    {
        value = resetValue;
    }

    Self::rowEnd();

    return *this;
}

auto PropertyTable::rowColor(
    PropertyTableRowTexts const& rowTexts,
    glm::vec3& value,
    glm::vec3 const& resetValue,
    PropertySliderBehavior behavior,
    size_t const digits
) -> PropertyTable&
{
    if (!Self::rowBegin(rowTexts))
    {
        return *this;
    }

    ImGui::TableSetColumnIndex(VALUE_INDEX);
    ImGui::PushMultiItemsWidths(3, ImGui::GetColumnWidth(VALUE_INDEX));
    for (size_t component{0}; component < 3; component++)
    {
        float const spacing{ImGui::GetStyle().ItemInnerSpacing.x};
        if (component > 0)
        {
            ImGui::SameLine(0.0F, spacing);
        }

        ImVec4 fieldColor{};
        ImVec4 fieldHovered{};
        ImVec4 fieldActive{};
        std::string fieldFormat{};

        if (component == 0)
        {
            ImVec4 constexpr FIELD_RED{0.2, 0.1, 0.1, 0.5};
            ImVec4 constexpr FIELD_RED_HOVERED{0.8, 0.2, 0.2, 0.5};
            ImVec4 constexpr FIELD_RED_ACTIVE{0.8, 0.2, 0.2, 0.7};
            fieldColor = FIELD_RED;
            fieldHovered = FIELD_RED_HOVERED;
            fieldActive = FIELD_RED_ACTIVE;
            fieldFormat = fmt::format("R: %.{}f", digits);
        }
        else if (component == 1)
        {
            ImVec4 constexpr FIELD_GREEN{0.1, 0.2, 0.1, 0.5};
            ImVec4 constexpr FIELD_GREEN_HOVERED{0.2, 0.8, 0.2, 0.5};
            ImVec4 constexpr FIELD_GREEN_ACTIVE{0.2, 0.8, 0.2, 0.7};
            fieldColor = FIELD_GREEN;
            fieldHovered = FIELD_GREEN_HOVERED;
            fieldActive = FIELD_GREEN_ACTIVE;
            fieldFormat = fmt::format("G: %.{}f", digits);
        }
        else if (component == 2)
        {
            ImVec4 constexpr FIELD_BLUE{0.1, 0.1, 0.2, 0.5};
            ImVec4 constexpr FIELD_BLUE_HOVERED{0.2, 0.2, 0.8, 0.5};
            ImVec4 constexpr FIELD_BLUE_ACTIVE{0.2, 0.2, 0.8, 0.7};
            fieldColor = FIELD_BLUE;
            fieldHovered = FIELD_BLUE_HOVERED;
            fieldActive = FIELD_BLUE_ACTIVE;
            fieldFormat = fmt::format("B: %.{}f", digits);
        }
        ImGui::PushStyleColor(ImGuiCol_FrameBg, fieldColor);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, fieldHovered);
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, fieldActive);

        ImGui::DragFloat(
            fmt::format("##{}{}{}", rowTexts.name, m_propertyCount, component)
                .c_str(),
            &value[component],
            behavior.speed,
            behavior.bounds.min,
            behavior.bounds.max,
            fieldFormat.c_str(),
            behavior.flags
        );
        ImGui::PopStyleColor(3);
        ImGui::PopItemWidth();
    }

    if (Self::resetColumn(rowTexts.name, value != resetValue))
    {
        value = resetValue;
    }

    Self::rowEnd();

    return *this;
}

auto PropertyTable::rowReadOnlyVec3(
    PropertyTableRowTexts const& rowTexts, glm::vec3 const& value
) -> PropertyTable&
{
    if (!Self::rowBegin(rowTexts))
    {
        return *this;
    }

    ImGui::TableSetColumnIndex(VALUE_INDEX);

    ImGui::BeginDisabled();

    ImGui::PushMultiItemsWidths(3, ImGui::GetColumnWidth(VALUE_INDEX));
    for (size_t component{0}; component < 3; component++)
    {
        float const interComponentSpacing{ImGui::GetStyle().ItemInnerSpacing.x};
        if (component > 0)
        {
            ImGui::SameLine(0.0F, interComponentSpacing);
        }

        float componentValue{value[component]};
        ImGui::DragFloat(
            fmt::format("##{}{}{}", rowTexts.name, m_propertyCount, component)
                .c_str(),
            &componentValue,
            0.0F,
            0.0F,
            0.0F,
            "%.6f",
            ImGuiSliderFlags_None
        );
        ImGui::PopItemWidth();
    }
    ImGui::EndDisabled();

    Self::rowEnd();

    return *this;
}

auto PropertyTable::rowFloat(
    PropertyTableRowTexts const& rowTexts,
    float& value,
    float const& resetValue,
    PropertySliderBehavior const behavior
) -> PropertyTable&
{
    if (!Self::rowBegin(rowTexts))
    {
        return *this;
    }

    ImGui::TableSetColumnIndex(VALUE_INDEX);
    ImGui::PushItemWidth(ImGui::GetColumnWidth(VALUE_INDEX));
    ImGui::DragFloat(
        fmt::format("##{}", rowTexts.name).c_str(),
        &value,
        behavior.speed,
        behavior.bounds.min,
        behavior.bounds.max,
        "%.6f",
        behavior.flags
    );
    ImGui::PopItemWidth();

    if (Self::resetColumn(rowTexts.name, value != resetValue))
    {
        value = resetValue;
    }

    Self::rowEnd();

    return *this;
}

auto PropertyTable::rowReadOnlyFloat(
    PropertyTableRowTexts const& rowTexts, float const& value
) -> PropertyTable&
{
    if (!Self::rowBegin(rowTexts))
    {
        return *this;
    }

    ImGui::TableSetColumnIndex(VALUE_INDEX);
    ImGui::PushItemWidth(ImGui::GetColumnWidth(VALUE_INDEX));

    ImGui::BeginDisabled();

    float valueCopy{value};
    ImGui::DragFloat(
        fmt::format("##{}", rowTexts.name).c_str(),
        &valueCopy,
        0.0,
        0.0,
        0.0,
        "%.6f",
        ImGuiSliderFlags_None
    );

    ImGui::EndDisabled();

    ImGui::PopItemWidth();

    Self::rowEnd();

    return *this;
}

auto PropertyTable::rowBoolean(
    PropertyTableRowTexts const& rowTexts, bool& value, bool const& resetValue
) -> PropertyTable&
{
    if (!Self::rowBegin(rowTexts))
    {
        return *this;
    }

    ImGui::TableSetColumnIndex(VALUE_INDEX);
    ImGui::Checkbox(fmt::format("##{}", rowTexts.name).c_str(), &value);

    if (Self::resetColumn(rowTexts.name, value != resetValue))
    {
        value = resetValue;
    }

    Self::rowEnd();

    return *this;
}

auto PropertyTable::rowReadOnlyBoolean(
    PropertyTableRowTexts const& rowTexts, bool const& value
) -> PropertyTable&
{
    if (!Self::rowBegin(rowTexts))
    {
        return *this;
    }

    ImGui::TableSetColumnIndex(VALUE_INDEX);

    ImGui::BeginDisabled();

    bool valueCopy{value};
    ImGui::Checkbox(fmt::format("##{}", rowTexts.name).c_str(), &valueCopy);

    ImGui::EndDisabled();

    Self::rowEnd();

    return *this;
}

void PropertyTable::demoWindow(bool& open)
{
    if (!ImGui::Begin("Property Table Demo Window", &open))
    {
        ImGui::End();
        return;
    }

    static bool valueBoolean{false};

    static int32_t valueBoundedInteger{0};
    static float valueBoundedFloat{0.0F};
    static glm::vec3 valueBoundedVec3{0.0F};

    static glm::vec3 valueUnboundedVec3{0.0F};
    static int32_t valueUnboundedInteger{0};
    static float valueUnboundedFloat{0.0F};

    static float const valueUnboundedFloat2{0.0F};
    static float valueUnboundedFloat3{0.0F};

    static float minimumBound{-1.0F};
    static float maximumBound{1.0F};

    static std::string valueText{"Default Text Value"};

    static size_t dropdownIndex{0};
    auto const dropdownLabels{
        std::to_array<std::string>({"First!", "Second!", "Third!", "Fourth!"})
    };

    // Since this is demo code, values here are arbitrary, so we do not lint
    // NOLINTBEGIN(readability-magic-numbers)
    PropertyTable::begin("Demo Table")
        .rowChildPropertyBegin({.name = "Available Fields"})
        .rowDropdown({.name = "Dropdown"}, dropdownIndex, 0, dropdownLabels)
        .rowTextInput({.name = "Text"}, valueText, "Default Text Value")
        .childPropertyBegin()
        .rowReadOnlyInteger(
            {.name = "Text Size"}, static_cast<int32_t>(valueText.size())
        )
        .rowReadOnlyInteger(
            {.name = "Text Capacity"},
            static_cast<int32_t>(valueText.capacity())
        )
        .childPropertyEnd()
        .rowTextLabel({.name = "Read-Only Text"}, "Hello!")
        .rowBoolean({.name = "Boolean"}, valueBoolean, false)
        .rowReadOnlyBoolean({.name = "Read-Only Boolean"}, true)
        .rowFloat(
            {.name = "Bounds Minimum"},
            minimumBound,
            -1.0F,
            PropertySliderBehavior{
                .speed = 1.0F,
            }
        )
        .rowFloat(
            {.name = "Bounds Maximum"},
            maximumBound,
            1.0F,
            PropertySliderBehavior{
                .speed = 1.0F,
            }
        )
        .rowInteger(
            {.name = "Bounded Integer"},
            valueBoundedInteger,
            0,
            PropertySliderBehavior{
                .bounds = FloatBounds{minimumBound, maximumBound},
            }
        )
        .rowFloat(
            {.name = "Bounded Float"},
            valueBoundedFloat,
            0.0F,
            PropertySliderBehavior{
                .bounds = FloatBounds{minimumBound, maximumBound},
            }
        )
        .rowVec3(
            {.name = "Bounded Vec3"},
            valueBoundedVec3,
            glm::vec3{0.0F},
            PropertySliderBehavior{
                .bounds = FloatBounds{minimumBound, maximumBound},
            }
        )
        .rowInteger(
            {.name = "Unbounded Integer"},
            valueUnboundedInteger,
            0,
            PropertySliderBehavior{
                .speed = 1.0F,
            }
        )
        .rowFloat(
            {.name = "Unbounded Float"},
            valueUnboundedFloat,
            0.0F,
            PropertySliderBehavior{
                .speed = 1.0F,
            }
        )
        .rowVec3(
            {.name = "Unbounded Vec3"},
            valueUnboundedVec3,
            glm::vec3{0.0F},
            PropertySliderBehavior{
                .speed = 0.1F,
            }
        )
        .rowColor(
            {.name = "Vec3 with RGB Labels"},
            valueUnboundedVec3,
            glm::vec3{0.0F},
            PropertySliderBehavior{
                .speed = 0.1F,
            }
        )
        .rowReadOnlyFloat({.name = "Read Only Float"}, 1.0F)
        .rowReadOnlyVec3({.name = "Read-Only Vec3"}, glm::vec3{1.0F})
        .rowReadOnlyInteger({.name = "Read-Only Integer"}, 592181)
        .childPropertyEnd() // Available Fields
        .rowTextLabel(
            {.name = "Child Properties"},
            "Child Properties remember their collapse status."
        )
        .childPropertyBegin()
        .rowChildPropertyBegin({.name = "Child"})
        .rowChildPropertyBegin({.name = "Child"})
        .rowTextLabel({.name = "Hello"}, "")
        .childPropertyEnd()
        .rowChildPropertyBegin({.name = "Child"})
        .rowTextLabel({.name = "Hello"}, "")
        .childPropertyEnd()
        .childPropertyEnd()
        .childPropertyEnd()
        .rowFloat(
            {.name = "Unbounded Float with Children"},
            valueUnboundedFloat3,
            0.0F,
            PropertySliderBehavior{
                .speed = 1.0F,
            }
        )
        .childPropertyBegin()
        .rowTextLabel({.name = "Some Child Property"}, "")
        .childPropertyEnd()
        .end();
    // NOLINTEND(readability-magic-numbers)

    ImGui::End(); // End window
}
} // namespace vkt