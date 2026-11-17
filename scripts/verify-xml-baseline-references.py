"""Integrate frozen baseline civil minutes independently, then inspect real XML/XER/XLSX/PDF outputs."""
import argparse
from datetime import datetime, timedelta
from decimal import Decimal, ROUND_HALF_UP
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument('root', type=Path)
parser.add_argument('--reports', action='store_true')
parser.add_argument('--reader', action='store_true')
args = parser.parse_args()
native = json.loads((args.root/'native.json').read_text(encoding='utf-8'))
source = json.loads((args.root/'source.ppm').read_text(encoding='utf-8'))
mapping = json.loads((args.root/'mapping.json').read_text(encoding='utf-8'))
assert source['schemaVersion'] in (50, 74) and source['payload'] == native
baseline = native['baselines'][0]
assert baseline['version'] == 1 and baseline['id'] == 'BASE-1'
frozen = baseline['input']
calendars = {c['id']: c for c in frozen['calendars']}
resources = {r['id']: r for r in frozen['resources']}
tasks = {t['id']: t for t in frozen['tasks']}
rows = {r['taskId']: r for r in baseline['schedule']}
assert native['startDate'] != frozen['startDate'] and native['calendars'] != frozen['calendars']
assert native['baselines'][1]['projectFinish'] != baseline['projectFinish']
work = {(a['taskId'], a['resourceId']): 0 for a in frozen['assignments']}
durations = {}


def working(at, calendar):
    c = calendars[calendar]
    minute = at.hour*60+at.minute
    return at.isoweekday() in c['weekdays'] and at.date().isoformat() not in c['holidays'] and any(
        p['begin'] <= minute < p['end'] for p in c['periods'])


