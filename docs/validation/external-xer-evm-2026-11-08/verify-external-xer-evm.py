"""Independent XER period/reference/anchor oracle and real native XLSX/PDF readers."""
from datetime import datetime, timedelta
from decimal import Decimal as D
from itertools import groupby
from pathlib import Path
import hashlib
import json
import sys
import unicodedata
from openpyxl import load_workbook
from pypdf import PdfReader

build = Path(sys.argv[1] if len(sys.argv) > 1 else 'build-governance').resolve()
root = build/'external-xer-evm-qa'
envelope = json.loads((root/'adopted.ppm').read_text(encoding='utf-8'))
p = envelope['payload']
assert envelope['schemaVersion'] == 29
canonical = lambda v: json.dumps(v, ensure_ascii=False, sort_keys=True, separators=(',', ':')).encode()
assert hashlib.sha256(canonical(p)).hexdigest() == envelope['sha256']
before = json.loads((root/'before.json').read_text(encoding='utf-8'))
assert p == json.loads((root/'adopted.json').read_text(encoding='utf-8'))
exclude = {'revision', 'externalEvmAdoptions'}
assert {k:v for k,v in p.items() if k not in exclude} == {k:v for k,v in before.items() if k not in exclude}
projection_exclude = exclude | {'xerFinancials', 'xerFinancialRevisions', 'documentId', 'derivedFrom'}
projection = lambda v: hashlib.sha256(canonical({k:x for k,x in v.items() if k not in projection_exclude})).hexdigest()
assert projection(p) == projection(before)
assert len(p['externalEvmAdoptions']) == 1
a = p['externalEvmAdoptions'][0]
s = p['xerFinancials']
assert a['policy'] == 'xer-native-evm-v1' and a['confirmed'] and not a['withdrawn'] and a['useEv'] and a['usePv']
assert a['financialRevisionId'] == '' and a['sourceSha256'] == s['sourceSha256'] == hashlib.sha256((root/'source.xer').read_bytes()).hexdigest()
assert a['sourceProjectionSha256'] == s['projectionSha256'] and a['currency'] == s['currency'] == p['currency'] == 'USD'
assert a['sourceStatusAt'] == a['nativeStatusAt'] == p['statusAt'] == '2026-10-28 18:00'
assert a['recordedRevision'] == before['revision']+1 == p['revision']
assert a['sourceDocumentId'] == p['documentId'] and a['taskIds'] == ['xer-task-20']

tables, rows, fields = {}, None, []
for line in (root/'source.xer').read_text(encoding='utf-8-sig').splitlines():
    parts = line.split('\t')
    if parts[0] == '%T':
        rows = tables[parts[1]] = []
    elif parts[0] == '%F':
        fields = parts[1:]
    elif parts[0] == '%R':
        assert len(parts)-1 == len(fields)
        rows.append(dict(zip(fields, parts[1:])))
source_ev = [int(D(r['bcwp'])*100) for r in tables['TASKFIN']]
source_pv = [int(D(r['bcws'])*100) for r in tables['TASKFIN']]
assert source_ev == [1200, 800] and source_pv == [1000, 1500]

stamp = lambda v: datetime.strptime(v[:16], '%Y-%m-%d %H:%M')
status = stamp(p['statusAt'])
baseline = next(b for b in p['baselines'] if b['id'] == a['baselineId'])
tasks = {t['id']:t for t in baseline['input']['tasks']}
budgets = {c['taskId']:c['fixedMinor'] for c in baseline['input']['taskCosts']}
assert budgets['xer-task-20'] == 10000 and sum(budgets.values()) == 10000
assert not baseline['input'].get('resourcePlan', {}).get('expenses', [])
assert all(rate['hourlyMinor'] == 0 for r in baseline['input']['resources'] for rate in r['rates'])
nodes = {n['id']:n for n in baseline['input']['wbsNodes']}
points = {}
for i, r in enumerate(a['rows']):
    source = s['periods'][r['periodIndex']]
    assert source['taskId'] == 'xer-task-20' and source['resourceId'] == ''
    assert source['bcwp'] == source_ev[i] and source['bcws'] == source_pv[i]
    assert (r['openingEv'], r['openingPv']) == ((0, 0) if i == 0 else (1200, 1000))
    start = stamp(source['start'][:10]+' 00:00')
    end = min(stamp(source['finish'][:10]+' 23:59'), stamp(s['statusAt']))
    opening = r['openingEv'], r['openingPv']
    closing = r['openingEv']+source['bcwp'], r['openingPv']+source['bcws']
    for key, value in [((start,False),opening),((start-timedelta(minutes=1),True),opening),
                       ((end,True),closing),((end+timedelta(minutes=1),False),closing)]:
        assert key not in points or points[key] == value
        points[key] = value


def point(task, at, inclusive, observed):
    reports = [(stamp(e['statusAt']), i, e) for i,e in enumerate(p['costEntries']) if e['taskId'] == task
               and (stamp(e['statusAt']) <= at if inclusive else stamp(e['statusAt']) < at)]
    report = max(reports, default=(None,None,None), key=lambda x:(x[0],x[1]))[2] if observed else None
    if task == 'xer-task-20':
        ev, pv = points.get((at,inclusive),(None,None))
        if not observed:
            ev = None
        ev_id = a['id'] if observed else ''
    else:
        assert budgets[task] == 0
        pv, ev = 0, (0 if observed else None)
        ev_id = '' if report is None else report['id']
    return {'bac':budgets[task], 'pv':pv, 'ev':ev, 'ac':None if report is None else report['actualMinor'],
            'ac_id':'' if report is None else report['id'], 'ev_id':ev_id}


