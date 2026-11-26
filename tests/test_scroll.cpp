#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

#include "Engine/Core/System/ScrollSettings.h"
#include "Engine/Core/System/Spatial/Camera.h"

using Npgs::System::FScrollSettings;
using Npgs::System::Spatial::FCamera;

namespace
{
void CheckNear(double Actual, double Expected, const char* Message, double Tolerance = 1e-12)
{
    if (!std::isfinite(Actual) || std::abs(Actual - Expected) > Tolerance)
    {
        std::cerr << "actual=" << Actual << ", expected=" << Expected << '\n';
        throw std::runtime_error(Message);
    }
}

void CheckScrollScale()
{
    FScrollSettings Settings;
    CheckNear(Settings.GetScale(1.0), 1.2, "Default step must preserve the old 20% increase");
    CheckNear(Settings.GetScale(1.5), std::pow(1.2, 1.5), "Fractional Wayland offsets must be preserved");
    CheckNear(Settings.GetScale(0.0), 1.0, "No input must leave the value unchanged");

    Settings.StepPercent = 5.0f;
    CheckNear(Settings.GetScale(1.0), 1.05, "UI percentage must control the base multiplier");
    CheckNear(Settings.GetScale(1.5), std::pow(1.05, 1.5), "Percentage must also scale fractional input");
    CheckNear(Settings.GetScale(1.5) * Settings.GetScale(-1.5), 1.0,
              "Opposite scroll directions must be reciprocal");
    CheckNear(Settings.GetScale(0.25) * Settings.GetScale(0.5) * Settings.GetScale(0.75),
              Settings.GetScale(1.5), "Event splitting or buffering must not change the total scale");

    Settings.StepPercent = 0.0f;
    CheckNear(Settings.GetScale(1.5), 1.0, "Zero percent must disable positive scrolling");
    CheckNear(Settings.GetScale(-1.5), 1.0, "Zero percent must disable negative scrolling");
    Settings.StepPercent = -10.0f;
    CheckNear(Settings.GetScale(1.0), 1.0, "Negative percentages must clamp to zero");
    Settings.StepPercent = 200.0f;
    CheckNear(Settings.GetScale(1.0), 2.0, "Percentages must clamp to the UI maximum");

    Settings.StepPercent = std::numeric_limits<float>::quiet_NaN();
    CheckNear(Settings.GetScale(1.0), 1.0, "Invalid percentage must not corrupt camera state");
    Settings.StepPercent = std::numeric_limits<float>::infinity();
    CheckNear(Settings.GetScale(1.0), 1.0, "Infinite percentage must not corrupt camera state");
    Settings.StepPercent = 5.0f;
    CheckNear(Settings.GetScale(std::numeric_limits<double>::quiet_NaN()), 1.0,
              "Invalid offset must not corrupt camera state");
    CheckNear(Settings.GetScale(std::numeric_limits<double>::infinity()), 1.0,
              "Infinite offset must not corrupt camera state");
    CheckNear(Settings.GetScale(std::numeric_limits<double>::max()), 1.0,
              "Overflowing scale must not corrupt camera state");
    CheckNear(Settings.GetScale(-std::numeric_limits<double>::max()), 1.0,
              "Underflowing scale must not zero the camera speed or radius");
}

double OrbitRadius(const FCamera& Camera)
{
    return glm::length(Camera.GetCameraVector(FCamera::EVectorType::kPosition));
}

double MoveForward(FCamera& Camera)
{
    const auto Before = Camera.GetCameraVector(FCamera::EVectorType::kPosition);
    Camera.ProcessKeyboard(FCamera::EMovement::kForward);
    Camera.ProcessTimeEvolution(0.25);
    return glm::length(Camera.GetCameraVector(FCamera::EVectorType::kPosition) - Before);
}

void CheckCameraModes()
{
    FScrollSettings Settings{5.0f};
    const double Scale = Settings.GetScale(1.5);
    const double ReverseScale = Settings.GetScale(-1.5);

    FCamera OrbitCamera(glm::vec3(0.0f));
    OrbitCamera.TeleportOrbit(0.0f, 45.0f);
    const double InitialRadius = OrbitRadius(OrbitCamera);
    OrbitCamera.ApplyScrollScale(Scale);
    OrbitCamera.ProcessTimeEvolution(1.0);
    CheckNear(OrbitRadius(OrbitCamera) / InitialRadius, 1.0 / Scale,
              "Positive scrolling must shrink orbit radius by the configured reciprocal scale", 1e-6);
    OrbitCamera.ApplyScrollScale(ReverseScale);
    OrbitCamera.ProcessTimeEvolution(1.0);
    CheckNear(OrbitRadius(OrbitCamera) / InitialRadius, 1.0,
              "Reverse scrolling must restore orbit radius", 1e-6);

    FCamera FreeCamera(glm::vec3(0.0f), 0.2f, 1.0f);
    FreeCamera.SetCameraMode(false);
    const double InitialStep = MoveForward(FreeCamera);
    FreeCamera.ApplyScrollScale(Scale);
    CheckNear(MoveForward(FreeCamera) / InitialStep, Scale,
              "Free-camera speed must use the same configured scale", 1e-6);
    FreeCamera.ApplyScrollScale(ReverseScale);
    CheckNear(MoveForward(FreeCamera) / InitialStep, 1.0,
              "Reverse scrolling must restore free-camera speed", 1e-6);

    Settings.StepPercent = 0.0f;
    OrbitCamera.ApplyScrollScale(Settings.GetScale(1.5));
    OrbitCamera.ProcessTimeEvolution(1.0);
    FreeCamera.ApplyScrollScale(Settings.GetScale(-1.5));
    CheckNear(OrbitRadius(OrbitCamera) / InitialRadius, 1.0,
              "Disabled scrolling must leave orbit radius unchanged", 1e-6);
    CheckNear(MoveForward(FreeCamera) / InitialStep, 1.0,
              "Disabled scrolling must leave free-camera speed unchanged", 1e-6);
}
}

int main()
{
    try
    {
        CheckScrollScale();
        CheckCameraModes();
        std::cout << "Scroll settings: percentages, fractional input, reciprocal scaling and camera modes passed\n";
    }
    catch (const std::exception& Error)
    {
        std::cerr << "Scroll regression: " << Error.what() << '\n';
        return 1;
    }
}
