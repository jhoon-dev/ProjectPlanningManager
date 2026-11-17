"""Check frozen baseline cost vectors with independent rational arithmetic and exports."""
import argparse
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument('root', type=Path)
root = parser.parse_args().root
load = lambda name: json.loads((root / name).read_text(encoding='utf-8-sig'))
half_up = lambda numerator, denominator: (numerator * 2 + denominator) // (2 * denominator)
vectors = load('vectors.json')
assert len(vectors) == 128
for row in vectors:
    n = lambda name: int(row[name])
    r = half_up(n('r1') * 300, 6000)
    a = half_up(n('s1') * 231, 6000) + half_up(n('s2') * 33, 6000)
    b = half_up(n('s1') * 501, 6000)
    tasks = [r + a + n('fixedA'), b + n('fixedB'), n('fixedM'),
             half_up(n('material') * 1500, 1000) + n('expense')]
    for name, expected in [('tasks', tasks), ('resources', [r, a + b, 0]),
                           ('assignments', [r, a, b])]:
        assert list(map(int, row[name])) == expected, (name, row, expected)
    assert int(row['project']) == sum(tasks), row

native = load('native.json')
task_cost = ['2.89', '4.54', '3.07', '2.44', None]
expected = {
    'Task': {'BC': task_cost, 'BCN': task_cost,
             'DOUBLE': ['5.78', '9.08', '6.14', '4.88', None]},
    'Resource': {'BC': ['0.50', '3.89', '0.00', None]},
    'Assignment': {'BC': ['0.50', '1.38', '2.51', None],
                   'TC': ['2.89', '2.89', '4.54', None],
                   'RC': ['0.50', '3.89', '3.89', None],
                   'PC': ['12.94'] * 4,
                   '원가전파': ['2.89', '2.89', '4.54', None]},
    'Project': {'BC': ['12.94']},
}
ns = {'p': 'http://schemas.microsoft.com/project'}
xml = ET.parse(root / 'baseline.xml').getroot()
text = lambda row, name: row.findtext('p:' + name, default='', namespaces=ns)
definitions = {text(d, 'FieldID'): d for d in xml.findall('p:ExtendedAttributes/p:ExtendedAttribute', ns)}
mapping = load('mapping.json')['mapping']
field_ids = {(m['scope'], m['name']): m['externalId'] for m in mapping if m['kind'] == 'udf'}
rows = {
    'Task': [t for t in xml.findall('p:Tasks/p:Task', ns) if text(t, 'Summary') != '1'],
    'Project': [t for t in xml.findall('p:Tasks/p:Task', ns) if text(t, 'UID') == '0'],
    'Resource': xml.findall('p:Resources/p:Resource', ns),
    'Assignment': xml.findall('p:Assignments/p:Assignment', ns),
}
task_by_name = {text(t, 'Name'): t for t in rows['Task']}
rows['Task'] = [task_by_name[t['name']] for t in native['tasks']]
groups = {text(t, 'Name'): t for t in xml.findall('p:Tasks/p:Task', ns)
          if text(t, 'Summary') == '1' and text(t, 'UID') != '0'}
for name, amount in [('시공', '987'), ('이정표', '307')]:
    values = {text(definitions[text(a, 'FieldID')], 'Alias'): text(a, 'Value')
              for a in groups[name].findall('p:ExtendedAttribute', ns)}
    assert values['BC'] == amount
xml_cells = missing_cells = inherited_cells = 0
for scope, fields in expected.items():
    for name, wanted in fields.items():
        found = []
        fid = field_ids['RSRC' if scope == 'Resource' else scope.upper(), name]
        for row in rows[scope]:
            attrs = {text(definitions[text(a, 'FieldID')], 'Alias'): text(a, 'Value')
                     for a in row.findall('p:ExtendedAttribute', ns)}
            raw = attrs.get(name)
            if raw is None:
                raw = row.findtext('p:' + format(int(fid), 'x'), None, ns)
            if raw is None:
                # Assignment roll-down is carried by SecondaryPID, not duplicated values.
                if name == '원가전파':
                    target = next((fid for fid, d in definitions.items()
                                   if text(d, 'Alias') == name), None)
                    source = next((d for d in definitions.values()
                                   if text(d, 'SecondaryPID') == target), None)
                    assert source is not None and text(source, 'AutoRollDown') == '1'
                    owner = next(t for t in rows['Task'] if text(t, 'UID') == text(row, 'TaskUID'))
                    owner_attrs = {text(definitions[text(a, 'FieldID')], 'Alias'): text(a, 'Value')
                                   for a in owner.findall('p:ExtendedAttribute', ns)}
                    raw = owner_attrs.get('BC')
                    inherited_cells += raw is not None
            found.append(None if raw is None else Decimal(raw) / (1 if name in ('BCN', 'DOUBLE') else 100))
        assert found == [None if v is None else Decimal(v) for v in wanted], (scope, name, found, wanted)
        xml_cells += sum(v is not None for v in found)
        missing_cells += sum(v is None for v in found)

tables, table, columns = {}, None, []
for line in (root / 'baseline.xer').read_text(encoding='utf-8').splitlines():
    fields = line.split('\t')
    if fields[0] == '%T':
        table = fields[1]
        tables[table] = []
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
        assert actual == [Decimal(v) for v in wanted if v is not None], (scope, name, actual, wanted)
        xer_cells += len(actual)

import openpyxl
from pypdf import PdfReader
book = openpyxl.load_workbook(root / 'fields.xlsx', data_only=True)
sheet = next(s for s in book if s.title.startswith('작업 사용자 필드'))
report_rows = {(r[0], r[2]): r for r in sheet.values if r[0] in ('A', 'B', 'M', 'D', 'X')}
for name, wanted in expected['Task'].items():
    for task, value in zip(native['tasks'], wanted):
        raw = report_rows[task['id'], 'field:' + name][4]
        if value is None:
            assert raw == '미정', raw
        else:
            assert Decimal(str(raw)) == Decimal(value), (task['id'], name, raw)
pdf = PdfReader(root / 'fields.pdf')
content = ''.join(''.join(p.extract_text() or '' for p in pdf.pages).split())
assert 'BaselineCost' in content
archive = load('archived.json')
assert archive['baselines'] and archive['scenarios'] and archive['currencyConversions']
assert archive['currency'] == 'EUR' and native['currency'] == 'USD'
assert xml_cells == xer_cells == 32 and missing_cells == 8
print(json.dumps(dict(status='PASS', schema=load('source.ppm')['schemaVersion'], rational_vectors=len(vectors),
                      rational_values=len(vectors) * 11,
                      xml_cells=xml_cells, xml_missing_cells=missing_cells,
                      xml_inherited_cells=inherited_cells, xer_static_values=xer_cells,
                      formula_definitions=sum(bool(text(d, 'Formula')) for d in definitions.values()),
                      report_rows=len(report_rows), pdf_pages=len(pdf.pages),
                      native_file_sha256=hashlib.sha256((root / 'source.ppm').read_bytes()).hexdigest(),
                      external_product_run=False), ensure_ascii=False, indent=2))
