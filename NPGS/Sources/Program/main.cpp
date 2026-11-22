#include "Npgs.h"
#include "Application.h"

#include <charconv>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

using namespace Npgs;
using namespace Npgs::Util;

namespace
{
struct FCommandLineOptions
{
    std::uint32_t Width = 1280;
    std::uint32_t Height = 960;
    std::uint64_t Frames = 0;
    bool Help = false;
    bool Validation = false;
    bool Fullscreen = false;
    bool SmokeResize = false;
};

void PrintHelp()
{
    std::cout
        << "Usage: NPGS [options]\n"
        << "\n"
        << "  --help, -h      Show this help without initializing graphics.\n"
        << "  --frames N      Render N successfully presented frames, then exit.\n"
        << "                  Uses a 320x240 window unless dimensions are given.\n"
        << "  --smoke-resize  Exercise window/swapchain resize (requires --frames >= 4).\n"
        << "  --width N       Window width in pixels (positive integer).\n"
        << "  --height N      Window height in pixels (positive integer).\n"
        << "  --validation    Request Vulkan validation (disabled by default).\n"
        << "  --fullscreen    Enable fullscreen (not allowed with --frames).\n"
        << "\n"
        << "Requires Vulkan >= 1.4 and a Wayland session (Wayland-only).\n"
        << "Without --frames, run interactively in a 1280x960 window.\n"
        << "Example: NPGS --frames 4 --smoke-resize --width 320 --height 240\n";
}

std::uint64_t ParsePositiveInteger(std::string_view Option, std::string_view Value,
                                   std::uint64_t Maximum)
{
    std::uint64_t Number = 0;
    const auto Result = std::from_chars(Value.data(), Value.data() + Value.size(), Number);
    if (Value.empty() || Result.ec != std::errc{} ||
        Result.ptr != Value.data() + Value.size() || Number == 0 || Number > Maximum)
    {
        throw std::invalid_argument(std::string(Option) + " requires an integer in [1, " +
                                    std::to_string(Maximum) + "], got '" + std::string(Value) + "'");
    }
    return Number;
}

FCommandLineOptions ParseCommandLine(int Argc, char* Argv[])
{
    FCommandLineOptions Options;
    bool WidthSpecified = false;
    bool HeightSpecified = false;
    for (int Index = 1; Index < Argc; ++Index)
    {
        const std::string_view Argument(Argv[Index]);
        if (Argument == "--help" || Argument == "-h")
        {
            Options.Help = true;
        }
        else if (Argument == "--validation")
        {
            Options.Validation = true;
        }
        else if (Argument == "--fullscreen")
        {
            Options.Fullscreen = true;
        }
        else if (Argument == "--smoke-resize")
        {
            Options.SmokeResize = true;
        }
        else if (Argument == "--frames" || Argument == "--width" || Argument == "--height")
        {
            if (++Index == Argc)
            {
                throw std::invalid_argument(std::string(Argument) + " requires a value");
            }
            const std::uint64_t Maximum = Argument == "--frames"
                ? std::numeric_limits<std::uint64_t>::max()
                : static_cast<std::uint64_t>(std::numeric_limits<int>::max());
            const auto Value = ParsePositiveInteger(Argument, Argv[Index], Maximum);
            if (Argument == "--frames")
            {
                Options.Frames = Value;
            }
            else if (Argument == "--width")
            {
                Options.Width = static_cast<std::uint32_t>(Value);
                WidthSpecified = true;
            }
            else
            {
                Options.Height = static_cast<std::uint32_t>(Value);
                HeightSpecified = true;
            }
        }
        else
        {
            throw std::invalid_argument("unknown argument '" + std::string(Argument) + "'");
        }
    }

    if (Options.SmokeResize && Options.Frames < 4)
    {
        throw std::invalid_argument("--smoke-resize requires --frames >= 4");
    }
    if (Options.Frames != 0)
    {
        if (Options.Fullscreen)
        {
            throw std::invalid_argument("--frames requires windowed mode; remove --fullscreen");
        }
        if (!WidthSpecified)
        {
            Options.Width = 320;
        }
        if (!HeightSpecified)
        {
            Options.Height = 240;
        }
    }
    return Options;
}
}

int main(int Argc, char* Argv[])
{
    try
    {
        const auto Options = ParseCommandLine(Argc, Argv);
        if (Options.Help)
        {
            PrintHelp();
            return 0;
        }

        FLogger::Initialize();
        std::uint64_t RenderedFrames = 0;
        {
            FApplication App({ Options.Width, Options.Height }, "Learn glNext FPS:", false,
                             Options.Fullscreen, Options.Validation);
            // Verify the active backend while App owns the initialized GLFW lifetime.
            if (glfwGetPlatform() != GLFW_PLATFORM_WAYLAND)
            {
                throw std::runtime_error("this build requires the GLFW Wayland backend");
            }
            RenderedFrames = App.ExecuteMainRender(Options.Frames, Options.SmokeResize);
        }
        if (Options.Frames != 0)
        {
            if (RenderedFrames != Options.Frames)
            {
                throw std::runtime_error("smoke incomplete: requested " + std::to_string(Options.Frames) +
                                         " frames, presented " + std::to_string(RenderedFrames));
            }
            std::cout << "NPGS_SMOKE_OK frames=" << RenderedFrames << " platform=wayland\n";
        }
        return 0;
    }
    catch (const std::invalid_argument& Error)
    {
        std::cerr << "NPGS: invalid arguments: " << Error.what() << "\nUse --help for usage.\n";
        return 2;
    }
    catch (const std::exception& Error)
    {
        std::cerr << "NPGS: startup/render failed: " << Error.what() << '\n';
        return 1;
    }
    catch (...)
    {
        std::cerr << "NPGS: startup/render failed: unknown exception\n";
        return 1;
    }
}
