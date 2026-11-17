"""Independent interval-walking oracle for working-calendar formulas and exports."""
import argparse
import csv
import json
from datetime import datetime, timedelta
from decimal import Decimal, ROUND_HALF_UP
from pathlib import Path
import xml.etree.ElementTree as ET

p = argparse.ArgumentParser()
p.add_argument('root', type=Path)
p.add_argument('--reports', action='store_true')
p.add_argument('--reader', action='store_true')
args = p.parse_args()
ns = {'p': 'http://schemas.microsoft.com/project'}
native = json.loads((args.root / 'native.json').read_text(encoding='utf-8'))
source = json.loads((args.root / 'source.ppm').read_text(encoding='utf-8'))
assert source['schemaVersion'] == 46 and source['payload'] == native
assert native['tasks'][0]['attributes'] == [{'key': 'field:work', 'value': '999'}]
standard = dict(weekdays=[1, 2, 3, 4, 5], periods=[dict(begin=540, end=720), dict(begin=780, end=1080)], holidays=native['holidays'])
calendars = {'': standard, 'STANDARD': standard, **{c['id']: c for c in native['calendars']}}


def dt(s):
    return datetime.fromisoformat(s)


def iso(t):
    return t.isoformat(timespec='microseconds').rstrip('0').rstrip('.') if t.microsecond else t.isoformat(timespec='seconds')


def periods(date, calendar):
    day = date.replace(hour=0, minute=0, second=0, microsecond=0)
    if day.isoweekday() not in calendar['weekdays'] or day.date().isoformat() in calendar['holidays']:
        return []
    return [(day + timedelta(minutes=p['begin']), day + timedelta(minutes=p['end'])) for p in calendar['periods']]


def between(a, b, calendar):
    if b < a:
        return -between(b, a, calendar)
    total = timedelta()
    day = a.replace(hour=0, minute=0, second=0, microsecond=0)
    while day <= b:
        for start, end in periods(day, calendar):
            if min(b, end) > max(a, start):
                total += min(b, end) - max(a, start)
        day += timedelta(days=1)
    micros = total.days * 86400000000 + total.seconds * 1000000 + total.microseconds
    return (Decimal(micros) / Decimal(60000000)).quantize(Decimal('.000001'), rounding=ROUND_HALF_UP)


def move(start, minutes, calendar):
    if not minutes:
        return start
    left = timedelta(microseconds=int(Decimal(minutes) * 60000000))
    positive = left > timedelta()
    left = abs(left)
    day = start.replace(hour=0, minute=0, second=0, microsecond=0)
    for _ in range(366 * 20 + 2):
        windows = periods(day, calendar)
        if positive:
            for lo, hi in windows:
                begin = max(start, lo)
                if begin >= hi:
                    continue
                if left <= hi - begin:
                    return begin + left
                left -= hi - begin
        else:
            for lo, hi in reversed(windows):
                end = min(start, hi)
                if end <= lo:
                    continue
                if left <= end - lo:
                    return end - left
                left -= end - lo
        day += timedelta(days=1 if positive else -1)
    raise AssertionError('oracle horizon')


def next_work(t, calendar):
    day = t.replace(hour=0, minute=0, second=0, microsecond=0)
    for _ in range(366 * 20):
        for a, b in periods(day, calendar):
            if t < b:
                return max(a, t)
        day += timedelta(days=1)
    raise AssertionError('no work')


with (args.root / 'vectors.tsv').open(encoding='utf-8', newline='') as f:
    vectors = list(csv.DictReader(f, delimiter='\t'))
for row in vectors:
    c = calendars[row['calendar']]
    a, b, n = dt(row['from']), dt(row['to']), Decimal(row['amount'])
    assert between(a, b, c) == Decimal(row['diff']), row
    assert move(a, n, c) == dt(row['added']), row
    assert move(a, -n, c) == dt(row['subtracted']), row
assert len(vectors) == 180

