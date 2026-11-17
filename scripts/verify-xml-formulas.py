"""Independent Decimal/MSPDI/native/report checks for the bounded XML formula contract."""
import argparse,csv,json,re
from decimal import Decimal,ROUND_HALF_UP
from datetime import datetime,timedelta
from pathlib import Path
import xml.etree.ElementTree as ET
p=argparse.ArgumentParser();p.add_argument("root",type=Path);p.add_argument("--reports",action="store_true");p.add_argument("--reader",action="store_true");args=p.parse_args();ns={"p":"http://schemas.microsoft.com/project"}
def text(n,k,default=""):return n.findtext("p:"+k,default,ns)
def attrs(v):return {a["key"]:a["value"] for a in v}
native=json.loads((args.root/"native.json").read_text(encoding="utf-8"));source=json.loads((args.root/"source.ppm").read_text(encoding="utf-8"));assert source["schemaVersion"]==44 and source["payload"]==native
assert attrs(native["tasks"][0]["attributes"])["field:n"]=="999" and attrs(native["tasks"][0]["attributes"])["field:error"]=="999"
assert attrs(native["taskSummaryAttributes"])["field:summaryCost"]=="999.99"
with (args.root/"arithmetic.tsv").open(encoding="utf-8",newline="") as f:
    cases=list(csv.DictReader(f,delimiter="\t"))
for case in cases:
    a,b,c=[Decimal(case[k]) for k in ("input","factor","offset")];expected=a*b+(c/7).quantize(Decimal(".000001"),rounding=ROUND_HALF_UP);assert Decimal(case["actual"])==expected,case
assert len(cases)==400
factor=Decimal(attrs(native["projectAttributes"])["field:factor"])
expected={};expected["PROJECT",native["name"]]={"factor":factor,"projectResult":factor*3}
for i,t in enumerate(native["tasks"]):
    raw=attrs(t["attributes"]);q=Decimal(raw["field:q"]);n=(q+1)*factor;date=datetime.fromisoformat(raw["field:baseDate"])
    values={"q":q,"baseCost":Decimal(raw["field:baseCost"]),"label":raw["field:label"],"baseDate":date.isoformat(),"empty":None,"n":n,"flag":q>=3,"text":("대형" if q>=3 else "소형")+":"+raw["field:label"],"cost":Decimal(raw["field:baseCost"])*factor,"duration":Decimal(t["durationMinutes"]*3),"date":(date+timedelta(days=2)).isoformat(),"start":(date+timedelta(hours=1)).isoformat(),"finish":"2026-11-15T17:30:06","nativeChain":n*2,"mixed":n*2+1,"summary":"말단","summaryCost":n*10,"error":None,"missing":None,"nullFlag":True,"unknown":Decimal(raw["field:unknown"]) if "field:unknown" in raw else None}
    expected["TASK",t["name"]]=values
r=native["resources"][0];skill=Decimal(attrs(r["attributes"])["field:skill"]);expected["RSRC",r["name"]]={"skill":skill,"r":skill*2,"rText":r["name"]+" 담당"}
for a in native["assignments"]:
    task=next(t for t in native["tasks"] if t["id"]==a["taskId"]);v=expected["TASK",task["name"]];manual=Decimal(attrs(a["attributes"])["field:manual"]);expected["ASSIGNMENT",task["name"]]={"manual":manual,"a":manual+v["n"]+factor,"aText":r["name"]+" 담당:"+v["label"],"aDuration":v["duration"]}
