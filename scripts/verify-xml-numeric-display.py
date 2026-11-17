"""Independent exact-decimal numeric display validation, plus XML/XER and reports."""
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
from decimal import localcontext, ROUND_HALF_UP
vectors=load('vectors.json')
assert len(vectors)==16200 and sum(r['windowsComparable'] for r in vectors)==7128
for row in vectors:
    with localcontext() as context:
        context.prec=50
        n=Decimal(row['input'])/Decimal(1000000)
        precision=2 if row['digits']==-1 else row['digits']
        flag=lambda key,default: default if row[key]==-2 else row[key]==-1
        value=abs(n)*(100 if row['kind']=='percent' else 1)
        rounded=value.quantize(Decimal(1).scaleb(-precision),rounding=ROUND_HALF_UP)
        whole,_,fraction=f'{rounded:.{precision}f}'.partition('.')
        if whole=='0' and not flag('leading',True):whole=''
        if whole and flag('grouping',True):whole=format(int(whole),',')
        wanted=('$' if row['kind']=='currency' else '')+whole+('.'+fraction if precision else '')+('%' if row['kind']=='percent' else '')
        if n<0:wanted='('+wanted+')' if flag('parentheses',row['kind']=='currency') else '-'+wanted
        assert row['value']==wanted,row
native = load('native.json')
expected = {
    'Task': {'숫자문자표시':['2.50','3.50'], '백분율문자표시':['2.5%','3.5%'], '통화문자표시':['$12.34','$2.00'],
             '숫자표시길이':['4','4'], '선행영생략':['(.50)']*2, '숫자표시요약':['($1,234.50)']*2,
             '숫자표시혼합식':['8']*2, '숫자표시오류':[None,None], '숫자표시미설정':[None,None]},
    'Project': {'프로젝트숫자표시':['16.00']}, 'Resource': {'자원백분율표시':['20%']},
    'Assignment': {'배정숫자표시':['3','4'], '숫자표시전파':['2.50','3.50']},
}
def canonical(value,name):
    if value is None:return None
    return Decimal(value) if name in ('숫자표시길이','숫자표시혼합식') else value


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
            if raw is None and name == '숫자표시전파':
                source = next(d for d in definitions.values() if text(d, 'SecondaryPID') == fid)
                assert text(source, 'AutoRollDown') == '1' and text(source, 'Alias') == '숫자문자표시'
                owner = next(t for t in rows['Task'] if text(t, 'UID') == text(row, 'TaskUID'))
                raw = next(text(a, 'Value') for a in owner.findall('p:ExtendedAttribute', ns)
                           if text(definitions[text(a, 'FieldID')], 'Alias') == '숫자문자표시')
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
assert all(statuses[t, 'field:numericError'] == '계산 오류' and
           statuses[t, 'field:numericNull'] == '미설정' for t in ('A', 'B'))
archive = load('archived.json')
assert archive['baselines'] and archive['scenarios'] and archive['currencyConversions']
assert archive['currency'] == 'EUR'
assert xml_cells == xer_cells == 20 and missing_cells == 4 and inherited_cells == 2
assert json.loads((root / 'source.ppm').read_text(encoding='utf-8'))['schemaVersion'] == 69
print(json.dumps(dict(status='PASS', schema=69, numeric_vectors=len(vectors),
                      scalar_cases=len(load('cases.json')), xml_cells=xml_cells, xml_missing_cells=missing_cells,
                      xml_inherited_cells=inherited_cells, xer_static_values=xer_cells,
                      formula_definitions=sum(bool(text(d, 'Formula')) for d in definitions.values()),
                      report_rows=len(report_rows), pdf_pages=len(pdf.pages),
                      native_file_sha256=hashlib.sha256((root / 'source.ppm').read_bytes()).hexdigest(),
                      external_product_run=False), ensure_ascii=False, indent=2))
