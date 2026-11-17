"""Independent 80-digit math reference and exchange/report inspection (QA only)."""
import argparse
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET
import mpmath as mp

parser=argparse.ArgumentParser();parser.add_argument('root',type=Path);root=parser.parse_args().root
load=lambda n:json.loads((root/n).read_text(encoding='utf-8-sig'))
mp.mp.dps=80
vectors=load('vectors.json');digits=vectors['long_double_digits'];eps=mp.mpf(2)**(1-digits)
assert digits>=53
exact=0;maximum_delta=0;maximum_tolerance=0;details=[]
functions={'atn':mp.atan,'sin':mp.sin,'cos':mp.cos,'tan':mp.tan,'log':mp.log,'exp':mp.exp}
for row in vectors['rows']:
    fn=row['function'];x=mp.mpf(row['input'])/1000000
    y=functions[fn](x)
    rounded=int(mp.sign(y)*mp.floor(abs(y)*1000000+mp.mpf('.5')))
    actual=int(row['output']);delta=abs(actual-rounded)
    # Allow rounding plus input conversion sensitivity and a small libm error budget.
    # This is an empirical numerical check, not a formal correctly-rounded proof.
    derivative={'atn':lambda:1/(1+x*x),'sin':lambda:abs(mp.cos(x)),
                'cos':lambda:abs(mp.sin(x)),'tan':lambda:1+y*y,
                'log':lambda:1/x,'exp':lambda:y}[fn]()
    tolerance=int(mp.ceil(4*eps*abs(x)*derivative*1000000+8*eps*abs(y)*1000000))+1
    assert delta<=tolerance,(row,str(y),delta,tolerance)
    exact+=delta==0;maximum_delta=max(maximum_delta,delta);maximum_tolerance=max(maximum_tolerance,tolerance)
    if delta:details.append(dict(row,reference=str(rounded),delta=delta,tolerance=tolerance))
assert len(vectors['rows'])==6189
if digits>=64:
    # The supported MinGW build also has this tighter observed regression gate.
    assert maximum_delta<=1
    assert all(r['function']=='exp' and abs(int(r['output']))>1000000000000000 for r in details)
native=load('native.json')
expected={'역탄젠트':['0.463648','-0.785398'],'코사인':['0.877583','0.540302'],
          '지수':['1.648721','0.367879'],'자연로그':['0.916291','1.252763'],
          '사인':['0.479426','-0.841471'],'탄젠트':['0.546302','-1.557408'],
          '금액로그':['2.512846','0.693147'],'수학혼합식':['0.958852','-1.682942'],'수학요약':['1']*2}
extra={'프로젝트로그':['2.772589'],'자원코사인':['1'],'배정역탄젠트':['0.463648','-0.785398'],
       '수학전파':['0.463648','-0.785398']}
xml=ET.parse(root/'numbers.xml').getroot();ns={'p':'http://schemas.microsoft.com/project'}
text=lambda r,n:r.findtext('p:'+n,default='',namespaces=ns)
definitions={text(d,'FieldID'):d for d in xml.findall('p:ExtendedAttributes/p:ExtendedAttribute',ns)}
tasks=[t for t in xml.findall('p:Tasks/p:Task',ns) if text(t,'Summary')!='1'];xml_cells=0
for i,t in enumerate(tasks):
    attrs={text(definitions[text(a,'FieldID')],'Alias'):text(a,'Value') for a in t.findall('p:ExtendedAttribute',ns)}
    for name,wanted in expected.items():assert attrs[name]==wanted[i],(name,repr(attrs[name]),repr(wanted[i]));xml_cells+=1
for selector,names in [('p:Tasks/p:Task',['프로젝트로그']),('p:Resources/p:Resource',['자원코사인']),('p:Assignments/p:Assignment',['배정역탄젠트','수학전파'])]:
    for name in names:
        fid=next(fid for fid,d in definitions.items() if text(d,'Alias')==name);found=[]
        for i,row in enumerate(xml.findall(selector,ns)):
            if name=='프로젝트로그' and text(row,'UID')!='0':continue
            ea=next((a for a in row.findall('p:ExtendedAttribute',ns) if text(a,'FieldID')==fid),None)
            raw=text(ea,'Value') if ea is not None else row.findtext('p:'+format(int(fid),'x'),None,ns)
            if raw is None and name=='수학전파':
                source=next(d for d in definitions.values() if text(d,'SecondaryPID')==fid)
                assert text(source,'AutoRollDown')=='1' and text(source,'Alias')=='역탄젠트'
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
assert all(s in content for s in ('Atn','Cos','Exp','Log','Sin','Tan','금액로그'))
archive=load('archived.json');assert archive['baselines'] and archive['scenarios'] and archive['currencyConversions']
print(json.dumps(dict(status='PASS',schema=58,math_vectors=len(vectors['rows']),reference_digits=80,
                     mpmath_version=mp.__version__,long_double_digits=digits,exact_rounded_matches=exact,maximum_scaled_delta=maximum_delta,
                     maximum_scaled_tolerance=maximum_tolerance,approximation_differences=details,
                     scalar_cases=len(load('cases.json')),xml_cells=xml_cells,xml_inherited_cells=2,xer_static_values=xer_cells,
                     formula_definitions=sum(bool(text(d,'Formula')) for d in definitions.values()),report_rows=len(rows),
                     pdf_pages=len(pdf.pages),native_file_sha256=hashlib.sha256((root/'source.ppm').read_bytes()).hexdigest(),
                     external_product_run=False),ensure_ascii=False,indent=2))
