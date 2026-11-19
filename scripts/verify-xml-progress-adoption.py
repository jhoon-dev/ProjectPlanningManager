"""Compare explicit XML task observations with native append-only entries, formula exports and real reports."""
import argparse
from datetime import datetime
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

parser=argparse.ArgumentParser()
parser.add_argument('root',type=Path)
parser.add_argument('--reports',action='store_true')
parser.add_argument('--reader',action='store_true')
args=parser.parse_args()
ns={'p':'http://schemas.microsoft.com/project'}
raw=(args.root/'original.xml').read_bytes()
digest=hashlib.sha256(raw).hexdigest()
original=ET.fromstring(raw)
native=json.loads((args.root/'native.json').read_text(encoding='utf-8'))
source=json.loads((args.root/'source.ppm').read_text(encoding='utf-8'))
conversion=json.loads((args.root/'conversion.json').read_text(encoding='utf-8'))
assert source['schemaVersion']==48 and source['payload']==native
assert conversion['source']['sha256']==digest
adoption=conversion['xmlProgressAdoption']
assert adoption['sourceSha256']==digest and adoption['mode']=='new-copy-task-progress-v1'
assert native['statusAt']==adoption['statusAt']=='2026-11-12 12:00'
assert native['revision']==2 and conversion['revision']=='2'
assert not native.get('periodActuals',[]) and not native.get('costEntries',[]) and not native.get('baselines',[])
def text(node,key,default=None):return node.findtext('p:'+key,default,ns)
def stamp(raw):return None if raw in (None,'','NA') else datetime.fromisoformat(raw).strftime('%Y-%m-%d %H:%M')
def duration(raw):
    m=re.fullmatch(r'PT(?:(\d+)H)?(?:(\d+)M)?(?:(\d+)S)?',raw)
    assert m,raw
    h,minute,sec=[int(v or 0) for v in m.groups()]
    assert sec%60==0
    return h*60+minute+sec//60
tasks={text(t,'UID'):t for t in original.findall('p:Tasks/p:Task',ns)}
mapping={m['xmlUid']:m['ppmId'] for m in conversion['mapping'] if m['kind']=='task'}
selected=adoption['selected']
assert len(selected)==len(native['progress'])==4
assert len({e['id'] for e in native['progress']})==4
entries={e['id']:e for e in native['progress']}
expected={tid:dict(AS=None,AF=None,AD=None,AW=None) for tid in mapping.values()}
checks=0
for row in selected:
    uid,tid,eid=row['xmlUid'],row['taskId'],row['entryId']
    t,e=tasks[uid],entries[eid]
    assert mapping[uid]==tid==e['taskId']
    assert e['actualStart']==stamp(text(t,'ActualStart')) and e['actualFinish']==stamp(text(t,'ActualFinish'))
    assert e['actualDurationMinutes']==duration(text(t,'ActualDuration'))
    assert e['remainingMinutes']==duration(text(t,'RemainingDuration'))
    assert e['planDurationMinutes']==duration(text(t,'Duration'))
    assert e['statusAt']==adoption['statusAt'] and e['recordedAt']==adoption['recordedAt']
    assert e['reporter']==adoption['reporter'] and e['recordedRevision']==2 and not e['previousEntryId']
    assert e['sourceDocumentId']==native['documentId']
    assert e['reason']==f'[XML-PROGRESS sha256={digest} uid={uid}] progress.xml · '+adoption['reason']
    expected[tid]=dict(AS=e['actualStart'].replace(' ','T')+':00' if e['actualStart'] else None,
                       AF=e['actualFinish'].replace(' ','T')+':00' if e['actualFinish'] else None,
                       AD=Decimal(e['actualDurationMinutes']),AW=None)
    checks+=12
assert sorted(e['actualDurationMinutes'] for e in entries.values())==[0,0,60,120]
assert sorted(e['remainingMinutes'] for e in entries.values())==[0,0,60,180]
assert len(conversion['taskProgressReview']['rows'])==9
assert any('ActualWork' in d['path'] for d in conversion['diagnostics'])
assert any('ActualCost' in d['path'] for d in conversion['diagnostics'])