def members(kind, row):
    if kind == 'periodEvm':
        return list(tasks)
    if kind == 'periodEvmTasks':
        return [row[3]]
    out = []
    for task,t in tasks.items():
        node = t['wbsId']
        while node:
            if node == row[3]:
                out.append(task)
                break
            node = nodes[node]['parentId']
    return out


def total(values,key):
    values = [v[key] for v in values]
    return None if None in values else sum(values)


def delta(x,y):
    return None if x is None or y is None else x-y


def ratio(x,y):
    return x/y if x is not None and y is not None and x >= 0 and y > 0 else None


dated = {}
for e in p['periodActuals']:
    if stamp(e['statusAt']) <= status:
        key = e['taskId'], e['resourceId'], e['date']
        if key not in dated or e['statusAt'] >= dated[key]['statusAt']:
            dated[key] = e
checked = 0
reports = {}
for filename in ['monthly.json', 'daily.json', 'weekly.json']:
    if not (root/filename).exists():
        continue
    report = json.loads((root/filename).read_text(encoding='utf-8'))
    reports[filename] = report
    assert {'externalEvmHistory','externalEvmPeriods'} <= {t['id'] for t in report['tables']}
    for table in report['tables'][:3]:
        for row in table['rows']:
            start, end = stamp(row[0]+' 00:00'), stamp(row[2])
            observed = start <= status
            ids = members(table['id'],row)
            opening = [point(t,start,False,observed) for t in ids]
            closing = [point(t,end,row[2].endswith('이하'),observed) for t in ids]
            bac = total(closing,'bac')
            pv0,pv1 = total(opening,'pv'),total(closing,'pv')
            ev0,ev1 = total(opening,'ev'),total(closing,'ev')
            ac0,ac1 = total(opening,'ac'),total(closing,'ac')
            pv,ev,ac = delta(pv1,pv0),delta(ev1,ev0),delta(ac1,ac0)
            amount = sum(e['actualMinor'] or 0 for e in dated.values() if e['taskId'] in ids and row[0] <= e['date'] <= row[1]) if observed else None
            expected = {5:bac,6:pv0,7:pv1,8:pv,9:ev0,10:ev1,11:ev,12:ac0,13:ac1,14:ac,15:delta(ev,ac),16:delta(ev,pv),
                        17:ratio(ev,ac),18:ratio(ev,pv),19:None if ev1 is None or ac1 is None else ac1+bac-ev1,
                        20:delta(bac,ev1),21:amount,22:delta(ac,amount)}
            for c,value in expected.items():
                assert row[c] == value or isinstance(value,float) and abs(row[c]-value) < 1e-12, (filename,table['id'],row[:5],c,row[c],value)
                checked += 1
            if table['id'] == 'periodEvmTasks':
                assert row[27:31] == [opening[0]['ac_id'],closing[0]['ac_id'],opening[0]['ev_id'],closing[0]['ev_id']]

monthly = reports['monthly.json']['tables'][0]['rows']
assert monthly[0][8:15:3] == [2500,2000,12280]
assert monthly[0][19:21] == [20280,8000]
xlsx_files = [root/'report.xlsx', build/'external-xer-evm-ui-qa/ui-report.xlsx']
for filename in xlsx_files:
    book = load_workbook(filename, data_only=False)
    rows = list(book['native 기간 EVM'].iter_rows(values_only=True))
    header = next(i for i,r in enumerate(rows) if r[0] == '시작일')
    actual = rows[header+1]
    for c in range(5,23):
        value = monthly[0][c]
        value = '미정' if value is None else value if c in (17,18) else value/100
        assert actual[c] == value or isinstance(value,(int,float)) and isinstance(actual[c],(int,float)) and abs(actual[c]-value) < 1e-12
    assert '외부 XER EV PV 채택 이력' in book.sheetnames and '채택 XER EV PV 기간' in book.sheetnames
    assert not any(c.data_type == 'f' for sh in book for row in sh for c in row)
generic = load_workbook(root/'generic.xlsx',data_only=False)
rows = list(generic['작업별 원가'].iter_rows(values_only=True))
native = next(r for r in rows if r[0] == 'xer-task-20')
assert native[3:7] == (100,25,20,122.8)
assert not any(c.data_type == 'f' for sh in generic for row in sh for c in row)
pdf_pages = {}
for filename in [root/'report.pdf',build/'external-xer-evm-ui-qa/ui-report.pdf']:
    pdf = PdfReader(filename,strict=True)
    text = unicodedata.normalize('NFKC','\n'.join(page.extract_text() or '' for page in pdf.pages))
    # Qt synthetic bold may repeat adjacent painted runs containing several words.
    tokens = text.split()
    for width in range(1, 9):
        i = 0
        while i+2*width <= len(tokens):
            if tokens[i:i+width] == tokens[i+width:i+2*width]:
                del tokens[i+width:i+2*width]
            else:
                i += 1
    compact = ''.join(tokens)
    for phrase in ['native기간EVM','외부XEREVPV채택이력','채택XEREVPV기간','미정','20.00','25.00','122.80']:
        assert phrase in compact,(filename.name,phrase)
    assert not any(ord(c)<32 and c not in '\n\r\t' for c in text)
    pdf_pages[str(filename.relative_to(build))] = len(pdf.pages)
print(json.dumps({'status':'passed','policy':'xer-native-evm-v1','schema':29,'native_inputs_journals_and_original_unchanged':True,
                  'native_projection_sha256_unchanged':True,'source_period_ev_minor':source_ev,'source_period_pv_minor':source_pv,
                  'adopted_ev_minor':2000,'adopted_pv_minor':2500,'native_ac_minor':12280,'oracle_numeric_cells':checked,
                  'xlsx_files':3,'xlsx_formulas':0,'pdf_pages':pdf_pages,'external_product_execution':'not run'},ensure_ascii=False,indent=2))
