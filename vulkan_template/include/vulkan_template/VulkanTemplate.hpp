#pragma once

namespace vkt
{
enum class RunResult
{
    SUCCESS,
    FAILURE,
};

auto run() -> RunResult;
} // namespace vkt