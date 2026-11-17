"""Independent Decimal/Unicode and XML/XER/native/report verification for additional functions."""
import argparse
from datetime import datetime
from decimal import Decimal, ROUND_HALF_EVEN, ROUND_HALF_UP, localcontext
import hashlib
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument('root', type=Path)
root = parser.parse_args().root
load = lambda n: json.loads((root / n).read_text(encoding='utf-8-sig'))
native = load('native.json')
envelope = load('source.ppm')
assert envelope['schemaVersion'] == 52 and envelope['payload'] == native
attrs = lambda row: {a['key']: a['value'] for a in row}
assert attrs(native['tasks'][0]['attributes'])['field:byte'] == '99'
assert attrs(native['tasks'][0]['attributes'])['field:eager'] == '777'
expected = {}
for t in native['tasks']:
    a = attrs(t['attributes'])
    q, amount, label = Decimal(a['field:q']), Decimal(a['field:amount']), a['field:label']
    byte = q.quantize(Decimal(1), rounding=ROUND_HALF_EVEN)
    cc = (amount / 3).quantize(Decimal('.000001'), rounding=ROUND_HALF_UP).quantize(Decimal('.0001'), rounding=ROUND_HALF_EVEN)
    values = dict(label=label, q=q, amount=amount, when=a['field:when'], missing=None,
                  choose=['one', 'two', 'three', 'four'][int(byte) - 1],
                  find=Decimal(label.find('가') + 1), compare=Decimal(0), lefttrim=label.lstrip(' '),
                  righttrim=label.rstrip(' '), space='x  y', repeat='가' * 3, asc=Decimal(ord('A')),
                  chr='A!', byte=byte, currency=cc.quantize(Decimal('.01'), rounding=ROUND_HALF_UP),
                  date=datetime.fromisoformat(a['field:when']).date().isoformat(),
                  sqrt=Decimal(2).sqrt().quantize(Decimal('.000001'), rounding=ROUND_HALF_UP),
                  duration=Decimal(60), flag='가' in label, eager=None, null=None, native=byte * 3, summary='말단')
    expected['TASK', t['id']] = values
expected['PROJECT', ''] = dict(input=Decimal(16), project=Decimal(4))
expected['RSRC', 'R'] = dict(resource=Decimal(0))
for a in native['assignments']:
    expected['ASSIGNMENT', a['taskId']] = dict(received=expected['TASK', a['taskId']]['byte'],
                                              assignment=Decimal(2) + expected['TASK', a['taskId']]['byte'])
roots = load('roots.json')
assert len(roots) == 512
with localcontext() as ctx:
    ctx.prec = 80
    for case in roots:
        result = Decimal(case['input']).sqrt().quantize(Decimal('.000001'), rounding=ROUND_HALF_UP)
        assert Decimal(case['value']) == result, case
cases = load('cases.json')
assert len(cases) == 22
assert any(c['source'] == 'Sqr(9000000000000)' and c['value'] == '3000000' for c in cases)

