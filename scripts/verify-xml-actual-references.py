"""Independent as-of ledger selection and output checks for XML actual date references."""
import argparse
import json
from datetime import datetime, timedelta
from decimal import Decimal
from pathlib import Path
import xml.etree.ElementTree as ET

p = argparse.ArgumentParser()
p.add_argument('root', type=Path)
p.add_argument('--reports', action='store_true')
p.add_argument('--reader', action='store_true')
args = p.parse_args()
ns = {'p': 'http://schemas.microsoft.com/project'}
native = json.loads((args.root/'native.json').read_text(encoding='utf-8'))
complete = json.loads((args.root/'complete.json').read_text(encoding='utf-8'))
source = json.loads((args.root/'source.ppm').read_text(encoding='utf-8'))
mapping = json.loads((args.root/'mapping.json').read_text(encoding='utf-8'))
assert source['schemaVersion'] == 47 and source['payload'] == native
assert {a['key']: a['value'] for a in native['tasks'][0]['attributes']} == {
    'field:AS': '2000-01-01T00:00:00', 'field:AF': '2000-01-02T00:00:00', 'field:work': '999'}


def iso(s):
    return datetime.fromisoformat(s).isoformat(timespec='seconds') if s else None


def working(a, b):
    if a is None or b is None:
        return None
    a, b = datetime.fromisoformat(a), datetime.fromisoformat(b)
    total = timedelta()
    day = a.replace(hour=0, minute=0, second=0, microsecond=0)
    while day <= b:
        if day.isoweekday() <= 5:
            for lo, hi in [(540, 720), (780, 1080)]:
                start, end = max(a, day+timedelta(minutes=lo)), min(b, day+timedelta(minutes=hi))
                if end > start:
                    total += end-start
        day += timedelta(days=1)
    return Decimal(int(total.total_seconds()/60))


def select(project, status=None):
    status = project.get('statusAt') if status is None else status
    chosen = {}
    if status:
        # Match status ordering; ties use the last appended report, never recordedAt.
        for index, e in enumerate(project['progress']):
            if e['statusAt'] <= status and (e['taskId'] not in chosen or
                                          (e['statusAt'], index) >= chosen[e['taskId']][0]):
                chosen[e['taskId']] = ((e['statusAt'], index), e)
    return {t['id']: (iso(chosen[t['id']][1].get('actualStart')), iso(chosen[t['id']][1].get('actualFinish')))
            if t['id'] in chosen else (None, None) for t in project['tasks']}


def summary(rows):
    starts = [a for a, b in rows if a is not None]
    finishes = [b for a, b in rows if b is not None]
    a = min(starts) if starts else None
    b = max(finishes) if rows and len(finishes) == len(rows) else None
    return dict(AS=a, AF=b, work=working(a, b))


def expected(project):
    dates = select(project)
    whole = summary(list(dates.values()))
    out = {('PROJECT', 'PROJECT'): dict(AS=whole['AS'], AF=whole['AF']),
           ('RESOURCE', 'R'): dict(AS=whole['AS'], AF=whole['AF'])}
    for t in project['tasks']:
        a, b = dates[t['id']]
        work = working(a, b)
        wall = Decimal(int((datetime.fromisoformat(b)-datetime.fromisoformat(a)).total_seconds()/60)) if a and b else None
        out['TASK', t['id']] = dict(AS=a, AF=b, work=work, wall=wall,
                                    state='미착수' if a is None else '진행' if b is None else '완료',
                                    globalAS=whole['AS'], globalAF=whole['AF'], native=work*2 if work is not None else None)
        out['ASSIGNMENT', t['id']] = dict(AS=a, AF=b, closed=b is not None, copy=a)
    out['SUMMARY', ''] = whole
    for g in project['wbsNodes']:
        out['SUMMARY', g['id']] = summary([dates[t['id']] for t in project['tasks'] if t['wbsId'] == g['id']])
    return out


partial, finished = expected(native), expected(complete)
assert partial['TASK', 'A']['AS'] == '2026-11-11T10:30:00'
assert partial['TASK', 'B']['AF'] is None and partial['TASK', 'M']['AS'] is None
assert finished['PROJECT', 'PROJECT']['AF'] == '2026-11-13T15:00:00'
assert select(native, '2026-11-11 18:00')['A'][0] == '2026-11-11T09:00:00'
assert select(native, '2026-11-17 18:00')['A'][0] == '2026-11-13T09:00:00'
udf = {v['externalId']: v for v in mapping['mapping'] if v['kind'] == 'udf'}
for field in udf.values():
    field['scope'] = {'RSRC': 'RESOURCE', 'ASSIGN': 'ASSIGNMENT'}.get(field['scope'], field['scope'])
