"""Keep generated validation output out of Git; inspect tracked paths, not local runs."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
active_pilot = {
    "docs/validation/weekly-pm-pilot-2026-11-18/pilot-start.ppm",
    "docs/validation/weekly-pm-pilot-2026-11-18/build_case.py",
}
paths = subprocess.check_output(
    ["git", "ls-files", "-z", "--", "docs/validation"], cwd=root
).decode("utf-8").split("\0")
errors = []
for name in filter(None, paths):
    path = root / name
    if name in active_pilot:
        continue
    if path.suffix != ".md":
        errors.append(f"{name}: generated output belongs in build*/ and CI artifacts")
    elif path.stat().st_size > 64 * 1024:
        errors.append(f"{name}: keep the validation summary within 64 KiB")
if errors:
    print("\n".join(errors), file=sys.stderr)
    sys.exit(1)
print("PASS: tracked validation files contain summaries and the active PM pilot only")
