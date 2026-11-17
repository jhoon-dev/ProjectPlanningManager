"""Independent civil-time oracle and XML/report validation; does not call the app."""
import argparse,json
from datetime import datetime,timedelta,time
from pathlib import Path
import xml.etree.ElementTree as ET
p=argparse.ArgumentParser();p.add_argument('root',type=Path);p.add_argument('--reports',action='store_true');p.add_argument('--reader',action='store_true');args=p.parse_args()
ns={'p':'http://schemas.microsoft.com/project'}
def text(node,key,default=''):return node.findtext('p:'+key,default,ns)
source=json.loads((args.root/'source.ppm').read_text(encoding='utf-8'));assert source['schemaVersion']==45
native=source['payload'];assert native['tasks'][0]['attributes'][0]['value']=='2000-01-01T00:00:00'
# Independent minute-by-minute standard calendar: weekdays 09-12 and 13-18.
def working(t):return t.weekday()<5 and (time(9)<=t.time()<time(12) or time(13)<=t.time()<time(18))
def next_work(t):
    while not working(t):t+=timedelta(minutes=1)
    return t
def advance(t,minutes):
    t=next_work(t);start=t
    for _ in range(minutes):t=next_work(t);t+=timedelta(minutes=1)
    return start,t
expected={}
for basis,begin in [('plan',native['startDate']+'T09:00:00'),('forecast',native['statusAt'].replace(' ','T'))]:
    current=datetime.fromisoformat(begin)
    for task in native['tasks']:
        a,b=advance(current,task['durationMinutes']);expected[basis,task['id']]=(a.isoformat(),b.isoformat());current=b
actual=json.loads((args.root/'schedule.json').read_text(encoding='utf-8'))
for row in actual:assert (row['start'].replace(' ','T')+(':00' if len(row['start'])==16 else ''),row['finish'].replace(' ','T')+(':00' if len(row['finish'])==16 else ''))==expected[row['basis'],row['id']],row
root=ET.parse(args.root/'plan.xml').getroot();defs={text(d,'FieldID'):d for d in root.findall('p:ExtendedAttributes/p:ExtendedAttribute',ns)}
assert len(defs)==9
labels={fid:text(d,'Alias') for fid,d in defs.items()}
def values(row):
    out={}
    for fid in defs:
        raw=row.findtext('p:'+format(int(fid),'x'),None,ns)
        for ea in row.findall('p:ExtendedAttribute',ns):
            if text(ea,'FieldID')==fid:raw=text(ea,'Value',None)
        if raw is not None:out[labels[fid]]=raw
    return out
checked=0;tasks={text(t,'UID'):t for t in root.findall('p:Tasks/p:Task',ns)};names={'설계':'A','검토':'B'}
project_start=expected['plan','A'][0];project_finish=expected['plan','B'][1]
for uid,row in tasks.items():
    v=values(row)
    if text(row,'Summary')=='1':
        a,b=project_start,project_finish;want={'s':a,'f':b,'elapsed':str(int((datetime.fromisoformat(b)-datetime.fromisoformat(a)).total_seconds()/60))}
        if uid=='0':want['pf']=project_finish
    else:
        a,b=expected['plan',names[text(row,'Name')]];minutes=int((datetime.fromisoformat(b)-datetime.fromisoformat(a)).total_seconds()/60);want={'s':a,'f':b,'elapsed':str(minutes),'global':project_finish,'native':str(minutes*2)}
    for key,val in want.items():assert v.get(key)==val,(uid,key,v,val);checked+=1
for row in root.findall('p:Resources/p:Resource',ns):assert values(row)['rf']==project_finish;checked+=1
for row in root.findall('p:Assignments/p:Assignment',ns):
    owner=names[text(tasks[text(row,'TaskUID')],'Name')];assert values(row)['as']==expected['plan',owner][0];checked+=1
    # copy receiver is absent because standard AutoRollDown carries its value.
    assert any(text(d,'AutoRollDown')=='1' for d in defs.values())
formulae={text(d,'Alias'):text(d,'Formula') for d in defs.values() if text(d,'Formula')}
assert formulae['s']=='[Start]' and formulae['f']=='[Finish]' and formulae['global']=='[Project.Finish]' and formulae['as']=='[Task.Start]'
tables={};columns=[];table=None
for line in (args.root/'plan.xer').read_text(encoding='utf-8-sig').splitlines():
    cells=line.split('\t')
    if cells[0]=='%T':table=cells[1];tables[table]=[]
    elif cells[0]=='%F':columns=cells[1:]
    elif cells[0]=='%R':tables[table].append(dict(zip(columns,cells[1:])))
