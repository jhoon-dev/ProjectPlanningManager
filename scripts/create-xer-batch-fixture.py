"""Create the batch XER package consumed by the independent reader."""
from pathlib import Path
import subprocess
import sys

build = Path(sys.argv[1] if len(sys.argv) > 1 else "build").resolve()
name = "jobs_xer_batch_tests" + (".exe" if sys.platform == "win32" else "")
binary = next((p for p in (build / name, build / "Release" / name) if p.is_file()), None)
if binary is None:
    raise SystemExit("Build jobs_xer_batch_tests first")
package = build / "xer-batch-qa" / "package"
package.parent.mkdir(parents=True, exist_ok=True)
subprocess.run([str(binary), str(package)],
               cwd=build, timeout=180, check=True)