for tid, t in tasks.items():
    row = rows[tid]
    at, finish = datetime.fromisoformat(row['start']), datetime.fromisoformat(row['finish'])
    pending = []
    while at < finish:
        if working(at, t['calendarId']) and not any(
                datetime.fromisoformat(b['start']) <= at < datetime.fromisoformat(b['finish']) for b in t['breaks']):
            pending.append(at)
        at += timedelta(minutes=1)
    durations[tid] = Decimal(len(pending))
    driven = t['taskType'] == 'fixed-work' or t['workDriven']
    target = t['workUnitMinutes'] if driven else None
    consumed = 0
    for local, at in enumerate(pending):
        capacity = []
        for a in frozen['assignments']:
            if a['taskId'] != tid or not working(at, resources[a['resourceId']]['calendarId']):
                continue
            units = a['units']
            for p in a.get('periods', []):
                if p['fromMinute'] <= local < p['toMinute']:
                    units = p['units']
                    break
            capacity.append((a['resourceId'], units))
        total = sum(n for _, n in capacity)
        take = min(total, target-consumed) if driven else total
        allocated = [n*take//total if total else 0 for _, n in capacity]
        extra = take-sum(allocated)
        for i in sorted(range(len(capacity)), key=lambda i: capacity[i][0]):
            if extra:
                allocated[i] += 1
                extra -= 1
        for (rid, _), amount in zip(capacity, allocated):
            work[tid, rid] += amount
        consumed += take
    if driven:
        assert consumed == target


def total(task=None, resource=None):
    return Decimal(sum(v for (tid, rid), v in work.items() if (task is None or task == tid)
                       and (resource is None or resource == rid)))/100


def stamp(raw):
    return datetime.fromisoformat(raw).isoformat(timespec='seconds')


def hours(amount):
    return (amount/60).quantize(Decimal('.000001'), rounding=ROUND_HALF_UP)


expected = {}
for t in native['tasks']:
    tid = t['id']
    known = tid in tasks
    expected['TASK', tid] = dict(BS=stamp(rows[tid]['start']) if known else None,
                                 BF=stamp(rows[tid]['finish']) if known else None,
                                 BD=durations.get(tid), DN=durations.get(tid),
                                 BW=total(task=tid) if known else None, hours=hours(total(task=tid)) if known else None)
expected['PROJECT', 'PROJECT'] = dict(BS=min(stamp(r['start']) for r in rows.values()),
                                     BF=stamp(baseline['projectFinish']), BW=total())
for r in native['resources']:
    expected['RESOURCE', r['id']] = dict(BW=total(resource=r['id']) if r['id'] in resources else None)
for a in native['assignments']:
    tid, rid = a['taskId'], a['resourceId']
    expected['ASSIGNMENT', tid+'/'+rid] = dict(BW=Decimal(work[tid, rid])/100 if (tid, rid) in work else None,
                                              TS=stamp(rows[tid]['start']) if tid in rows else None,
                                              TF=stamp(rows[tid]['finish']) if tid in rows else None,
                                              TD=durations.get(tid), TW=total(task=tid) if tid in tasks else None,
                                              RW=total(resource=rid) if rid in resources else None, PW=total())
# Summary periods use the project calendar, independently of leaf calendars/breaks.
project_calendar = frozen.get('xmlFormulaCalendarId') or '__standard_summary__'
calendars['__standard_summary__'] = dict(weekdays=[1,2,3,4,5], periods=[dict(begin=540,end=720),dict(begin=780,end=1080)], holidays=frozen.get('holidays', []))
def summary_duration(members):
    if source['schemaVersion'] < 74 or not members:
        return None
    if any(t not in rows for t in members):
        return None
    active = [rows[t] for t in members]
    at = min(datetime.fromisoformat(row['start']) for row in active)
    finish = max(datetime.fromisoformat(row['finish']) for row in active)
    result = 0
    while at < finish:
        result += working(at, project_calendar)
        at += timedelta(minutes=1)
    return Decimal(result)

for group in ['', *[g['id'] for g in native['wbsNodes']]]:
    members = [t['id'] for t in native['tasks'] if not group or t['wbsId'] == group]
    known = bool(members) and all(t in tasks for t in members)
    expected['SUMMARY', group] = dict(BS=min(stamp(rows[t]['start']) for t in members) if known else None,
                                      BF=max(stamp(rows[t]['finish']) for t in members) if known else None,
                                      BD=summary_duration(members), BW=sum(total(task=t) for t in members) if known else None)
assert durations == dict(A=8, B=6, M=0, D=3)
assert total() == Decimal('10.65') and total('A') == Decimal('5.64') and total('B') == Decimal('5.01')
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
root = ET.parse(args.root/'baseline.xml').getroot()
assert text(root, 'StartDate').startswith(native['startDate']), 'XML retains the current project start'
definitions = {text(d, 'FieldID'): d for d in root.findall('p:ExtendedAttributes/p:ExtendedAttribute', ns)}
for fid, d in definitions.items():
    assert 'Baseline' in text(d, 'Formula') and '[field:' not in text(d, 'Formula')
xml_cells = missing = 0


def check_xml(row, scope, owner):
    global xml_cells, missing
    lookup_scope = 'TASK' if scope == 'SUMMARY' else scope
    raw = {text(e, 'FieldID'): text(e, 'Value') for e in row.findall('p:ExtendedAttribute', ns)}
    for fid in definitions:
        value = row.findtext('p:'+format(int(fid), 'x'), None, ns)
        if value is not None:
            raw[fid] = value
    for key, want in expected[scope, owner].items():
        fid = next(fid for fid, d in udf.items() if d['scope'] == lookup_scope and d['jobsId'] == key)
        actual = raw.get(fid)
        if actual is not None:
            if isinstance(want, str):
                actual = stamp(actual)
            else:
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
for row in root.findall('p:Resources/p:Resource', ns):
    check_xml(row, 'RESOURCE', ids['resource'][text(row, 'UID')])
for row in root.findall('p:Assignments/p:Assignment', ns):
    check_xml(row, 'ASSIGNMENT', ids['assignment'][text(row, 'UID')])

tables, columns, table = {}, [], None
for line in (args.root/'baseline.xer').read_text(encoding='utf-8-sig').splitlines():
    values = line.split('\t')
    if values[0] == '%T':
        table = values[1]
        tables[table] = []
    elif values[0] == '%F':
        columns = values[1:]
    elif values[0] == '%R':
        tables[table].append(dict(zip(columns, values[1:])))
types = {r['udf_type_id']: r for r in tables['UDFTYPE']}
numeric = {'DN', 'BW', 'hours', 'TD', 'TW', 'RW', 'PW'}
xer_cells = 0
for row in tables['UDFVALUE']:
    d = types[row['udf_type_id']]
    label = d['udf_type_label']
    if label not in numeric:
        continue
    scope = {'TASK': 'TASK', 'PROJECT': 'PROJECT', 'RSRC': 'RESOURCE', 'TASKRSRC': 'ASSIGNMENT'}[d['table_name']]
    owner = ('PROJECT' if scope == 'PROJECT' else
             native['tasks'][int(row['fk_id'])-1]['id'] if scope == 'TASK' else
             native['resources'][int(row['fk_id'])-1]['id'] if scope == 'RESOURCE' else
             native['assignments'][int(row['fk_id'])-1]['taskId']+'/'+native['assignments'][int(row['fk_id'])-1]['resourceId'])
    assert Decimal(row['udf_number']) == expected[scope, owner][label], (scope, owner, label, row)
    xer_cells += 1
assert xer_cells == sum(v is not None for (scope, owner), fields in expected.items() if scope != 'SUMMARY'
                        for k, v in fields.items() if k in numeric)
result = dict(status='PASS', schema=source['schemaVersion'], baseline='BASE-1', xml_cells=xml_cells, missing_xml_cells=missing,
              baseline_formula_definitions=len(definitions), xer_baseline_numbers=xer_cells, external_product_run=False)


def report_value(key, want):
    if want is None:
        return '미정'
    if key == 'BD':
        return f'{want} m'
    return want if isinstance(want, str) else float(want)


if args.reports:
    import openpyxl
    from pypdf import PdfReader
    wb = openpyxl.load_workbook(args.root/'fields.xlsx', data_only=True)
    report_cells = 0
    for title, scope in [('작업 사용자 필드', 'TASK'), ('프로젝트 사용자 필드', 'PROJECT'), ('자원 사용자 필드', 'RESOURCE')]:
        sheet = next(s for s in wb if s.title.startswith(title))
        actual = {(r[0], r[2]): r[4] for r in sheet.values if isinstance(r[2], str) and r[2].startswith('field:')}
        for (s, owner), fields in expected.items():
            if s == scope:
                for key, want in fields.items():
                    value = actual[owner if scope != 'PROJECT' else native['documentId'], 'field:'+key]
                    assert value == report_value(key, want), (scope, owner, key, value, want)
                    report_cells += 1
    sheet = next(s for s in wb if s.title.startswith('배정 사용자 필드'))
    actual = {(r[0]+'/'+r[1], r[2]): r[3] for r in sheet.values if r[0] in {t['id'] for t in native['tasks']}}
    for (scope, owner), fields in expected.items():
        if scope == 'ASSIGNMENT':
            for key, want in fields.items():
                assert actual[owner, key] == report_value(key, want), (owner, key, actual[owner, key], want)
                report_cells += 1
    sheet = next(s for s in wb if s.title.startswith('XML 사용자 필드 요약'))
    actual = {(r[0], r[2]): r[4] for r in sheet.values if r[2] in ('BS', 'BF', 'BD', 'BW')}
    for (scope, owner), fields in expected.items():
        if scope == 'SUMMARY':
            for key, want in fields.items():
                # Summary cells use the raw canonical duration string only for known values; BD is always missing.
                assert actual[owner, key] == report_value(key, want), (owner, key, actual[owner, key], want)
                report_cells += 1
    all_text = [str(c) for sheet in wb for row in sheet.values for c in row if c is not None]
    assert any('BASE-1' in c and '최초' in c for c in all_text)
    pdf = PdfReader(args.root/'fields.pdf')
    content = ''.join(''.join(page.extract_text() or '' for page in pdf.pages).split())
    assert '5.64' in content and 'BaselineWork' in content and 'BASE-1' in content
    result.update(report_cells=report_cells, xlsx_sheets=len(wb.sheetnames), pdf_pages=len(pdf.pages))
if args.reader:
    import jpype
    import mpxj
    if not jpype.isJVMStarted():
        jpype.startJVM()
    reader = jpype.JClass('org.mpxj.mspdi.MSPDIReader')()
    reader.setIgnoreErrors(False)
    project = reader.read(str((args.root/'baseline.xml').resolve()))
    assert not list(project.getIgnoredErrors())
    result.update(mpxj_tasks=project.getTasks().size(), mpxj_assignments=project.getResourceAssignments().size(), mpxj_errors=0)
print(json.dumps(result, ensure_ascii=False, indent=2))
