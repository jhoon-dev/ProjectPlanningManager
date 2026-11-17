"""Independent fixed-profile Project date and duration conversion checks."""
import argparse
from datetime import datetime,date
from decimal import Decimal,ROUND_HALF_UP,localcontext
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET

parser=argparse.ArgumentParser();parser.add_argument('root',type=Path);root=parser.parse_args().root
load=lambda n:json.loads((root/n).read_text(encoding='utf-8-sig'))
months=['January','February','March','April','May','June','July','August','September','October','November','December']
days=['Monday','Tuesday','Wednesday','Thursday','Friday','Saturday','Sunday']
dates,durations=load('dates.json'),load('durations.json')
for row in dates:
    d=datetime.fromisoformat(row['input']);short=f'{d.month}/{d.day}/{d.year%100:02d}';long=f'{d.month}/{d.day}/{d.year:04d}'
    t=f'{d.hour%12 or 12}:{d.minute:02d} '+('AM' if d.hour<12 else 'PM')
    m=months[d.month-1];wd=days[d.weekday()][:3];suffix=f"{m[:3]} {d.day}, '{d.year%100:02d}"
    jan=date(d.year,1,1);week=((d.date()-jan).days+(jan.weekday()+1)%7)//7+1
    weektext=f'W{week:02d}/{(d.weekday()+1)%7+1}'
    values=[short+' '+t,short,f'{m} {d.day}, {d.year:04d} '+t,f'{m} {d.day}, {d.year:04d}',
      f'{m[:3]} {d.day} '+t,suffix,f'{m} {d.day}',f'{m[:3]} {d.day}',wd+' '+short+' '+t,wd+' '+short,
      wd+' '+suffix,wd+' '+t,f'{d.month}/{d.day}',str(d.day),t,f'{wd} {m[:3]} {d.day}',f'{wd} {d.month}/{d.day}',
      f'{wd} {d.day}',weektext,weektext+f'/{d.year%100:02d} '+t,long]
    expected=values[0 if row['format']==255 else row['format']]
    assert row['output']==expected,(row,expected)
factors={3:(1,'m'),4:(1,'em'),5:(60,'h'),6:(60,'eh'),7:(480,'d'),8:(1440,'ed'),9:(2400,'w'),10:(10080,'ew'),11:(9600,'mo'),12:(43200,'emo')}
with localcontext() as context:
    context.prec=64
    for row in durations:
        fmt=row['format'];factor,label=factors[fmt-32 if fmt>=35 else fmt]
        scaled=(Decimal(row['input'])/factor).quantize(Decimal(1),rounding=ROUND_HALF_UP)
        value=format(scaled/1000000,'f')
        if '.' in value:value=value.rstrip('0').rstrip('.')
        if value=='-0':value='0'
        expected=value+' '+label+('?' if fmt>=35 else '')
        assert row['output']==expected,(row,expected)
        assert int(row['parsed'])==int(scaled)*factor,(row,factor)
assert len(dates)==2816 and len(durations)==10240
native=load('native.json')
expected={'Project날짜값':['2026-11-14T09:30:12.123456','2024-02-29T23:05:06.123456'],
 'Project날짜표시':['11/14/2026','2/29/2024'],'Project기본표시':['11/14/26 9:30 AM','2/29/24 11:05 PM'],
 'Project시간표시':['1.5 h','48 h'],'Project경과표시':['0.0625 ed?','2 ed?'],'Project월표시':['0.009375 mo','0.3 mo'],
 'Project분추출':['90','2880'],'Project숫자표시':['2.5 m','3.5 m'],'Project변환혼합':['180','5760'],
 'Project변환요약':['11/14/2026']*2}
