"""Independent Unicode code-point string algorithms and exported artifact inspection."""
import argparse
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET

parser=argparse.ArgumentParser();parser.add_argument('root',type=Path);root=parser.parse_args().root
load=lambda n:json.loads((root/n).read_text(encoding='utf-8-sig'))
vectors=load('vectors.json')
for row in vectors:
    source,needle,replacement=[row[k] for k in ('source','find','replacement')]
    start,count,reverse_start,compare=[row[k] for k in ('start','count','reverseStart','compare')]
    hay=source.lower() if compare else source;find=needle.lower() if compare else needle
    suffix=source[start-1:];remaining=count;parts=[];pos=start-1
    while find and remaining!=0 and pos<len(source):
        hit=hay.find(find,pos)
        if hit<0:break
        parts.extend((source[pos:hit],replacement));pos=hit+len(find)
        if remaining>0:remaining-=1
    expected=''.join(parts)+source[pos:]
    assert row['replaced']==expected,(row,expected)
    end=len(source) if reverse_start==-1 else reverse_start
    expected_position=0 if not source or end>len(source) else end if not find else hay[:end].rfind(find)+1
    assert row['position']==expected_position,(row,expected_position)
    assert row['reversed']==''.join(reversed(source)),row
    assert row['reversed'][::-1]==source
assert len(vectors)==1024
native=load('native.json')
expected={'치환문자':['XAbXAB','XcXc'],'역순문자':['BA가bA가','cbacba'],'역검색위치':['4','4'],
          '시작횟수':['AbXAB','bcXc'],'끝경계검색':['1','1'],'ASCII치환':['가가']*2,
          '금액치환':['12,34','2'],'문자열혼합식':['8']*2,'문자열요약':['요약']*2}
extra={'프로젝트역순':['61'],'자원치환':['a/a'],'배정역순':['BA가bA가','cbacba'],'문자열전파':['XAbXAB','XcXc']}
xml=ET.parse(root/'numbers.xml').getroot();ns={'p':'http://schemas.microsoft.com/project'}
text=lambda r,n:r.findtext('p:'+n,default='',namespaces=ns)
definitions={text(d,'FieldID'):d for d in xml.findall('p:ExtendedAttributes/p:ExtendedAttribute',ns)}
tasks=[t for t in xml.findall('p:Tasks/p:Task',ns) if text(t,'Summary')!='1'];xml_cells=0
for i,t in enumerate(tasks):
    attrs={text(definitions[text(a,'FieldID')],'Alias'):text(a,'Value') for a in t.findall('p:ExtendedAttribute',ns)}
    for name,wanted in expected.items():assert attrs[name]==wanted[i],(name,repr(attrs[name]),repr(wanted[i]));xml_cells+=1
for selector,names in [('p:Tasks/p:Task',['프로젝트역순']),('p:Resources/p:Resource',['자원치환']),('p:Assignments/p:Assignment',['배정역순','문자열전파'])]:
    for name in names:
        fid=next(fid for fid,d in definitions.items() if text(d,'Alias')==name);found=[]
        for i,row in enumerate(xml.findall(selector,ns)):
            if name=='프로젝트역순' and text(row,'UID')!='0':continue
            ea=next((a for a in row.findall('p:ExtendedAttribute',ns) if text(a,'FieldID')==fid),None)
            raw=text(ea,'Value') if ea is not None else row.findtext('p:'+format(int(fid),'x'),None,ns)
            if raw is None and name=='문자열전파':
                source=next(d for d in definitions.values() if text(d,'SecondaryPID')==fid)
                assert text(source,'AutoRollDown')=='1' and text(source,'Alias')=='치환문자'
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
assert all(s in content for s in ('Replace','InStrRev','StrReverse','금액치환'))
archive=load('archived.json');assert archive['baselines'] and archive['scenarios'] and archive['currencyConversions']
print(json.dumps(dict(status='PASS',schema=59,string_vectors=len(vectors),string_comparisons=len(vectors)*3,
                     scalar_cases=len(load('cases.json')),xml_cells=xml_cells,xml_inherited_cells=2,xer_static_values=xer_cells,
                     formula_definitions=sum(bool(text(d,'Formula')) for d in definitions.values()),report_rows=len(rows),
                     pdf_pages=len(pdf.pages),native_file_sha256=hashlib.sha256((root/'source.ppm').read_bytes()).hexdigest(),
                     external_product_run=False),ensure_ascii=False,indent=2))
