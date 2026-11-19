"""Independently enumerate the fixture's calendar-constrained forward/backward dates and inspect exports."""
import argparse
from datetime import datetime, timedelta
from decimal import Decimal
import json
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
assert source['schemaVersion'] == 51 and source['payload'] == native
tasks = {t['id']: t for t in native['tasks']}
calendars = {c['id']: c for c in native['calendars']}
minute = timedelta(minutes=1)
begin = datetime.fromisoformat(native['startDate']+'T00:00')
end = begin + timedelta(days=3)


def working(at, tid):
    c = calendars[tasks[tid]['calendarId']]
    return at.isoweekday() in c['weekdays'] and at.date().isoformat() not in c['holidays'] and any(
        p['begin'] <= at.hour*60+at.minute < p['end'] for p in c['periods'])


minutes = {tid: [] for tid in tasks}
points = {tid: set() for tid in tasks}
for tid in tasks:
    at = begin
    while at < end:
        if working(at, tid):
            minutes[tid].append(at)
            points[tid].update((at, at+minute))
        at += minute


def dates(tid, lower, duration):
    if duration == 0:
        at = min(at for at in points[tid] if at >= lower)
        return at, at
    execution = [at for at in minutes[tid] if at >= lower][:duration]
    assert len(execution) == duration
    return execution[0], execution[-1]+minute


def oracle(forecast):
    selected = {}
    if forecast:
        for e in native['progress']:
            if e['statusAt'] <= native['statusAt'] and (e['taskId'] not in selected or e['statusAt'] >= selected[e['taskId']]['statusAt']):
                selected[e['taskId']] = e
    early, late, done, durations = {}, {}, set(), {}
    order = []
    pending = set(tasks)
    while pending:
        ready = sorted(t for t in pending if all(p in early for p in tasks[t]['predecessors']))
        assert ready
        for tid in ready:
            order.append(tid)
            pending.remove(tid)
            t, e = tasks[tid], selected.get(tid)
            if e and e['actualFinish']:
                early[tid] = datetime.fromisoformat(e['actualStart']), datetime.fromisoformat(e['actualFinish'])
                done.add(tid)
                continue
            duration = e['remainingMinutes'] if e else t['durationMinutes']
            durations[tid] = duration
            lower = max([begin, *[early[p][1] for p in t['predecessors']],
                         *([datetime.fromisoformat(native['statusAt'])] if forecast else [])])
            early[tid] = dates(tid, lower, duration)
    finish = max(v[1] for v in early.values())
    for tid in reversed(order):
        if tid in done:
            continue
        successors = [t for t in tasks if tid in tasks[t]['predecessors'] and t not in done]
        feasible = []
        for at in sorted(points[tid]):
            if early[tid][0] <= at <= finish:
                trial = dates(tid, at, durations[tid])
                if trial[1] <= finish and all(trial[1] <= late[t][0] for t in successors):
                    feasible.append(trial)
        assert feasible, tid
        late[tid] = max(feasible)
    eligible = set(tasks)-done
    def canon(at): return at.isoformat(timespec='seconds')
    def fields(members):
        members = set(members)-done
        return (dict(ES=min(canon(early[t][0]) for t in members), EF=max(canon(early[t][1]) for t in members),
                     LS=min(canon(late[t][0]) for t in members), LF=max(canon(late[t][1]) for t in members))
                if members else dict.fromkeys(('ES','EF','LS','LF')))
    expected = {}
    project = fields(eligible)
    for tid in tasks:
        row = fields([tid])
        row['delay'] = Decimal(sum(early[tid][0] <= at < late[tid][0] for at in minutes[tid])) if tid in eligible else None
        expected['TASK',tid] = row
    expected['PROJECT','PROJECT'] = project
    for r in native['resources']:
        expected['RESOURCE',r['id']] = project
    for a in native['assignments']:
        row = fields([a['taskId']])
        row['PF'] = project['LF']
        expected['ASSIGNMENT',a['taskId']+'/'+a['resourceId']] = row
    for group in ['',*[g['id'] for g in native['wbsNodes']]]:
        expected['SUMMARY',group] = fields(t for t in tasks if not group or tasks[t]['wbsId'] == group)
    return expected


planned, forecast = oracle(False), oracle(True)
assert planned['TASK','C']['LS'] == '2026-11-12T09:04:00'
assert forecast['TASK','B']['ES'] == '2026-11-12T09:07:00' and forecast['TASK','B']['LS'] == '2026-11-13T09:00:00'
assert all(forecast['TASK','A'][k] is None for k in ('ES','EF','LS','LF','delay'))
ns = {'p':'http://schemas.microsoft.com/project'}
def text(node,key,default=''): return node.findtext('p:'+key,default,ns)
def stamp(raw): return datetime.fromisoformat(raw).isoformat(timespec='seconds')
udf = {m['externalId']:dict(m,scope={'RSRC':'RESOURCE','ASSIGN':'ASSIGNMENT'}.get(m['scope'],m['scope']))
       for m in mapping['mapping'] if m['kind']=='udf'}
ids = {kind:{m['xmlUid']:m['ppmId'] for m in mapping['mapping'] if m['kind']==kind}
       for kind in ('task','resource','assignment','wbs')}
root = ET.parse(args.root/'timing.xml').getroot()
definitions = {text(d,'FieldID'):d for d in root.findall('p:ExtendedAttributes/p:ExtendedAttribute',ns)}
for fid,d in definitions.items():
    assert any(x in text(d,'Formula') for x in ('Early','Late')) and '[field:' not in text(d,'Formula')
