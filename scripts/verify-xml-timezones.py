"""Reconcile native/formula/XML/XER/reports with pinned TZif via independent CPython ZoneInfo."""
import argparse
from bisect import bisect_right
import csv
from datetime import datetime, timedelta, timezone
from decimal import Decimal
import hashlib
import io
import json
from pathlib import Path
import re
import zipfile
from zoneinfo import ZoneInfo

parser = argparse.ArgumentParser()
parser.add_argument('root', type=Path)
parser.add_argument('--data', type=Path, default=Path('resources/timezones'))
args = parser.parse_args()
root, data = args.root, args.data
load = lambda n: json.loads((root / n).read_text(encoding='utf-8-sig'))
manifest = json.loads((data / 'manifest.json').read_text())
assert manifest['version'] == '2026f' and manifest['zones'] == 597 and manifest['patterns'] == 306 and manifest['transitions'] == 30397
for name, digest in manifest['files'].items():
    assert hashlib.sha256((data / name).read_bytes()).hexdigest() == digest, name
assert manifest['wheel_sha256'] == '0a10eec6e13ef16b627fabed9a5f2ba61a62b1c67f327305f4e7e412a70b4aec'
assert manifest['source_sha256'] == '01b4a91cec5167ed48c11e8ab78f2048cead78ce94b0cafcad0ae8c5a3044d3d'
archive = zipfile.ZipFile(data / 'tzif-2026f.zip')
zones = {}
def zone(name):
    if name.startswith('2026f:'):
        key = name[6:]
        if key not in zones:
            zones[key] = ZoneInfo.from_file(io.BytesIO(archive.read(key)), key=key)
        return zones[key]
    if name in ('UTC', 'Z'): return timezone.utc
    if name.startswith('UTC'): name = name[3:]
    m = re.fullmatch(r'([+-])(\d{2}):(\d{2})', name)
    assert m
    return timezone(timedelta(minutes=(int(m[2]) * 60 + int(m[3])) * (-1 if m[1] == '-' else 1)))

def iso(d):
    s = d.isoformat()
    if d.tzinfo is not None:
        # Roundtrip vectors have offsets of whole minutes; avoid touching offset zeros.
        plain = iso(d.replace(tzinfo=None))
        offset = d.utcoffset()
        if not offset: return plain + 'Z'
        seconds = int(offset.total_seconds())
        assert seconds % 60 == 0
        return plain + ('-' if seconds < 0 else '+') + f'{abs(seconds)//3600:02}:{abs(seconds)//60%60:02}'
    if '.' in s: s = s.rstrip('0').rstrip('.')
    return s

def resolve(civil, spec, policy='reject'):
    if isinstance(civil, str): civil = datetime.fromisoformat(civil)
    z = zone(spec)
    points = {civil.replace(tzinfo=z, fold=fold).astimezone(timezone.utc) for fold in (0, 1)
              if civil.replace(tzinfo=z, fold=fold).astimezone(timezone.utc).astimezone(z).replace(tzinfo=None) == civil}
    assert points, (civil, spec, 'gap')
    assert len(points) == 1 or policy in ('earlier', 'later'), (civil, spec, 'overlap')
    return max(points) if policy == 'later' else min(points)

def convert(civil, src, dst, policy='reject'):
    return resolve(civil, src, policy).astimezone(zone(dst))

# The C++ output samples every embedded transition at -1us/0/+1us and all aliases.
transition_rows = 0
with (root / 'transitions.tsv').open(encoding='utf-8', newline='') as f:
    for r in csv.DictReader(f, delimiter='\t'):
        actual = datetime.fromisoformat(r['utc']).replace(tzinfo=timezone.utc).astimezone(zone(r['zone']))
        assert int(r['offset']) == int(actual.utcoffset().total_seconds()), r
        assert r['civil'] == iso(actual.replace(tzinfo=None)), r
        transition_rows += 1
assert transition_rows == 92079

# Also check every zone for every month across the complete supported year range.
# This catches missing transitions, not just incorrect values at stored transitions.
patterns, names = {}, {}
for line in (data / 'tzdb-2026f.inc').read_text(encoding='ascii').splitlines():
    if not line.startswith('R"PPMTZ('): continue
    cols = line[8:-8].split('\t')
    if cols[0] == 'P': patterns[int(cols[1])] = [tuple(map(int, p.split(':'))) for p in cols[2].split(',')]
    else: names[cols[1]] = int(cols[2])