summary={"n":sum(expected["TASK",t["name"]]["n"] for t in native["tasks"]),"duration":sum(expected["TASK",t["name"]]["duration"] for t in native["tasks"]),"summary":"요약"};summary["summaryCost"]=summary["n"]*10
all_fields={f["id"]:f for key in ("customFields","projectFields","resourceFields","assignmentFields") for f in native[key]}
label_to_id={f["name"]:f["id"] for f in all_fields.values()}
def parse(kind,raw):
    if kind==0:return Decimal(raw)/100
    if kind==5:return Decimal(raw)
    if kind==4:assert raw in ("0","1");return raw=="1"
    if kind==2:
        m=re.fullmatch(r"PT(?:(\d+(?:\.\d+)?)H)?(?:(\d+(?:\.\d+)?)M)?(?:(\d+(?:\.\d+)?)S)?",raw);assert m,raw
        h,minutes,seconds=[Decimal(v or "0") for v in m.groups()];return h*60+minutes+seconds/60
    return raw
checked=0;formula_defs=0
for filename in ("export.xml","reexport.xml"):
    root=ET.parse(args.root/filename).getroot();defs={text(d,"FieldID"):d for d in root.findall("p:ExtendedAttributes/p:ExtendedAttribute",ns)}
    by_label={text(d,"Alias"):fid for fid,d in defs.items()};assert len(defs)==len(all_fields)
    for fid,d in defs.items():
        original=all_fields[label_to_id[text(d,"Alias")]]
        if "xmlFormula" in original:
            formula_defs+=1;assert text(d,"Formula"),original["name"]
            # Native IDs never leak into external field references; quoted strings remain intact.
            assert "[field:" not in text(d,"Formula")
    def read(row,scope):
        values={}
        for fid,d in defs.items():
            raw=row.findtext("p:"+format(int(fid),"x"),None,ns)
            ea=next((e for e in row.findall("p:ExtendedAttribute",ns) if text(e,"FieldID")==fid),None)
            if ea is not None:raw=text(ea,"Value",None)
            if raw is not None:values[label_to_id[text(d,"Alias")]]=parse(int(text(d,"CFType")),raw)
        return values
    tasks={text(t,"UID"):t for t in root.findall("p:Tasks/p:Task",ns)}
    for uid,row in tasks.items():
        actual=read(row,"TASK")
        if text(row,"Summary")=="1":
            for key,value in summary.items():assert actual.get(key)==value,(filename,uid,key,actual.get(key),value);checked+=1
            if uid=="0":
                for key,value in expected["PROJECT",native["name"]].items():assert actual.get(key)==value;checked+=1
        else:
            for key,value in expected["TASK",text(row,"Name")].items():assert actual.get(key)==value,(filename,uid,key,actual.get(key),value);checked+=1
    for row in root.findall("p:Resources/p:Resource",ns):
        actual=read(row,"RSRC")
        for key,value in expected["RSRC",text(row,"Name")].items():assert actual.get(key)==value;checked+=1
    for row in root.findall("p:Assignments/p:Assignment",ns):
        task=tasks[text(row,"TaskUID")];name=text(task,"Name");actual=read(row,"ASSIGNMENT")
        for definition in defs.values():
            if text(definition,"AutoRollDown")=="1":
                target=label_to_id[text(defs[text(definition,"SecondaryPID")],"Alias")]
                if target not in actual:
                    source_key=label_to_id[text(definition,"Alias")];actual[target]=read(task,"TASK").get(source_key)
        for key,value in expected["ASSIGNMENT",name].items():assert actual.get(key)==value,(filename,name,key,actual.get(key),value);checked+=1
converted=json.loads((args.root/"converted.json").read_text(encoding="utf-8"));assert converted["currency"]=="EUR" and attrs(converted["tasks"][0]["attributes"])["field:baseCost"]=="6.17" and attrs(converted["taskSummaryAttributes"])["field:summaryCost"]=="500"
# XER is a static projection: compare computed task and assignment columns, including temporal text.
tables={};name=None;columns=[]
for line in (args.root/"export.xer").read_text(encoding="utf-8").splitlines():
    parts=line.split("\t")
    if parts[0]=="%T":name=parts[1];tables[name]=[]
    elif parts[0]=="%F":columns=parts[1:]
    elif parts[0]=="%R":tables[name].append(dict(zip(columns,parts[1:])))
