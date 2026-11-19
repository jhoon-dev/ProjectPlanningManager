"""Independently select raw actual ledgers and compare XML/XER/XLSX/PDF outputs."""
import argparse
import json
import re
from decimal import Decimal, ROUND_HALF_UP
from pathlib import Path
from datetime import datetime, timedelta
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument('root', type=Path)
parser.add_argument('--reports', action='store_true')
parser.add_argument('--reader', action='store_true')
args = parser.parse_args()
native = json.loads((args.root/'native.json').read_text(encoding='utf-8'))
source = json.loads((args.root/'source.ppm').read_text(encoding='utf-8'))
mapping = json.loads((args.root/'mapping.json').read_text(encoding='utf-8'))
assert source['schemaVersion'] in (48, 74) and source['payload'] == native
status = native['statusAt']
progress, work = {}, {}
for e in native['progress']:
    if e['statusAt'] <= status and (e['taskId'] not in progress or
                                   e['statusAt'] >= progress[e['taskId']]['statusAt']):
        progress[e['taskId']] = e
for e in native['periodActuals']:
    key = e['taskId'], e['resourceId'], e['date']
    if e['statusAt'] <= status and (key not in work or e['statusAt'] >= work[key]['statusAt']):
        work[key] = e


def total(task=None, resource=None):
    rows = [Decimal(e['workUnitMinutes'])/100 for e in work.values()
            if e.get('workUnitMinutes') is not None
            and (task is None or e['taskId'] == task)
            and (resource is None or e['resourceId'] == resource)]
    return sum(rows) if rows else None


def duration(task):
    return Decimal(progress[task]['actualDurationMinutes']) if task in progress else None


def hours(value):
    return (value/60).quantize(Decimal('.000001'), rounding=ROUND_HALF_UP) if value is not None else None


expected = {}
for t in native['tasks']:
    expected['TASK', t['id']] = dict(AD=duration(t['id']), DN=duration(t['id']),
                                      AW=total(task=t['id']), hours=hours(total(task=t['id'])))
expected['PROJECT', 'PROJECT'] = dict(AW=total())
for r in native['resources']:
    expected['RESOURCE', r['id']] = dict(AW=total(resource=r['id']))
for a in native['assignments']:
    expected['ASSIGNMENT', a['taskId']+'/'+a['resourceId']] = dict(
        AW=total(a['taskId'], a['resourceId']), TD=duration(a['taskId']), TW=total(task=a['taskId']),
        RW=total(resource=a['resourceId']), PW=total())
def summary_duration(members):
    if source['schemaVersion'] < 74 or not members or any(t not in progress for t in members):
        return None
    started = [progress[t] for t in members if progress[t].get('actualStart')]
    if not started:
        return Decimal(0)
    at = min(datetime.fromisoformat(e['actualStart']) for e in started)
    finish = max(datetime.fromisoformat(e.get('actualFinish') or status) for e in started)
    calendar = next((c for c in native['calendars'] if c['id'] == native.get('xmlFormulaCalendarId')),
                    dict(weekdays=[1,2,3,4,5], periods=[dict(begin=540,end=720),dict(begin=780,end=1080)], holidays=native.get('holidays', [])))
    result = 0
    while at < finish:
        minute = at.hour*60+at.minute
        result += (at.isoweekday() in calendar['weekdays'] and at.date().isoformat() not in calendar['holidays']
                   and any(p['begin'] <= minute < p['end'] for p in calendar['periods']))
        at += timedelta(minutes=1)
    return Decimal(result)


for group in ['', *[g['id'] for g in native['wbsNodes']]]:
    rows = [total(task=t['id']) for t in native['tasks'] if not group or t['wbsId'] == group]
    known = [v for v in rows if v is not None]
    members = [t['id'] for t in native['tasks'] if not group or t['wbsId'] == group]
    expected['SUMMARY', group] = dict(AD=summary_duration(members), AW=sum(known) if known else None)
assert total() == Decimal('75.01') and total('B') == 0 and total('C') is None
assert duration('A') == 300 and duration('C') is None and duration('M') == 0

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
ids = {kind: {m['xmlUid']: m['ppmId'] for m in mapping['mapping'] if m['kind'] == kind}
       for kind in ['task', 'resource', 'assignment', 'wbs']}