task_ids = {v['xmlUid']: v['jobsId'] for v in mapping['mapping'] if v['kind'] == 'task'}
wbs_ids = {v['xmlUid']: v['jobsId'] for v in mapping['mapping'] if v['kind'] == 'wbs'}


def text(node, key, default=''):
    return node.findtext('p:'+key, default, ns)


xml_cells, formulae, missing_cells = 0, 0, 0
for filename, want in [('actuals.xml', partial), ('complete.xml', finished)]:
    root = ET.parse(args.root/filename).getroot()
    defs = {text(d, 'FieldID'): d for d in root.findall('p:ExtendedAttributes/p:ExtendedAttribute', ns)}
    assert len(defs) == 16
    for fid, definition in defs.items():
        formula = text(definition, 'Formula')
        if formula:
            formulae += 1
            assert '[field:' not in formula
        if udf[fid]['jobsId'] in ('AS', 'AF') and udf[fid]['scope'] == 'TASK':
            assert formula == ('[Actual Start]' if udf[fid]['jobsId'] == 'AS' else '[Actual Finish]')

    def values(row):
        out = {}
        for fid, definition in defs.items():
            raw = row.findtext('p:'+format(int(fid), 'x'), None, ns)
            for ea in row.findall('p:ExtendedAttribute', ns):
                if text(ea, 'FieldID') == fid:
                    raw = text(ea, 'Value', None)
            if raw is not None:
                kind = int(text(definition, 'CFType'))
                out[udf[fid]['scope'], udf[fid]['jobsId']] = Decimal(raw) if kind == 5 else raw == '1' if kind == 4 else raw
        return out

    def check(row, scope, owner):
        global xml_cells, missing_cells
        actual = values(row)
        for key, value in want[scope, owner].items():
            lookup_scope = 'TASK' if scope == 'SUMMARY' else scope
            assert actual.get((lookup_scope, key)) == value, (filename, scope, owner, key, actual, value)
            xml_cells += 1
            missing_cells += value is None

    for row in root.findall('p:Tasks/p:Task', ns):
        uid = text(row, 'UID')
        if uid == '0':
            check(row, 'PROJECT', 'PROJECT')
            check(row, 'SUMMARY', '')
        elif uid in wbs_ids:
            check(row, 'SUMMARY', wbs_ids[uid])
        else:
            check(row, 'TASK', task_ids[uid])
    for row in root.findall('p:Resources/p:Resource', ns):
        check(row, 'RESOURCE', 'R')
    for row in root.findall('p:Assignments/p:Assignment', ns):
        owner = task_ids[text(row, 'TaskUID')]
        actual = values(row)
        for key, value in want['ASSIGNMENT', owner].items():
            if key == 'copy' and ('ASSIGNMENT', key) not in actual:
                assert any(text(d, 'AutoRollDown') == '1' for d in defs.values())
                # Native rule is explicitly linked to the task's AS value.
                inherited = want['TASK', owner]['AS']
                assert inherited == value
            else:
                assert actual.get(('ASSIGNMENT', key)) == value, (filename, owner, key, actual, value)
            xml_cells += 1
            missing_cells += value is None
assert xml_cells == 122 and formulae == 28
assert '2000-01-01' not in (args.root/'actuals.xml').read_text(encoding='utf-8')

tables, columns, table = {}, [], None
for line in (args.root/'actuals.xer').read_text(encoding='utf-8-sig').splitlines():
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
    scope = {'TASK': 'TASK', 'PROJECT': 'PROJECT', 'RSRC': 'RESOURCE', 'TASKRSRC': 'ASSIGNMENT'}[d['table_name']]
    owner = native['tasks'][int(row['fk_id'])-1]['id'] if scope in ('TASK', 'ASSIGNMENT') else 'PROJECT' if scope == 'PROJECT' else 'R'
    value = partial[scope, owner][d['udf_type_label']]
    raw = row['udf_text'] or row['udf_number'] or row['udf_date']
    assert value is not None
    assert (raw == '예' if isinstance(value, bool) else Decimal(raw) if isinstance(value, Decimal) else raw) == value
    xer_cells += 1
