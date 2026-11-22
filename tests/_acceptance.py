"""Shared, standard-library-only acceptance helpers (Python 3.9+)."""

import argparse
import json
import math
import os
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

LOG_ROOT = Path(__file__).resolve().parent.parent / "local" / "acceptance"


class AcceptanceError(Exception):
    pass


def positive_timeout(value):
    try:
        number = float(value)
    except ValueError:
        raise argparse.ArgumentTypeError("timeout must be a positive finite number")
    if not math.isfinite(number) or number <= 0:
        raise argparse.ArgumentTypeError("timeout must be a positive finite number")
    return number


def positive_integer(value):
    try:
        number = int(value)
    except ValueError:
        raise argparse.ArgumentTypeError("expected a positive integer")
    if number <= 0:
        raise argparse.ArgumentTypeError("expected a positive integer")
    return number


def executable_path(value):
    path = Path(value).expanduser().resolve()
    if not path.is_file() or not os.access(path, os.X_OK):
        raise AcceptanceError(f"not an executable file: {path}")
    return path


def require(condition, message):
    if not condition:
        raise AcceptanceError(message)


def entrypoint(main):
    try:
        main()
    except (AcceptanceError, OSError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("FAIL: interrupted; child process group cleaned up", file=sys.stderr)
        return 130
    return 0


class Run:
    """Keep per-run logs and an empty, isolated working directory."""

    def __init__(self, name, log_root=LOG_ROOT):
        root = Path(log_root).expanduser().resolve()
        root.mkdir(parents=True, exist_ok=True)
        self.directory = Path(tempfile.mkdtemp(prefix=f"{name}-", dir=root))
        self.cwd = self.directory / "cwd"
        self.cwd.mkdir()
        print(f"Logs: {self.directory}", flush=True)

    @staticmethod
    def _stop_group(process):
        # The child starts a new session: never signal the caller/compositor.
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            pass
        # Also terminate descendants if the group leader exited on SIGTERM.
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        process.wait()

    def command(self, name, command, env, timeout):
        stdout_path = self.directory / f"{name}.stdout.log"
        stderr_path = self.directory / f"{name}.stderr.log"
        metadata_path = self.directory / f"{name}.json"
        metadata = {
            "command": [str(part) for part in command],
            "cwd": str(self.cwd),
            "timeout_seconds": timeout,
            # Do not serialize the entire environment (tokens, credentials).
            "display": env.get("DISPLAY"),
            "wayland_display": env.get("WAYLAND_DISPLAY"),
            "wayland_debug": env.get("WAYLAND_DEBUG"),
        }
        process = None
        started = time.monotonic()
        try:
            with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
                process = subprocess.Popen(
                    metadata["command"], cwd=self.cwd, env=env,
                    stdout=stdout, stderr=stderr, start_new_session=True,
                )
                metadata["pid"] = process.pid
                try:
                    process.wait(timeout=timeout)
                except subprocess.TimeoutExpired:
                    metadata["timed_out"] = True
                    self._stop_group(process)
                    raise AcceptanceError(
                        f"{name}: timed out after {timeout}s; logs: {self.directory}"
                    )
                except BaseException:
                    self._stop_group(process)
                    raise
        finally:
            metadata["returncode"] = process.returncode if process else None
            metadata["elapsed_seconds"] = round(time.monotonic() - started, 3)
            metadata_path.write_text(json.dumps(metadata, indent=2) + "\n")
        return (
            process.returncode,
            stdout_path.read_text(errors="replace"),
            stderr_path.read_text(errors="replace"),
        )
