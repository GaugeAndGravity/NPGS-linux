#!/usr/bin/env python3
"""Validate built SPIR-V, not checked-in binaries; no graphics required.

python3 tests/check_spirv.py <build>

Requires <build>/npgs-resource-manifest.txt and <build>/share/npgs/. Fails for
missing expected variants, zero SPIR-V files, missing spirv-val or invalid SPIR-V.
Validates every staged *.spv (including extras) against Vulkan 1.4. This does not
compile shaders or prove freshness: run the CMake npgs_shaders target first,
preferably in a fresh build directory.
"""

import argparse
import os
import shutil
from pathlib import Path

from _acceptance import (
    LOG_ROOT,
    Run,
    entrypoint,
    executable_path,
    positive_timeout,
    require,
)


def inside(path, root):
    try:
        path.resolve().relative_to(root.resolve())
    except ValueError:
        return False
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("build")
    parser.add_argument("--spirv-val", default="spirv-val")
    parser.add_argument("--timeout", type=positive_timeout, default=30,
                        help="timeout per module in seconds")
    parser.add_argument("--log-root", default=str(LOG_ROOT))
    args = parser.parse_args()
    tool = shutil.which(args.spirv_val)
    require(tool, f"spirv-val not available: {args.spirv_val}; validation NOT RUN (failure)")
    tool = executable_path(tool)
    build = Path(args.build).expanduser().resolve()
    root = build / "share/npgs"
    manifest = build / "npgs-resource-manifest.txt"
    require(manifest.is_file(), f"missing generated manifest: {manifest}")
    require(root.is_dir(), f"missing staged resource directory: {root}")
    expected = set()
    for line in manifest.read_text().splitlines():
        relative = Path(line.strip())
        if relative.suffix != ".spv":
            continue
        require(not relative.is_absolute() and inside(root / relative, root),
                f"shader manifest entry escapes resource directory: {line}")
        expected.add((root / relative).resolve())
    require(expected, "manifest contains zero shader variants; validation NOT RUN (failure)")
    missing = sorted(str(path) for path in expected if not path.is_file())
    require(not missing, "missing built shader variants:\n" + "\n".join(missing))
    staged = sorted(root.rglob("*.spv"))
    require(staged, "zero staged SPIR-V files; validation NOT RUN (failure)")
    run = Run("spirv", args.log_root)
    failures = []
    for index, path in enumerate(staged):
        require(inside(path, root), f"staged shader escapes resource directory: {path}")
        code, stdout, stderr = run.command(
            f"{index:04d}", [tool, "--target-env", "vulkan1.4", path],
            os.environ.copy(), args.timeout,
        )
        if code != 0:
            failures.append(f"{path.relative_to(root)}: exit {code}\n{stdout}{stderr}")
    require(not failures, "SPIR-V validation failed:\n" + "\n".join(failures))
    print(f"PASS: {len(staged)} staged SPIR-V modules validated for Vulkan 1.4; "
          f"{len(expected)} expected variants present; no runtime coverage")


if __name__ == "__main__":
    raise SystemExit(entrypoint(main))