# Fixed-duration FS chain without progress: derive both schedules independently.
expected = {}
for basis, begin in [('plan', native['startDate'] + 'T00:00:00'), ('forecast', native['statusAt'])]:
    cursor, dates = dt(begin), {}
    for t in native['tasks']:
        cal = calendars[t.get('calendarId', '')]
        a = next_work(cursor, cal)
        b = move(a, t['durationMinutes'], cal)
        dates[t['id']] = a, b
        cursor = b
    project_finish = max(b for a, b in dates.values())
    project_start = min(a for a, b in dates.values())
    base_a, base_b = dt('2026-11-11T09:00:00'), dt('2026-11-13T18:00:00')
    expected[basis, 'PROJECT', 'p'] = between(base_a, base_b, standard)
    expected[basis, 'PROJECT', 'pf'] = iso(move(project_finish, 60, standard))
    for t in native['tasks']:
        a, b = dates[t['id']]
        cal = calendars[t.get('calendarId', '')]
        literal = between(base_a, base_b, cal)
        values = dict(work=between(a, b, cal), wall=Decimal(int((b-a).total_seconds()/60)),
                      next=iso(move(b, 480, cal)), prev=iso(move(a, -480, cal)),
                      explicit=between(a, b, calendars['AM']), literal=literal,
                      first=iso(move(dt('2026-11-11T11:30:00'), 60, cal)),
                      elapsed=iso(b+timedelta(days=1)), scope=expected[basis, 'PROJECT', 'p']+literal,
                      native=between(a, b, cal)*2)
        for k, v in values.items():
            expected[basis, t['id'], k] = v
        expected[basis, 'ASSIGN:'+t['id'], 'a'] = literal
        expected[basis, 'ASSIGN:'+t['id'], 'mix'] = between(base_a, base_b, calendars['AM'])+literal+expected[basis, 'PROJECT', 'p']
        expected[basis, 'ASSIGN:'+t['id'], 'copy'] = values['work']
    r = between(base_a, base_b, calendars['AM'])
    expected[basis, 'RESOURCE', 'r'] = r
    expected[basis, 'RESOURCE', 'nested'] = r+expected[basis, 'PROJECT', 'p']
    for k, v in dict(work=between(project_start, project_finish, standard), literal=expected[basis, 'PROJECT', 'p'], explicit=between(project_start, project_finish, calendars['AM'])).items():
        expected[basis, 'SUMMARY', k] = v


def text(n, k, default=''):
    return n.findtext('p:'+k, default, ns)


xml_cells, formulae = 0, 0
for filename in ('plan.xml', 'reexport.xml'):
    root = ET.parse(args.root/filename).getroot()
    defs = {text(d, 'FieldID'): d for d in root.findall('p:ExtendedAttributes/p:ExtendedAttribute', ns)}
    assert len(defs) == 17
    for d in defs.values():
        if text(d, 'Formula'):
            formulae += 1
            assert '[field:' not in text(d, 'Formula')

    def values(row):
        out = {}
        for fid, d in defs.items():
            value = row.findtext('p:'+format(int(fid), 'x'), None, ns)
            for ea in row.findall('p:ExtendedAttribute', ns):
                if text(ea, 'FieldID') == fid:
                    value = text(ea, 'Value', None)
            if value is not None:
                out[text(d, 'Alias')] = Decimal(value) if int(text(d, 'CFType')) == 5 else value
        return out

    def verify(row, owner):
        global xml_cells
        actual = values(row)
        for (basis, obj, key), want in expected.items():
            if basis == 'plan' and obj == owner:
                assert actual.get(key) == want, (filename, owner, key, actual.get(key), want)
                xml_cells += 1

    tasks = {text(t, 'UID'): t for t in root.findall('p:Tasks/p:Task', ns)}
    names = {'표준 작업': 'A', '오전 작업': 'B'}
    for uid, row in tasks.items():
        if text(row, 'Summary') == '1':
            verify(row, 'SUMMARY')
            if uid == '0':
                verify(row, 'PROJECT')
        else:
            verify(row, names[text(row, 'Name')])
    for row in root.findall('p:Resources/p:Resource', ns):
        verify(row, 'RESOURCE')
    for row in root.findall('p:Assignments/p:Assignment', ns):
        owner = names[text(tasks[text(row, 'TaskUID')], 'Name')]
        v = values(row)
        for key in ('a', 'mix'):
            assert v[key] == expected['plan', 'ASSIGN:'+owner, key]
            xml_cells += 1
        assert any(text(d, 'AutoRollDown') == '1' and text(defs[text(d, 'SecondaryPID')], 'Alias') == 'copy' for d in defs.values())
assert xml_cells == 68 and formulae == 30

tables, columns, table = {}, [], None
for line in (args.root/'plan.xer').read_text(encoding='utf-8-sig').splitlines():
    a = line.split('\t')
    if a[0] == '%T':
        table = a[1]
        tables[table] = []
    elif a[0] == '%F':
        columns = a[1:]
    elif a[0] == '%R':
        tables[table].append(dict(zip(columns, a[1:])))