root = ET.parse(args.root/'quantities.xml').getroot()
definitions = {text(d, 'FieldID'): d for d in root.findall('p:ExtendedAttributes/p:ExtendedAttribute', ns)}
xml_cells = 0
formulae = 0
missing = 0
for fid, d in definitions.items():
    if udf[fid]['ppmId'] in {'AD', 'DN', 'AW', 'hours', 'TD', 'TW', 'RW', 'PW'}:
        assert 'Actual' in text(d, 'Formula') and '[field:' not in text(d, 'Formula')
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
        fid = next(fid for fid, d in udf.items() if d['scope'] == lookup_scope and d['ppmId'] == key)
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
for row in root.findall('p:Resources/p:Resource', ns):
    check_xml(row, 'RESOURCE', ids['resource'][text(row, 'UID')])
for row in root.findall('p:Assignments/p:Assignment', ns):
    check_xml(row, 'ASSIGNMENT', ids['assignment'][text(row, 'UID')])

tables, columns, table = {}, [], None
for line in (args.root/'quantities.xer').read_text(encoding='utf-8-sig').splitlines():
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
    if label not in {'DN', 'AW', 'hours', 'TD', 'TW', 'RW', 'PW'}:
        continue
    scope = {'TASK': 'TASK', 'PROJECT': 'PROJECT', 'RSRC': 'RESOURCE', 'TASKRSRC': 'ASSIGNMENT'}[d['table_name']]
    owner = ('PROJECT' if scope == 'PROJECT' else
             native['tasks'][int(row['fk_id'])-1]['id'] if scope == 'TASK' else
             native['resources'][int(row['fk_id'])-1]['id'] if scope == 'RESOURCE' else
             native['assignments'][int(row['fk_id'])-1]['taskId']+'/'+native['assignments'][int(row['fk_id'])-1]['resourceId'])
    assert Decimal(row['udf_number']) == expected[scope, owner][label], (scope, owner, label, row)
    xer_cells += 1
assert xer_cells == sum(v is not None for (scope, owner), fields in expected.items() if scope != 'SUMMARY'
                        for k, v in fields.items() if k != 'AD')
result = dict(status='PASS', schema=source['schemaVersion'], xml_cells=xml_cells, missing_xml_cells=missing,
              quantity_formula_definitions=formulae, xer_quantity_values=xer_cells, external_product_run=False)
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
                if key == 'AD':
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
    rows = {(r[0], r[2]): r[4] for r in sheet.values if r[2] in ('AD', 'AW')}
    for (scope, owner), fields in expected.items():
        if scope == 'SUMMARY':
            for key, want in fields.items():
                display = '미정' if want is None else f'{want} m' if key == 'AD' else float(want)
                assert rows[owner, key] == display, (owner, key, rows[owner, key], want)
                report_cells += 1
    all_text = [str(c) for sheet in wb for row in sheet.values for c in row if c is not None]
    assert any('보고 공수 합계(미보고 제외)' in c for c in all_text)
    pdf = PdfReader(args.root/'fields.pdf')
    content = ''.join(''.join(page.extract_text() or '' for page in pdf.pages).split())
    assert '75.01' in content and 'ActualWork' in content and '보고공수합계(미보고제외)' in content
    result.update(report_cells=report_cells, xlsx_sheets=len(wb.sheetnames), pdf_pages=len(pdf.pages))
if args.reader:
    import jpype
    import mpxj
    if not jpype.isJVMStarted():
        jpype.startJVM()
    reader = jpype.JClass('org.mpxj.mspdi.MSPDIReader')()
    reader.setIgnoreErrors(False)
    project = reader.read(str((args.root/'quantities.xml').resolve()))
    assert not list(project.getIgnoredErrors())
    result.update(mpxj_tasks=project.getTasks().size(), mpxj_assignments=project.getResourceAssignments().size(), mpxj_errors=0)
print(json.dumps(result, ensure_ascii=False, indent=2))
