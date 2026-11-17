"""Independently reconcile standard MSPDI actuals against the native as-of ledgers."""
import argparse
from collections import defaultdict
from datetime import date, timedelta
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser()
parser.add_argument('root', type=Path)
parser.add_argument('--reader', action='store_true')
args = parser.parse_args()
root = args.root
load = lambda name: json.loads((root / name).read_text(encoding='utf-8-sig'))
native, receipt, envelope = [load(n) for n in ('native.json', 'conversion.json', 'source.ppm')]
assert envelope['payload'] == native and envelope['schemaVersion'] == 11
canonical = json.dumps(native, ensure_ascii=False, sort_keys=True, separators=(',', ':')).encode()
assert hashlib.sha256(canonical).hexdigest() == envelope['sha256'] == receipt['actualExport']['sourceInputSha256']
raw = (root / 'project.xml').read_bytes()
assert hashlib.sha256(raw).hexdigest() == receipt['xmlSha256']
assert receipt['exportMode'] == 'observed-actuals-v1' and receipt['contract'] == 'jobs-mspdi-observed-v1'
ns = {'p': 'http://schemas.microsoft.com/project'}
xml = ET.fromstring(raw)
text = lambda n, tag: n.findtext('p:' + tag, namespaces=ns)
assert text(xml, 'StatusDate') == native['statusAt'].replace(' ', 'T') + ':00'
assert text(xml, 'EditableActualCosts') == '1' and text(xml, 'SpreadActualCost') == '0'
assert text(xml, 'CurrencyCode') == native['currency'] == 'USD'
assert text(ET.parse(root / 'plan.xml').getroot(), 'StatusDate') is None

def asof(entries, key):
    selected = {}
    for entry in entries:
        if entry['statusAt'] <= native['statusAt']:
            identity = key(entry)
            if identity not in selected or selected[identity]['statusAt'] <= entry['statusAt']:
                selected[identity] = entry
    return selected

def units(value):
    if value is None:
        return None
    m = re.fullmatch(r'PT(?:(\d+(?:\.\d+)?)H)?(?:(\d+(?:\.\d+)?)M)?(?:(\d+(?:\.\d+)?)S)?', value)
    assert m, value
    h, minute, second = [Decimal(v or '0') for v in m.groups()]
    amount = h * 6000 + minute * 100 + second * Decimal(5) / Decimal(3)
    assert amount == amount.to_integral_value()
    return int(amount)

def cents(value):
    if value is None:
        return None
    amount = Decimal(value)
    assert amount == amount.to_integral_value()
    return int(amount)

def complete_sum(values):
    return sum(values) if values and all(v is not None for v in values) else None

progress = asof(native['progress'], lambda e: e['taskId'])
costs = asof(native['costEntries'], lambda e: e['taskId'])
days = asof(native['periodActuals'], lambda e: (e['taskId'], e['resourceId'], e['date']))
maps = {kind: {m['xmlUid']: m['jobsId'] for m in receipt['mapping'] if m['kind'] == kind}
        for kind in ('task', 'resource')}
tasks = {maps['task'][text(t, 'UID')]: t for t in xml.findall('p:Tasks/p:Task', ns)
         if text(t, 'UID') in maps['task']}
resources = {maps['resource'][text(r, 'UID')]: r for r in xml.findall('p:Resources/p:Resource', ns)}
pair_days = defaultdict(list)
for (task, resource, day), entry in days.items():
    pair_days[task, resource].append(entry)
totals = {pair: {field: complete_sum([e[field] for e in observations])
                 for field in ('workUnitMinutes', 'actualMinor')}
          for pair, observations in pair_days.items()}
assignments = {}
daily_checks = 0
for a in xml.findall('p:Assignments/p:Assignment', ns):
    pair = maps['task'][text(a, 'TaskUID')], maps['resource'][text(a, 'ResourceUID')]
    assignments[pair] = a
    expected = totals.get(pair, {'workUnitMinutes': None, 'actualMinor': None})
    assert units(text(a, 'ActualWork')) == expected['workUnitMinutes'], pair
    assert cents(text(a, 'ActualCost')) == expected['actualMinor'], pair
    assert all(text(a, field) is None for field in ('Work', 'RemainingWork', 'ActualStart', 'ActualFinish'))
    actual_days = {}
    for td in a.findall('p:TimephasedData', ns):
        kind = text(td, 'Type')
        assert kind in ('2', '6') and text(td, 'UID') == text(a, 'UID') and text(td, 'Unit') == '2'
        start, finish = text(td, 'Start'), text(td, 'Finish')
        assert start[10:] == finish[10:] == 'T00:00:00'
        assert date.fromisoformat(start[:10]) + timedelta(days=1) == date.fromisoformat(finish[:10])
        key = start[:10], kind
        assert key not in actual_days
        actual_days[key] = units(text(td, 'Value')) if kind == '2' else cents(text(td, 'Value'))
    expected_days = {(e['date'], kind): e[field] for e in pair_days[pair]
                     for field, kind in [('workUnitMinutes', '2'), ('actualMinor', '6')] if e[field] is not None}
    assert actual_days == expected_days, (pair, actual_days, expected_days)
    daily_checks += len(actual_days)
for tid, t in tasks.items():
    e = progress.get(tid)
    for field, native_field in [('ActualStart', 'actualStart'), ('ActualFinish', 'actualFinish')]:
        value = e[native_field] if e else None
        assert text(t, field) == (value.replace(' ', 'T') + ':00' if value else None)
    for field, native_field in [('ActualDuration', 'actualDurationMinutes'), ('RemainingDuration', 'remainingMinutes')]:
        assert units(text(t, field)) == (e[native_field] * 100 if e else None)
    if e:
        assert units(text(t, 'Duration')) == (e['actualDurationMinutes'] + e['remainingMinutes']) * 100
    assert cents(text(t, 'ActualCost')) == (costs[tid]['actualMinor'] if tid in costs else None)
    expected_work = complete_sum([totals.get(pair, {}).get('workUnitMinutes') for pair in assignments if pair[0] == tid])
    assert units(text(t, 'ActualWork')) == expected_work
    assert text(t, 'PercentComplete') is None
