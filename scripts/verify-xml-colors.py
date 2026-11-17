"""Independent byte-packed color validation, plus XML/XER and reports."""
import argparse
from decimal import Decimal
import re
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument('root', type=Path)
root = parser.parse_args().root
load = lambda name: json.loads((root / name).read_text(encoding='utf-8-sig'))
vectors=load('vectors.json')
assert len(vectors)==5609
for row in vectors:
    if row['function']=='rgb':
        channels=[min(row[k],255) for k in ('red','green','blue')]
    else:
        i=row['index']
        level=255 if i>=8 else 128
        channels=[level if i&bit else 0 for bit in (4,2,1)]
        if i==7:channels=[192]*3
        if i==8:channels=[128]*3
    wanted=int.from_bytes(bytes(channels),'little')
    assert row['value']==wanted,(row,wanted)
native = load('native.json')
expected = {
    'Task': {'계산색상':['131330','131332'], '성분상한색상':['131583']*2,
             '팔레트색상':['65535']*2, '색상진수문자':['30201']*2,
             '금액성분색상':['12','2'], '색상요약':['16777215']*2,
             '색상혼합식':['262660','262664'], '색상오류':[None,None], '색상미설정':[None,None]},
    'Project': {'프로젝트색상':['16']}, 'Resource': {'자원색상':['2']},
    'Assignment': {'배정색상':['2','4'], '색상전파':['131330','131332']},
}

def canonical(value,name):
    if value is None:return None
    return Decimal(value) if name != '색상진수문자' else value


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
            if raw is None and name == '색상전파':
                source = next(d for d in definitions.values() if text(d, 'SecondaryPID') == fid)
                assert text(source, 'AutoRollDown') == '1' and text(source, 'Alias') == '계산색상'
                owner = next(t for t in rows['Task'] if text(t, 'UID') == text(row, 'TaskUID'))
                raw = next(text(a, 'Value') for a in owner.findall('p:ExtendedAttribute', ns)
                           if text(definitions[text(a, 'FieldID')], 'Alias') == '계산색상')
                inherited_cells += 1
            found.append(canonical(raw,name))
        assert found == [canonical(v,name) for v in wanted], (scope, name, found, wanted)
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
        actual = [canonical(r['udf_text'] or r['udf_number'],name) for r in tables['UDFVALUE']
                  if types[r['udf_type_id']]['table_name'] ==
                  {'Task': 'TASK', 'Resource': 'RSRC', 'Assignment': 'TASKRSRC', 'Project': 'PROJECT'}[scope]
                  and types[r['udf_type_id']]['udf_type_label'] == name]
        assert actual == [canonical(v,name) for v in wanted if v is not None], (name, actual, wanted)
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
        elif field['type'] == 'text':
            assert raw == wanted, (field, raw)
        else:
            assert Decimal(str(raw)) == Decimal(wanted), (field, raw)
pdf = PdfReader(root / 'fields.pdf')
statuses = {(r[1], r[3]): r[4] for r in book['계산식 미설정과 오류'].values if r[1] in ('A', 'B')}
assert all(statuses[t, 'field:colorError'] == '계산 오류' and
           statuses[t, 'field:colorNull'] == '미설정' for t in ('A', 'B'))
archive = load('archived.json')
assert archive['baselines'] and archive['scenarios'] and archive['currencyConversions']
assert archive['currency'] == 'EUR'
assert xml_cells == xer_cells == 20 and missing_cells == 4 and inherited_cells == 2
assert json.loads((root / 'source.ppm').read_text(encoding='utf-8'))['schemaVersion'] == 70
print(json.dumps(dict(status='PASS', schema=70, color_vectors=len(vectors), rgb_vectors=5593, palette_vectors=16,
                      scalar_cases=len(load('cases.json')), xml_cells=xml_cells, xml_missing_cells=missing_cells,
                      xml_inherited_cells=inherited_cells, xer_static_values=xer_cells,
                      formula_definitions=sum(bool(text(d, 'Formula')) for d in definitions.values()),
                      report_rows=len(report_rows), pdf_pages=len(pdf.pages),
                      native_file_sha256=hashlib.sha256((root / 'source.ppm').read_bytes()).hexdigest(),
                      external_product_run=False), ensure_ascii=False, indent=2))
