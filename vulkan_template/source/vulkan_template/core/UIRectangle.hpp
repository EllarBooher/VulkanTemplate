#pragma once

#include <glm/common.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace vkt
{
struct UIRectangle
{
    glm::vec2 min{};
    glm::vec2 max{};

    [[nodiscard]] auto pos() const -> glm::vec2 { return min; }
    [[nodiscard]] auto size() const -> glm::vec2 { return max - min; }

    [[nodiscard]] auto contains(glm::vec2 const pos) const -> bool
    {
        return glm::all(glm::bvec4{
            glm::greaterThanEqual(pos, min), glm::lessThanEqual(pos, max)
        });
    }

    static auto fromPosSize(glm::vec2 const pos, glm::vec2 const size)
        -> UIRectangle
    {
        return UIRectangle{
            .min{pos},
            .max{pos + size},
        };
    }

    [[nodiscard]] auto clampToMin() const -> UIRectangle
    {
        return UIRectangle{
            .min{min},
            .max{glm::max(min, max)},
        };
    }

    [[nodiscard]] auto shrink(glm::vec2 const margins) const -> UIRectangle
    {
        return UIRectangle{
            .min{min + margins},
            .max{max - margins},
        };
    }
    [[nodiscard]] auto shrinkMin(glm::vec2 const margins) const -> UIRectangle
    {
        return UIRectangle{
            .min{min + margins},
            .max{max},
        };
    }
    [[nodiscard]] auto shrinkMax(glm::vec2 const margins) const -> UIRectangle
    {
        return UIRectangle{
            .min{min},
            .max{max - margins},
        };
    }
};
} // namespace vkt