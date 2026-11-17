"""Independent integer/Decimal checks for native number-to-text functions."""
import argparse
from decimal import Decimal,ROUND_HALF_UP,localcontext
import hashlib
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

parser=argparse.ArgumentParser();parser.add_argument('root',type=Path);args=parser.parse_args();root=args.root
load=lambda n:json.loads((root/n).read_text(encoding='utf-8-sig'))
conversions,prefixes=[load(n) for n in ('conversions.json','prefixes.json')]
for row in conversions:
    n=int(row['integer']);bits=n&0xffffffff
    assert row['hex']==format(bits,'X') and row['oct']==format(bits,'o')
    assert row['str']==(' ' if n>=0 else '')+str(n)
    assert int(row['roundtrip'])==n
    with localcontext() as context:
        context.prec=64
        fixed=int(row['fixedScaled']);magnitude=Decimal(abs(fixed))/1000000
        if magnitude and magnitude<Decimal('.0001'):
            exponent=magnitude.adjusted();mantissa=format(magnitude.scaleb(-exponent),'f')
            expected=mantissa+f'E{exponent:+03d}'
        else:
            expected=format(magnitude,'f')
            if '.' in expected:expected=expected.rstrip('0').rstrip('.')
            if expected.startswith('0.'):expected=expected[1:]
        expected=('-' if fixed<0 else ' ')+expected
        assert row['fixedStr']==expected,(row,expected)
        assert int(row['fixedBack'])==fixed
with localcontext() as context:
    context.prec=64
    for row in prefixes:
        text=''.join(c for c in row['input'] if c not in ' \t\r\n')
        match=re.match(r'[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eEdD][+-]?\d+)?',text)
        assert match,row
        value=Decimal(match.group().replace('D','E').replace('d','E'))
        expected=int(value.quantize(Decimal('.000001'),rounding=ROUND_HALF_UP)*1000000)
        assert int(row['scaled'])==expected,(row,expected)
native=load('native.json')
expected={'금액문자열':[' 12.34',' 2'],'접두숫자':['12.345679','65535'],'진수16':['100','FFFFFFFE'],
          '진수8':['400','37777777776'],'진수왕복':['256','-2'],'작은숫자문자':[' 1E-06']*2,
          '접두혼합식':['24.691358','131070'],'숫자요약':['12']*2}
extra={'프로젝트변환':['-1'],'자원변환':['-1'],'배정변환':['12.345679','65535'],'숫자전파':['12.345679','65535']}
xml=ET.parse(root/'numbers.xml').getroot();ns={'p':'http://schemas.microsoft.com/project'}
text=lambda r,n:r.findtext('p:'+n,default='',namespaces=ns)
definitions={text(d,'FieldID'):d for d in xml.findall('p:ExtendedAttributes/p:ExtendedAttribute',ns)}
tasks=[t for t in xml.findall('p:Tasks/p:Task',ns) if text(t,'Summary')!='1'];xml_cells=0
for i,t in enumerate(tasks):
    attrs={text(definitions[text(a,'FieldID')],'Alias'):text(a,'Value') for a in t.findall('p:ExtendedAttribute',ns)}
    for name,wanted in expected.items():assert attrs[name]==wanted[i],(name,repr(attrs[name]),repr(wanted[i]));xml_cells+=1
for selector,names in [('p:Tasks/p:Task',['프로젝트변환']),('p:Resources/p:Resource',['자원변환']),('p:Assignments/p:Assignment',['배정변환','숫자전파'])]:
    for name in names:
        fid=next(fid for fid,d in definitions.items() if text(d,'Alias')==name);found=[]
        for i,row in enumerate(xml.findall(selector,ns)):
            if name=='프로젝트변환' and text(row,'UID')!='0':continue
            ea=next((a for a in row.findall('p:ExtendedAttribute',ns) if text(a,'FieldID')==fid),None)
            raw=text(ea,'Value') if ea is not None else row.findtext('p:'+format(int(fid),'x'),None,ns)
            if raw is None and name=='숫자전파':
                source=next(d for d in definitions.values() if text(d,'SecondaryPID')==fid)
                assert text(source,'AutoRollDown')=='1' and text(source,'Alias')=='접두숫자'
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
assert all(s in content for s in ('Str','Val','Hex','Oct','금액문자열'))
archive=load('archived.json');assert archive['baselines'] and archive['scenarios'] and archive['currencyConversions']
print(json.dumps(dict(status='PASS',schema=57,conversion_vectors=len(conversions),prefix_vectors=len(prefixes),
                     fixed_precision_roundtrips=len(conversions),
                     scalar_cases=len(load('cases.json')),xml_cells=xml_cells,xml_inherited_cells=2,xer_static_values=xer_cells,
                     formula_definitions=sum(bool(text(d,'Formula')) for d in definitions.values()),report_rows=len(rows),
                     pdf_pages=len(pdf.pages),native_file_sha256=hashlib.sha256((root/'source.ppm').read_bytes()).hexdigest(),
                     external_product_run=False),ensure_ascii=False,indent=2))
