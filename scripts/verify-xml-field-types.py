"""Independent Flag/Cost/Duration XML/native/report inspection; no MSP/P6 execution claim."""
import argparse
from decimal import Decimal
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

p=argparse.ArgumentParser();p.add_argument("root",type=Path);p.add_argument("--reader",action="store_true");p.add_argument("--reports",action="store_true");args=p.parse_args()
ns={"p":"http://schemas.microsoft.com/project"}
def text(n,key,default=""):return n.findtext("p:"+key,default,ns)
def duration(raw):
 m=re.fullmatch(r"(-)?PT(?:(\d+)H)?(?:(\d+)M)?(?:(\d+(?:\.\d+)?)S)?",raw);assert m
 return (-1 if m[1] else 1)*(Decimal(m[2] or 0)*60+Decimal(m[3] or 0)+Decimal(m[4] or 0)/60)
def read(filename):
 root=ET.parse(args.root/filename).getroot();defs={}
 for n in root.findall("p:ExtendedAttributes/p:ExtendedAttribute",ns):
  identifier=text(n,"FieldID");assert identifier not in defs
  defs[identifier]=(text(n,"Alias"),text(n,"CFType"),text(n,"Guid"),text(n,"Ltuid"))
 assert len(defs)==18 and set(t[1] for t in defs.values())=={"0","2","4"}
 tables={text(n,"Guid"):[(text(v,"Type"),text(v,"Value"),text(v,"ValueID"),text(v,"FieldGUID")) for v in n.findall("p:Values/p:Value",ns)] for n in root.findall("p:OutlineCodes/p:OutlineCode",ns)}
 assert len(tables)==2 and {t for vs in tables.values() for t,*_ in vs}=={"6","17"}
 assert sorted(v for vs in tables.values() for t,v,*_ in vs if t=="6")==["0","15"]
 cost=next(n for n in root.findall("p:ExtendedAttributes/p:ExtendedAttribute",ns) if text(n,"Alias")=="금액 목록")
 assert [text(v,"Value") for v in cost.findall("p:ValueList/p:Value",ns)]==["0","-1234"]
 actual={};flags=set();formats=set()
 for wrapper,tag,scope in (("Tasks","Task","TASK"),("Resources","Resource","RESOURCE"),("Assignments","Assignment","ASSIGNMENT")):
  for owner in root.findall(f"p:{wrapper}/p:{tag}",ns):
   uid=text(owner,"UID")
   for node in owner:
    name=node.tag.split("}")[-1]
    if name=="ExtendedAttribute":identifier=text(node,"FieldID");raw=text(node,"Value");fmt=text(node,"DurationFormat")
    elif re.fullmatch("[bcf][0-9a-f]{6}",name):identifier=str(int(name,16));raw=node.text or "";fmt=""
    else:continue
    alias,kind,_,table=defs[identifier]
    if table and kind!="2":
     assert raw in {guid for vs in tables.values() for _,_,_,guid in vs};continue
    key=(scope,uid,alias);assert key not in actual
    if "목록" in alias:
     if kind=="0":assert raw=="-1234"
     else:assert duration(raw)==Decimal("1.5")
     continue
    if kind=="0":actual[key]=str(Decimal(raw)/100)
    elif kind=="4":assert raw in ("0","1");actual[key]=raw;flags.add(raw)
    else:actual[key]=(str(duration(raw)),fmt);formats.add(fmt)
 expected={
  ("TASK","1","작업 승인"):"0",("TASK","2","작업 승인"):"1",
  ("TASK","1","작업 금액"):"-12.34",("TASK","2","작업 금액"):"0",
  ("TASK","1","작업 기간"):("90.5","36"),
  ("TASK","1","기업 승인"):"1",("TASK","1","기업 금액"):"42.01",("TASK","1","기업 기간"):("1.000001","3"),
  ("TASK","0","프로젝트 승인"):"0",("TASK","0","프로젝트 금액"):"100.01",("TASK","0","프로젝트 기간"):("-15.25","35"),
  ("RESOURCE","1","자원 승인"):"1",("RESOURCE","1","자원 금액"):"0",("RESOURCE","1","자원 기간"):("0","3"),
  ("ASSIGNMENT","1","배정 승인"):"0",("ASSIGNMENT","1","배정 금액"):"5.67",("ASSIGNMENT","1","배정 기간"):("0","3"),("ASSIGNMENT","2","배정 기간"):("1.5","3")}
 assert actual==expected,(filename,actual)
 assert flags=={"0","1"} and formats=={"3","35","36"}
 return defs,tables
assert read("export.xml")==read("reexport.xml")
native=json.loads((args.root/"native.json").read_text(encoding="utf-8"));assert {f["type"] for key in ("customFields","projectFields","resourceFields","assignmentFields") for f in native[key]}=={"flag","cost","duration","choice"}
result={"status":"PASS","xml_definitions":18,"typed_lookup_tables":2,"scalar_cells":18,"false_zero_missing":"distinct","cost_scale":100,"duration_minutes_precision":6,"external_product_run":False}
if args.reports:
 import openpyxl
 from pypdf import PdfReader
 book=openpyxl.load_workbook(args.root/"fields.xlsx",data_only=True)
 rows={(row[0].value,row[3].value):row for row in book["작업 사용자 필드와 코드"] if row[0].value in ("xml-task-1","xml-task-2")}
 cost=rows["xml-task-1","작업 금액"][4];assert cost.data_type=="n" and Decimal(str(cost.value))==Decimal("-12.34")
 assert rows["xml-task-1","작업 승인"][4].value=="아니요" and rows["xml-task-1","작업 기간"][4].value=="90.5 em?"
 assert rows["xml-task-2","작업 금액"][4].value==0 and rows["xml-task-2","작업 기간"][4].value=="미정"
 pdf=PdfReader(args.root/"fields.pdf");content=re.sub(r"\s+","","".join(page.extract_text() for page in pdf.pages));assert "-12.34" in content and "90.5em?" in content and "아니요" in content
 result["reports"]={"pdf_pages":len(pdf.pages),"numeric_cost":True,"flag_duration_labels":True}
if args.reader:
 import jpype
 import mpxj
 jpype.startJVM("-Dlog4j2.statusLoggerLevel=OFF")
 try:
  from org.mpxj.reader import UniversalProjectReader
  from org.mpxj import TimeUnit
  proxy=UniversalProjectReader().getProjectReaderProxy(str((args.root/"export.xml").resolve()))
  try:
   reader=proxy.getProjectReader();reader.setIgnoreErrors(False);model=proxy.read();assert not list(model.getIgnoredErrors())
   fields={str(f.getAlias()):f.getFieldType() for f in model.getCustomFields()}
   task=next(t for t in model.getTasks() if str(t.getName())=="설계")
   assert str(task.get(fields["작업 승인"])).lower()=="false"
   assert Decimal(str(task.get(fields["작업 금액"])))==Decimal("-12.34")
   d=task.get(fields["작업 기간"]);assert Decimal(str(d.getDuration()))==Decimal("90.5") and d.getUnits()==TimeUnit.ELAPSED_MINUTES
   result["strict_reader"]={"ignored_errors":0,"local_typed_values":3,"limitation":"MPXJ16.10.0 omits enterprise hex values; estimated bit and enterprise values checked independently"}
  finally:proxy.close()
 finally:jpype.shutdownJVM()
print(json.dumps(result,ensure_ascii=False,indent=2))
