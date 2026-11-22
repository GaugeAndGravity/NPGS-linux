#include "Logger.h"

#include <mutex>

#include <spdlog/sinks/stdout_color_sinks.h>

_NPGS_BEGIN
_UTIL_BEGIN

void FLogger::Initialize()
{
    static std::once_flag Initialized;
    std::call_once(Initialized, []
    {
        // Console logging also works from a read-only working directory.
        // Automatic color mode avoids escape sequences in redirected output.
        auto ConsoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        ConsoleSink->set_pattern("[%T][%^%l%$] %n: %v");
        ConsoleSink->set_color(spdlog::level::trace, "\033[34m\033[1m");

        auto CoreLogger = std::make_shared<spdlog::logger>("NPGS", ConsoleSink);
        auto ClientLogger = std::make_shared<spdlog::logger>("App", ConsoleSink);
        CoreLogger->set_level(spdlog::level::trace);
        ClientLogger->set_level(spdlog::level::trace);
        CoreLogger->flush_on(spdlog::level::err);
        ClientLogger->flush_on(spdlog::level::err);

        _kCoreLogger = std::move(CoreLogger);
        _kClientLogger = std::move(ClientLogger);
    });
}


std::shared_ptr<spdlog::logger> FLogger::_kCoreLogger   = nullptr;
std::shared_ptr<spdlog::logger> FLogger::_kClientLogger = nullptr;

_UTIL_END
_NPGS_END
