"""Independent XML/native/report clock checks; separates MPXJ reader limits from source correctness."""
import argparse
from datetime import datetime,timedelta
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

p=argparse.ArgumentParser();p.add_argument("root",type=Path);p.add_argument("--reader",action="store_true");p.add_argument("--reports",action="store_true");args=p.parse_args();ns={"p":"http://schemas.microsoft.com/project"}
def text(n,k,default=""):return n.findtext("p:"+k,default,ns)
def unpack(raw):
 n=int(raw);days,ticks=divmod(n,65536);assert n>=0 and 0<=ticks<14400
 return (datetime(1983,12,31)+timedelta(days=days,seconds=ticks*6)).isoformat(timespec="seconds")
def scope(identifier):
 n=int(identifier)
 return "PROJECT" if 0xB608000<=n<=0xB617FFF else "RESOURCE" if n>>24==0xC else "ASSIGNMENT" if n>>24==0xF else "TASK"
def read(filename):
 root=ET.parse(args.root/filename).getroot();tables={}
 for table in root.findall("p:OutlineCodes/p:OutlineCode",ns):
  values={}
  for v in table.findall("p:Values/p:Value",ns):
   assert text(v,"Type") in ("4","27")
   key=text(v,"FieldGUID");assert key not in values
   values[key]=(unpack(text(v,"Value")),text(v,"ValueID"),text(v,"Type"))
  tables[text(table,"Guid")]=values
 assert len(tables)==2 and {v[2] for vs in tables.values() for v in vs.values()}=={"4","27"}
 defs={}
 for n in root.findall("p:ExtendedAttributes/p:ExtendedAttribute",ns):
  identifier=text(n,"FieldID");assert identifier not in defs
  defs[identifier]=(text(n,"Alias"),text(n,"CFType"),text(n,"Ltuid"),text(n,"Guid"))
 assert len(defs)==18 and {v[1] for v in defs.values()}=={"1","3","6"}
 local=next(n for n in root.findall("p:ExtendedAttributes/p:ExtendedAttribute",ns) if text(n,"Alias")=="시작 목록")
 assert [text(v,"Value") for v in local.findall("p:ValueList/p:Value",ns)]==["2026-11-10T09:00:00.123456","2026-11-10T11:00:00"]
 by_name={v[0]:v for v in defs.values()};assert by_name["날짜 목록"][2]==by_name["자원 시작 목록"][2]
 actual={}
 for wrapper,tag in (("Tasks","Task"),("Resources","Resource"),("Assignments","Assignment")):
  for owner in root.findall(f"p:{wrapper}/p:{tag}",ns):
   uid=text(owner,"UID")
   for node in owner:
    name=node.tag.split("}")[-1]
    if name=="ExtendedAttribute":identifier=text(node,"FieldID");raw=text(node,"Value");ref=text(node,"ValueGUID")
    elif re.fullmatch("[bcf][0-9a-f]{6}",name):identifier=str(int(name,16));raw=node.text or "";ref=""
    else:continue
    alias,kind,table,_=defs[identifier]
    if table and raw in tables[table]:ref=raw;raw=""
    if ref:
     assert ref in tables[table]
     resolved=tables[table][ref][0]
     assert not raw or raw==resolved
     raw=resolved
    dt=datetime.fromisoformat(raw);assert dt.tzinfo is None
    key=scope(identifier),uid,alias;assert key not in actual;actual[key]=raw
 expected={
 ("TASK","1","작업 날짜"):"2026-11-10T12:34:56.123456",("TASK","1","작업 시작 일시"):"2026-11-10T09:30:06",("TASK","1","작업 종료 일시"):"2026-11-11T00:00:00",
 ("TASK","2","작업 날짜"):"2026-11-11T00:00:00",("TASK","2","작업 시작 일시"):"2026-11-10T10:00:00.5",
 ("TASK","1","기업 시작 일시"):"2026-11-10T23:59:59.999999",("TASK","1","기업 종료 일시"):"2026-11-12T00:00:00",
 ("TASK","1","시작 목록"):"2026-11-10T11:00:00",("TASK","1","종료 목록"):"2026-11-11T00:00:06",("TASK","1","날짜 목록"):"2026-11-10T15:30:12",
 ("PROJECT","0","프로젝트 날짜"):"2026-11-10T12:00:00",("PROJECT","0","프로젝트 시작 일시"):"2026-11-10T00:00:00",("PROJECT","0","프로젝트 종료 일시"):"2026-11-11T18:30:00",
 ("RESOURCE","1","자원 날짜"):"2026-11-10T00:00:00",("RESOURCE","1","자원 시작 일시"):"2026-11-12T09:00:00",("RESOURCE","1","자원 종료 일시"):"2026-11-12T18:15:12",
 ("RESOURCE","2","자원 날짜"):"2026-11-10T01:02:03",("RESOURCE","2","자원 시작 일시"):"2026-11-09T11:00:00",("RESOURCE","1","자원 시작 목록"):"2026-11-10T15:30:12",
 ("ASSIGNMENT","1","배정 날짜"):"2026-11-10T00:00:00",("ASSIGNMENT","1","배정 시작 일시"):"2026-11-10T08:00:00.123",("ASSIGNMENT","1","배정 종료 일시"):"2026-11-10T19:00:00",("ASSIGNMENT","2","배정 시작 일시"):"2026-11-11T09:00:00"}
 assert actual==expected,(filename,actual)
 return defs,tables