expected_xer = sum(v is not None for (scope, owner), fields in partial.items() if scope != 'SUMMARY' for v in fields.values())
assert xer_cells == expected_xer

archives = json.loads((args.root/'archives.json').read_text(encoding='utf-8'))
archive_count = 0


def archived(node):
    global archive_count
    if isinstance(node, dict):
        if 'progress' in node and 'tasks' in node:
            assert select(node) == select(native)
            archive_count += 1
        elif 'progress' in node and 'input' in node:
            assert select(dict(node['input'], progress=node['progress'], statusAt=node['statusAt'])) == select(native)
            archive_count += 1
        for value in node.values():
            archived(value)
    elif isinstance(node, list):
        for value in node:
            archived(value)


archived(archives)
assert archive_count == 6
result = dict(status='PASS', schema=47, xml_cells=xml_cells, missing_xml_cells=missing_cells,
              formula_definitions=formulae, xer_static_values=xer_cells, archive_ledgers=archive_count,
              external_product_run=False)
if args.reports:
    import openpyxl
    from pypdf import PdfReader
    wb = openpyxl.load_workbook(args.root/'fields.xlsx', data_only=True)
    task = next(ws for ws in wb if ws.title.startswith('작업 사용자 필드'))
    ids = {t['id'] for t in native['tasks']}
    rows = {(r[0], r[2]): r for r in task.values if r[0] in ids}
    report_cells = 0

    def displayed(value):
        return '미정' if value is None else '예' if value is True else '아니요' if value is False else float(value) if isinstance(value, Decimal) else value

    for (scope, owner), fields in partial.items():
        if scope == 'TASK':
            for key, value in fields.items():
                assert rows[owner, 'field:'+key][4] == displayed(value), (owner, key, rows[owner, 'field:'+key][4], value)
                report_cells += 1
    sheet = next(ws for ws in wb if ws.title.startswith('XML 사용자 필드 요약'))
    rows = {(r[0], r[2]): r for r in sheet.values if r[2] in ('AS', 'AF', 'work')}
    for group in ('', 'G', 'H'):
        for key, value in partial['SUMMARY', group].items():
            assert rows[group, key][4] == displayed(value), (group, key, rows[group, key][4], value)
            report_cells += 1
    for title, scope, owner in [('프로젝트 사용자 필드', 'PROJECT', 'PROJECT'), ('자원 사용자 필드', 'RESOURCE', 'R')]:
        sheet = next(ws for ws in wb if ws.title.startswith(title))
        rows = {r[2]: r for r in sheet.values if isinstance(r[2], str) and r[2].startswith('field:')}
        for key, value in partial[scope, owner].items():
            assert rows['field:'+key][4] == displayed(value)
            report_cells += 1
    sheet = next(ws for ws in wb if ws.title.startswith('배정 사용자 필드'))
    rows = {(r[0], r[2]): r for r in sheet.values if r[0] in ids}
    for t in native['tasks']:
        for key, value in partial['ASSIGNMENT', t['id']].items():
            assert rows[t['id'], key][3] == displayed(value), (t['id'], key, rows[t['id'], key][3], value)
            report_cells += 1
    assert report_cells == 61
    all_text = [str(c) for ws in wb for row in ws.values for c in row if c is not None]
    assert any('실적 · 상태일 2026-11-13 18:00' in c for c in all_text)
    assert not any('2000-01-01' in c or '1999-01-01' in c for c in all_text)
    pdf = PdfReader(args.root/'fields.pdf')
    content = ''.join(''.join(page.extract_text() or '' for page in pdf.pages).split())
    assert 'ActualStart' in content and 'ActualFinish' in content and '10:30' in content and '실적·상태일' in content
    result.update(report_cells=report_cells, xlsx_sheets=len(wb.sheetnames), pdf_pages=len(pdf.pages))
if args.reader:
    import jpype
    import mpxj
    if not jpype.isJVMStarted():
        jpype.startJVM()
    reader = jpype.JClass('org.mpxj.mspdi.MSPDIReader')()
    reader.setIgnoreErrors(False)
    project = reader.read(str((args.root/'actuals.xml').resolve()))
    assert not list(project.getIgnoredErrors())
    result.update(mpxj_tasks=project.getTasks().size(), mpxj_assignments=project.getResourceAssignments().size(), mpxj_errors=0)
print(json.dumps(result, ensure_ascii=False, indent=2))
