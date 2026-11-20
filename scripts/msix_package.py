"""MSIX identity, manifest and payload contracts (no Windows dependencies)."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import struct
from urllib.parse import unquote
import xml.etree.ElementTree as ET
import zipfile

FOUNDATION = 'http://schemas.microsoft.com/appx/manifest/foundation/windows10'
UAP = 'http://schemas.microsoft.com/appx/manifest/uap/windows10'
UAP10 = 'http://schemas.microsoft.com/appx/manifest/uap/windows10/10'
RESCAP = 'http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities'
DEV_OID = 'OID.2.25.311729368913984317654407730594956997722=1'
FIELDS = {'IdentityName', 'Publisher', 'PublisherDisplayName', 'DisplayName'}
ASSETS = {'Square44x44Logo.png': 44, 'Square150x150Logo.png': 150, 'StoreLogo.png': 50, 'StoreListing300.png': 300}


def package_output(root, mode, requested=None):
    root = Path(root).resolve()
    folder = Path(requested) if requested is not None else root / 'build' / (
        'msix-development' if mode == 'development' else 'msix-store-upload')
    folder = folder.resolve()
    if folder.is_relative_to((root / 'dist').resolve()):
        raise ValueError('Unsigned MSIX files are internal build inputs; do not write them to public dist')
    return folder


def version(value):
    if not re.fullmatch(r'\d+\.\d+\.\d+\.0', value) or any(int(part) > 65535 for part in value.split('.')):
        raise ValueError('MSIX version must have four components in 0..65535 and a final 0')
    return value


def verify_executable_version(executable, expected):
    """Read the built PE version resource through Windows, independently of CMake."""
    import ctypes
    from ctypes import wintypes
    api = ctypes.WinDLL('version', use_last_error=True)
    api.GetFileVersionInfoSizeW.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(wintypes.DWORD)]
    api.GetFileVersionInfoSizeW.restype = wintypes.DWORD
    api.GetFileVersionInfoW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
    api.GetFileVersionInfoW.restype = wintypes.BOOL
    api.VerQueryValueW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR, ctypes.POINTER(ctypes.c_void_p),
                                   ctypes.POINTER(wintypes.UINT)]
    api.VerQueryValueW.restype = wintypes.BOOL
    path = str(Path(executable).resolve())
    unused = wintypes.DWORD()
    size = api.GetFileVersionInfoSizeW(path, ctypes.byref(unused))
    if not size:
        raise ValueError('Missing executable version resource: ' + path)
    buffer = ctypes.create_string_buffer(size)
    if not api.GetFileVersionInfoW(path, 0, size, buffer):
        raise ctypes.WinError(ctypes.get_last_error())

    def query(key):
        pointer, length = ctypes.c_void_p(), wintypes.UINT()
        if not api.VerQueryValueW(buffer, key, ctypes.byref(pointer), ctypes.byref(length)):
            raise ValueError('Missing executable version field: ' + key)
        return pointer, length.value

    pointer, length = query('\\')
    if length < 52:
        raise ValueError('Invalid fixed executable version resource')
    fixed = struct.unpack('<13I', ctypes.string_at(pointer, 52))
    if fixed[0] != 0xFEEF04BD:
        raise ValueError('Invalid executable version signature')
    result = {}
    for name, high, low in [('file_numeric', fixed[2], fixed[3]), ('product_numeric', fixed[4], fixed[5])]:
        result[name] = f'{high >> 16}.{high & 65535}.{low >> 16}.{low & 65535}'
    pointer, length = query('\\VarFileInfo\\Translation')
    translations = ctypes.string_at(pointer, length)
    if not translations or len(translations) % 4:
        raise ValueError('Invalid executable version translations')
    for language, codepage in struct.iter_unpack('<HH', translations):
        for name in ('FileVersion', 'ProductVersion'):
            pointer, length = query(f'\\StringFileInfo\\{language:04x}{codepage:04x}\\{name}')
            result[f'{language:04x}{codepage:04x}/{name}'] = ctypes.wstring_at(pointer, length).rstrip('\0')
    if any(value != expected for value in result.values()):
        raise ValueError(f'Executable version differs from MSIX {expected}: {result}')
    return result


def identity(mode, config=None):
    if mode == 'development':
        if config is not None:
            raise ValueError('Development identity is fixed; do not supply Store credentials')
        return {'IdentityName': 'ToolMoa.PPM.MsixDevelopment',
                'Publisher': 'CN=PPM MSIX Development, ' + DEV_OID,
                'PublisherDisplayName': 'PPM Development', 'DisplayName': 'Project Planning Manager Development'}
    if mode != 'store' or not isinstance(config, dict) or set(config) != FIELDS:
        raise ValueError('Store mode requires exactly the Partner Center identity fields')
    for key, value in config.items():
        if not isinstance(value, str) or not value.strip() or value != value.strip() or len(value) > 8192:
            raise ValueError('Missing or invalid Store identity field: ' + key)
        if any(ord(c) < 32 for c in value) or any(c in value for c in '<>'):
            raise ValueError('Control characters or placeholders in Store identity')
    name = config['IdentityName']
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9.-]{2,49}', name) or name.endswith('.'):
        raise ValueError('Invalid package IdentityName')
    if not config['Publisher'].startswith('CN=') or len(config['Publisher']) <= 3:
        raise ValueError('Use the exact Package/Identity/Publisher value from Partner Center')
    if any(word in (name + ' ' + config['Publisher']).lower()
           for word in ('msixdevelopment', 'placeholder', 'example', 'todo', 'oid.2.25.')):
        raise ValueError('Development or placeholder identity cannot be submitted to Store')
    if len(config['DisplayName']) > 50 or len(config['PublisherDisplayName']) > 256:
        raise ValueError('Store display name is too long')
    return dict(config)


def manifest(config, package_version, probe=False):
    version(package_version)
    for prefix, uri in [('', FOUNDATION), ('uap', UAP), ('uap10', UAP10), ('rescap', RESCAP)]:
        ET.register_namespace(prefix, uri)
    def element(parent, name, attrs=None, text=None, ns=FOUNDATION):
        node = ET.SubElement(parent, '{' + ns + '}' + name, attrs or {})
        node.text = text
        return node
    root = ET.Element('{' + FOUNDATION + '}Package', {'IgnorableNamespaces': 'uap uap10 rescap'})
    element(root, 'Identity', {'Name': config['IdentityName'], 'Publisher': config['Publisher'],
                              'Version': package_version, 'ProcessorArchitecture': 'x64'})
    properties = element(root, 'Properties')
    for name, value in [('DisplayName', config['DisplayName']), ('PublisherDisplayName', config['PublisherDisplayName']),
                        ('Description', 'PPM 프로젝트 일정·자원·원가 관리'), ('Logo', 'Assets\\StoreLogo.png')]:
        element(properties, name, text=value)
    dependencies = element(root, 'Dependencies')
    element(dependencies, 'TargetDeviceFamily', {'Name': 'Windows.Desktop', 'MinVersion': '10.0.19041.0',
                                                'MaxVersionTested': '10.0.26100.0'})
    resources = element(root, 'Resources')
    element(resources, 'Resource', {'Language': 'ko-KR'})
    applications = element(root, 'Applications')
    # Store packages have one application; the test probe is development-only.
    entries = [('PPM', 'PPM.exe', False)]
    for app_id, executable, hidden in entries + ([('PackageProbe', 'ppm_msix_probe.exe', True)] if probe else []):
        app = element(applications, 'Application', {'Id': app_id, 'Executable': executable,
                      'EntryPoint': 'Windows.FullTrustApplication', '{' + UAP10 + '}RuntimeBehavior': 'packagedClassicApp',
                      '{' + UAP10 + '}TrustLevel': 'mediumIL'})
        visual = {'DisplayName': config['DisplayName'], 'Description': 'PPM 프로젝트 관리',
                  'Square150x150Logo': 'Assets\\Square150x150Logo.png',
                  'Square44x44Logo': 'Assets\\Square44x44Logo.png', 'BackgroundColor': 'transparent'}
        if hidden:
            visual['AppListEntry'] = 'none'
        element(app, 'VisualElements', visual, ns=UAP)
    capabilities = element(root, 'Capabilities')
    element(capabilities, 'Capability', {'Name': 'runFullTrust'}, ns=RESCAP)
    return ET.tostring(root, encoding='utf-8', xml_declaration=True)


def validate_assets(folder):
    entries = json.loads((folder / 'assets.json').read_text())['files']
    for name, expected in ASSETS.items():
        content = (folder / name).read_bytes()
        if content[:8] != b'\x89PNG\r\n\x1a\n' or struct.unpack('>II', content[16:24]) != (expected, expected):
            raise ValueError('Invalid PNG asset size: ' + name)
        if hashlib.sha256(content).hexdigest() != entries[name]['sha256']:
            raise ValueError('Asset differs from vector-generated inventory: ' + name)


MAX_PAYLOAD_BYTES = 300 * 1024 * 1024
MAX_PROBE_BYTES = 32 * 1024 * 1024


def validate_payload(stage, mode, probe=False):
    if mode == 'store' and probe:
        raise ValueError('Test probe is not a Store payload')
    names = set()
    total = 0
    probe_total = 0
    for file in stage.rglob('*'):
        if file.is_symlink():
            raise ValueError('Payload links are not allowed')
        if not file.is_file():
            continue
        relative = file.relative_to(stage).as_posix()
        if not relative.isascii() or ':' in relative or relative.casefold() in names:
            raise ValueError('Non-ASCII or duplicate Windows payload path')
        names.add(relative.casefold())
        if file.suffix.lower() in {'.pfx', '.p12', '.cer', '.key', '.pdb', '.obj', '.ps1', '.bat', '.cmd'}:
            raise ValueError('Development or executable helper file in payload: ' + relative)
        if 'uninstall' in file.name.lower() or file.name.lower().endswith('setup.exe'):
            raise ValueError('MSIX must not embed an installer/uninstaller')
        if mode == 'store' and ('probe' in file.name.lower() or file.name.lower().startswith(('ppm_', 'jobs_')) or file.name.lower() == 'qt6test.dll'):
            raise ValueError('Test artifact in Store payload')
        total += file.stat().st_size
        if probe and relative.casefold() in {'ppm_msix_probe.exe', 'qt6test.dll'}:
            probe_total += file.stat().st_size
        # The explicit development probe has its own bounded budget; the app
        # and its offline reader retain the production package limit.
        if len(names) > 2000 or total - probe_total > MAX_PAYLOAD_BYTES or probe_total > MAX_PROBE_BYTES:
            raise ValueError('MSIX payload size/count limit exceeded')
    if 'ppm.exe' not in names or 'appxmanifest.xml' not in names:
        raise ValueError('Required app or manifest missing')
    return total


def verify_archive(package, expected_files, expected_manifest):
    with zipfile.ZipFile(package) as archive:
        members = archive.infolist()
        seen = set()
        decoded = {}
        for member in members:
            # OPC ZIP part names escape '+', spaces, etc.; never use unquote_plus.
            name = unquote(member.filename, errors='strict').replace('\\', '/')
            path = PurePosixPath(name)
            if path.is_absolute() or '..' in path.parts or ':' in name:
                raise ValueError('Unsafe MSIX archive path')
            if name.casefold() in seen:
                raise ValueError('Duplicate MSIX archive path')
            seen.add(name.casefold())
            decoded[name] = member
        if archive.read(decoded['AppxManifest.xml']) != expected_manifest:
            raise ValueError('Packaged manifest differs from the checked input')
        for item in expected_files:
            content = archive.read(decoded[item['path']])
            if len(content) != item['bytes'] or hashlib.sha256(content).hexdigest() != item['sha256']:
                raise ValueError('Packaged file differs from payload inventory: ' + item['path'])
        footprint = {'[Content_Types].xml', 'AppxBlockMap.xml', 'AppxSignature.p7x', 'AppxMetadata/CodeIntegrity.cat'}
        extras = set(decoded) - {f['path'] for f in expected_files} - footprint
        if extras:
            raise ValueError('Unexpected files in MSIX: ' + str(sorted(extras)))