def oracle(source):
    """Small independent parser for the saved numeric call vectors; no Python eval."""
    tokens = re.findall(r'"(?:[^"]|"")*"|[A-Za-z]+|[+-]?\d+(?:\.\d+)?|[(),]', source)
    at = 0
    def read():
        nonlocal at
        token = tokens[at]
        at += 1
        if token.startswith('"'): return token[1:-1].replace('""', '"')
        if token == 'Null': return None
        if re.fullmatch(r'[+-]?\d+(?:\.\d+)?', token): return Decimal(token)
        assert tokens[at] == '('
        at += 1
        args = [read()]
        while tokens[at] == ',':
            at += 1
            args.append(read())
        assert tokens[at] == ')'
        at += 1
        fn = token.lower()
        if fn == 'choose':
            index = int(args[0].quantize(Decimal(1), rounding=ROUND_HALF_EVEN))
            return args[index] if 1 <= index < len(args) else None
        if fn == 'instr':
            start, a, b, compare = (1, *args, 0) if len(args) == 2 else (int(args[0]), *args[1:], 0) if len(args) == 3 else args
            start = int(start)
            if compare: a, b = a.lower(), b.lower()
            return Decimal(0 if not a or start > len(a) else a.find(b, start - 1) + 1)
        if fn == 'strcomp':
            a, b = args[:2]
            if len(args) == 3 and args[2]: a, b = a.lower(), b.lower()
            return Decimal((a > b) - (a < b))
        if fn == 'cbyte': return Decimal(args[0]).quantize(Decimal(1), rounding=ROUND_HALF_EVEN)
        if fn == 'ccur': return Decimal(args[0]).quantize(Decimal('.0001'), rounding=ROUND_HALF_EVEN)
        if fn == 'asc': return Decimal(ord(args[0][0]))
        if fn == 'chr': return chr(int(args[0]))
        assert fn == 'sqr', fn
        with localcontext() as ctx:
            ctx.prec = 80
            return args[0].sqrt().quantize(Decimal('.000001'), rounding=ROUND_HALF_UP)
    result = read()
    assert at == len(tokens)
    return result

for case in cases:
    assert Decimal(case['value']) == oracle(case['source']), case

ns = {'p': 'http://schemas.microsoft.com/project'}
text = lambda n, k: n.findtext('p:' + k, '', ns)
xml = ET.parse(root / 'functions.xml').getroot()
definitions = {text(d, 'FieldID'): d for d in xml.findall('p:ExtendedAttributes/p:ExtendedAttribute', ns)}
fields = {f['name']: f for group in ('customFields', 'projectFields', 'resourceFields', 'assignmentFields') for f in native[group]}
assert len(definitions) == len(fields) == 29
formula_definitions = 0
for d in definitions.values():
    f = fields[text(d, 'Alias')]
    if f.get('xmlFormula'):
        assert text(d, 'Formula') and '[field:' not in text(d, 'Formula')
        formula_definitions += 1

def parse(f, raw):
    kind = f['type']
    if kind == 'cost':
        return Decimal(raw) / 100
    if kind == 'number':
        return Decimal(raw)
    if kind == 'flag':
        assert raw in ('0', '1')
        return raw == '1'
    if kind == 'duration':
        m = re.fullmatch(r'PT(?:(\d+(?:\.\d+)?)H)?(?:(\d+(?:\.\d+)?)M)?(?:(\d+(?:\.\d+)?)S)?', raw)
        assert m, raw
        h, n, s = [Decimal(v or '0') for v in m.groups()]
        return h * 60 + n + s / 60
    if kind == 'date':
        stamp = datetime.fromisoformat(raw)
        return stamp.date().isoformat() if not stamp.time().isoformat().strip('0:.') else stamp.isoformat()
    return raw

xml_cells = 0
def verify_row(row, values, inherited=None):
    global xml_cells
    found = {}
    for fid, d in definitions.items():
        f = fields[text(d, 'Alias')]
        if f['id'] not in values:
            continue
        ea = next((v for v in row.findall('p:ExtendedAttribute', ns) if text(v, 'FieldID') == fid), None)
        raw = row.findtext('p:' + format(int(fid), 'x'), None, ns)
        if ea is not None:
            raw = text(ea, 'Value')
        found[f['id']] = (inherited or {}).get(f['id']) if raw is None else parse(f, raw)
    assert found == values, (text(row, 'Name'), found, values)
    xml_cells += sum(v is not None for v in found.values())

tasks = [r for r in xml.findall('p:Tasks/p:Task', ns) if text(r, 'Summary') != '1']
assert len(tasks) == 2
for r, t in zip(tasks, native['tasks']):
    verify_row(r, expected['TASK', t['id']])
for r in xml.findall('p:Tasks/p:Task', ns):
    if text(r, 'Summary') == '1':
        values = dict(byte=Decimal(6), summary='요약')
        if text(r, 'UID') == '0':
            values.update(expected['PROJECT', ''])
        verify_row(r, values)
for r, original in zip(xml.findall('p:Resources/p:Resource', ns), native['resources']):
    verify_row(r, expected['RSRC', original['id']])