xml_cells = 0
def check_xml(row,scope,owner):
    global xml_cells
    raw = {text(e,'FieldID'):text(e,'Value') for e in row.findall('p:ExtendedAttribute',ns)}
    for fid in definitions:
        value = row.findtext('p:'+format(int(fid),'x'),None,ns)
        if value is not None: raw[fid] = value
    for key,want in planned[scope,owner].items():
        fid = next(fid for fid,d in udf.items() if d['scope']==('TASK' if scope=='SUMMARY' else scope) and d['ppmId']==key)
        value = raw.get(fid)
        if value is not None: value = Decimal(value) if key=='delay' else stamp(value)
        assert value == want,(scope,owner,key,value,want)
        xml_cells += 1
for row in root.findall('p:Tasks/p:Task',ns):
    uid = text(row,'UID')
    if uid == '0':
        check_xml(row,'PROJECT','PROJECT'); check_xml(row,'SUMMARY','')
    elif uid in ids['wbs']: check_xml(row,'SUMMARY',ids['wbs'][uid])
    else: check_xml(row,'TASK',ids['task'][uid])
for row in root.findall('p:Resources/p:Resource',ns): check_xml(row,'RESOURCE',ids['resource'][text(row,'UID')])
for row in root.findall('p:Assignments/p:Assignment',ns): check_xml(row,'ASSIGNMENT',ids['assignment'][text(row,'UID')])
tables,columns,table = {},[],None
for line in (args.root/'timing.xer').read_text(encoding='utf-8-sig').splitlines():
    v=line.split('\t')
    if v[0]=='%T': table=v[1]; tables[table]=[]
    elif v[0]=='%F': columns=v[1:]
    elif v[0]=='%R': tables[table].append(dict(zip(columns,v[1:])))
types={r['udf_type_id']:r for r in tables['UDFTYPE']}
xer_cells=0
for row in tables['UDFVALUE']:
    d=types[row['udf_type_id']]; key=d['udf_type_label']
    scope={'TASK':'TASK','PROJECT':'PROJECT','RSRC':'RESOURCE','TASKRSRC':'ASSIGNMENT'}[d['table_name']]
    owner=('PROJECT' if scope=='PROJECT' else native['tasks'][int(row['fk_id'])-1]['id'] if scope=='TASK' else
           native['resources'][int(row['fk_id'])-1]['id'] if scope=='RESOURCE' else
           native['assignments'][int(row['fk_id'])-1]['taskId']+'/'+native['assignments'][int(row['fk_id'])-1]['resourceId'])
    # Start/Finish UDFs use XER text projections to preserve their time component.
    value=Decimal(row['udf_number']) if key=='delay' else stamp(row['udf_text'] or row['udf_date'])
    assert value==planned[scope,owner][key],(scope,owner,key,value,planned[scope,owner][key])
    xer_cells+=1
assert xer_cells==sum(len(f) for (s,_),f in planned.items() if s!='SUMMARY')
result=dict(status='PASS',schema=51,xml_plan_cells=xml_cells,xer_plan_cells=xer_cells,formula_definitions=len(definitions),
            independent_oracle='civil-minute forward/backward enumeration',external_product_run=False)
if args.reports:
    import openpyxl
    from pypdf import PdfReader
    wb=openpyxl.load_workbook(args.root/'fields.xlsx',data_only=True); report_cells=missing=0
    for title,scope in [('작업 사용자 필드','TASK'),('프로젝트 사용자 필드','PROJECT'),('자원 사용자 필드','RESOURCE')]:
        sheet=next(s for s in wb if s.title.startswith(title))
        actual={(r[0],r[2]):r[4] for r in sheet.values if isinstance(r[2],str) and r[2].startswith('field:')}
        for (s,owner),fields in forecast.items():
            if s==scope:
                for key,want in fields.items():
                    value=actual[native['documentId'] if scope=='PROJECT' else owner,'field:'+key]
                    assert value==('미정' if want is None else float(want) if key=='delay' else want),(s,owner,key,value,want)
                    report_cells+=1; missing+=want is None
    sheet=next(s for s in wb if s.title.startswith('배정 사용자 필드'))
    actual={(r[0]+'/'+r[1],r[2]):r[3] for r in sheet.values if r[0] in tasks}
    for (scope,owner),fields in forecast.items():
        if scope=='ASSIGNMENT':
            for key,want in fields.items():
                assert actual[owner,key]==('미정' if want is None else want),(owner,key,actual[owner,key],want)
                report_cells+=1; missing+=want is None
    sheet=next(s for s in wb if s.title.startswith('XML 사용자 필드 요약'))
    actual={(r[0],r[2]):r[4] for r in sheet.values if r[2] in ('ES','EF','LS','LF')}
    for (scope,owner),fields in forecast.items():
        if scope=='SUMMARY':
            for key,want in fields.items():
                assert actual[owner,key]==('미정' if want is None else want),(owner,key,actual[owner,key],want)
                report_cells+=1; missing+=want is None
    pdf=PdfReader(args.root/'fields.pdf'); content=''.join(''.join(p.extract_text() or '' for p in pdf.pages).split())
    assert 'Early/Late' in content and '2026-11-13T09:00:00' in content
    result.update(report_forecast_cells=report_cells,missing_forecast_cells=missing,xlsx_sheets=len(wb.sheetnames),pdf_pages=len(pdf.pages))
if args.reader:
    import jpype
    import mpxj
    if not jpype.isJVMStarted(): jpype.startJVM()
    reader=jpype.JClass('org.mpxj.mspdi.MSPDIReader')(); reader.setIgnoreErrors(False)
    project=reader.read(str((args.root/'timing.xml').resolve())); assert not list(project.getIgnoredErrors())
    result.update(mpxj_tasks=project.getTasks().size(),mpxj_assignments=project.getResourceAssignments().size(),mpxj_errors=0)
print(json.dumps(result,ensure_ascii=False,indent=2))
