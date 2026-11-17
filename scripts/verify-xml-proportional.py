"""Independently reconcile multi-day allocation, native provenance and rendered reports."""
import argparse
from collections import defaultdict
from datetime import datetime, timedelta
from decimal import Decimal
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument('root', type=Path)
root = parser.parse_args().root
load = lambda n: json.loads((root / n).read_text(encoding='utf-8-sig'))
before, merged, corrected = [load(n + '.json') for n in ('before', 'merged', 'corrected')]
raw = (root / 'original.xml').read_bytes()
digest = hashlib.sha256(raw).hexdigest()
ns = {'p': 'http://schemas.microsoft.com/project'}
text = lambda n, k: n.findtext('p:' + k, namespaces=ns)
source = ET.fromstring(raw)
records = {(text(a, 'UID'), i): r for a in source.findall('p:Assignments/p:Assignment', ns)
           for i, r in enumerate(a.findall('p:TimephasedData', ns), 1)}
source_tasks = {text(t, 'UID'): t for t in source.findall('p:Tasks/p:Task', ns)}
tasks = {t['id']: t for t in before['tasks']}
resources = {r['id']: r for r in before['resources']}
calendars = {c['id']: c for c in before['calendars']}

def work_units(value):
    match = re.fullmatch(r'PT(?:(\d+(?:\.\d+)?)H)?(?:(\d+(?:\.\d+)?)M)?(?:(\d+(?:\.\d+)?)S)?', value)
    assert match, value
    h, m, s = [Decimal(v or '0') for v in match.groups()]
    q = h * 6000 + m * 100 + s * Decimal(5) / Decimal(3)
    assert q == q.to_integral_value()
    return int(q)

def money(value):
    # MSPDI is 100 * display currency. USD minor units coincide with its wire scale.
    minor = Decimal(value) / 100 * 100
    assert minor == minor.to_integral_value()
    return int(minor)

def working(stamp, calendar):
    return (stamp.isoweekday() in calendar['weekdays'] and
            stamp.strftime('%Y-%m-%d') not in calendar['holidays'] and
            any(p['begin'] <= stamp.hour * 60 + stamp.minute < p['end'] for p in calendar['periods']))

def weights(r, basis):
    start, finish = [datetime.fromisoformat(r[k]) for k in ('start', 'finish')]
    result = defaultdict(int)
    task = tasks[r['taskId']]
    resource = resources[r['resourceId']]
    while start < finish:
        day = start.strftime('%Y-%m-%d')
        result[day] += 0
        if basis == 'elapsed-minute-v1' or (
                working(start, calendars[task['calendarId']]) and
                working(start, calendars[resource['calendarId']]) and
                not any(datetime.fromisoformat(b['start']) <= start < datetime.fromisoformat(b['finish'])
                        for b in task['breaks'])):
            result[day] += 1
        start += timedelta(minutes=1)
    return dict(result)