for rid, r in resources.items():
    expected = complete_sum([totals.get(pair, {}).get('workUnitMinutes') for pair in assignments if pair[1] == rid])
    assert units(text(r, 'ActualWork')) == expected and text(r, 'ActualCost') is None
assert len(progress) == 3 and len(costs) == 3 and len(days) == 5 and daily_checks == 10
assert sum(e['workUnitMinutes'] for e in days.values()) == 26001
assert sum(e['actualMinor'] for e in days.values()) == 4875
assert sum(e['actualMinor'] for e in costs.values()) == 14000
assert len(native['progress']) == 5 and len(native['periodActuals']) > len(days)
assert any(e['statusAt'] > native['statusAt'] for e in native['costEntries'])
balances = {r['taskId']: r for r in receipt['actualExport']['costReconciliation']}
assert balances['TN-A']['cumulative'] == 10000 and balances['TN-A']['datedAllocated'] == 3875
assert balances['TN-A']['unallocated'] == 6125 and balances['TN-B']['unallocated'] == 3000
assert not load('roundtrip.json').get('periodActuals') and not load('roundtrip.json').get('progress')
unknown = ET.parse(root / 'unknown.xml').getroot()
unknown_a = unknown.find('p:Assignments/p:Assignment', ns)
assert text(unknown_a, 'ActualWork') is None and text(unknown_a, 'ActualCost') is None
result = dict(status='PASS', schema=11, selected_task_progress=len(progress), selected_task_cost=len(costs),
              assignment_day_rows=len(days), timephased_values=daily_checks, work_units=26001,
              dated_cost_minor=4875, task_cost_minor=14000, preserved_future_history=True,
              native_sha256=envelope['sha256'], external_product_run=False)
if args.reader:
    import importlib.metadata
    import jpype
    import mpxj  # Registers the independent reader jars.
    assert importlib.metadata.version('mpxj') == '16.10.0'
    jpype.startJVM('-Dlog4j2.statusLoggerLevel=OFF')
    try:
        from org.mpxj.mspdi import MSPDIReader
        from org.mpxj import TimeUnit
        reader = MSPDIReader()
        reader.setIgnoreErrors(False)
        reader.setMicrosoftProjectCompatibleInput(False)
        model = reader.read(str((root / 'project.xml').resolve()))
        assert not list(model.getIgnoredErrors())
        props = model.getProjectProperties()
        assert str(props.getStatusDate()) == '2026-11-12T18:00'
        minutes = lambda d: Decimal(str(d.convertUnits(TimeUnit.MINUTES, props).getDuration()))
        for t in model.getTasks():
            uid = str(t.getUniqueID())
            if uid not in maps['task']:
                continue
            tid = maps['task'][uid]
            if tid in progress:
                assert minutes(t.getActualDuration()) == progress[tid]['actualDurationMinutes']
                assert minutes(t.getRemainingDuration()) == progress[tid]['remainingMinutes']
            if tid in costs:
                assert Decimal(str(t.getActualCost())) * 100 == costs[tid]['actualMinor'], (tid,str(t.getActualCost()),costs[tid]['actualMinor'])
            expected_work = units(text(tasks[tid], 'ActualWork'))
            if expected_work is not None:
                assert abs(minutes(t.getActualWork()) * 100 - expected_work) < Decimal('0.000001')
        for resource in model.getResources():
            rid = maps['resource'].get(str(resource.getUniqueID()))
            if rid:
                expected_work = units(text(resources[rid], 'ActualWork'))
                if expected_work is not None:
                    assert abs(minutes(resource.getActualWork()) * 100 - expected_work) < Decimal('0.000001')
        read_work = read_cost = 0
        for a in model.getResourceAssignments():
            pair = maps['task'][str(a.getTaskUniqueID())], maps['resource'][str(a.getResourceUniqueID())]
            expected = totals.get(pair)
            if expected:
                assert abs(minutes(a.getActualWork()) * 100 - expected['workUnitMinutes']) < Decimal('0.000001')
                assert Decimal(str(a.getActualCost())) * 100 == expected['actualMinor']
            reader_work = {str(d.getStart())[:10]: minutes(d.getTotalAmount()) * 100
                           for d in a.getRawTimephasedActualRegularWork() or []}
            reader_cost = {str(d.getStart())[:10]: Decimal(str(d.getTotalAmount())) * 100
                           for d in a.getRawTimephasedActualCost() or []}
            expected_work = {e['date']: e['workUnitMinutes'] for e in pair_days[pair] if e['workUnitMinutes'] is not None}
            expected_cost = {e['date']: e['actualMinor'] for e in pair_days[pair] if e['actualMinor'] is not None}
            assert reader_work.keys() == expected_work.keys() and reader_cost == expected_cost
            assert all(abs(reader_work[d] - value) < Decimal('0.000001') for d, value in expected_work.items())
            read_work += len(reader_work)
            read_cost += len(reader_cost)
        assert read_work == read_cost == 5, (read_work, read_cost)
        result.update(reader='MPXJ16.10.0 strict', reader_errors=0, reader_daily_work=read_work, reader_daily_cost=read_cost)
    finally:
        jpype.shutdownJVM()
print(json.dumps(result, ensure_ascii=False, indent=2))
