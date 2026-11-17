"""Independently integrate civil-minute remaining calendars and compare XML/XER/XLSX/PDF outputs."""
import argparse
import json
import re
from decimal import Decimal, ROUND_HALF_UP
from pathlib import Path
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument('root', type=Path)
parser.add_argument('--reports', action='store_true')
parser.add_argument('--reader', action='store_true')
args = parser.parse_args()
native = json.loads((args.root/'native.json').read_text(encoding='utf-8'))
source = json.loads((args.root/'source.ppm').read_text(encoding='utf-8'))
mapping = json.loads((args.root/'mapping.json').read_text(encoding='utf-8'))
assert source['schemaVersion'] in (49, 74) and source['payload'] == native
status = native['statusAt']
from datetime import datetime, timedelta
progress = {}
for e in native['progress']:
    if e['statusAt'] <= status and (e['taskId'] not in progress or e['statusAt'] >= progress[e['taskId']]['statusAt']):
        progress[e['taskId']] = e
rows = {r['taskId']: r for r in json.loads((args.root/'forecast.json').read_text(encoding='utf-8'))}
calendars = {c['id']: c for c in native['calendars']}
resources = {r['id']: r for r in native['resources']}
assignment_work = {(a['taskId'], a['resourceId']): 0 for a in native['assignments']}
durations = {}

def working(at, calendar):
    c = calendars[calendar]
    minute = at.hour*60+at.minute
    return at.isoweekday() in c['weekdays'] and at.date().isoformat() not in c['holidays'] and any(
        p['begin'] <= minute < p['end'] for p in c['periods'])