monthly = 0
epoch = datetime(1970, 1, 1, tzinfo=timezone.utc)
for name, index in names.items():
    z, entries = zone('2026f:' + name), patterns[index]
    times, offsets = zip(*entries)
    for year in range(1970, 2101):
        for month in range(1, 13):
            when = datetime(year, month, 1, 12, tzinfo=timezone.utc)
            seconds = int((when - epoch).total_seconds())
            assert offsets[bisect_right(times, seconds) - 1] == int(when.astimezone(z).utcoffset().total_seconds()), (name, when)
            monthly += 1
assert monthly == 938484

native, envelope = load('native.json'), load('source.ppm')
assert envelope['schemaVersion'] == 53 and envelope['payload'] == native
attrs = lambda row: {a['key']: a['value'] for a in row}
assert attrs(native['tasks'][0]['attributes'])['field:utc'] == '1999-01-01T00:00:00'
assert attrs(native['tasks'][0]['attributes'])['field:error'] == '2000-01-01T00:00:00'
assert not native.get('periodActuals') and not native.get('progress')
expected = {}
for t in native['tasks']:
    raw = attrs(t['attributes'])
    civil, src, dst = raw['field:civil'], raw['field:from'], raw['field:to']
    utc, target = resolve(civil, src), convert(civil, src, dst)
    seconds = int(utc.astimezone(zone(src)).utcoffset().total_seconds())
    expected['TASK', t['id']] = dict(civil=civil, **{'from': src, 'to': dst}, missing=None,
        utc=iso(utc.replace(tzinfo=None)), local=iso(target.replace(tzinfo=None)), wire=iso(target),
        offset=Decimal(seconds), parsed=iso(utc.replace(tzinfo=None)), date=utc.date().isoformat(),
        elapsed=Decimal(0), flag=seconds > 0, error=None, null=None, native=Decimal(seconds)/60, summary='leaf')
expected['PROJECT', ''] = dict(project=iso(convert('2026-11-13T00:00:00', 'UTC', '2026f:Australia/Lord_Howe').replace(tzinfo=None)))
expected['RSRC', 'R'] = dict(resource=Decimal(int(resolve('2026-07-01T12:00:00', '2026f:America/New_York').astimezone(zone('2026f:America/New_York')).utcoffset().total_seconds())))
for a in native['assignments']:
    e = expected['TASK', a['taskId']]
    expected['ASSIGNMENT', a['taskId']] = dict(received=e['wire'], assignment=e['local'], text=e['wire'])

cases = load('cases.json')
assert len(cases) == 9
for case in cases:
    source = case['source']
    if source.startswith('PPMParseUtc'):
        s = re.search(r'"([^"]+)"', source)[1]
        value = iso(datetime.fromisoformat(s).astimezone(timezone.utc).replace(tzinfo=None))
    else:
        d = re.search(r'#([^#]+)#', source)[1]
        strings = re.findall(r'"([^"]+)"', source)
        value = iso(convert(d, strings[0], strings[1], strings[2] if len(strings) == 3 else 'reject').replace(tzinfo=None))
    assert value == case['value'], case

ns = {'p': 'http://schemas.microsoft.com/project'}
import xml.etree.ElementTree as ET
text = lambda n, k: n.findtext('p:' + k, '', ns)
xml = ET.parse(root / 'timezone.xml').getroot()
definitions = {text(d, 'FieldID'): d for d in xml.findall('p:ExtendedAttributes/p:ExtendedAttribute', ns)}
fields = {f['name']: f for group in ('customFields', 'projectFields', 'resourceFields', 'assignmentFields') for f in native[group]}
assert len(fields) == len(definitions) == 21
formula_definitions = 0
for d in definitions.values():
    if fields[text(d, 'Alias')].get('xmlFormula'):
        assert text(d, 'Formula') and '[field:' not in text(d, 'Formula')
        formula_definitions += 1

def parsed(f, raw):
    if f['type'] == 'number': return Decimal(raw)
    if f['type'] == 'flag': return raw == '1'
    if f['type'] in ('date', 'start', 'finish'):
        d = datetime.fromisoformat(raw)
        return d.date().isoformat() if f['type'] == 'date' and not d.time().isoformat().strip('0:.') else iso(d)
    return raw

xml_cells = 0
def verify_row(row, values, inherited=None):
    global xml_cells
    found = {}
    for fid, definition in definitions.items():
        f = fields[text(definition, 'Alias')]
        if f['id'] not in values: continue
        ea = next((x for x in row.findall('p:ExtendedAttribute', ns) if text(x, 'FieldID') == fid), None)
        raw = row.findtext('p:' + format(int(fid), 'x'), None, ns)
        if ea is not None: raw = text(ea, 'Value')
        found[f['id']] = parsed(f, raw) if raw is not None else (inherited or {}).get(f['id'])
    assert found == values, (text(row, 'Name'), found, values)
    xml_cells += sum(v is not None for v in found.values())

