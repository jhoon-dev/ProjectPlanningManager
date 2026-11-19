"""Adapt compile-time names in the pinned performance reference, without changing its algorithms."""
from pathlib import Path
import sys

root = Path(sys.argv[1]).resolve()
if not (root / 'CMakeLists.txt').is_file() or not (root / 'src').is_dir():
    raise ValueError('Expected the checked-out editing reference')
for path in [root / 'CMakeLists.txt', *root.glob('src/**/*.cpp'), *root.glob('src/**/*.h'), *root.glob('src/**/*.mm'), *root.glob('tests/**/*.cpp'), *root.glob('tests/**/*.h'), *root.glob('scripts/*.cmake'), *root.glob('cmake/*.cmake')]:
    text = path.read_text(encoding='utf-8')
    updated = text.replace('namespace jobs', 'namespace ppm').replace('jobs::', 'ppm::')
    updated = updated.replace('jobs_', 'ppm_').replace('JOBS_', 'PPM_')
    if updated != text:
        path.write_text(updated, encoding='utf-8')
for path in root.glob('src/**/*jobs_*.qrc'):
    path.rename(path.with_name(path.name.replace('jobs_', 'ppm_')))
print('Prepared PPM compile-time names; reference calculation policies and saved identifiers unchanged')
