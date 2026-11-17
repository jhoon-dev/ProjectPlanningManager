"""Independent Decimal/tree evaluator for MSPDI summary roll-up and exported reports."""
import argparse,json,re
from decimal import Decimal,ROUND_HALF_UP
from pathlib import Path
import xml.etree.ElementTree as ET
p=argparse.ArgumentParser();p.add_argument("root",type=Path);p.add_argument("--reports",action="store_true");p.add_argument("--reader",action="store_true");a=p.parse_args();ns={"p":"http://schemas.microsoft.com/project"}
def text(n,k,default=""):return n.findtext("p:"+k,default,ns)
def attrs(values):return {v["key"]:v["value"] for v in values}
def scalar(kind,v):
    if kind in (0,5):return Decimal(v)/(100 if kind==0 else 1)
    if kind==4:return int(v)
    if kind==2:
        m=re.fullmatch(r"PT(?:(\d+(?:\.\d+)?)H)?(?:(\d+(?:\.\d+)?)M)?(?:(\d+(?:\.\d+)?)S)?",v);assert m,v
        h,mm,s=[Decimal(x or "0") for x in m.groups()];return h*60+mm+s/60
    return v
native=json.loads((a.root/"native.json").read_text(encoding="utf-8"));source=json.loads((a.root/"source.ppm").read_text(encoding="utf-8"));assert source["schemaVersion"]==43 and source["payload"]==native
assert attrs(native["taskSummaryAttributes"])["field:n3"]=="999"
checked=0
for filename in ("export.xml","reexport.xml"):
    root=ET.parse(a.root/filename).getroot();definitions={text(f,"FieldID"):f for f in root.findall("p:ExtendedAttributes/p:ExtendedAttribute",ns)}
    nodes={};children={"0":[]};stack=[]
    for row in root.findall("p:Tasks/p:Task",ns):
        uid=text(row,"UID");values={text(e,"FieldID"):scalar(int(text(definitions[text(e,"FieldID")],"CFType")),text(e,"Value")) for e in row.findall("p:ExtendedAttribute",ns)}
        nodes[uid]=(row,values)
        if uid=="0":continue
        level=int(text(row,"OutlineLevel"));stack=stack[:level-1];parent=stack[-1] if stack else "0";children.setdefault(parent,[]).append(uid);children.setdefault(uid,[])
        if text(row,"Summary")=="1":stack.append(uid)
    native_fields={f["name"]:f for f in native["customFields"]}
    native_tasks={t["name"]:t for t in native["tasks"]}
    for uid,(row,values) in nodes.items():
        if text(row,"Summary")=="1":continue
        original=attrs(native_tasks[text(row,"Name")]["attributes"])
        for fid,definition in definitions.items():
            if text(definition,"ElemType")!="20" or text(definition,"Alias") not in native_fields:continue
            f=native_fields[text(definition,"Alias")];raw=original.get("field:"+f["id"])
            if raw is None:assert fid not in values;continue
            kind=int(text(definition,"CFType"))
            expected=Decimal(raw) if kind in (0,5) else int(raw) if kind==4 else Decimal(raw.split()[0]) if kind==2 else raw+"T00:00:00" if kind==1 and len(raw)==10 else raw
            assert values[fid]==expected,(uid,f["id"],values[fid],expected)
            if kind==2:assert all(text(e,"DurationFormat")=="36" for e in row.findall("p:ExtendedAttribute",ns) if text(e,"FieldID")==fid)
    def descendants(uid):
        return [c for ch in children[uid] for c in [ch]+descendants(ch)]
    def evaluate(uid,fid):
        definition=definitions[fid];kind=int(text(definition,"CFType"));method=int(text(definition,"RollupType"));desc=descendants(uid);leaf=[x for x in desc if text(nodes[x][0],"Summary")!="1"];values=[nodes[x][1].get(fid) for x in leaf];assert all(v is not None for v in values)
        if method==2:return Decimal(len(desc))
        if method==6:return Decimal(len(children[uid]))
        if method==7:return Decimal(len(leaf))
        if not values:return None
        if method==0:return max(values)
        if method==1:return min(values)
        if method==3:return sum(values)
        if method==5:values=[evaluate(x,fid) if text(nodes[x][0],"Summary")=="1" else nodes[x][1].get(fid) for x in children[uid]];values=[v for v in values if v is not None]
        return (sum(values)/len(values)).quantize(Decimal(".01") if kind==0 else Decimal(".000001"),rounding=ROUND_HALF_UP)
    for uid,(row,values) in nodes.items():
        if text(row,"Summary")!="1":continue
        for fid,d in definitions.items():
            if text(d,"ElemType")=="20" and text(d,"CalculationType")=="1":
                expected=evaluate(uid,fid);actual=values.get(fid)
                # Date-only values are midnight in MSPDI.
                assert actual==expected,(filename,uid,text(d,"Alias"),actual,expected)
                checked+=1
    assert len(nodes)==7 and len(definitions)==18
converted=json.loads((a.root/"converted.json").read_text(encoding="utf-8"));assert converted["currency"]=="EUR" and attrs(converted["taskSummaryAttributes"])["field:cost"]=="10.01" and attrs(converted["tasks"][0]["attributes"])["field:cost"]=="0.51"
result={"status":"PASS","native_schema":43,"xml_summary_cells":checked,"xml_rows":7,"definitions":18,"retained_original_summary":999,"computed_export_summary":30,"currency":"stored snapshots and leaf values converted once; summaries recomputed","external_product_run":False}
if a.reports:
    import openpyxl
    from pypdf import PdfReader
    book=openpyxl.load_workbook(a.root/"fields.xlsx",data_only=True);sheet=next(s for s in book if s.title.startswith("XML 사용자 필드 요약"));rows={(r[1].value,r[2].value):r for r in sheet if r[1].value in (native["name"],"상위","하위")}
    assert len(rows)==48 and rows[native["name"],"수치 3"][4].value==30 and rows[native["name"],"수치 5"][4].value==10 and rows[native["name"],"cost"][4].value==4.04 and rows[native["name"],"수치 7"][5].value=="개"
    resource=openpyxl.load_workbook(a.root/"resources.xlsx",data_only=True);rs=next(s for s in resource if s.title.startswith("자원 사용자 필드 요약"));rr=[r for r in rs if r[1].value=="자원 수치"];assert len(rr)==2 and all(r[3].value==2 for r in rr)
    pdf=PdfReader(a.root/"fields.pdf");content=re.sub(r"\s+","","".join(page.extract_text() for page in pdf.pages));assert "XML사용자필드요약" in content and "자동roll-up" in content
    result["reports"]={"summary_rows":48,"filtered_resource_summary_rows":2,"numeric_cost":True,"pdf_pages":len(pdf.pages)}
if a.reader:
    import jpype,mpxj
    jpype.startJVM("-Dlog4j2.statusLoggerLevel=OFF")
    try:
        from org.mpxj.reader import UniversalProjectReader
        proxy=UniversalProjectReader().getProjectReaderProxy(str((a.root/"export.xml").resolve()));reader=proxy.getProjectReader();reader.setIgnoreErrors(False)
        try:model=proxy.read()
        finally:proxy.close()
        assert not list(model.getIgnoredErrors()) and len(list(model.getTasks()))==7 and len(list(model.getResources()))==2
        result["reader"]={"strict_read":"PASS","ignored_errors":0,"tasks_including_summaries":7,"resources":2,"limitation":"MPXJ does not execute these custom summary calculations; rules and computed values checked separately"}
    finally:jpype.shutdownJVM()
print(json.dumps(result,ensure_ascii=False,indent=2))