export=json.loads((args.root/'export-mapping.json').read_text(encoding='utf-8'))
udf={m['externalId']:m for m in export['mapping'] if m['kind']=='udf'}
field_names={f['id']:f['name'] for f in native['customFields']}
ids={m['xmlUid']:m['ppmId'] for m in export['mapping'] if m['kind']=='task'}
result_xml=ET.parse(args.root/'fields.xml').getroot()
xml_cells=missing=0
for t in result_xml.findall('p:Tasks/p:Task',ns):
    uid=text(t,'UID')
    if uid not in ids:continue
    values={text(v,'FieldID'):text(v,'Value') for v in t.findall('p:ExtendedAttribute',ns)}
    for fid,m in udf.items():
        if m['scope']!='TASK':continue
        name=field_names[m['ppmId']]
        raw_value=values.get(fid)
        value=Decimal(raw_value) if raw_value is not None and name in ('AD','AW') else raw_value
        want=expected[ids[uid]][name]
        assert value==want,(uid,name,value,want)
        xml_cells+=1;missing+=want is None
result=dict(status='PASS',schema=48,native_entries=4,native_checks=checks,xml_formula_cells=xml_cells,
            missing_formula_cells=missing,source_sha256=digest,external_product_run=False)
if args.reports:
    import openpyxl
    from pypdf import PdfReader
    wb=openpyxl.load_workbook(args.root/'fields.xlsx',data_only=True)
    sheet=next(s for s in wb if s.title.startswith('작업 사용자 필드'))
    values={(r[0],r[2]):r[4] for r in sheet.values if isinstance(r[2],str) and r[2].startswith('field:')}
    for tid,fields in expected.items():
        for fid,name in field_names.items():
            want=fields[name]
            assert values[tid,'field:'+fid]==('미정' if want is None else float(want) if isinstance(want,Decimal) else want)
    def audit(book):
        s=next(s for s in book if s.title.startswith('XML 채택 작업의 진척 원장'))
        return [r for r in s.values if isinstance(r[1],str) and r[0] in expected and isinstance(r[5],(int,float))]
    rows=audit(wb)
    assert len(rows)==4
    for r in rows:
        e=entries[r[1]]
        assert (r[0],r[2],r[5],r[6],r[7],r[8],r[10],r[11])==(e['taskId'],e['statusAt'],e['actualDurationMinutes'],e['remainingMinutes'],e['reporter'],e['recordedAt'],'선택',e['reason'])
    corrected=openpyxl.load_workbook(args.root/'corrected.xlsx',data_only=True)
    corrected_rows=audit(corrected)
    assert len(corrected_rows)==5
    old_ids=set(entries)
    appended=[r for r in corrected_rows if r[1] not in old_ids]
    assert len(appended)==1 and appended[0][5:7]==(50,150) and appended[0][9] in old_ids
    assert next(r for r in corrected_rows if r[1]==appended[0][9])[10] in (None,'') and appended[0][10]=='선택'
    pdf=PdfReader(args.root/'fields.pdf')
    content=''.join(''.join(p.extract_text() or '' for p in pdf.pages).split())
    assert digest in content and 'XML채택작업의진척원장' in content
    result.update(report_formula_cells=sum(len(v) for v in expected.values()),audit_entries=4,corrected_audit_entries=5,
                  xlsx_sheets=len(wb.sheetnames),pdf_pages=len(pdf.pages))
if args.reader:
    import jpype
    import mpxj
    if not jpype.isJVMStarted():jpype.startJVM()
    reader=jpype.JClass('org.mpxj.mspdi.MSPDIReader')();reader.setIgnoreErrors(False)
    project=reader.read(str((args.root/'fields.xml').resolve()));assert not list(project.getIgnoredErrors())
    result.update(mpxj_tasks=project.getTasks().size(),mpxj_assignments=project.getResourceAssignments().size(),mpxj_errors=0)
print(json.dumps(result,ensure_ascii=False,indent=2))
