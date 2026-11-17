"""Invoke the existing native Document batch API without shell evaluation."""

import asyncio
import json
from pathlib import Path
import tempfile
from .store import fail, canonical


class NativeEngine:
    def __init__(self, executable, timeout=45):
        self.executable = str(Path(executable).resolve())
        if not Path(self.executable).is_file():
            raise ValueError("Build PPM-Automation before starting the server")
        self.timeout = timeout
        self.slots = asyncio.Semaphore(2)

    async def evaluate(self, native, request=None, *, apply=False):
        async with self.slots:
            with tempfile.TemporaryDirectory(prefix="ppm-engine-") as temp:
                project = Path(temp) / "project.ppm"
                project.write_bytes(native)
                args = [self.executable, "--project", str(project)]
                if request is not None:
                    path = Path(temp) / "request.json"
                    path.write_text(canonical(request), encoding="utf-8")
                    args += ["--request", str(path)]
                    if apply:
                        args += ["--apply"]
                flags = {}
                import sys

                if sys.platform == "win32":
                    import subprocess

                    flags["creationflags"] = subprocess.CREATE_NO_WINDOW
                process = await asyncio.create_subprocess_exec(
                    *args, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE, **flags
                )
                try:
                    stdout, stderr = await asyncio.wait_for(process.communicate(), self.timeout)
                except (asyncio.TimeoutError, asyncio.CancelledError):
                    if process.returncode is None:
                        process.kill()
                    await process.communicate()
                    raise
                if len(stdout) > 64 * 1024 * 1024:
                    fail(422, "native_output_limit", "Native response exceeds limit")
                try:
                    result = json.loads(stdout)
                except (ValueError, UnicodeError):
                    fail(422, "native_validation_failed", "Native engine did not return valid JSON")
                if process.returncode or not isinstance(result, dict):
                    fail(
                        422,
                        "native_validation_failed",
                        (
                            result.get("message", "Native validation rejected the request")
                            if isinstance(result, dict)
                            else "Native validation rejected the request"
                        ),
                    )
                summary = result
                if apply:
                    inspector = await asyncio.create_subprocess_exec(
                        self.executable,
                        "--project",
                        str(project),
                        stdout=asyncio.subprocess.PIPE,
                        stderr=asyncio.subprocess.PIPE,
                        **flags
                    )
                    try:
                        inspected, _ = await asyncio.wait_for(inspector.communicate(), self.timeout)
                    except (asyncio.TimeoutError, asyncio.CancelledError):
                        if inspector.returncode is None:
                            inspector.kill()
                        await inspector.communicate()
                        raise
                    if inspector.returncode:
                        fail(422, "native_validation_failed", "Native result validation failed")
                    summary = json.loads(inspected)
                return result, project.read_bytes(), summary
