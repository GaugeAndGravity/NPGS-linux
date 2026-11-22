#include "Logger.h"

_NPGS_BEGIN
_UTIL_BEGIN

NPGS_INLINE std::shared_ptr<spdlog::logger>& FLogger::GetCoreLogger()
{
    Initialize();
    return _kCoreLogger;
}

NPGS_INLINE std::shared_ptr<spdlog::logger>& FLogger::GetClientLogger()
{
    Initialize();
    return _kClientLogger;
}

_UTIL_END
_NPGS_END
