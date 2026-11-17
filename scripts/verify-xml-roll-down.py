"""Independent MSPDI roll-down evaluator; checks effective values without using the native implementation."""
import argparse
from datetime import datetime,timedelta
from decimal import Decimal
import json,re
from pathlib import Path
import xml.etree.ElementTree as ET

parser=argparse.ArgumentParser();parser.add_argument("root",type=Path);parser.add_argument("--reports",action="store_true");parser.add_argument("--reader",action="store_true");args=parser.parse_args();ns={"p":"http://schemas.microsoft.com/project"}
def text(n,k,default=""):return n.findtext("p:"+k,default,ns)
def decimal(v):
    result=format(Decimal(v),"f").rstrip("0").rstrip(".") if "." in format(Decimal(v),"f") else format(Decimal(v),"f")
    return "0" if result in ("-0","") else result
def scalar(kind,raw,fmt="3",packed=False):
    if packed and kind==1:
        days,ticks=divmod(int(raw),65536);assert days>=0 and 0<=ticks<14400
        raw=(datetime(1983,12,31)+timedelta(days=days,seconds=ticks*6)).isoformat(timespec="seconds")
    if kind==7:return raw
    if kind==5:return decimal(raw)
    if kind==4:assert raw in ("0","1");return raw
    if kind==0:return decimal(Decimal(raw)/100)
    if kind in (1,3,6):
        v=datetime.fromisoformat(raw);assert v.tzinfo is None
        return v.date().isoformat() if kind==1 and v.time()==datetime.min.time() else v.isoformat(timespec="microseconds").rstrip("0").rstrip(".") if v.microsecond else v.isoformat(timespec="seconds")
    if kind==2:
        m=re.fullmatch(r"PT(?:(\d+(?:\.\d+)?)H)?(?:(\d+(?:\.\d+)?)M)?(?:(\d+(?:\.\d+)?)S)?",raw);assert m,raw
        h,minutes,seconds=[Decimal(v or "0") for v in m.groups()];assert fmt in ("3","4","35","36")
        return decimal(h*60+minutes+seconds/60)+(" em" if fmt in ("4","36") else " m")+("?" if int(fmt)>=32 else "")
    raise AssertionError(kind)
