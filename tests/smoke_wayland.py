#!/usr/bin/env python3
"""Explicit, windowed Wayland smoke; never run implicitly by CLI tests.

python3 tests/smoke_wayland.py <build>/bin/npgs --smoke-resize

Opens a small application window in the selected Wayland session. No screenshots,
input injection or compositor commands. Cwd is a fresh directory under the log
root, so assets must resolve independently of cwd. --validation requires a real
installed layer; missing layers must fail in the application, never skip.

Contract: last stdout line NPGS_SMOKE_OK frames=N platform=wayland.
Resize additionally requires NPGS_SMOKE_RESIZE_OK by default;
--resize-pattern can accommodate the final agreed success-log format.
"""

import argparse
import os
import re
import stat
from pathlib import Path

from _acceptance import (
    LOG_ROOT,
    Run,
    entrypoint,
    executable_path,
    positive_integer,
    positive_timeout,
    require,
)


def check_result(code, stdout, stderr, frames, resize_pattern=None):
    require(code == 0, f"application exited {code}; stderr tail:\n{stderr[-4000:]}")
    combined = stdout + "\n" + stderr
    require(not re.search(r"Validation Error|VUID-[A-Za-z]|VK_ERROR_VALIDATION_FAILED",
                          combined, re.IGNORECASE),
            "Vulkan validation errors in application output")
    lines = [line.strip() for line in stdout.splitlines() if line.strip()]
    require(bool(lines), "application produced no stdout success marker")
    marker = re.fullmatch(
        r"NPGS_SMOKE_OK frames=([0-9]+) platform=wayland", lines[-1]
    )
    require(marker is not None,
            "last stdout line must be NPGS_SMOKE_OK frames=N platform=wayland")
    require(int(marker.group(1)) == frames,
            f"presented-frame marker does not match requested {frames} frames")
    require(sum("NPGS_SMOKE_OK" in line for line in lines) == 1,
            "ambiguous/duplicate smoke success markers")
    if resize_pattern is not None:
        require(resize_pattern.search(combined), "missing resize success log")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("exe")
    parser.add_argument("--frames", type=positive_integer, default=8)
    parser.add_argument("--width", type=positive_integer, default=320)
    parser.add_argument("--height", type=positive_integer, default=240)
    parser.add_argument("--smoke-resize", action="store_true")
    parser.add_argument("--resize-pattern", default=r"\bNPGS_SMOKE_RESIZE_OK\b")
    parser.add_argument("--validation", action="store_true")
    parser.add_argument("--wayland-debug", action="store_true")
    parser.add_argument("--timeout", type=positive_timeout, default=120)
    parser.add_argument("--log-root", default=str(LOG_ROOT))
    args = parser.parse_args()
    if args.smoke_resize and args.frames < 4:
        parser.error("--smoke-resize requires --frames >= 4")
    if args.frames > 2**64 - 1 or max(args.width, args.height) > 2**31 - 1:
        parser.error("frame count or dimensions exceed the application integer range")
    resize_pattern = None
    if args.smoke_resize:
        try:
            resize_pattern = re.compile(args.resize_pattern)
        except re.error as error:
            parser.error(f"invalid --resize-pattern: {error}")
        if resize_pattern.search(""):
            parser.error("--resize-pattern must not match empty output")

    exe = executable_path(args.exe)
    display = os.environ.get("WAYLAND_DISPLAY")
    require(display, "WAYLAND_DISPLAY is unset; Wayland smoke NOT RUN (failure)")
    socket = Path(display)
    if not socket.is_absolute():
        runtime = os.environ.get("XDG_RUNTIME_DIR")
        require(runtime and Path(runtime).is_absolute(),
                "relative WAYLAND_DISPLAY requires absolute XDG_RUNTIME_DIR")
        socket = Path(runtime) / socket
    require(socket.exists() and stat.S_ISSOCK(socket.stat().st_mode),
            f"Wayland socket unavailable: {socket}; smoke NOT RUN (failure)")
    env = os.environ.copy()
    # Do not inherit an alternate Wayland fd or accidentally fall back to X11.
    for key in ("DISPLAY", "WAYLAND_SOCKET", "WAYLAND_DEBUG"):
        env.pop(key, None)
    if args.wayland_debug:
        env["WAYLAND_DEBUG"] = "client"
    run = Run("wayland", args.log_root)
    command = [exe, "--frames", str(args.frames),
               "--width", str(args.width), "--height", str(args.height)]
    if args.smoke_resize:
        command.append("--smoke-resize")
    if args.validation:
        command.append("--validation")
    print("Starting explicit small-window Wayland smoke (no desktop automation).",
          flush=True)
    code, stdout, stderr = run.command("smoke", command, env, args.timeout)
    check_result(code, stdout, stderr, args.frames, resize_pattern)
    print(f"PASS: native Wayland, {args.frames} presented frames; "
          f"resize={'checked' if args.smoke_resize else 'not requested'}; "
          f"validation={'requested' if args.validation else 'not requested'}")


if __name__ == "__main__":
    raise SystemExit(entrypoint(main))