types = {r['udf_type_id']: r for r in tables['UDFTYPE']}
xer_cells = 0
for row in tables['UDFVALUE']:
    d = types[row['udf_type_id']]
    owner = {'TASK': '', 'PROJECT': 'PROJECT', 'RSRC': 'RESOURCE', 'TASKRSRC': 'ASSIGN:'}[d['table_name']]
    if d['table_name'] in ('TASK', 'TASKRSRC'):
        owner += native['tasks'][int(row['fk_id'])-1]['id']
    want = expected['plan', owner, d['udf_type_label']]
    actual = row['udf_text'] or row['udf_number'] or row['udf_date']
    assert (Decimal(actual) if isinstance(want, Decimal) else actual) == want, (owner, d['udf_type_label'], actual, want)
    xer_cells += 1
assert xer_cells == 30
archives = json.loads((args.root/'archives.json').read_text(encoding='utf-8'))
archive_plans = []


def archive_walk(node):
    if isinstance(node, dict):
        if 'tasks' in node and 'calendars' in node:
            assert node.get('xmlFormulaCalendarId') == 'AM'
            archive_plans.append(node)
        for value in node.values():
            archive_walk(value)
    elif isinstance(node, list):
        for value in node:
            archive_walk(value)


archive_walk(archives)
assert len(archive_plans) == 6 and archives['currency'] == 'USD'
result = dict(status='PASS', schema=46, oracle_vectors=len(vectors), xml_cells_both_exports=xml_cells,
              formula_definitions_both_exports=formulae, xer_static_values=xer_cells,
              archive_plans=len(archive_plans), external_product_run=False)
if args.reports:
    import openpyxl
    from pypdf import PdfReader
    wb = openpyxl.load_workbook(args.root/'forecast.xlsx', data_only=True)
    checked = 0
    task = next(s for s in wb if s.title.startswith('작업 사용자 필드'))
    rows = {(r[0], r[2]): r for r in task.values if r[0] in ('A', 'B')}
    for (basis, owner, key), want in expected.items():
        if basis == 'forecast' and owner in ('A', 'B'):
            assert rows[owner, 'field:'+key][4] == (float(want) if isinstance(want, Decimal) else want), (owner, key)
            checked += 1
    summary = next(s for s in wb if s.title.startswith('XML 사용자 필드 요약'))
    rows = {(r[0], r[2]): r for r in summary.values if r[2] in ('work', 'literal', 'explicit')}
    for group in ('', 'G'):
        for key in ('work', 'literal', 'explicit'):
            assert rows[group, key][4] == float(expected['forecast', 'SUMMARY', key])
            checked += 1
    for title, owner in [('프로젝트 사용자 필드', 'PROJECT'), ('자원 사용자 필드', 'RESOURCE')]:
        sheet = next(s for s in wb if s.title.startswith(title))
        rows = {r[2][6:]: r for r in sheet.values if isinstance(r[2], str) and r[2].startswith('field:')}
        for (basis, obj, key), want in expected.items():
            if basis == 'forecast' and obj == owner:
                assert rows[key][4] == (float(want) if isinstance(want, Decimal) else want)
                checked += 1
    sheet = next(s for s in wb if s.title.startswith('배정 사용자 필드'))
    rows = {(r[0], r[2]): r for r in sheet.values if r[0] in ('A', 'B')}
    for owner in ('A', 'B'):
        for key in ('a', 'mix', 'copy'):
            assert rows[owner, key][3] == float(expected['forecast', 'ASSIGN:'+owner, key])
            checked += 1
    assert checked == 36
    pdf = PdfReader(args.root/'forecast.pdf')
    content = ''.join(''.join(page.extract_text() or '' for page in pdf.pages).split())
    assert 'ProjDateDiff' in content and 'ProjDateAdd' in content and '상태일예측' in content and '2026-11-17' in content
    rw = openpyxl.load_workbook(args.root/'resources.xlsx', data_only=True)
    assert any('1320' in str(c) for ws in rw for row in ws.values for c in row)
    result.update(report_cells=checked, xlsx_sheets=len(wb.sheetnames), pdf_pages=len(pdf.pages), resource_sheets=len(rw.sheetnames))
if args.reader:
    import jpype
    import mpxj
    if not jpype.isJVMStarted():
        jpype.startJVM()
    reader = jpype.JClass('org.mpxj.mspdi.MSPDIReader')()
    reader.setIgnoreErrors(False)
    project = reader.read(str((args.root/'plan.xml').resolve()))
    assert not list(project.getIgnoredErrors())
    result.update(mpxj_tasks=project.getTasks().size(), mpxj_assignments=project.getResourceAssignments().size(), mpxj_errors=0)
print(json.dumps(result, ensure_ascii=False, indent=2))
