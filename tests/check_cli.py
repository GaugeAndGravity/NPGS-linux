#!/usr/bin/env python3
"""No-graphics CLI acceptance: python3 tests/check_cli.py <build>/bin/npgs.

No successful rendering or validation-layer coverage is claimed by this test.
Missing executables, timeouts and missing diagnostics are failures, not skips.
All cases run without displays, from an independent empty cwd, with
NPGS_ASSET_DIR set to a nonexistent absolute path. Help and invalid arguments
must not depend on resources, including through initialization before main.
"""

import argparse
import os

from _acceptance import (
    LOG_ROOT,
    Run,
    entrypoint,
    executable_path,
    positive_timeout,
    require,
)

OPTIONS = (
    "--help", "--frames", "--width", "--height", "--smoke-resize",
    "--fullscreen", "--validation",
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe")
    parser.add_argument("--timeout", type=positive_timeout, default=10)
    parser.add_argument("--log-root", default=str(LOG_ROOT))
    args = parser.parse_args()
    exe = executable_path(args.exe)
    run = Run("cli", args.log_root)
    env = os.environ.copy()
    for key in ("DISPLAY", "WAYLAND_DISPLAY", "WAYLAND_SOCKET",
                "XDG_RUNTIME_DIR", "WAYLAND_DEBUG"):
        env.pop(key, None)
    missing_assets = run.directory / "assets-must-not-exist"
    env["NPGS_ASSET_DIR"] = str(missing_assets)
    print(f"CLI resource isolation: NPGS_ASSET_DIR={missing_assets}", flush=True)

    def command(name, options):
        require(missing_assets.is_absolute() and not missing_assets.exists(),
                "CLI isolation requires an absolute, nonexistent NPGS_ASSET_DIR")
        require(not any(run.cwd.iterdir()), "CLI isolation requires an empty cwd")
        return run.command(name, [exe, *options], env, args.timeout)

    count = 0
    for options in (["--help"], ["-h"], ["--help", "--validation"]):
        code, stdout, stderr = command(f"{count:03d}-help", options)
        require(code == 0, f"help {options}: exit {code}; stderr: {stderr[-2000:]}")
        require("usage:" in stdout.lower(), "help must print usage to stdout")
        for option in OPTIONS:
            require(option in stdout, f"help missing {option}")
        require("NPGS_SMOKE_OK" not in stdout + stderr,
                "help unexpectedly reported rendering")
        count += 1

    invalid = [(["--not-an-option"], "--not-an-option"), (["positional"], "positional")]
    for option, overflow in (
        ("--frames", "18446744073709551616"),
        ("--width", "2147483648"),
        ("--height", "2147483648"),
    ):
        invalid.append(([option], option))
        for value in ("", "0", "-1", "+1", "1.5", "1x", overflow):
            invalid.append(([option, value], option))
    invalid.extend([
        (["--frames", "4", "--fullscreen"], "--fullscreen"),
        (["--fullscreen", "--frames", "4"], "--fullscreen"),
        (["--smoke-resize"], "--smoke-resize"),
        (["--frames", "3", "--smoke-resize"], "--smoke-resize"),
    ])
    for options, diagnostic in invalid:
        code, stdout, stderr = command(f"{count:03d}-invalid", options)
        require(code == 2, f"{options}: expected argument exit 2, got {code}; "
                f"stderr: {stderr[-2000:]}")
        require("NPGS: invalid arguments:" in stderr and diagnostic in stderr,
                f"{options}: missing argument-specific diagnostic: {stderr[-2000:]}")
        require("startup/render failed" not in stderr and
                "NPGS_SMOKE_OK" not in stdout + stderr,
                f"{options}: reached graphics instead of rejecting arguments")
        count += 1
    print(f"PASS: CLI only ({count} cases), no asset/display prerequisites; "
          "no graphics/shader/Wayland coverage")


if __name__ == "__main__":
    raise SystemExit(entrypoint(main))