def evaluate(name):
    root=ET.parse(args.root/name).getroot();tables={}
    for table in root.findall("p:OutlineCodes/p:OutlineCode",ns):
        vals={}
        for v in table.findall("p:Values/p:Value",ns):
            kind={21:7,4:1}[int(text(v,"Type"))]
            vals[text(v,"FieldGUID")]=scalar(kind,text(v,"Value"),packed=kind==1)
        tables[text(table,"Guid")]=vals
    defs={};links={};aliases={}
    for f in root.findall("p:ExtendedAttributes/p:ExtendedAttribute",ns):
        identifier=text(f,"FieldID");kind=int(text(f,"CFType"));alias=text(f,"Alias");entry={"kind":kind,"alias":alias,"scope":text(f,"ElemType"),"multi":text(f,"MaxMultiValues","1")!="1","table":text(f,"Ltuid"),"local":{text(v,"ID"):scalar(kind,text(v,"Value")) for v in f.findall("p:ValueList/p:Value",ns)}}
        defs[identifier]=entry;aliases[alias]=identifier
        if f.find("p:AutoRollDown",ns) is not None:
            assert text(f,"AutoRollDown") in ("0","1")
            assert entry["scope"] in ("20","21")
            links[text(f,"SecondaryPID")]=(identifier,text(f,"AutoRollDown")=="1")
    assert len(defs)==27 and len(links)==13 and all(enabled for _,enabled in links.values())
    for target,(source,_) in links.items():assert defs[target]["scope"]=="23" and defs[target]["kind"]==defs[source]["kind"]
    def values(owner):
        result={};choice={}
        for n in owner:
            tag=n.tag.rsplit("}",1)[-1]
            if re.fullmatch(r"[bcf][0-9a-f]{6}",tag):identifier=str(int(tag,16));raw=n.text or "";guid="";present=True
            elif tag in ("ExtendedAttribute","EnterpriseExtendedAttribute"):
                identifier=text(n,"FieldID");raw=text(n,"Value");guid=text(n,"ValueGUID",text(n,"FieldValueGUID"));present=n.find("p:Value",ns) is not None
            else:continue
            assert identifier in defs,(tag,identifier)
            f=defs[identifier]
            if not present and not guid:assert identifier not in result;result[identifier]=None;continue
            if f["table"]:
                guid=guid or raw;value=tables[f["table"]][guid]
                if guid!=raw and present:assert value==scalar(f["kind"],raw)
                choice.setdefault(identifier,[]).append(value)
            elif f["local"]:
                value=f["local"].get(text(n,"ValueID"),scalar(f["kind"],raw));assert value in f["local"].values()
                choice.setdefault(identifier,[]).append(value)
            else:assert identifier not in result;result[identifier]=scalar(f["kind"],raw,text(n,"DurationFormat","3"))
        for identifier,items in choice.items():assert identifier not in result;result[identifier]=", ".join(sorted(items)) if defs[identifier]["multi"] else items[0]
        return result
    tasks={text(t,"UID"):values(t) for t in root.findall("p:Tasks/p:Task",ns)};resources={text(r,"UID"):values(r) for r in root.findall("p:Resources/p:Resource",ns)}
    matrix={};manual_count=0;missing_count=0;inherited_count=0
    for row,a in enumerate(root.findall("p:Assignments/p:Assignment",ns)):
        direct=values(a);manual_count+=sum(v is not None for v in direct.values());missing_count+=sum(v is None for v in direct.values())
        for identifier,f in defs.items():
            if f["scope"]!="23":continue
            v=direct.get(identifier)
            if identifier not in direct and identifier in links:
                source,enabled=links[identifier];source_owner=tasks[text(a,"TaskUID")] if defs[source]["scope"]=="20" else resources[text(a,"ResourceUID")]
                v=source_owner.get(source) if enabled else None
                if v is not None:inherited_count+=1
            matrix[row,f["alias"]]=v
        if row==1:assert not direct,"inherited values were materialized on the wire"
    assert manual_count==6 and missing_count==1 and inherited_count==28
    assert len(matrix)==42 and sum(v is not None for v in matrix.values())==34
    return matrix
wire=evaluate("export.xml");assert wire==evaluate("reexport.xml")
native=json.loads((args.root/"native.json").read_text(encoding="utf-8"));source=json.loads((args.root/"source.ppm").read_text(encoding="utf-8"));assert source["schemaVersion"]==42 and source["payload"]==native
sources={f["xmlRollDown"]["assignmentFieldId"]:(scope,f) for scope in ("customFields","resourceFields") for f in native[scope]}
def raw(attrs,key):return next((a["value"] for a in attrs if a["key"]==key),None)
def choices(f,value):
    if value.startswith("@choices:"):
        ids=[];at=9
        while at<len(value):colon=value.index(":",at);length=int(value[at:colon]);ids.append(value[colon+1:colon+1+length]);at=colon+1+length
    else:ids=[value]
    options={v["optionId"]:v["value"] for v in f["xmlLookup"]["values"]}
    return ", ".join(sorted(options[i] for i in ids))
for row,a in enumerate(native["assignments"]):
    for f in native["assignmentFields"]:
        key="field:"+f["id"];v=raw(a.get("attributes",[]),key);definition=f
        if v is None and key not in a.get("rollDownMissing",[]) and f["id"] in sources:
            scope,definition=sources[f["id"]];owners=native["tasks"] if scope=="customFields" else native["resources"];owner_id=a["taskId"] if scope=="customFields" else a["resourceId"];owner=next(o for o in owners if o["id"]==owner_id);v=raw(owner.get("attributes",[]),"field:"+definition["id"])
        if v is not None and definition["type"]=="choice":v=choices(definition,v)
        assert v==wire[row,f["name"]],(row,f["name"],v,wire[row,f["name"]])
