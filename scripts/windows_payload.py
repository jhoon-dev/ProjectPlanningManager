"""Shared self-contained Windows runtime payload; no installer or signing policy."""
import hashlib
from pathlib import Path
import shutil
import subprocess
from mpp_payload import deploy_reader


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def deploy(root, stage, executable, qt, compiler, environment, log):
    stage.mkdir(parents=True, exist_ok=False)
    shutil.copyfile(executable, stage / 'PPM.exe')
    automation = Path(executable).with_name('PPM-Automation.exe')
    if not automation.is_file():
        raise RuntimeError('Build PPM-Automation before packaging the runtime payload')
    shutil.copyfile(automation, stage / automation.name)
    deploy_reader(Path(executable).parent / 'mpp-reader', stage / 'mpp-reader')
    with log.open('w', encoding='utf-8') as output:
        # TUIO touch is unrelated to collaboration; exclude its unused plugin.
        subprocess.run([str(qt / 'bin/windeployqt.exe'), '--release', '--compiler-runtime', '--no-translations',
                        '--exclude-plugins', 'qtuiotouchplugin',
                        '--dir', str(stage), str(stage / 'PPM.exe')], env=environment,
                       stdout=output, stderr=subprocess.STDOUT, check=True)
    for name in ['PPM.exe', 'PPM-Automation.exe', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6PrintSupport.dll', 'Qt6Network.dll', 'platforms/qwindows.dll', 'tls/qschannelbackend.dll',
                 'libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll']:
        if not (stage / name).is_file():
            raise RuntimeError('Required MinGW runtime missing: ' + name)
    (stage / 'qt.conf').write_text('[Paths]\nPrefix=.\nPlugins=.\n', encoding='utf-8')
    shutil.copyfile(root / 'packaging/NOTICE.txt', stage / 'NOTICE.txt')
    shutil.copytree(root / 'packaging/licenses/qtbase', stage / 'licenses/qtbase')
    shutil.copytree(root / 'resources/i18n', stage / 'licenses/qttranslations',
                    ignore=shutil.ignore_patterns('*.qm', 'README.md'))
    shutil.copytree(root / 'resources/timezones', stage / 'licenses/timezones', ignore=shutil.ignore_patterns('*.inc', '*.zip'))
    shutil.copytree(root / 'resources/text', stage / 'licenses/unicode', ignore=shutil.ignore_patterns('*.inc', '*.zip'))
    shutil.copyfile(qt / 'sbom/qtbase-6.8.3.spdx.json', stage / 'licenses/qtbase/qtbase-6.8.3.spdx.json')
    for name in ('gcc', 'mingw-w64', 'winpthreads'):
        shutil.copytree(compiler.parent / 'licenses' / name, stage / 'licenses/runtime' / name)


def inventory(stage):
    return [{'path': p.relative_to(stage).as_posix(), 'bytes': p.stat().st_size, 'sha256': sha256(p)}
            for p in sorted(stage.rglob('*')) if p.is_file()]