types={row["udf_type_id"]:row for row in tables["UDFTYPE"]};xer_checked=0
for row in tables["UDFVALUE"]:
    d=types[row["udf_type_id"]];scope=d["table_name"];key=label_to_id[d["udf_type_label"]]
    if scope=="TASK":name=native["tasks"][int(row["fk_id"])-1]["name"];scope="TASK"
    elif scope=="TASKRSRC":name=native["tasks"][int(row["fk_id"])-1]["name"];scope="ASSIGNMENT"
    elif scope=="RSRC":name=native["resources"][int(row["fk_id"])-1]["name"];scope="RSRC"
    else:name=native["name"];scope="PROJECT"
    value=expected[scope,name][key];raw=row["udf_number"] or row["udf_text"] or row["udf_date"]
    if isinstance(value,bool):assert raw==("예" if value else "아니요")
    elif all_fields[key]["type"]=="duration":assert Decimal(raw.split()[0])==value
    elif isinstance(value,Decimal):assert Decimal(raw)==value
    else:assert raw==value,(key,raw,value)
    xer_checked+=1
result={"status":"PASS","schema":44,"arithmetic_vectors":400,"checked_xml_cells":checked,"formula_definitions_in_both_exports":formula_defs,"xer_static_values":xer_checked,"preserved_snapshots":{"n":999,"summaryCost":"999.99"},"external_product_run":False}
if args.reports:
    import openpyxl
    from pypdf import PdfReader
    book=openpyxl.load_workbook(args.root/"fields.xlsx",data_only=True)
    sheet=next(s for s in book if s.title.startswith("작업 사용자 필드"));rows={(r[0].value,r[2].value):r for r in sheet if r[0].value in ("A","B")}
    assert rows["A","field:n"][4].value==6 and rows["A","field:cost"][4].value==24.68 and rows["A","field:text"][4].value=="소형:설계" and rows["A","field:error"][4].value=="미정"
    summaries=next(s for s in book if s.title.startswith("XML 사용자 필드 요약"));sr={(r[1].value,r[2].value):r for r in summaries if r[1].value in (native["name"],"요약") and r[2].value in ("합산값","환산기간","요약표시","요약비용")};assert sr[native["name"],"요약비용"][4].value==160 and sr[native["name"],"요약비용"][6].value=="XML 요약 계산식"
    definitions=next(s for s in book if s.title.startswith("계산식 필드 정의"));assert any("[XML 활성]" in str(r[4].value) for r in definitions) and any("[XML 비활성]" in str(r[4].value) for r in definitions)
    pdf=PdfReader(args.root/"fields.pdf");content=re.sub(r"\s+","","".join(page.extract_text() for page in pdf.pages));assert "XML요약계산식" in content and "미지원함수" in content
    result["reports"]={"task_attribute_rows":len(rows),"summary_rows":len(sr),"numeric_cost":True,"formula_metadata":True,"pdf_pages":len(pdf.pages)}
if args.reader:
    import jpype,mpxj
    jpype.startJVM("-Dlog4j2.statusLoggerLevel=OFF")
    try:
        from org.mpxj.reader import UniversalProjectReader
        proxy=UniversalProjectReader().getProjectReaderProxy(str((args.root/"export.xml").resolve()));reader=proxy.getProjectReader();reader.setIgnoreErrors(False)
        try:model=proxy.read()
        finally:proxy.close()
        assert not list(model.getIgnoredErrors()) and len(list(model.getTasks()))==4 and len(list(model.getResourceAssignments()))==2
        result["reader"]={"strict_read":"PASS","ignored_errors":0,"task_rows_including_summaries":4,"assignment_rows":2,"limitation":"Reader parses metadata/snapshots but does not validate formula execution parity with Microsoft Project/P6"}
    finally:jpype.shutdownJVM()
print(json.dumps(result,ensure_ascii=False,indent=2))