for r, original in zip(xml.findall('p:Assignments/p:Assignment', ns), native['assignments']):
    source_def = next(d for d in definitions.values() if text(d, 'Alias') == '바이트변환')
    assert text(source_def, 'AutoRollDown') == '1'
    assert text(definitions[text(source_def, 'SecondaryPID')], 'Alias') == '수신변환'
    verify_row(r, expected['ASSIGNMENT', original['taskId']],
               dict(received=expected['TASK', original['taskId']]['byte']))

tables, name, columns = {}, None, []
for line in (root / 'functions.xer').read_text(encoding='utf-8').splitlines():
    parts = line.split('\t')
    if parts[0] == '%T': name = parts[1]; tables[name] = []
    elif parts[0] == '%F': columns = parts[1:]
    elif parts[0] == '%R': tables[name].append(dict(zip(columns, parts[1:])))
types = {r['udf_type_id']: r for r in tables['UDFTYPE']}
xer_values = 0
for row in tables['UDFVALUE']:
    d = types[row['udf_type_id']]
    scope = d['table_name']
    f = fields[d['udf_type_label']]
    if scope == 'TASK': key = ('TASK', native['tasks'][int(row['fk_id']) - 1]['id'])
    elif scope == 'TASKRSRC': key = ('ASSIGNMENT', native['tasks'][int(row['fk_id']) - 1]['id'])
    elif scope == 'RSRC': key = ('RSRC', native['resources'][int(row['fk_id']) - 1]['id'])
    else: key = ('PROJECT', '')
    value = expected[key][f['id']]
    raw = row['udf_number'] or row['udf_text'] or row['udf_date']
    if isinstance(value, bool): assert raw == ('예' if value else '아니요')
    elif f['type'] == 'duration': assert Decimal(raw.split()[0]) == value
    elif isinstance(value, Decimal): assert Decimal(raw) == value
    elif f['type'] == 'date' and len(value) == 10: assert datetime.fromisoformat(raw).date().isoformat() == value
    else: assert raw == value, (key, f, raw, value)
    xer_values += 1

import openpyxl
from pypdf import PdfReader
book = openpyxl.load_workbook(root / 'fields.xlsx', data_only=True)
sheet = next(s for s in book if s.title.startswith('작업 사용자 필드'))
rows = {(r[0], r[2]): r for r in sheet.values if r[0] in ('A', 'B')}
assert len(rows) == 48
for t in native['tasks']:
    for f in native['customFields']:
        value = expected['TASK', t['id']][f['id']]
        actual = rows[t['id'], 'field:' + f['id']][4]
        if value is None: assert actual == '미정'
        elif isinstance(value, bool): assert actual == ('예' if value else '아니요')
        elif f['type'] == 'duration': assert Decimal(str(actual).split()[0]) == value
        elif isinstance(value, Decimal): assert abs(Decimal(str(actual)) - value) < Decimal('.0000001')
        elif f['type'] == 'date': assert str(actual).startswith(value)
        else: assert actual == value, (f, actual, value)
pdf = PdfReader(root / 'fields.pdf')
content = ''.join(''.join(p.extract_text() or '' for p in pdf.pages).split())
assert 'Sqr(2)' in content and 'DateValue' in content and 'Choose' in content
archived = load('archived.json')
assert archived['currency'] == 'EUR' and archived['currencyConversions'] and archived['baselines'] and archived['scenarios']
assert attrs(archived['tasks'][0]['attributes'])['field:amount'] == '6.17'
print(json.dumps(dict(status='PASS', schema=52, functions=13, root_vectors=len(roots), scalar_cases=len(cases),
                     xml_cells=xml_cells, formula_definitions=formula_definitions, xer_static_values=xer_values,
                     report_rows=len(rows), pdf_pages=len(pdf.pages), external_product_run=False,
                     native_file_sha256=hashlib.sha256((root / 'source.ppm').read_bytes()).hexdigest()),
                 ensure_ascii=False, indent=2))
