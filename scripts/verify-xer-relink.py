"""Independent source version/ID receipts, immutable native data and XER readback."""
from pathlib import Path
from decimal import Decimal as D
import hashlib
import importlib.metadata
import json
import sys

root = Path(sys.argv[1] if len(sys.argv) > 1 else 'build-governance').resolve()/'xer-relink-qa'


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':')).encode()


def tables(path):
    result, rows, fields = {}, None, []
    for line in path.read_text(encoding='utf-8-sig').splitlines():
        cells = line.split('\t')
        if cells[0] == '%T':
            rows = result[cells[1]] = []
        elif cells[0] == '%F':
            fields = cells[1:]
        elif cells[0] == '%R':
            assert len(cells)-1 == len(fields)
            rows.append(dict(zip(fields, cells[1:])))
    return result


def money(value):
    raw = D(str(value or '0'))
    cents = raw.quantize(D('.01'))
    assert abs(raw-cents) <= D('.00000001')
    return cents


before = json.loads((root/'before.json').read_text(encoding='utf-8'))
envelope = json.loads((root/'relinked.ppm').read_text(encoding='utf-8'))
assert envelope['schemaVersion'] == 28
body = envelope['payload']
assert hashlib.sha256(canonical(body)).hexdigest() == envelope['sha256']
excluded = {'revision', 'documentId', 'derivedFrom', 'xerFinancialRevisions'}
assert {k: v for k, v in before.items() if k not in excluded} == {k: v for k, v in body.items() if k not in excluded}
assert body['xerFinancials']['sourceSha256'] == hashlib.sha256((root/'source.xer').read_bytes()).hexdigest()
history = body['xerFinancialRevisions']
assert len(history) == 2
receipt = history[0]['relink']
assert receipt['policy'] == 'explicit-source-ids-v1' and receipt['confirmed'] is True
assert receipt['replaceMissingData'] is False
incoming = receipt['incoming']
assert incoming['sourceSha256'] == hashlib.sha256((root/'version2.xer').read_bytes()).hexdigest()
assert incoming['sourceSha256'] != body['xerFinancials']['sourceSha256']
assert receipt['sourceStatusAt'] == incoming['statusAt'] == '2026-10-28 18:00'
source = tables(root/'version2.xer')
assert {t['id'] for t in incoming['tasks']} == {'xer-task-'+r['task_id'] for r in source['TASK']}
assert {r['id'] for r in incoming['resources']} == {'xer-resource-'+r['rsrc_id'] for r in source['RSRC']}
maps = []
for name, origin in [('incomingLinks', incoming), ('previousLinks', body['xerFinancials'])]:
    links = receipt[name]
    expected = {('task', t['id']) for t in origin['tasks']} | {('resource', r['id']) for r in origin['resources']}
    assert {(l['kind'], l['sourceId']) for l in links} == expected and len(links) == len(expected)
    targets = [(l['kind'], l['targetId']) for l in links if l['targetId']]
    assert len(targets) == len(set(targets))
    live = {('task', t['id']) for t in body['tasks']} | {('resource', r['id']) for r in body['resources']}
    assert set(targets) <= live
    maps.append({(l['kind'], l['sourceId']): l['targetId'] for l in links})
new_map, old_map = maps
old_ids = {t['id'] for t in body['xerFinancials']['tasks']}
live_ids = {t['id'] for t in body['tasks']}
deleted, added = old_ids-live_ids, live_ids-old_ids
assert len(deleted) == len(added) == 1
assert old_map[('task', next(iter(deleted)))] == next(iter(added))
assert new_map[('task', 'xer-task-1020')] == body['tasks'][0]['id']
assert new_map[('resource', 'xer-resource-1099')] == body['resources'][0]['id']
working = history[-1]['financials']
assert working['sourceSha256'] == incoming['sourceSha256']
assert working['tasks'][0]['name'] == body['tasks'][0]['name'] != incoming['tasks'][0]['name']
projection = {k: v for k, v in body.items() if k not in excluded | {'xerFinancials'}}
assert hashlib.sha256(canonical(projection)).hexdigest() == working['projectionSha256']
entries = {e['id']: e for e in body['periodActuals']}
previous_hash = body['xerFinancials']['projectionSha256']
for r in history:
    assert r['merge']['baseProjectionSha256'] == previous_hash
    for entry_id in r['ownedEntryIds']:
        e = entries[entry_id]
        assert e['recordedRevision'] < r['recordedRevision'] and e['statusAt'] <= r['financials']['statusAt']
    previous_hash = r['financials']['projectionSha256']
assert sum(e['actualMinor'] or 0 for e in entries.values()) == 3334
assert sum(e['workUnitMinutes'] or 0 for e in entries.values()) == 7500
report = json.loads((root/'export-report.json').read_text(encoding='utf-8'))
assert report['actualCosts']['sourceRelinkCount'] == 1
assert report['actualCosts']['preservedFinancials'] == 'reexported'
t = tables(root/'export.xer')
assert len({(r['task_id'], r['fin_dates_id']) for r in t['TASKFIN']}) == len(t['TASKFIN'])
assert D(t['TASKRSRC'][0]['act_reg_qty'])+D(t['TASKRSRC'][0]['act_ot_qty']) == D('3.75')

assert importlib.metadata.version('mpxj') == '16.10.0'
assert importlib.metadata.version('JPype1') == '1.6.0'
import jpype
import mpxj
jpype.startJVM()
try:
    from org.mpxj.reader import UniversalProjectReader
    from org.mpxj import TimeUnit
    proxy = UniversalProjectReader().getProjectReaderProxy(str(root/'export.xer'))
    try:
        proxy.getProjectReader().setIgnoreErrors(False)
        model = proxy.read()
        assert not list(model.getIgnoredErrors())
        assignments = list(model.getResourceAssignments())
        work = assignments[0].getActualWork().convertUnits(TimeUnit.HOURS, model.getProjectProperties()).getDuration()
        assert D(str(work)) == D('3.75')
        assert sum(money(a.getActualCost()) for a in assignments) == D('110.46')
        tasks = [t for t in model.getTasks() if not t.getSummary()]
        target = [t for t in tasks if str(t.getName()) == body['tasks'][0]['name']]
        assert len(target) == 1 and money(target[0].getActualCost()) == D('122.80')
    finally:
        proxy.close()
finally:
    jpype.shutdownJVM()
print(json.dumps({'status': 'passed', 'schema': 28, 'source_updates': 1,
                  'deleted_task_ids_reconnected': 1, 'native_input_and_journals_unchanged': True,
                  'native_ac_minor': 12280, 'native_allocated_minor': 3334,
                  'readers': ['native and projection SHA256', 'full native equality', 'source TSV and explicit ID map oracle', 'MPXJ16.10.0 strict'],
                  'external_p6_execution': 'not run'}, indent=2))
