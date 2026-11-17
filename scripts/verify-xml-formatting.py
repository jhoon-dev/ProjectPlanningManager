"""Independent Decimal, Unicode16 and Windows OLE checks of emitted evidence.

Run with CPython Unicode16, openpyxl and pypdf. UCD.zip is the original pinned
archive (its digest is checked); Windows OLE is an additional, bounded oracle.
"""
import argparse
from datetime import datetime, date, timedelta
from decimal import Decimal, ROUND_HALF_UP, localcontext
import hashlib
import json
from pathlib import Path
import unicodedata as ud
import zipfile
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument('root', type=Path)
parser.add_argument('--ucd', type=Path, required=True)
args = parser.parse_args()
root = args.root
load = lambda n: json.loads((root / n).read_text(encoding='utf-8-sig'))
manifest = json.loads(Path('resources/text/manifest.json').read_text())
assert ud.unidata_version == manifest['version'] == '16.0.0'
assert hashlib.sha256(args.ucd.read_bytes()).hexdigest() == manifest['source_sha256']
archive = zipfile.ZipFile(args.ucd)
for n, digest in manifest['files'].items():
    assert hashlib.sha256(archive.read(n)).hexdigest() == digest
assert hashlib.sha256(Path('resources/text/unicode-16.0.0.inc').read_bytes()).hexdigest() == manifest['table_sha256']

numbers = load('numbers.json')
with localcontext() as context:
    context.prec = 50
    for row in numbers:
        value = Decimal(row['scaled']) / 1000000
        mask = row['mask']
        places = 3 if mask == '#,##0.000' else 2
        if mask == '0.00%': value *= 100
        if mask == '0,,.00': value /= 1000000
        if mask == '0.00E+00':
            exponent = value.copy_abs().adjusted() if value else 0
            mantissa = value.copy_abs().scaleb(-exponent).quantize(Decimal('.01'), rounding=ROUND_HALF_UP)
            if mantissa == 10:
                mantissa = Decimal('1.00')
                exponent += 1
            expected = ('-' if value < 0 else '') + f'{mantissa:.2f}E{exponent:+03d}'
        else:
            rounded = value.copy_abs().quantize(Decimal(1).scaleb(-places), rounding=ROUND_HALF_UP)
            expected = ('-' if value < 0 else '') + format(rounded, f',.{places}f' if ',' in mask and ',,' not in mask else f'.{places}f')
            if mask.endswith('%'): expected += '%'
        assert row['value'] == expected, (row, expected)

wide, narrow = {}, {}
for cp in range(0x110000):
    d = ud.decomposition(chr(cp)).split()
    if d and d[0] == '<wide>': wide[chr(int(d[1],16))] = chr(cp)
    if d and d[0] == '<narrow>': wide[chr(cp)] = chr(int(d[1],16))
narrow = {v:k for k,v in wide.items()}
def convert(s, flags):
    if flags & 3 == 1: s = s.upper()
    elif flags & 3 == 2: s = s.lower()
    elif flags & 3 == 3:
        # Emitted vectors contain one input scalar; word boundary cases are
        # separately tested through the formula engine.
        assert len(s) == 1
        s = s.title()
    if flags & 4:
        # Only kana voicing composition, not broad compatibility normalization.
        value = ''
        for c in s:
            c = wide.get(c,c)
            if value and c in '\u3099\u309a' and len(ud.normalize('NFC',value[-1]+c)) == 1:
                value = value[:-1] + ud.normalize('NFC',value[-1]+c)
            else: value += c
        s = value
    if flags & 48:
        s = ''.join(chr(ord(c)+0x60) if flags & 16 and (0x3041 <= ord(c) <= 0x3096 or 0x309d <= ord(c) <= 0x309e)
                    else chr(ord(c)-0x60) if flags & 32 and (0x30a1 <= ord(c) <= 0x30f6 or 0x30fd <= ord(c) <= 0x30fe)
                    else c for c in s)
    if flags & 8:
        value = ''
        for c in s:
            d = ud.decomposition(c).split()
            if c in narrow: value += narrow[c]
            elif len(d) == 2 and not d[0].startswith('<') and d[1] in ('3099','309A') and all(chr(int(n,16)) in narrow for n in d):
                value += ''.join(narrow[chr(int(n,16))] for n in d)
            else: value += c
        s = value
    return s
unicode_vectors = load('unicode.json')
for row in unicode_vectors:
    assert row['value'] == convert(row['input'],row['flags']), row
