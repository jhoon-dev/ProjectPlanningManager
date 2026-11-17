"""Independent Decimal rounding, OLE day folding and exact time parsing."""
import argparse
from datetime import datetime,timedelta
from decimal import Decimal,ROUND_HALF_EVEN
import hashlib
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

parser=argparse.ArgumentParser();parser.add_argument('root',type=Path);args=parser.parse_args();root=args.root
load=lambda n:json.loads((root/n).read_text(encoding='utf-8-sig'))
anchor=datetime(1899,12,30)
def normalized(d):
    return d.strftime('%Y-%m-%dT%H:%M:%S')+(f'.{d.microsecond:06d}'.rstrip('0') if d.microsecond else '')
def serial(h,m,s):
    total=h*3600+m*60+s
    day=(1 if total>=0 else -1)*(abs(total)//86400)
    return normalized(anchor+timedelta(days=day,seconds=abs(total)%86400))
def time_value(s):
    value=s.strip()
    if re.match(r'^\d{4}-\d{2}-\d{2}',value):
        datetime.fromisoformat(value[:10])
        if len(value)==10:return normalized(anchor)
        assert value[10] in 'T '
        value=value[11:].strip()
    match=re.fullmatch(r'(\d{1,2})(?::(\d{1,2})(?::(\d{1,2})(?:\.(\d{1,6}))?)?)?\s*([aApP][mM])?',value)
    assert match,value
    h,m,s,f,period=match.groups();h=int(h);m=int(m or 0);s=int(s or 0)
    assert m<60 and s<60 and h<(13 if period else 24)
    if period:h=h%12+(12 if period.lower()=='pm' else 0)
    return normalized(anchor.replace(hour=h,minute=m,second=s,microsecond=int((f or '').ljust(6,'0') or 0)))
serials,values,cases=[load(n) for n in ('serials.json','values.json','cases.json')]
for row in serials:assert row['value']==serial(row['hour'],row['minute'],row['second']),row
for row in values:assert row['value']==time_value(row['input']),row
rounded_cases=0
for row in cases:
    match=re.fullmatch(r'TimeSerial\((-?[\d.]+),(-?[\d.]+),(-?[\d.]+)\)',row['source'])
    if match:
        parts=[int(Decimal(n).quantize(Decimal(1),rounding=ROUND_HALF_EVEN)) for n in match.groups()]
        assert row['value']==serial(*parts),row
        rounded_cases+=1
native=load('native.json')
expected={
    '생성시각':['1899-12-30T03:15:00','1899-12-29T01:00:00'],
    '추출시각':['1899-12-30T09:30:12.123456','1899-12-30T23:59:59.654321'],
    '문자시각':['1899-12-30T14:24:00','1899-12-30T00:30:00'],
    '시각표시':['14:24:00','00:30:00'],'음수시각':['1899-12-29T01:00:00']*2,
    '추출시간수':['9','23'],'시각혼합식':['90','230'],'시각요약':['1899-12-30T07:00:00']*2}
extra={'프로젝트시각':['1899-12-30T01:15:00'],'자원시각':['1899-12-30T16:35:17'],
       '배정시각':['1899-12-30T03:15:00','1899-12-30T01:00:00'],'시각전파':expected['추출시각']}
xml=ET.parse(root/'times.xml').getroot();ns={'p':'http://schemas.microsoft.com/project'}
text=lambda r,n:r.findtext('p:'+n,default='',namespaces=ns)
definitions={text(d,'FieldID'):d for d in xml.findall('p:ExtendedAttributes/p:ExtendedAttribute',ns)}
tasks=[t for t in xml.findall('p:Tasks/p:Task',ns) if text(t,'Summary')!='1'];xml_cells=0
for i,t in enumerate(tasks):
    attrs={text(definitions[text(a,'FieldID')],'Alias'):text(a,'Value') for a in t.findall('p:ExtendedAttribute',ns)}
    for name,values_expected in expected.items():
        raw=attrs[name];actual=normalized(datetime.fromisoformat(raw)) if 'T' in values_expected[i] else raw
        assert actual==values_expected[i],(name,actual,values_expected[i]);xml_cells+=1
for selector,names in [('p:Tasks/p:Task',['프로젝트시각']),('p:Resources/p:Resource',['자원시각']),('p:Assignments/p:Assignment',['배정시각','시각전파'])]:
    for name in names:
        fid=next(fid for fid,d in definitions.items() if text(d,'Alias')==name);found=[]
        for index,row in enumerate(xml.findall(selector,ns)):
            if name=='프로젝트시각' and text(row,'UID')!='0':continue
            ea=next((a for a in row.findall('p:ExtendedAttribute',ns) if text(a,'FieldID')==fid),None)
            raw=text(ea,'Value') if ea is not None else row.findtext('p:'+format(int(fid),'x'),None,ns)
            if raw is None and name=='시각전파':
                source=next(d for d in definitions.values() if text(d,'SecondaryPID')==fid)
                assert text(source,'AutoRollDown')=='1' and text(source,'Alias')=='추출시각'
                task_attrs={a['key']:a['value'] for a in native['tasks'][index]['attributes']}
                raw=time_value(task_attrs['field:when'])
            found.append(normalized(datetime.fromisoformat(raw)))
        assert found==extra[name],(name,found);xml_cells+=len(found)
tables,table,cols={},None,[]
for line in (root/'times.xer').read_text(encoding='utf-8').splitlines():
    fields=line.split('\t')
    if fields[0]=='%T':table=fields[1];tables[table]=[]
    elif fields[0]=='%F':cols=fields[1:]
    elif fields[0]=='%R':tables[table].append(dict(zip(cols,fields[1:])))
types={r['udf_type_id']:r for r in tables['UDFTYPE']};xer_cells=0
for name,wanted in dict(expected,**extra).items():
    actual=[r['udf_text'] or r['udf_date'] or r['udf_number'] for r in tables['UDFVALUE'] if types[r['udf_type_id']]['udf_type_label']==name]
    if 'T' in wanted[0]:actual=[normalized(datetime.fromisoformat(a)) for a in actual]
    assert actual==wanted,(name,actual,wanted);xer_cells+=len(actual)
import openpyxl
from pypdf import PdfReader
book=openpyxl.load_workbook(root/'fields.xlsx',data_only=True)
sheet=next(s for s in book if s.title.startswith('작업 사용자 필드'))
rows={(r[0],r[2]):r for r in sheet.values if r[0] in ('A','B')}
for field in native['customFields']:
    if field['name'] not in expected:continue
    for i,task in enumerate(native['tasks']):
        raw=rows[task['id'],'field:'+field['id']][4];want=expected[field['name']][i]
        actual=normalized(datetime.fromisoformat(raw)) if 'T' in want else str(raw)
        assert actual==want,(field,actual,want)
pdf=PdfReader(root/'fields.pdf');content=''.join(''.join(p.extract_text() or '' for p in pdf.pages).split())
assert all(s in content for s in ('TimeSerial','TimeValue','생성시각','1899-12-30'))
archive=load('archived.json');assert archive['baselines'] and archive['scenarios'] and archive['currencyConversions']
print(json.dumps(dict(status='PASS',schema=56,serial_vectors=len(serials),time_vectors=len(values),scalar_cases=len(cases),
                     rounding_cases=rounded_cases,xml_cells=xml_cells,xml_inherited_cells=2,xer_static_values=xer_cells,
                     formula_definitions=sum(bool(text(d,'Formula')) for d in definitions.values()),report_rows=len(rows),
                     pdf_pages=len(pdf.pages),native_file_sha256=hashlib.sha256((root/'source.ppm').read_bytes()).hexdigest(),
                     external_product_run=False),ensure_ascii=False,indent=2))