for t in native['tasks']:
    row = rows[t['id']]
    pending = []
    if not row['completed']:
        at, finish = datetime.fromisoformat(row['remainingStart']), datetime.fromisoformat(row['finish'])
        while at < finish:
            if working(at, t['calendarId']) and not any(datetime.fromisoformat(b['start']) <= at < datetime.fromisoformat(b['finish']) for b in t['breaks']):
                pending.append(at)
            at += timedelta(minutes=1)
    durations[t['id']] = Decimal(len(pending))
    driven = t['taskType'] == 'fixed-work' or t['workDriven']
    entry = progress.get(t['id'])
    target = (entry['remainingWorkUnitMinutes'] if entry else t['workUnitMinutes']) if driven else None
    consumed = 0
    for local, at in enumerate(pending):
        capacity = []
        for a in native['assignments']:
            if a['taskId'] != t['id'] or not working(at, resources[a['resourceId']]['calendarId']):
                continue
            offset = local + (max(0, t['durationMinutes']-len(pending)) if entry and not driven else 0)
            units = a['units']
            for p in a.get('periods', []):
                if p['fromMinute'] <= offset < p['toMinute']:
                    units = p['units']
                    break
            capacity.append((a['resourceId'], units))
        total = sum(n for _, n in capacity)
        take = min(total, target-consumed) if driven else total
        allocated = [n*take//total if total else 0 for _, n in capacity]
        extra = take-sum(allocated)
        for i in sorted(range(len(capacity)), key=lambda i:capacity[i][0]):
            if extra:
                allocated[i] += 1
                extra -= 1
        for (rid, units), n in zip(capacity, allocated):
            assignment_work[t['id'], rid] += n
        consumed += take
    if driven:
        assert consumed == (0 if row['completed'] else target)

def total(task=None, resource=None):
    return Decimal(sum(v for (tid,rid),v in assignment_work.items() if (task is None or task == tid) and (resource is None or resource == rid)))/100

def hours(value):
    return (value/60).quantize(Decimal('.000001'), rounding=ROUND_HALF_UP)

expected = {}
for t in native['tasks']:
    expected['TASK', t['id']] = dict(RD=durations[t['id']], DN=durations[t['id']], RW=total(task=t['id']), hours=hours(total(task=t['id'])))
expected['PROJECT', 'PROJECT'] = dict(RW=total())
for r in native['resources']:
    expected['RESOURCE', r['id']] = dict(RW=total(resource=r['id']))
for a in native['assignments']:
    expected['ASSIGNMENT', a['taskId']+'/'+a['resourceId']] = dict(RW=Decimal(assignment_work[a['taskId'],a['resourceId']])/100, TD=durations[a['taskId']], TW=total(task=a['taskId']), RWResource=total(resource=a['resourceId']), PW=total())
# Summary periods use the project calendar, independently of leaf calendars/breaks.
project_calendar = native.get('xmlFormulaCalendarId') or '__standard_summary__'
calendars['__standard_summary__'] = dict(weekdays=[1,2,3,4,5], periods=[dict(begin=540,end=720),dict(begin=780,end=1080)], holidays=native.get('holidays', []))
def summary_duration(members):
    if source['schemaVersion'] < 74 or not members:
        return None
    active = [rows[t['id']] for t in members if not rows[t['id']]['completed']]
    if not active:
        return Decimal(0)
    at = min(datetime.fromisoformat(row['remainingStart']) for row in active)
    finish = max(datetime.fromisoformat(row['finish']) for row in active)
    result = 0
    while at < finish:
        result += working(at, project_calendar)
        at += timedelta(minutes=1)
    return Decimal(result)

for group in ['', *[g['id'] for g in native['wbsNodes']]]:
    members = [t for t in native['tasks'] if not group or t['wbsId'] == group]
    expected['SUMMARY', group] = dict(RD=summary_duration(members), RW=sum(total(task=t['id']) for t in members) if members else None)
assert [durations[t['id']] for t in native['tasks']] == [0,4,3,8,3,0]
assert total() == Decimal('14.83') and total('B') == Decimal('5.32') and total('W') == Decimal('5.01')
assert rows['W']['finish'] == '2026-11-13 09:02'

ns = {'p': 'http://schemas.microsoft.com/project'}


def text(node, key, default=''):
    return node.findtext('p:'+key, default, ns)


def minutes(raw):
    m = re.fullmatch(r'PT(?:(\d+)H)?(?:(\d+)M)?(?:(\d+(?:\.\d+)?)S)?', raw)
    assert m, raw
    h, minute, sec = (Decimal(x or 0) for x in m.groups())
    return h*60+minute+sec/60


udf = {m['externalId']: dict(m, scope={'RSRC': 'RESOURCE', 'ASSIGN': 'ASSIGNMENT'}.get(m['scope'], m['scope']))
       for m in mapping['mapping'] if m['kind'] == 'udf'}
ids = {kind: {m['xmlUid']: m['jobsId'] for m in mapping['mapping'] if m['kind'] == kind}
       for kind in ['task', 'resource', 'assignment', 'wbs']}
root = ET.parse(args.root/'remaining.xml').getroot()
definitions = {text(d, 'FieldID'): d for d in root.findall('p:ExtendedAttributes/p:ExtendedAttribute', ns)}
xml_cells = 0
formulae = 0
missing = 0
for fid, d in definitions.items():
    if udf[fid]['jobsId'] in {'RD', 'DN', 'RW', 'hours', 'TD', 'TW', 'RWResource', 'PW'}:
        assert 'Remaining' in text(d, 'Formula') and '[field:' not in text(d, 'Formula')
        formulae += 1


def check_xml(row, scope, owner):
    global xml_cells, missing
    lookup_scope = 'TASK' if scope == 'SUMMARY' else scope
    raw = {text(e, 'FieldID'): text(e, 'Value') for e in row.findall('p:ExtendedAttribute', ns)}
    for fid, definition in definitions.items():
        value = row.findtext('p:'+format(int(fid), 'x'), None, ns)
        if value is not None:
            raw[fid] = value
    for key, want in expected[scope, owner].items():
        fid = next(fid for fid, d in udf.items() if d['scope'] == lookup_scope and d['jobsId'] == key)
        actual = raw.get(fid)
        if actual is not None:
            actual = minutes(actual) if text(definitions[fid], 'CFType') == '2' else Decimal(actual)
        assert actual == want, (scope, owner, key, actual, want)
        xml_cells += 1
        missing += want is None


for row in root.findall('p:Tasks/p:Task', ns):
    uid = text(row, 'UID')
    if uid == '0':
        check_xml(row, 'PROJECT', 'PROJECT')
        check_xml(row, 'SUMMARY', '')
    elif uid in ids['wbs']:
        check_xml(row, 'SUMMARY', ids['wbs'][uid])
    else:
        check_xml(row, 'TASK', ids['task'][uid])
        if ids['task'][uid] == 'B':
            assert minutes(text(row, 'Duration')) == 9
            assert next(t for t in native['tasks'] if t['id'] == 'B')['durationMinutes'] == 8
        if ids['task'][uid] == 'C':
            start_id = next(fid for fid, d in udf.items() if d['scope'] == 'TASK' and d['jobsId'] == 'start')
            start = next(text(v, 'Value') for v in row.findall('p:ExtendedAttribute', ns)
                         if text(v, 'FieldID') == start_id)
            assert start == '2026-11-12T09:00:00' and rows['C']['start'] == '2026-11-12 09:02'
for row in root.findall('p:Resources/p:Resource', ns):
    check_xml(row, 'RESOURCE', ids['resource'][text(row, 'UID')])
for row in root.findall('p:Assignments/p:Assignment', ns):
    owner = ids['assignment'][text(row, 'UID')]
    check_xml(row, 'ASSIGNMENT', owner)
    if owner in ('B/R', 'B/S'):
        planned = sum(minutes(text(v, 'Value')) for v in row.findall('p:TimephasedData', ns))
        assert planned == (Decimal(4) if owner == 'B/R' else Decimal('2.64')), (owner, planned)

tables, columns, table = {}, [], None
for line in (args.root/'remaining.xer').read_text(encoding='utf-8-sig').splitlines():
    values = line.split('\t')
    if values[0] == '%T':
        table = values[1]
        tables[table] = []
    elif values[0] == '%F':
        columns = values[1:]
    elif values[0] == '%R':
        tables[table].append(dict(zip(columns, values[1:])))
types = {r['udf_type_id']: r for r in tables['UDFTYPE']}
xer_cells = 0
for row in tables['UDFVALUE']:
    d = types[row['udf_type_id']]
    label = d['udf_type_label']
    if label not in {'DN', 'RW', 'hours', 'TD', 'TW', 'RWResource', 'PW'}:
        continue
    scope = {'TASK': 'TASK', 'PROJECT': 'PROJECT', 'RSRC': 'RESOURCE', 'TASKRSRC': 'ASSIGNMENT'}[d['table_name']]
    owner = ('PROJECT' if scope == 'PROJECT' else
             native['tasks'][int(row['fk_id'])-1]['id'] if scope == 'TASK' else
             native['resources'][int(row['fk_id'])-1]['id'] if scope == 'RESOURCE' else
             native['assignments'][int(row['fk_id'])-1]['taskId']+'/'+native['assignments'][int(row['fk_id'])-1]['resourceId'])
    assert Decimal(row['udf_number']) == expected[scope, owner][label], (scope, owner, label, row)
    xer_cells += 1
assert xer_cells == sum(v is not None for (scope, owner), fields in expected.items() if scope != 'SUMMARY'
                        for k, v in fields.items() if k != 'RD')
result = dict(status='PASS', schema=source['schemaVersion'], xml_cells=xml_cells, missing_xml_cells=missing,
              remaining_formula_definitions=formulae, xer_remaining_values=xer_cells, external_product_run=False)
if args.reports:
    import openpyxl
    from pypdf import PdfReader
    wb = openpyxl.load_workbook(args.root/'fields.xlsx', data_only=True)
    report_cells = 0
    for title, scope in [('작업 사용자 필드', 'TASK'), ('프로젝트 사용자 필드', 'PROJECT'), ('자원 사용자 필드', 'RESOURCE')]:
        sheet = next(s for s in wb if s.title.startswith(title))
        actual = {(r[0], r[2]): r[4] for r in sheet.values if isinstance(r[2], str) and r[2].startswith('field:')}
        for (s, owner), fields in expected.items():
            if s != scope:
                continue
            for key, want in fields.items():
                value = actual[owner if scope != 'PROJECT' else native['documentId'], 'field:'+key]
                if key == 'RD':
                    assert value == ('미정' if want is None else f'{want} m'), (key, value, want)
                else:
                    assert value == ('미정' if want is None else float(want)), (scope, owner, key, value, want)
                report_cells += 1
    sheet = next(s for s in wb if s.title.startswith('배정 사용자 필드'))
    rows = {(r[0]+'/'+r[1], r[2]): r[3] for r in sheet.values if r[0] in {t['id'] for t in native['tasks']}}
    for (scope, owner), fields in expected.items():
        if scope == 'ASSIGNMENT':
            for key, want in fields.items():
                assert rows[owner, key] == ('미정' if want is None else float(want)), (owner, key, rows[owner, key], want)
                report_cells += 1
    sheet = next(s for s in wb if s.title.startswith('XML 사용자 필드 요약'))
    rows = {(r[0], r[2]): r[4] for r in sheet.values if r[2] in ('RD', 'RW')}
    for (scope, owner), fields in expected.items():
        if scope == 'SUMMARY':
            for key, want in fields.items():
                display = '미정' if want is None else f'{want} m' if key == 'RD' else float(want)
                assert rows[owner, key] == display, (owner, key, rows[owner, key], want)
                report_cells += 1
    all_text = [str(c) for sheet in wb for row in sheet.values for c in row if c is not None]
    assert any('잔여 예측' in c for c in all_text)
    pdf = PdfReader(args.root/'fields.pdf')
    content = ''.join(''.join(page.extract_text() or '' for page in pdf.pages).split())
    assert '5.32' in content and 'RemainingWork' in content and '잔여예측' in content
    result.update(report_cells=report_cells, xlsx_sheets=len(wb.sheetnames), pdf_pages=len(pdf.pages))
if args.reader:
    import jpype
    import mpxj
    if not jpype.isJVMStarted():
        jpype.startJVM()
    reader = jpype.JClass('org.mpxj.mspdi.MSPDIReader')()
    reader.setIgnoreErrors(False)
    project = reader.read(str((args.root/'remaining.xml').resolve()))
    assert not list(project.getIgnoredErrors())
    result.update(mpxj_tasks=project.getTasks().size(), mpxj_assignments=project.getResourceAssignments().size(), mpxj_errors=0)
print(json.dumps(result, ensure_ascii=False, indent=2))