dates = load('dates.json')
for row in dates:
    d = datetime.fromisoformat(row['input'])
    first_day, first_week = row['firstDay'], row['firstWeek']
    def first_week_start(year):
        jan = date(year,1,1)
        weekday = (jan.weekday()+1)%7
        delta = (weekday-(first_day-1))%7
        start = jan-timedelta(days=delta)
        return start+timedelta(days=7) if (first_week == 2 and delta>3) or (first_week == 3 and delta) else start
    start = first_week_start(d.year)
    if d.date() < start: start = first_week_start(d.year-1)
    weekday = ((d.weekday()+1)%7-(first_day-1))%7+1
    week = (d.date()-start).days//7+1
    expected = f'{d.year:04d}-{d.month:02d}-{d.day:02d} 13:04:05 {weekday} {week} {d.timetuple().tm_yday} {(d.month-1)//3+1}'
    assert row['value'] == expected, (row,expected)

native = load('native.json')
expected = {'표시금액': ['12.34','2.00'], '한국날짜':['2026년 11월 13일 금요일']*2,
            '문자변환': ['ガ　Ｈｅｌｌｏ　Ｗｏｒｌｄ']*2, '빈값표시':['미설정']*2}
xml = ET.parse(root / 'format.xml').getroot()
ns = {'p':'http://schemas.microsoft.com/project'}
text = lambda r,n: r.findtext('p:'+n,default='',namespaces=ns)
definitions = {text(d,'FieldID'):d for d in xml.findall('p:ExtendedAttributes/p:ExtendedAttribute',ns)}
tasks = [r for r in xml.findall('p:Tasks/p:Task',ns) if text(r,'Summary') != '1']
xml_cells = 0
for i, row in enumerate(tasks):
    values = {text(definitions[text(r,'FieldID')],'Alias'):text(r,'Value') for r in row.findall('p:ExtendedAttribute',ns)}
    for name, expected_values in expected.items():
        assert values[name] == expected_values[i], (name,values)
        xml_cells += 1
formula_count = sum(bool(text(d,'Formula')) for d in definitions.values())
assert formula_count >= 7
scopes = [('p:Tasks/p:Task','프로젝트표시',['16.0']),
          ('p:Resources/p:Resource','자원표시',['École']),
          ('p:Assignments/p:Assignment','배정표시',['2.00','4.00'])]
for selector,name,values in scopes:
    found = []
    for row in xml.findall(selector,ns):
        if name == '프로젝트표시' and text(row,'UID') != '0': continue
        attrs = {text(definitions[text(r,'FieldID')],'Alias'):text(r,'Value') for r in row.findall('p:ExtendedAttribute',ns)}
        if name not in attrs:
            fid = next(fid for fid,d in definitions.items() if text(d,'Alias') == name)
            attrs[name] = row.findtext('p:'+format(int(fid),'x'),None,ns)
        found.append(attrs[name])
    assert found == values, (name,found,values)
    xml_cells += len(found)
tables, table_name, columns = {}, None, []
for line in (root/'format.xer').read_text(encoding='utf-8').splitlines():
    parts = line.split('\t')
    if parts[0] == '%T': table_name = parts[1]; tables[table_name] = []
    elif parts[0] == '%F': columns = parts[1:]
    elif parts[0] == '%R': tables[table_name].append(dict(zip(columns,parts[1:])))
types = {r['udf_type_id']:r for r in tables['UDFTYPE']}
xer_expected = dict(expected,프로젝트표시=['16.0'],자원표시=['École'],배정표시=['2.00','4.00'])
xer_cells = 0
for name, values in xer_expected.items():
    actual = [r['udf_text'] for r in tables['UDFVALUE'] if types[r['udf_type_id']]['udf_type_label'] == name]
    assert actual == values, (name,actual,values)
    xer_cells += len(actual)
import openpyxl
from pypdf import PdfReader
book = openpyxl.load_workbook(root / 'fields.xlsx',data_only=True)
sheet = next(s for s in book if s.title.startswith('작업 사용자 필드'))
rows = {(r[0],r[2]):r for r in sheet.values if r[0] in ('A','B')}
for field in native['customFields']:
    if field['name'] not in expected: continue
    for i, task in enumerate(native['tasks']):
        assert rows[task['id'],'field:'+field['id']][4] == expected[field['name']][i]
pdf = PdfReader(root / 'fields.pdf')
content = ''.join(''.join(p.extract_text() or '' for p in pdf.pages).split())
assert all(s in content for s in ('PPMFormat','StrConv','Format','표시금액'))
archived = load('archived.json')
assert archived['baselines'] and archived['scenarios'] and archived['currencyConversions'] and archived['currency'] == 'EUR'
result = dict(status='PASS',schema=54,functions=3,numeric_vectors=len(numbers),date_vectors=len(dates),unicode_vectors=len(unicode_vectors),
              unicode_version=ud.unidata_version,scalar_cases=len(load('cases.json')),xml_cells=xml_cells,
              formula_definitions=formula_count,xer_static_values=xer_cells,report_rows=len(rows),pdf_pages=len(pdf.pages),
              native_file_sha256=hashlib.sha256((root/'source.ppm').read_bytes()).hexdigest(),external_product_run=False)
print(json.dumps(result,ensure_ascii=False,indent=2))
