"""Independent Gregorian calendar/datetime reconciliation of emitted evidence."""
import argparse
import calendar
from datetime import datetime, date, timedelta
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET

parser=argparse.ArgumentParser()
parser.add_argument('root',type=Path)
args=parser.parse_args()
root=args.root
load=lambda n:json.loads((root/n).read_text(encoding='utf-8-sig'))
parse=datetime.fromisoformat
def normalized(d):
    return f'{d.year:04d}-{d.month:02d}-{d.day:02d}T{d.hour:02d}:{d.minute:02d}:{d.second:02d}'+(f'.{d.microsecond:06d}'.rstrip('0') if d.microsecond else '')
def weekday(d,first):
    return ((d.weekday()+1)%7-(first-1))%7+1
def week_start(d,first):
    return d-timedelta(days=weekday(d,first)-1)
def week_number(d,first,rule):
    def year_start(year):
        jan=date(year,1,1)
        start=week_start(jan,first)
        offset=(jan-start).days
        return start+timedelta(days=7) if (rule==2 and offset>3) or (rule==3 and offset) else start
    start=year_start(d.year)
    if d<start:start=year_start(d.year-1)
    return (d-start).days//7+1
def part(key,d,first,rule):
    return {'yyyy':lambda:d.year,'q':lambda:(d.month-1)//3+1,'m':lambda:d.month,'d':lambda:d.day,
            'y':lambda:d.timetuple().tm_yday,'h':lambda:d.hour,'n':lambda:d.minute,'s':lambda:d.second,
            'w':lambda:weekday(d,first),'ww':lambda:week_number(d.date(),first,rule)}[key]()
parts,adds,diffs=[load(n) for n in ('parts.json','adds.json','diffs.json')]
for r in parts:
    expected=part(r['interval'],parse(r['input']),r['firstDay'],r['firstWeek'])
    assert int(r['value'])==expected,(r,expected)
for r in adds:
    d=parse(r['input']);key,count=r['interval'],r['count']
    if key in ('yyyy','q','m'):
        months=(d.year-1)*12+d.month-1+count*{'yyyy':12,'q':3,'m':1}[key]
        y,m=months//12+1,months%12+1
        expected=d.replace(year=y,month=m,day=min(d.day,calendar.monthrange(y,m)[1]))
    else:
        expected=d+timedelta(seconds=count*{'y':86400,'d':86400,'w':86400,'ww':604800,'h':3600,'n':60,'s':1}[key])
    assert r['value']==normalized(expected),(r,expected)
epoch=datetime(1970,1,1)
def micros(d):
    delta=d-epoch
    return (delta.days*86400+delta.seconds)*1000000+delta.microseconds