types={r['udf_type_id']:r for r in tables['UDFTYPE']};xer_checked=0
for row in tables['UDFVALUE']:
    d=types[row['udf_type_id']];label=d['udf_type_label'];scope=d['table_name'];raw=row['udf_text'] or row['udf_number'] or row['udf_date']
    a,b=expected['plan',native['tasks'][int(row['fk_id'])-1]['id']] if scope in ('TASK','TASKRSRC') else (project_start,project_finish)
    minutes=str(int((datetime.fromisoformat(b)-datetime.fromisoformat(a)).total_seconds()/60))
    want={'s':a,'f':b,'global':project_finish,'elapsed':minutes,'native':str(int(minutes)*2),'pf':project_finish,'rf':project_finish,'as':a,'copy':a}[label]
    assert raw==want,(label,raw,want);xer_checked+=1
assert xer_checked==16
result={'schedule_rows':len(actual),'xml_values':checked,'formula_definitions':len(formulae),'raw_snapshot_preserved':True,'schema':45,'xer_static_values':xer_checked}
if args.reports:
    import openpyxl
    from pypdf import PdfReader
    wb=openpyxl.load_workbook(args.root/'forecast.xlsx',data_only=True)
    cells=[(ws.title,list(ws.values)) for ws in wb]
    all_values=[str(c) for _,rows in cells for row in rows for c in row if c is not None]
    for date in {v for pair in (expected['forecast','A'],expected['forecast','B']) for v in pair}:
        assert any(date in cell or date.replace('T',' ')[:16] in cell for cell in all_values),date
    assert any('XML 일정 참조 기준' in cell for cell in all_values) and any('상태일 예측' in cell for cell in all_values)
    assert not any('2000-01-01' in cell for cell in all_values)
    task_sheet=next(ws for ws in wb if ws.title.startswith('작업 사용자 필드'))
    task_rows={(r[0],r[2]):r for r in task_sheet.values if r[0] in ('A','B')};report_cells=0
    for task in native['tasks']:
        a,b=expected['forecast',task['id']];minutes=int((datetime.fromisoformat(b)-datetime.fromisoformat(a)).total_seconds()/60)
        for key,want in {'s':a,'f':b,'elapsed':minutes,'global':expected['forecast','B'][1],'native':minutes*2}.items():
            assert task_rows[task['id'],'field:'+key][4]==want,(task['id'],key);report_cells+=1
    summary_sheet=next(ws for ws in wb if ws.title.startswith('XML 사용자 필드 요약'))
    summary_rows={(r[0],r[2]):r for r in summary_sheet.values if r[2] in ('s','f','elapsed')}
    for group in ('','G'):
        for key,want in {'s':expected['forecast','A'][0],'f':expected['forecast','B'][1],'elapsed':1560}.items():
            assert summary_rows[group,key][4]==want and summary_rows[group,key][6]=='XML 요약 계산식';report_cells+=1
    for title,key in [('프로젝트 사용자 필드','field:pf'),('자원 사용자 필드','field:rf')]:
        sheet=next(ws for ws in wb if ws.title.startswith(title));row=next(r for r in sheet.values if r[2]==key)
        assert row[4]==expected['forecast','B'][1];report_cells+=1
    assignment_sheet=next(ws for ws in wb if ws.title.startswith('배정 사용자 필드'))
    for row in assignment_sheet.values:
        if row[0] in ('A','B'):
            assert row[3]==expected['forecast',row[0]][0];report_cells+=1
    assert report_cells==22
    pdf=PdfReader(args.root/'forecast.pdf');pdftext='\n'.join(page.extract_text() or '' for page in pdf.pages)
    compact=''.join(pdftext.split())
    assert '2026-11-16' in compact and '2026-11-17' in compact and '상태일예측' in compact
    result.update(xlsx_sheets=len(wb.sheetnames),report_cells=report_cells,pdf_pages=len(pdf.pages))
if args.reader:
    import jpype,mpxj
    if not jpype.isJVMStarted():jpype.startJVM()
    Reader=jpype.JClass('org.mpxj.mspdi.MSPDIReader');reader=Reader();reader.setIgnoreErrors(False)
    project=reader.read(str((args.root/'plan.xml').resolve()));assert not list(project.getIgnoredErrors())
    result.update(mpxj_tasks=project.getTasks().size(),mpxj_assignments=project.getResourceAssignments().size(),mpxj_errors=0)
print(json.dumps(result,ensure_ascii=False,indent=2))