assert read("export.xml")==read("reexport.xml")
native=json.loads((args.root/"native.json").read_text(encoding="utf-8"));assert {f["type"] for key in ("customFields","projectFields","resourceFields","assignmentFields") for f in native[key]}=={"date","start","finish","choice"}
assert native["tasks"][0]["attributes"][0]["value"]=="2026-11-10T12:34:56.123456" and native["tasks"][1]["attributes"][0]["value"]=="2026-11-11"
result={"status":"PASS","xml_definitions":18,"packed_guid_tables":2,"selected_cells":23,"clock":"civil time, no implicit timezone","fractional_seconds":6,"midnight_missing":"distinct","external_product_run":False}
if args.reports:
 import openpyxl
 from pypdf import PdfReader
 book=openpyxl.load_workbook(args.root/"fields.xlsx",data_only=True)
 rows={(r[0].value,r[3].value):r for r in book["작업 사용자 필드와 코드"] if r[0].value in ("xml-task-1","xml-task-2")}
 assert rows["xml-task-1","작업 날짜"][4].value=="2026-11-10T12:34:56.123456"
 assert rows["xml-task-1","작업 종료 일시"][4].value=="2026-11-11T00:00:00" and rows["xml-task-2","작업 종료 일시"][4].value=="미정"
 pdf=PdfReader(args.root/"fields.pdf");content=re.sub(r"\s+","","".join(page.extract_text() for page in pdf.pages));assert "2026-11-10T12:34:56.123456" in content and "2026-11-11T00:00:00" in content
 result["reports"]={"pdf_pages":len(pdf.pages),"datetime_text_preserved":True}
if args.reader:
 import jpype
 import mpxj
 jpype.startJVM("-Dlog4j2.statusLoggerLevel=OFF")
 try:
  from org.mpxj.reader import UniversalProjectReader
  def model_read(path):
   proxy=UniversalProjectReader().getProjectReaderProxy(str(path.resolve()));reader=proxy.getProjectReader();reader.setIgnoreErrors(False)
   try:return proxy.read()
   finally:proxy.close()
  try:
   model_read(args.root/"export.xml")
  except Exception as error:
   assert "could not be parsed" in str(error) and "index 19" in str(error),str(error)
   source_limit="MPXJ16.10.0 rejects fractional seconds in extended attribute timestamps"
  else:
   raise AssertionError("Pinned reader changed: review fractional-second compatibility expectation")
  # A named derivative proves only whole-second compatibility. It never replaces the original fixture.
  ET.register_namespace("",ns["p"])
  tree=ET.parse(args.root/"export.xml");changes=0
  for node in tree.iter():
   if node.text and re.fullmatch(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{1,6}",node.text):node.text=node.text[:19];changes+=1
  derivative=args.root/"reader-whole-seconds.xml";tree.write(derivative,encoding="utf-8",xml_declaration=True);assert changes>0
  model=model_read(derivative);assert not list(model.getIgnoredErrors())
  fields={str(f.getAlias()):f.getFieldType() for f in model.getCustomFields()}
  task=next(t for t in model.getTasks() if str(t.getName())=="설계")
  assert str(task.get(fields["작업 시작 일시"]))=="2026-11-10T09:30:06"
  assert str(task.get(fields["작업 종료 일시"]))=="2026-11-11T00:00"
  resource=next(r for r in model.getResources() if str(r.getName())=="검토 인력")
  assert str(resource.get(fields["자원 날짜"]))=="2026-11-10T01:02:03"
  shared=next(r for r in model.getResources() if str(r.getName())=="설계 인력")
  assert str(shared.get(fields["자원 시작 목록"]))=="2026-11-10T15:30:12"
  result["reader"]={"original_strict_read":"unsupported fractional seconds","limitation":source_limit,"derivative":"reader-whole-seconds.xml","derivative_ignored_errors":0,"derivative_values_checked":4,"fractional_nodes_removed":changes,"enterprise_limit":"MPXJ16.10.0 also omits enterprise hex values; original precision/values verified independently"}
 finally:jpype.shutdownJVM()
print(json.dumps(result,ensure_ascii=False,indent=2))
