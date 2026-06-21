#pragma once

#include <cassert>
#include <memory>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <utility>

class Log {
  public:
    static void Init() {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_pattern("%^[%Y-%m-%d %H:%M:%S.%e] [%l] [thread %t] %v%$");

        s_Logger = std::make_shared<spdlog::logger>("GxBuild3", console_sink);
        spdlog::register_logger(s_Logger);

#ifdef NDEBUG
        s_Logger->set_level(spdlog::level::info);
#else
        s_Logger->set_level(spdlog::level::trace);
#endif
        s_Logger->flush_on(spdlog::level::err);
    }

    static void Shutdown() {
        spdlog::drop("GxBuild3");
        s_Logger.reset();
    }

    template <typename... Args>
    static void Trace(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        assert(s_Logger && "Log::Init() not called");
        s_Logger->trace(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void Info(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        assert(s_Logger && "Log::Init() not called");
        s_Logger->info(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void Warn(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        assert(s_Logger && "Log::Init() not called");
        s_Logger->warn(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void Error(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        assert(s_Logger && "Log::Init() not called");
        s_Logger->error(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void Critical(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        assert(s_Logger && "Log::Init() not called");
        s_Logger->critical(fmt, std::forward<Args>(args)...);
    }

  private:
    static std::shared_ptr<spdlog::logger> s_Logger;
};