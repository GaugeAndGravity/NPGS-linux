#pragma once

#include <algorithm>
#include <cmath>

namespace Npgs::System
{

struct FScrollSettings
{
    static constexpr float kMinStepPercent = 0.0f;
    static constexpr float kMaxStepPercent = 100.0f;

    float StepPercent = 20.0f;

    // GLFW offsets may be fractional and are not necessarily one unit per detent.
    double GetScale(double OffsetY) const
    {
        if (!std::isfinite(StepPercent) || !std::isfinite(OffsetY))
            return 1.0;

        const double Base = 1.0 + std::clamp(StepPercent, kMinStepPercent, kMaxStepPercent) / 100.0;
        const double Scale = std::pow(Base, OffsetY);
        return std::isfinite(Scale) && Scale > 0.0 ? Scale : 1.0;
    }
};

}
