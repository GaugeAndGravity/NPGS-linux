#!/usr/bin/env python3
"""Self-tests with fake processes/sockets only; never connect to the desktop.

python3 -B tests/test_acceptance.py

Fixtures use the standard temporary directory and respect TMPDIR. No compiler,
Vulkan driver, Wayland compositor or validation layer is needed.
"""

import json
import os
import re
import socket
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

from _acceptance import AcceptanceError, LOG_ROOT, Run
from smoke_wayland import check_result

SCRIPTS = Path(__file__).resolve().parent
FIXTURE = r'''
import os
from pathlib import Path
import re
import sys
import time

args = sys.argv[1:]
if "--target-env" in args:
    assert args[:2] == ["--target-env", "vulkan1.4"]
    if Path(args[2]).read_bytes() != b"fixture-spirv":
        print("fixture invalid SPIR-V", file=sys.stderr)
        sys.exit(1)
    sys.exit(0)

if os.environ.get("FIXTURE_EXPECT_RESOURCE_FREE_CLI"):
    assert not any(key in os.environ for key in
                   ("DISPLAY", "WAYLAND_DISPLAY", "WAYLAND_SOCKET",
                    "XDG_RUNTIME_DIR", "WAYLAND_DEBUG"))
    assets = Path(os.environ["NPGS_ASSET_DIR"])
    assert assets.is_absolute() and not assets.exists()
    assert assets.parent == Path.cwd().parent
    assert not any(Path.cwd().iterdir())
    mode = os.environ.get("FIXTURE_MODE", "")
    help_requested = "--help" in args or "-h" in args
    if (mode == "static-resource-init-help" and help_requested or
            mode == "static-resource-init-invalid" and not help_requested):
        print("fixture resource initialization failed before main", file=sys.stderr)
        sys.exit(1)

def bad(option):
    print("NPGS: invalid arguments: " + option, file=sys.stderr)
    sys.exit(2)

values = {}
flags = set()
index = 0
while index < len(args):
    option = args[index]
    if option in ("--frames", "--width", "--height"):
        index += 1
        if index == len(args):
            bad(option)
        value = args[index]
        if not re.fullmatch(r"[0-9]+", value):
            bad(option)
        number = int(value)
        limit = 2**64 - 1 if option == "--frames" else 2**31 - 1
        if not 0 < number <= limit:
            bad(option)
        values[option] = number
    elif option in ("--help", "-h", "--validation", "--fullscreen", "--smoke-resize"):
        flags.add(option)
    else:
        bad(option)
    index += 1
if "--smoke-resize" in flags and values.get("--frames", 0) < 4:
    bad("--smoke-resize")
if "--frames" in values and "--fullscreen" in flags:
    bad("--fullscreen")
assert "DISPLAY" not in os.environ and "WAYLAND_SOCKET" not in os.environ
assert str(Path.cwd()).startswith(os.environ["FIXTURE_LOG_ROOT"] + "/")
if flags.intersection(("--help", "-h")):
    assert "WAYLAND_DISPLAY" not in os.environ and "XDG_RUNTIME_DIR" not in os.environ
    print("Usage: fixture --help --frames --width --height --smoke-resize "
          "--validation --fullscreen")
    sys.exit(0)
mode = os.environ.get("FIXTURE_MODE", "")
if mode == "timeout":
    time.sleep(60)
if mode == "validation-missing" and "--validation" in flags:
    print("NPGS: startup/render failed: VK_LAYER_KHRONOS_validation unavailable",
          file=sys.stderr)
    sys.exit(1)
if mode == "validation-error":
    print("Validation Error: VUID-fixture-error", file=sys.stderr)
if "--smoke-resize" in flags and mode != "resize-missing":
    print("NPGS_SMOKE_RESIZE_OK")
print("NPGS_SMOKE_OK frames=" + str(values.get("--frames", 0)) + " platform=wayland")
'''


class AcceptanceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(
            prefix="npgs-acceptance-selftest-"
        )
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.logs = self.root / "logs"
        self.exe = self.root / "fixture"
        self.exe.write_text(f"#!{sys.executable}\n" + FIXTURE)
        self.exe.chmod(0o700)
        self.env = os.environ.copy()
        self.env.update({
            "DISPLAY": ":fixture-must-be-removed",
            "WAYLAND_SOCKET": "fixture-must-be-removed",
            "FIXTURE_LOG_ROOT": str(self.logs),
            "PYTHONDONTWRITEBYTECODE": "1",
        })
        self.env.pop("WAYLAND_DISPLAY", None)

    def invoke(self, script, *options, env=None):
        child_env = dict(self.env if env is None else env)
        if script == "check_cli.py":
            child_env["FIXTURE_EXPECT_RESOURCE_FREE_CLI"] = "1"
        result = subprocess.run(
            [sys.executable, "-B", str(SCRIPTS / script), *map(str, options),
             "--log-root", str(self.logs)],
            env=child_env,
            capture_output=True, text=True, timeout=15, check=False,
        )
        return result

    def wayland_fixture(self):
        # An unconnected local socket is enough for the preflight. The fake
        # application does not speak Wayland or access any desktop socket.
        connection = socket.socket(socket.AF_UNIX)
        connection.bind(str(self.root / "fixture-wayland"))
        self.addCleanup(connection.close)
        self.env["WAYLAND_DISPLAY"] = str(self.root / "fixture-wayland")

    def shader_fixture(self):
        build = self.root / "build"
        shaders = build / "share/npgs/Shaders"
        shaders.mkdir(parents=True)
        (build / "npgs-resource-manifest.txt").write_text("Shaders/test.spv\n")
        (shaders / "test.spv").write_bytes(b"fixture-spirv")
        return build, shaders

    def test_log_root_and_custom_working_directory(self):
        self.assertEqual(LOG_ROOT, SCRIPTS.parent / "local" / "acceptance")
        run = Run("custom", self.logs)
        self.assertEqual(run.directory.parent, self.logs)
        self.assertEqual(run.cwd.parent, run.directory)
        self.assertEqual(list(run.cwd.iterdir()), [])

    def test_cli_success(self):
        # Override an inherited valid resource path rather than accidentally
        # relying on the caller's environment or working-directory resources.
        self.env["NPGS_ASSET_DIR"] = str(self.root)
        result = self.invoke("check_cli.py", self.exe)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("PASS: CLI only (33 cases), no asset/display prerequisites",
                      result.stdout)

    def test_cli_rejects_resource_initialization_before_main(self):
        for mode in ("static-resource-init-help", "static-resource-init-invalid"):
            with self.subTest(mode=mode):
                self.env["FIXTURE_MODE"] = mode
                result = self.invoke("check_cli.py", self.exe)
                self.assertEqual(result.returncode, 1)
                self.assertIn("fixture resource initialization failed before main",
                              result.stderr)
                self.assertNotIn("PASS:", result.stdout)

    def test_cli_missing_executable_and_wrong_exit(self):
        for exe in (self.root / "missing", Path("/bin/false")):
            with self.subTest(exe=exe):
                result = self.invoke("check_cli.py", exe)
                self.assertEqual(result.returncode, 1)
                self.assertIn("FAIL:", result.stderr)
                self.assertNotIn("PASS:", result.stdout)

    def test_smoke_missing_display(self):
        result = self.invoke("smoke_wayland.py", self.exe)
        self.assertEqual(result.returncode, 1)
        self.assertIn("NOT RUN (failure)", result.stderr)

    def test_smoke_success_and_resize_missing(self):
        self.wayland_fixture()
        result = self.invoke("smoke_wayland.py", self.exe, "--smoke-resize")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.env["FIXTURE_MODE"] = "resize-missing"
        result = self.invoke("smoke_wayland.py", self.exe, "--smoke-resize")
        self.assertEqual(result.returncode, 1)
        self.assertIn("missing resize success log", result.stderr)

    def test_validation_missing_and_validation_error_fail(self):
        self.wayland_fixture()
        for mode in ("validation-missing", "validation-error"):
            with self.subTest(mode=mode):
                self.env["FIXTURE_MODE"] = mode
                result = self.invoke("smoke_wayland.py", self.exe, "--validation")
                self.assertEqual(result.returncode, 1)
                self.assertNotIn("PASS:", result.stdout)

    def test_timeout_kills_and_reaps_child(self):
        self.wayland_fixture()
        self.env["FIXTURE_MODE"] = "timeout"
        result = self.invoke("smoke_wayland.py", self.exe, "--timeout", "0.1")
        self.assertEqual(result.returncode, 1)
        self.assertIn("timed out", result.stderr)
        metadata = json.loads(next(self.logs.glob("*/smoke.json")).read_text())
        self.assertTrue(metadata["timed_out"])
        self.assertLess(metadata["returncode"], 0)
        self.assertFalse(Path(f"/proc/{metadata['pid']}").exists())

    def test_timeout_also_kills_descendants(self):
        pid_file = self.root / "descendant.pid"
        code = (
            "import subprocess,sys,time\n"
            "from pathlib import Path\n"
            "child = subprocess.Popen([sys.executable, '-c', "
            "'import signal,time; signal.signal(signal.SIGTERM, signal.SIG_IGN); "
            "time.sleep(60)'])\n"
            "Path(sys.argv[1]).write_text(str(child.pid))\n"
            "time.sleep(60)\n"
        )
        run = Run("descendant", self.logs)
        with self.assertRaises(AcceptanceError):
            run.command("timeout", [sys.executable, "-c", code, pid_file],
                        self.env, timeout=0.5)
        pid = int(pid_file.read_text())
        # A killed orphan can briefly remain a zombie until init reaps it.
        status = Path(f"/proc/{pid}/stat")
        for _ in range(100):
            try:
                state = status.read_text().split(") ", 1)[1].split()[0]
            except FileNotFoundError:
                break
            if state == "Z":
                break
            time.sleep(0.01)
        else:
            self.fail(f"descendant {pid} survived process-group cleanup")

    def test_invalid_script_arguments(self):
        for options in (
            ("--timeout", "nan"), ("--timeout", "0"),
            ("--smoke-resize", "--frames", "3"),
            ("--smoke-resize", "--resize-pattern", "["),
            ("--smoke-resize", "--resize-pattern", ".*"),
        ):
            with self.subTest(options=options):
                result = self.invoke("smoke_wayland.py", self.exe, *options)
                self.assertEqual(result.returncode, 2)

    def test_success_marker_contract(self):
        good = "NPGS_SMOKE_OK frames=8 platform=wayland\n"
        check_result(0, good, "", 8)
        for code, stdout, stderr in (
            (1, good, ""),
            (0, good, "Validation Error: VUID-test-bad"),
            (0, good.replace("frames=8", "frames=7"), ""),
            (0, good.replace(" platform=wayland", ""), ""),
            (0, good.replace("wayland", "x11"), ""),
            (0, good + "not the final marker\n", ""),
            (0, good + good, ""),
        ):
            with self.subTest(code=code, stdout=stdout, stderr=stderr), \
                    self.assertRaises(AcceptanceError):
                check_result(code, stdout, stderr, 8)
        with self.assertRaises(AcceptanceError):
            check_result(0, good, "", 8, re.compile("NPGS_SMOKE_RESIZE_OK"))

    def test_spirv_success_missing_and_invalid(self):
        build, shaders = self.shader_fixture()
        result = self.invoke("check_spirv.py", build, "--spirv-val", self.exe)
        self.assertEqual(result.returncode, 0, result.stderr)
        (shaders / "test.spv").write_bytes(b"invalid")
        result = self.invoke("check_spirv.py", build, "--spirv-val", self.exe)
        self.assertEqual(result.returncode, 1)
        self.assertIn("SPIR-V validation failed", result.stderr)
        (shaders / "test.spv").unlink()
        result = self.invoke("check_spirv.py", build, "--spirv-val", self.exe)
        self.assertEqual(result.returncode, 1)
        self.assertIn("missing built shader", result.stderr)

    def test_spirv_missing_tool_and_empty_manifest(self):
        build, _ = self.shader_fixture()
        result = self.invoke("check_spirv.py", build, "--spirv-val", self.root / "missing")
        self.assertEqual(result.returncode, 1)
        self.assertIn("NOT RUN (failure)", result.stderr)
        (build / "npgs-resource-manifest.txt").write_text("Textures/test.png\n")
        result = self.invoke("check_spirv.py", build, "--spirv-val", self.exe)
        self.assertEqual(result.returncode, 1)
        self.assertIn("zero shader variants", result.stderr)


if __name__ == "__main__":
    unittest.main()
