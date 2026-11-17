"""Independent Python regex matching of valid BMP patterns, plus exports and reports."""
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
def regex(pattern):
    out = []; i = 0
    while i < len(pattern):
        c = pattern[i]; i += 1
        if c == '*': out.append('.*')
        elif c == '?': out.append('.')
        elif c == '#': out.append('[0-9]')
        elif c != '[': out.append(re.escape(c))
        else:
            end = pattern.index(']', i); content = pattern[i:end]; i = end + 1
            if not content: continue
            negative = content.startswith('!') and len(content) > 1
            if negative: content = content[1:]
            ranges = []; j = 0
            while j < len(content):
                first = content[j]; j += 1
                if j + 1 < len(content) and content[j] == '-':
                    last = content[j + 1]; j += 2
                    assert ord(first) <= ord(last)
                    ranges.append(re.escape(first) + '-' + re.escape(last))
                else: ranges.append(re.escape(first))
            out.append('[' + ('^' if negative else '') + ''.join(ranges) + ']')
    return ''.join(out)
vectors = load('vectors.json')
assert len(vectors) == 8932
for row in vectors:
    assert row['matched'] == (re.fullmatch(regex(row['pattern']), row['text'], re.DOTALL) is not None), row
native = load('native.json')
expected = {
    'Task': {'문자패턴': ['1', '0'], '숫자패턴': ['1', '1'],
             '금액패턴': ['1', '0'], '패턴숫자': ['1', '0'],
             '리터럴패턴': ['1', '1'], '패턴요약': ['1', '1'], '패턴혼합식': ['2', '0'],
             '패턴오류': [None, None], '패턴미설정': [None, None]},
    'Project': {'프로젝트패턴': ['1']}, 'Resource': {'자원패턴': ['1']},
    'Assignment': {'배정패턴': ['1', '0'], '패턴전파': ['1', '0']},
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
            if raw is None and name == '패턴전파':
                source = next(d for d in definitions.values() if text(d, 'SecondaryPID') == fid)
                assert text(source, 'AutoRollDown') == '1' and text(source, 'Alias') == '문자패턴'
                owner = next(t for t in rows['Task'] if text(t, 'UID') == text(row, 'TaskUID'))
                raw = next(text(a, 'Value') for a in owner.findall('p:ExtendedAttribute', ns)
                           if text(definitions[text(a, 'FieldID')], 'Alias') == '문자패턴')
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
        actual = [Decimal({'예': '1', '아니요': '0'}.get(r['udf_text'], r['udf_text'] or r['udf_number'])) for r in tables['UDFVALUE']
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
        elif field['type'] == 'flag':
            assert raw == ('예' if wanted == '1' else '아니요'), (field, raw)
        else:
            assert Decimal(str(raw)) == Decimal(wanted), (field, raw)
pdf = PdfReader(root / 'fields.pdf')
statuses = {(r[1], r[3]): r[4] for r in book['계산식 미설정과 오류'].values if r[1] in ('A', 'B')}
assert all(statuses[t, 'field:likeError'] == '계산 오류' and
           statuses[t, 'field:likeNull'] == '미설정' for t in ('A', 'B'))
archive = load('archived.json')
assert archive['baselines'] and archive['scenarios'] and archive['currencyConversions']
assert archive['currency'] == 'EUR'
assert xml_cells == xer_cells == 20 and missing_cells == 4 and inherited_cells == 2
assert json.loads((root / 'source.ppm').read_text(encoding='utf-8'))['schemaVersion'] == 65
print(json.dumps(dict(status='PASS', schema=65, pattern_vectors=len(vectors),
                      scalar_cases=len(load('cases.json')), xml_cells=xml_cells, xml_missing_cells=missing_cells,
                      xml_inherited_cells=inherited_cells, xer_static_values=xer_cells,
                      formula_definitions=sum(bool(text(d, 'Formula')) for d in definitions.values()),
                      report_rows=len(report_rows), pdf_pages=len(pdf.pages),
                      native_file_sha256=hashlib.sha256((root / 'source.ppm').read_bytes()).hexdigest(),
                      external_product_run=False), ensure_ascii=False, indent=2))