extra={'프로젝트날짜표시':['11/14/26'],'자원기간표시':['5 d'],'배정기간표시':['1.5 eh','48 eh'],'Project변환전파':['90','2880']}
xml=ET.parse(root/'numbers.xml').getroot();ns={'p':'http://schemas.microsoft.com/project'}
text=lambda r,n:r.findtext('p:'+n,default='',namespaces=ns)
definitions={text(d,'FieldID'):d for d in xml.findall('p:ExtendedAttributes/p:ExtendedAttribute',ns)}
tasks=[t for t in xml.findall('p:Tasks/p:Task',ns) if text(t,'Summary')!='1'];xml_cells=0
for i,t in enumerate(tasks):
    attrs={text(definitions[text(a,'FieldID')],'Alias'):text(a,'Value') for a in t.findall('p:ExtendedAttribute',ns)}
    for name,wanted in expected.items():assert attrs[name]==wanted[i],(name,attrs[name],wanted[i]);xml_cells+=1
for selector,names in [('p:Tasks/p:Task',['프로젝트날짜표시']),('p:Resources/p:Resource',['자원기간표시']),('p:Assignments/p:Assignment',['배정기간표시','Project변환전파'])]:
    for name in names:
        fid=next(fid for fid,d in definitions.items() if text(d,'Alias')==name);found=[]
        for i,row in enumerate(xml.findall(selector,ns)):
            if name=='프로젝트날짜표시' and text(row,'UID')!='0':continue
            ea=next((a for a in row.findall('p:ExtendedAttribute',ns) if text(a,'FieldID')==fid),None)
            raw=text(ea,'Value') if ea is not None else row.findtext('p:'+format(int(fid),'x'),None,ns)
            if raw is None and name=='Project변환전파':
                source=next(d for d in definitions.values() if text(d,'SecondaryPID')==fid)
                assert text(source,'AutoRollDown')=='1' and text(source,'Alias')=='Project분추출'
                raw=extra[name][i]
            found.append(raw)
        assert found==extra[name],(name,found);xml_cells+=len(found)
tables,table,cols={},None,[]
for line in (root/'numbers.xer').read_text(encoding='utf-8').splitlines():
    fields=line.split('\t')
    if fields[0]=='%T':table=fields[1];tables[table]=[]
    elif fields[0]=='%F':cols=fields[1:]
    elif fields[0]=='%R':tables[table].append(dict(zip(cols,fields[1:])))
types={r['udf_type_id']:r for r in tables['UDFTYPE']};xer_cells=0
for name,wanted in dict(expected,**extra).items():
    actual=[r['udf_text'] or r['udf_date'] or r['udf_number'] for r in tables['UDFVALUE'] if types[r['udf_type_id']]['udf_type_label']==name]
    assert actual==wanted,(name,actual,wanted);xer_cells+=len(actual)
import openpyxl
from pypdf import PdfReader
book=openpyxl.load_workbook(root/'fields.xlsx',data_only=True)
sheet=next(s for s in book if s.title.startswith('작업 사용자 필드'))
rows={(r[0],r[2]):r for r in sheet.values if r[0] in ('A','B')}
for field in native['customFields']:
    if field['name'] not in expected:continue
    for i,task in enumerate(native['tasks']):
        actual=rows[task['id'],'field:'+field['id']][4];want=expected[field['name']][i]
        assert str(actual)==want,(field,repr(actual),repr(want))
pdf=PdfReader(root/'fields.pdf');content=''.join(''.join(p.extract_text() or '' for p in pdf.pages).split())
assert all(s in content for s in ('ProjDateValue','ProjDateConv','ProjDurConv'))
archive=load('archived.json');assert archive['baselines'] and archive['scenarios'] and archive['currencyConversions']
print(json.dumps(dict(status='PASS',schema=61,date_values=128,date_formats=len(dates),duration_formats=len(durations),
 scalar_cases=len(load('cases.json')),xml_cells=xml_cells,xml_inherited_cells=2,xer_static_values=xer_cells,
 formula_definitions=sum(bool(text(d,'Formula')) for d in definitions.values()),report_rows=len(rows),pdf_pages=len(pdf.pages),
 native_file_sha256=hashlib.sha256((root/'source.ppm').read_bytes()).hexdigest(),external_product_run=False),ensure_ascii=False,indent=2))