def apportioned(amount, weight):
    total = sum(weight.values())
    assert total > 0
    exact = {d: Fraction(amount * w, total) for d, w in weight.items()}
    values = {d: q.numerator // q.denominator for d, q in exact.items()}
    order = sorted((d for d in values if weight[d]), key=lambda d: (-(exact[d] - values[d]), d))
    for d in order[:amount - sum(values.values())]:
        values[d] += 1
    assert sum(values.values()) == amount
    return values

def verify_projection(projection, original=True):
    assert projection['classification'] == 'proportional-estimate-not-measured'
    assert projection['rangeTotalsConfirmed'] and projection['rounding'] == 'largest-remainder-date-ascending'
    assert projection['zeroWeightPolicy'] == 'omit-not-reported-zero'
    expected = defaultdict(dict)
    for r in projection['ranges']:
        if original:
            record = records[r['assignmentUid'], r['record']]
            assert r['unit'] == int(text(record, 'Unit'))
            quantity = work_units(text(record, 'Value')) if r['type'] == 2 else money(text(record, 'Value'))
            assert r['amount'] == quantity  # Unit does not implicitly repeat a confirmed range total.
        weight = weights(r, projection['basis'])
        shares = apportioned(r['amount'], weight)
        assert r['totalWeight'] == sum(weight.values())
        assert {d['date']: d['weight'] for d in r['days']} == weight
        assert {d['date']: d['amount'] for d in r['days']} == shares
        for d in r['days']:
            assert d['remainder'] == (r['amount'] * d['weight']) % r['totalWeight']
            if d['weight']:
                key = r['taskId'], r['resourceId'], d['date']
                field = 'allocatedWork' if r['type'] == 2 else 'allocatedCostSourceMinor'
                expected[key][field] = expected[key].get(field, 0) + d['amount']
    actual = {(d['taskId'], d['resourceId'], d['date']): d for d in projection['days']}
    assert actual.keys() == expected.keys()
    for key, e in expected.items():
        for field in ('allocatedWork', 'allocatedCostSourceMinor'):
            assert actual[key][field] == e.get(field)
    return actual

working_projection, elapsed_projection = [load(n + '.json') for n in ('working', 'elapsed')]
working_days = verify_projection(working_projection)
elapsed_days = verify_projection(elapsed_projection)
assert len(working_days) == 3 and len(elapsed_days) == 6
cases = load('cases.json')
assert len(cases) == 16
for case in cases:
    verify_projection(case, original=False)

key = lambda e: (e['taskId'], e['resourceId'], e['date'])
def asof(rows, status):
    selected = {}
    for e in rows:
        if e['statusAt'] <= status and (key(e) not in selected or selected[key(e)]['statusAt'] <= e['statusAt']):
            selected[key(e)] = e
    return selected

assert len(before['periodActuals']) == 4 and len(merged['periodActuals']) == 7
assert len(merged['costEntries']) == 5 and len(corrected['periodActuals']) == 8
assert merged['revision'] == before['revision'] + 1 == 6
changed = {'periodActuals', 'costEntries', 'revision'}
assert {k: v for k, v in before.items() if k not in changed} == {k: v for k, v in merged.items() if k not in changed}
assert merged['periodActuals'][:4] == before['periodActuals']
assert merged['costEntries'][:2] == before['costEntries']
latest = asof(merged['periodActuals'], merged['statusAt'])
old = asof(before['periodActuals'], before['statusAt'])
costs = {e['id']: e for e in merged['costEntries']}
for entry in merged['periodActuals'][4:]:
    day = working_days[key(entry)]
    assert (entry['workUnitMinutes'], entry['actualMinor']) == (day['adoptedWork'], day['adoptedCostNativeMinor'])
    marker = re.match(r'\[XML-PRORATA sha256=([0-9a-f]{64}) assignment=(\d+) basis=([\w-]+) input=([0-9a-f]{64}) records=([\d,]+)\]', entry['reason'])
    assert marker and marker[1] == digest and marker[3] == working_projection['basis']
    group_ranges = [{k: v for k, v in r.items() if k not in ('assignmentUid', 'taskId', 'resourceId', 'alreadyApplied')}
                    for r in working_projection['ranges'] if r['assignmentUid'] == marker[2]]
    canonical = json.dumps({'basis': marker[3], 'ranges': group_ranges}, sort_keys=True,
                           ensure_ascii=False, separators=(',', ':')).encode()
    assert hashlib.sha256(canonical).hexdigest() == marker[4]
    assert marker[5] == ','.join(str(r['record']) for r in group_ranges)
    assert f"date={entry['date']} work=estimated cost=estimated parts={day['parts']}" in entry['reason']
    assert entry['recordedRevision'] == 6 and entry['sourceDocumentId'] == merged['documentId']
    cost = costs[entry['costEntryId']]
    for field in ('reason', 'reporter', 'statusAt', 'recordedAt', 'recordedRevision', 'sourceDocumentId', 'currency'):
        assert cost[field] == entry[field]
    assert cost['actualMinor'] == money(text(source_tasks[marker[2]], 'ActualCost'))
assert latest['NA', 'NR1', '2026-11-14'] == old['NA', 'NR1', '2026-11-14']
assert sum(e['workUnitMinutes'] or 0 for e in latest.values()) == 326
assert sum(e['actualMinor'] or 0 for e in latest.values()) == 5702
assert sum(e['actualMinor'] or 0 for e in asof(corrected['periodActuals'], corrected['statusAt']).values()) == 5702
assert asof(corrected['periodActuals'], corrected['statusAt'])['NA', 'NR1', '2026-11-13']['workUnitMinutes'] == 80
assert [(b['taskId'], b['cumulative'], b['afterAllocated'], b['unallocated']) for b in working_projection['costReconciliation']] == [
    ('NA', 15000, 5701, 9299), ('NB', 10000, 1, 9999)]
envelope = load('source.ppm')
assert envelope['schemaVersion'] == 11 and envelope['payload'] == merged

receipt = load('conversion.json')['actualExport']
receipt_days = receipt['assignmentDays']
estimated = [d for d in receipt_days if d.get('proportionalWork')]
assert len(estimated) == 3 and all(d['proportionalCost'] for d in estimated)
assert {d['entryId'] for d in estimated} == {e['id'] for e in merged['periodActuals'][4:]}
assert {s['entryId'] for s in receipt['allocationSources']} == {d['entryId'] for d in estimated}
corrected_day = asof(corrected['periodActuals'], corrected['statusAt'])['NA', 'NR1', '2026-11-13']
corrected_receipt = load('corrected-conversion.json')['actualExport']
corrected_flags = next(d for d in corrected_receipt['assignmentDays'] if d['entryId'] == corrected_day['id'])
assert not corrected_flags['proportionalWork'] and corrected_flags['proportionalCost']
assert corrected_flags['costAllocationEntryId'] == latest['NA', 'NR1', '2026-11-13']['id']
assert not corrected_flags['workAllocationEntryId']
exported = ET.fromstring((root / 'project.xml').read_bytes())
exported_rows = [r for a in exported.findall('p:Assignments/p:Assignment', ns) for r in a.findall('p:TimephasedData', ns)]
assert sum(work_units(text(r, 'Value')) for r in exported_rows if text(r, 'Type') == '2') == 326
assert sum(money(text(r, 'Value')) for r in exported_rows if text(r, 'Type') == '6') == 5702

import openpyxl
from pypdf import PdfReader
book = openpyxl.load_workbook(root / 'audit.xlsx', data_only=True)
sheet = next(s for s in book if s.title.startswith('비례 배분 실적 근거'))
by_id = {e['id']: e for e in latest.values()}
rows = [r for r in sheet.values if r[7] in by_id]
assert len(rows) == 3
for row in rows:
    entry = by_id[row[7]]
    assert row[:3] == (entry['date'], entry['taskId'], entry['resourceId'])
    assert row[3:5] == ('비례 추정 기원', '비례 추정 기원')
    assert row[5:7] == (entry['workUnitMinutes'], entry['actualMinor'])
    assert row[8:11] == (entry['id'], entry['id'], entry['reason'])
history = next(s for s in book if s.title.startswith('기간 실적 정정 이력'))
ids = {e['id'] for e in merged['periodActuals']}
assert len([r for r in history.values if r[0] in ids]) == 7
pdf = PdfReader(root / 'audit.pdf')
content = ''.join(''.join(p.extract_text() or '' for p in pdf.pages).split())
assert '비례배분실적근거' in content and '비례추정기원' in content and digest in content
print(json.dumps(dict(status='PASS', source_sha256=digest, schema=11, allocation_bases=2,
                     quantity_cases=len(cases), working_dates=len(working_days), elapsed_dates=len(elapsed_days),
                     work_units=326, allocated_minor=5702, cumulative_minor=25000,
                     preserved_weekend_work=222, corrected_work=80, estimated_report_rows=len(rows),
                     history_rows=7, pdf_pages=len(pdf.pages), external_product_run=False,
                     native_file_sha256=hashlib.sha256((root / 'source.ppm').read_bytes()).hexdigest()),
                 ensure_ascii=False, indent=2))