tasks = [r for r in xml.findall('p:Tasks/p:Task', ns) if text(r, 'Summary') != '1']
assert len(tasks) == 2
for row, t in zip(tasks, native['tasks']): verify_row(row, expected['TASK', t['id']])
for row in xml.findall('p:Tasks/p:Task', ns):
    if text(row, 'Summary') == '1':
        values = dict(summary='2026-11-13T09:00:00+09:00')
        if text(row, 'UID') == '0': values.update(expected['PROJECT', ''])
        verify_row(row, values)
for row, r in zip(xml.findall('p:Resources/p:Resource', ns), native['resources']): verify_row(row, expected['RSRC', r['id']])
for row, a in zip(xml.findall('p:Assignments/p:Assignment', ns), native['assignments']):
    definition = next(d for d in definitions.values() if text(d, 'Alias') == '오프셋 문자열')
    assert text(definition, 'AutoRollDown') == '1' and text(definitions[text(definition, 'SecondaryPID')], 'Alias') == '수신 오프셋 문자열'
    verify_row(row, expected['ASSIGNMENT', a['taskId']], dict(received=expected['TASK', a['taskId']]['wire']))

tables, name, columns = {}, None, []
for line in (root / 'timezone.xer').read_text(encoding='utf-8').splitlines():
    parts = line.split('\t')
    if parts[0] == '%T': name = parts[1]; tables[name] = []
    elif parts[0] == '%F': columns = parts[1:]
    elif parts[0] == '%R': tables[name].append(dict(zip(columns, parts[1:])))
types = {r['udf_type_id']: r for r in tables['UDFTYPE']}
xer_values = 0
for row in tables['UDFVALUE']:
    d = types[row['udf_type_id']]
    f = fields[d['udf_type_label']]
    scope = d['table_name']
    key = ('TASK', native['tasks'][int(row['fk_id'])-1]['id']) if scope == 'TASK' else ('ASSIGNMENT', native['tasks'][int(row['fk_id'])-1]['id']) if scope == 'TASKRSRC' else ('RSRC', 'R') if scope == 'RSRC' else ('PROJECT', '')
    value, raw = expected[key][f['id']], row['udf_number'] or row['udf_text'] or row['udf_date']
    if isinstance(value, bool): assert raw == ('예' if value else '아니요')
    elif isinstance(value, Decimal): assert Decimal(raw) == value
    else: assert parsed(f, raw) == value, (key, f, raw, value)
    xer_values += 1

import openpyxl
from pypdf import PdfReader
book = openpyxl.load_workbook(root / 'fields.xlsx', data_only=True)
sheet = next(s for s in book if s.title.startswith('작업 사용자 필드'))
rows = {(r[0], r[2]): r for r in sheet.values if r[0] in ('A','B')}
assert len(rows) == 32
for t in native['tasks']:
    for f in native['customFields']:
        value, actual = expected['TASK', t['id']][f['id']], rows[t['id'], 'field:' + f['id']][4]
        if value is None: assert actual == '미정'
        elif isinstance(value, bool): assert actual == ('예' if value else '아니요')
        elif isinstance(value, Decimal): assert Decimal(str(actual)) == value
        else: assert parsed(f, str(actual)) == value, (f, actual, value)
pdf = PdfReader(root / 'fields.pdf')
content = ''.join(''.join(p.extract_text() or '' for p in pdf.pages).split())
assert 'PPMTimeZone' in content and 'PPMParseUtc' in content and '2026f:Asia/Seoul' in content
archived = load('archived.json')
assert archived['currency'] == 'EUR' and archived['currencyConversions'] and archived['baselines'] and archived['scenarios']
print(json.dumps(dict(status='PASS', schema=53, functions=4, tzdb='2026f', zones=597,
                     transition_vectors=transition_rows, independent_monthly_samples=monthly,
                     scalar_cases=len(cases), xml_cells=xml_cells, formula_definitions=formula_definitions,
                     xer_static_values=xer_values, report_rows=len(rows), pdf_pages=len(pdf.pages),
                     external_product_run=False, native_file_sha256=hashlib.sha256((root / 'source.ppm').read_bytes()).hexdigest()),
                 ensure_ascii=False, indent=2))