converted=json.loads((args.root/"converted.json").read_text(encoding="utf-8"));assert converted["currency"]=="EUR" and raw(converted["tasks"][0]["attributes"],"field:source5")=="6.17" and raw(converted["assignments"][1].get("attributes",[]),"field:target5") is None
# XER carries the effective assignment values as a static projection.
tables={};current=None;columns=[]
for line in (args.root/"export.xer").read_text(encoding="utf-8").splitlines():
    parts=line.split("\t")
    if parts[0]=="%T":current=parts[1];tables[current]=[]
    elif parts[0]=="%F":columns=parts[1:]
    elif parts[0]=="%R":tables[current].append(dict(zip(columns,parts[1:])))
types={r["udf_type_id"]:r for r in tables["UDFTYPE"] if r["table_name"]=="TASKRSRC"}
rows={(int(r["fk_id"])-1,types[r["udf_type_id"]]["udf_type_label"]):r for r in tables["UDFVALUE"] if r["udf_type_id"] in types}
assert len(types)==14 and len(rows)==34
for key,value in wire.items():
    if value is None:assert key not in rows
    else:
        row=rows[key];actual=row["udf_number"] or row["udf_date"] or row["udf_text"]
        if key[1]=="배정 4":value="예" if value=="1" else "아니요"
        if key[1]=="배정 1":assert Decimal(actual)==Decimal(value)
        else:assert actual==value,(key,actual,value)
result={"status":"PASS","definitions":27,"roll_down_links":13,"assignment_rows":3,"effective_cells":42,"inherited_nonmissing_cells":28,"explicit_values":6,"explicit_missing":1,"native_schema":42,"xer_effective_values":34,"currency_conversion":"inherited cost converted once","external_product_run":False}
if args.reports:
    import openpyxl
    from pypdf import PdfReader
    book=openpyxl.load_workbook(args.root/"fields.xlsx",data_only=True)
    name=next(n for n in book.sheetnames if n.startswith("배정 사용자"));rows={(r[0].value,r[1].value,r[2].value):r for r in book[name] if r[0].value in ("T","B")}
    assert len(rows)==42
    assert rows["T","R","배정 0"][3].value=="직접" and rows["T","S","배정 0"][3].value=="표준" and rows["B","R","배정 0"][3].value=="(빈 문자열)"
    assert rows["T","R","배정 2"][3].value=="미정" and rows["T","S","배정 2"][3].value=="2026-11-11T12:34:56.123456"
    assert rows["T","R","배정 5"][3].value==0 and rows["T","S","배정 5"][3].value==12.34
    assert rows["T","S","배정 4"][3].value=="아니요" and rows["T","S","배정 6"][3].value=="90.5 em?"
    pdf=PdfReader(args.root/"fields.pdf");content=re.sub(r"\s+","","".join(page.extract_text() for page in pdf.pages));assert "2026-11-11T12:34:56.123456" in content and "90.5em?" in content
    result["reports"]={"assignment_rows_checked":42,"pdf_pages":len(pdf.pages),"numeric_cost_cells":True}
if args.reader:
    import jpype,mpxj
    jpype.startJVM("-Dlog4j2.statusLoggerLevel=OFF")
    try:
        from org.mpxj.reader import UniversalProjectReader
        proxy=UniversalProjectReader().getProjectReaderProxy(str((args.root/"export.xml").resolve()));reader=proxy.getProjectReader();reader.setIgnoreErrors(False)
        try:model=proxy.read()
        finally:proxy.close()
        assert not list(model.getIgnoredErrors()) and len(list(model.getResourceAssignments()))==3
        result["reader"]={"strict_read":"PASS","ignored_errors":0,"assignment_rows":3,"limitation":"MPXJ16.10.0 does not execute AutoRollDown or expose its metadata and omits enterprise hex values; flags and effective cells verified independently"}
    finally:jpype.shutdownJVM()
print(json.dumps(result,ensure_ascii=False,indent=2))
