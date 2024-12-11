#pragma once

#include <memory>

#ifndef SPDLOG_ACTIVE_LEVEL
#ifdef VKT_DEBUG_BUILD
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#else
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#endif
#endif

#include <spdlog/logger.h> // IWYU pragma: keep
#include <spdlog/spdlog.h>

namespace vkt
{
struct Logger
{
public:
    static auto getLogger() -> spdlog::logger&;
    static void initLogging();

private:
    static std::shared_ptr<spdlog::logger> m_logger;
};
} // namespace vkt

#define VKT_TRACE(...)                                                         \
    SPDLOG_LOGGER_TRACE(&vkt::Logger::getLogger(), __VA_ARGS__);
#define VKT_DEBUG(...)                                                         \
    SPDLOG_LOGGER_DEBUG(&vkt::Logger::getLogger(), __VA_ARGS__);
#define VKT_INFO(...)                                                          \
    SPDLOG_LOGGER_INFO(&vkt::Logger::getLogger(), __VA_ARGS__);
#define VKT_WARNING(...)                                                       \
    SPDLOG_LOGGER_WARN(&vkt::Logger::getLogger(), __VA_ARGS__);
#define VKT_ERROR(...)                                                         \
    SPDLOG_LOGGER_ERROR(&vkt::Logger::getLogger(), __VA_ARGS__);
#define VKT_CRITICAL(...)                                                      \
    SPDLOG_LOGGER_CRITICAL(&vkt::Logger::getLogger(), __VA_ARGS__);