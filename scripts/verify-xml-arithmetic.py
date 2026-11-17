"""Independent exact integer division and 80-digit power checks, plus export/report reads."""
import argparse
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET
import mpmath as mp

parser = argparse.ArgumentParser()
parser.add_argument('root', type=Path)
root = parser.parse_args().root
load = lambda name: json.loads((root / name).read_text(encoding='utf-8-sig'))
mp.mp.dps = 80
scale = 1000000
def integer(n):
    q, r = divmod(abs(n), scale)
    q += r > scale // 2 or (r == scale // 2 and q % 2 != 0)
    return -q if n < 0 else q

exact_powers = powers = divisions = maximum_error = 0
for row in load('vectors.json'):
    a, b, actual = (int(row[k]) for k in ('left', 'right', 'output'))
    if row['op'] == '^':
        value = mp.power(mp.mpf(a) / scale, mp.mpf(b) / scale)
        expected = int(mp.floor(abs(value) * scale + mp.mpf('0.5'))) * (-1 if value < 0 else 1)
        error = abs(actual - expected)
        assert error <= 1, (row, expected)
        exact_powers += error == 0
        maximum_error = max(maximum_error, error)
        powers += 1
    else:
        a, b = integer(a), integer(b)
        expected = (abs(a) // abs(b)) * (-1 if (a < 0) != (b < 0) else 1) * scale
        assert actual == expected, (row, expected)
        divisions += 1
assert powers == divisions == 2048
native = load('native.json')
expected = {
    'Task': {'거듭제곱': ['15.625', '42.875'], '정수나눗셈': ['1', '2'],
             '금액제곱': ['152.2756', '4'], '부호우선순위': ['-4', '-4'],
             '역수': ['0.125', '0.125'], '산술요약': ['64', '64'], '산술혼합식': ['31.25', '85.75'],
             '산술오류': [None, None], '산술미설정': [None, None]},
    'Project': {'프로젝트제곱근': ['4']}, 'Resource': {'자원정수몫': ['4']},
    'Assignment': {'배정제곱': ['6.25', '12.25'], '산술전파': ['15.625', '42.875']},
}
ns = {'p': 'http://schemas.microsoft.com/project'}
xml = ET.parse(root / 'operators.xml').getroot()
text = lambda r, n: r.findtext('p:' + n, default='', namespaces=ns)
definitions = {text(d, 'FieldID'): d for d in xml.findall('p:ExtendedAttributes/p:ExtendedAttribute', ns)}
mapping = load('mapping.json')['mapping']
field_ids = {(m['scope'], m['name']): m['externalId'] for m in mapping if m['kind'] == 'udf'}
rows = {'Task': [t for t in xml.findall('p:Tasks/p:Task', ns) if text(t, 'Summary') != '1'],
        'Project': [t for t in xml.findall('p:Tasks/p:Task', ns) if text(t, 'UID') == '0'],
        'Resource': xml.findall('p:Resources/p:Resource', ns),
        'Assignment': xml.findall('p:Assignments/p:Assignment', ns)}
xml_cells = missing_cells = inherited_cells = 0
for scope, fields in expected.items():
    for name, wanted in fields.items():
        found = []
        fid = field_ids['RSRC' if scope == 'Resource' else scope.upper(), name]
        for row in rows[scope]:
            attrs = {text(definitions[text(a, 'FieldID')], 'Alias'): text(a, 'Value')
                     for a in row.findall('p:ExtendedAttribute', ns)}
            raw = attrs.get(name, row.findtext('p:' + format(int(fid), 'x'), None, ns))
            if raw is None and name == '산술전파':
                source = next(d for d in definitions.values() if text(d, 'SecondaryPID') == fid)
                assert text(source, 'AutoRollDown') == '1' and text(source, 'Alias') == '거듭제곱'
                owner = next(t for t in rows['Task'] if text(t, 'UID') == text(row, 'TaskUID'))
                raw = next(text(a, 'Value') for a in owner.findall('p:ExtendedAttribute', ns)
                           if text(definitions[text(a, 'FieldID')], 'Alias') == '거듭제곱')
                inherited_cells += 1
            found.append(None if raw is None else Decimal(raw))
        assert found == [None if v is None else Decimal(v) for v in wanted], (scope, name, found, wanted)
        xml_cells += sum(v is not None for v in found)
        missing_cells += sum(v is None for v in found)
tables, table, columns = {}, None, []
for line in (root / 'operators.xer').read_text(encoding='utf-8').splitlines():
    fields = line.split('\t')
    if fields[0] == '%T':
        table = fields[1]; tables[table] = []
    elif fields[0] == '%F':
        columns = fields[1:]
    elif fields[0] == '%R':
        tables[table].append(dict(zip(columns, fields[1:])))
types = {r['udf_type_id']: r for r in tables['UDFTYPE']}
xer_cells = 0
for scope, fields in expected.items():
    for name, wanted in fields.items():
        actual = [Decimal(r['udf_text'] or r['udf_number']) for r in tables['UDFVALUE']
                  if types[r['udf_type_id']]['table_name'] ==
                  {'Task': 'TASK', 'Resource': 'RSRC', 'Assignment': 'TASKRSRC', 'Project': 'PROJECT'}[scope]
                  and types[r['udf_type_id']]['udf_type_label'] == name]
        assert actual == [Decimal(v) for v in wanted if v is not None], (name, actual, wanted)
        xer_cells += len(actual)
import openpyxl
from pypdf import PdfReader
book = openpyxl.load_workbook(root / 'fields.xlsx', data_only=True)
sheet = next(s for s in book if s.title.startswith('작업 사용자 필드'))
report_rows = {(r[0], r[2]): r for r in sheet.values if r[0] in ('A', 'B')}
for field in native['customFields']:
    if field['name'] not in expected['Task']:
        continue
    for i, task in enumerate(native['tasks']):
        raw = report_rows[task['id'], 'field:' + field['id']][4]
        wanted = expected['Task'][field['name']][i]
        if wanted is None:
            assert raw == '미정', raw
        else:
            assert Decimal(str(raw)) == Decimal(wanted), (field, raw)
pdf = PdfReader(root / 'fields.pdf')
statuses = {(r[1], r[3]): r[4] for r in book['계산식 미설정과 오류'].values if r[1] in ('A', 'B')}
assert all(statuses[t, 'field:arithError'] == '계산 오류' and
           statuses[t, 'field:arithNull'] == '미설정' for t in ('A', 'B'))
archive = load('archived.json')
assert archive['baselines'] and archive['scenarios'] and archive['currencyConversions']
assert archive['currency'] == 'EUR'
assert xml_cells == xer_cells == 20 and missing_cells == 4 and inherited_cells == 2
assert json.loads((root / 'source.ppm').read_text(encoding='utf-8'))['schemaVersion'] == 63
print(json.dumps(dict(status='PASS', schema=63, power_vectors=powers, exact_power_values=exact_powers,
                      power_maximum_difference_scaled=maximum_error, division_vectors=divisions,
                      scalar_cases=len(load('cases.json')), xml_cells=xml_cells, xml_missing_cells=missing_cells,
                      xml_inherited_cells=inherited_cells, xer_static_values=xer_cells,
                      formula_definitions=sum(bool(text(d, 'Formula')) for d in definitions.values()),
                      report_rows=len(report_rows), pdf_pages=len(pdf.pages),
                      native_file_sha256=hashlib.sha256((root / 'source.ppm').read_bytes()).hexdigest(),
                      external_product_run=False), ensure_ascii=False, indent=2))