for r in diffs:
    a,b=parse(r['from']),parse(r['to']);key=r['interval']
    if key=='yyyy':expected=b.year-a.year
    elif key=='q':expected=(b.year-a.year)*4+(b.month-1)//3-(a.month-1)//3
    elif key=='m':expected=(b.year-a.year)*12+b.month-a.month
    elif key=='w':
        days=(b.date()-a.date()).days
        expected=(1 if days>=0 else -1)*(abs(days)//7)
    elif key=='ww':expected=(week_start(b.date(),r['firstDay'])-week_start(a.date(),r['firstDay'])).days//7
    else:
        unit={'y':86400000000,'d':86400000000,'h':3600000000,'n':60000000,'s':1000000}[key]
        expected=micros(b)//unit-micros(a)//unit
    assert int(r['value'])==expected,(r,expected)

native=load('native.json')
expected={
    '날짜분기':['1','4'],'날짜요일':['3','7'],'날짜주차':['5','52'],
    '월말이동':['2024-02-29T09:30:12.123456','2024-01-31T23:59:59.654321'],
    '월경계':['1','1'],'동적이동':['2024-02-29T09:30:12.123456','2024-01-01T23:59:59.654321'],
    '날짜혼합식':['10','40'], '날짜요약':['4','4']}
xml=ET.parse(root/'dates.xml').getroot();ns={'p':'http://schemas.microsoft.com/project'}
text=lambda r,n:r.findtext('p:'+n,default='',namespaces=ns)
definitions={text(d,'FieldID'):d for d in xml.findall('p:ExtendedAttributes/p:ExtendedAttribute',ns)}
tasks=[t for t in xml.findall('p:Tasks/p:Task',ns) if text(t,'Summary')!='1']
xml_cells=0
for i,t in enumerate(tasks):
    values={text(definitions[text(a,'FieldID')],'Alias'):text(a,'Value') for a in t.findall('p:ExtendedAttribute',ns)}
    for n,values_expected in expected.items():
        actual=values[n]
        if 'T' in values_expected[i]:assert normalized(parse(actual))==values_expected[i],(n,actual)
        else:assert actual==values_expected[i],(n,actual)
        xml_cells+=1
extra={'프로젝트연도':['2024'],'자원요일':['5'],'배정분기이동':['2023-10-31T09:30:12.123456','2023-09-30T23:59:59.654321'],'분기전파':['1','4']}
for selector,names in [('p:Tasks/p:Task',['프로젝트연도']),('p:Resources/p:Resource',['자원요일']),('p:Assignments/p:Assignment',['배정분기이동','분기전파'])]:
    for name in names:
        fid=next(fid for fid,d in definitions.items() if text(d,'Alias')==name)
        found=[]
        for index,row in enumerate(xml.findall(selector,ns)):
            if name=='프로젝트연도' and text(row,'UID')!='0':continue
            ea=next((a for a in row.findall('p:ExtendedAttribute',ns) if text(a,'FieldID')==fid),None)
            raw=text(ea,'Value') if ea is not None else row.findtext('p:'+format(int(fid),'x'),None,ns)
            if raw is None and name=='분기전파':
                source=next(d for d in definitions.values() if text(d,'SecondaryPID')==fid)
                assert text(source,'AutoRollDown')=='1' and text(source,'Alias')=='날짜분기'
                attrs={a['key']:a['value'] for a in native['tasks'][index]['attributes']}
                raw=str(part('q',parse(attrs['field:when']),1,1))
            found.append(normalized(parse(raw)) if raw and 'T' in raw else raw)
        assert found==extra[name],(name,found)
        xml_cells+=len(found)
tables,table,cols={},None,[]
for line in (root/'dates.xer').read_text(encoding='utf-8').splitlines():
    p=line.split('\t')
    if p[0]=='%T':table=p[1];tables[table]=[]
    elif p[0]=='%F':cols=p[1:]
    elif p[0]=='%R':tables[table].append(dict(zip(cols,p[1:])))
types={r['udf_type_id']:r for r in tables['UDFTYPE']}
xer_cells=0
for name,values in dict(expected,**extra).items():
    actual=[r['udf_text'] or r['udf_date'] or r['udf_number'] for r in tables['UDFVALUE'] if types[r['udf_type_id']]['udf_type_label']==name]
    # Non-minute DateTime values use the existing XER text representation.
    actual=[normalized(parse(a)) if 'T' in a or (' ' in a and '-' in a) else a for a in actual]
    assert actual==values,(name,actual,values)
    xer_cells+=len(actual)
import openpyxl
from pypdf import PdfReader
book=openpyxl.load_workbook(root/'fields.xlsx',data_only=True)
sheet=next(s for s in book if s.title.startswith('작업 사용자 필드'))
rows={(r[0],r[2]):r for r in sheet.values if r[0] in ('A','B')}
for f in native['customFields']:
    if f['name'] not in expected:continue
    for i,task in enumerate(native['tasks']):
        raw=rows[task['id'],'field:'+f['id']][4];want=expected[f['name']][i]
        if 'T' in want:assert normalized(parse(raw))==want,(f,raw)
        else:assert str(raw)==want,(f,raw,want)
pdf=PdfReader(root/'fields.pdf');content=''.join(''.join(p.extract_text() or '' for p in pdf.pages).split())
assert all(s in content for s in ('DatePart','Weekday','DateAdd','DateDiff','월말이동'))
archive=load('archived.json');assert archive['baselines'] and archive['scenarios'] and archive['currencyConversions']
print(json.dumps(dict(status='PASS',schema=55,parts=len(parts),additions=len(adds),differences=len(diffs),
                     scalar_cases=len(load('cases.json')),xml_cells=xml_cells,xml_inherited_cells=2,xer_static_values=xer_cells,
                     formula_definitions=sum(bool(text(d,'Formula')) for d in definitions.values()),report_rows=len(rows),
                     pdf_pages=len(pdf.pages),native_file_sha256=hashlib.sha256((root/'source.ppm').read_bytes()).hexdigest(),
                     external_product_run=False),ensure_ascii=False,indent=2))
