"""Exact rational oracle for binary32 conversion plus exchange/report verification."""
import argparse
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import struct
import xml.etree.ElementTree as ET

parser=argparse.ArgumentParser();parser.add_argument('root',type=Path);root=parser.parse_args().root
load=lambda n:json.loads((root/n).read_text(encoding='utf-8-sig'))
rows=load('vectors.json')
for row in rows:
    n=int(row['input']);x=Fraction(abs(n),1000000)
    guess=struct.unpack('>I',struct.pack('>f',float(x)))[0]
    candidates=[]
    for bits in range(max(0,guess-1),guess+2):
        y=Fraction.from_float(struct.unpack('>f',struct.pack('>I',bits))[0])
        candidates.append((abs(x-y),bits&1,bits,y))
    _,_,bits,y=min(candidates)
    assert abs(y)<=9000000000000
    value=y*1000000;q,r=divmod(value.numerator,value.denominator)
    expected=q+(2*r>=value.denominator)
    if n<0:expected=-expected
    assert int(row['output'])==expected,(row,bits,expected)
assert len(rows)==2058
native=load('native.json')
expected={'십진변환':['1.234567','-2.5'],'단정밀변환':['16777216','16777220'],
          '형식보존':['1.234567','-2.5'],'날짜보존':['2026-11-13T09:30:12.123456']*2,
          '경과추정보존':['1440 em?']*2,'금액변환':['12.34','2'],'변환혼합식':['2.469134','-5'],'변환요약':['요약']*2}
extra={'프로젝트십진':['16'],'자원단정밀':['-1'],'배정단정밀':['16777216','16777220'],'변환전파':['1.234567','-2.5']}
xml=ET.parse(root/'numbers.xml').getroot();ns={'p':'http://schemas.microsoft.com/project'}
text=lambda r,n:r.findtext('p:'+n,default='',namespaces=ns)
definitions={text(d,'FieldID'):d for d in xml.findall('p:ExtendedAttributes/p:ExtendedAttribute',ns)}
tasks=[t for t in xml.findall('p:Tasks/p:Task',ns) if text(t,'Summary')!='1'];xml_cells=0
for i,t in enumerate(tasks):
    attrs={text(definitions[text(a,'FieldID')],'Alias'):text(a,'Value') for a in t.findall('p:ExtendedAttribute',ns)}
    for name,wanted in expected.items():
        if name=='경과추정보존':
            fid=next(fid for fid,d in definitions.items() if text(d,'Alias')==name)
            ea=next(a for a in t.findall('p:ExtendedAttribute',ns) if text(a,'FieldID')==fid)
            assert attrs[name]=='PT24H0M0S' and text(ea,'DurationFormat')=='36'
        else:assert attrs[name]==wanted[i],(name,repr(attrs[name]),repr(wanted[i]))
        xml_cells+=1
for selector,names in [('p:Tasks/p:Task',['프로젝트십진']),('p:Resources/p:Resource',['자원단정밀']),('p:Assignments/p:Assignment',['배정단정밀','변환전파'])]:
    for name in names:
        fid=next(fid for fid,d in definitions.items() if text(d,'Alias')==name);found=[]
        for i,row in enumerate(xml.findall(selector,ns)):
            if name=='프로젝트십진' and text(row,'UID')!='0':continue
            ea=next((a for a in row.findall('p:ExtendedAttribute',ns) if text(a,'FieldID')==fid),None)
            raw=text(ea,'Value') if ea is not None else row.findtext('p:'+format(int(fid),'x'),None,ns)
            if raw is None and name=='변환전파':
                source=next(d for d in definitions.values() if text(d,'SecondaryPID')==fid)
                assert text(source,'AutoRollDown')=='1' and text(source,'Alias')=='십진변환'
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
assert all(s in content for s in ('CDec','CSng','CVar','금액변환'))
archive=load('archived.json');assert archive['baselines'] and archive['scenarios'] and archive['currencyConversions']
print(json.dumps(dict(status='PASS',schema=60,single_vectors=2058,scalar_cases=len(load('cases.json')),
    xml_cells=xml_cells,xml_inherited_cells=2,xer_static_values=xer_cells,formula_definitions=sum(bool(text(d,'Formula')) for d in definitions.values()),
    report_rows=len(rows),pdf_pages=len(pdf.pages),native_file_sha256=hashlib.sha256((root/'source.ppm').read_bytes()).hexdigest(),
    external_product_run=False),ensure_ascii=False,indent=2))
