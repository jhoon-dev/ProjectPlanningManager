"""Independent exact allocation and edited XER/native regression oracle."""
from decimal import Decimal as D, ROUND_HALF_UP
from pathlib import Path
import hashlib
import importlib.metadata
import json
import sys

root = Path(sys.argv[1] if len(sys.argv) > 1 else 'build-governance').resolve() / 'xer-allocation-qa'


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':')).encode()


def tables(path):
    out = {}
    rows, fields = None, []
    lines = path.read_text(encoding='utf-8-sig').splitlines()
    assert lines[0].startswith('ERMHDR\t') and lines[-1] == '%E'
    for line in lines[1:-1]:
        cells = line.split('\t')
        if cells[0] == '%T':
            assert cells[1] not in out
            rows = out[cells[1]] = []
        elif cells[0] == '%F':
            fields = cells[1:]
        elif cells[0] == '%R':
            assert len(cells) - 1 == len(fields)
            rows.append(dict(zip(fields, cells[1:])))
    return out


def reader_money(value):
    raw = D(str(value or '0'))
    rounded = raw.quantize(D('.01'))
    assert abs(raw - rounded) <= D('.00000001')
    return rounded


envelope = json.loads((root/'edited.ppm').read_text(encoding='utf-8'))
body = envelope['payload']
assert envelope['schemaVersion'] == 26
assert hashlib.sha256(canonical(body)).hexdigest() == envelope['sha256']
source = json.loads((root/'source-body.json').read_text(encoding='utf-8'))
assert body['xerFinancials'] == source['xerFinancials']
assert hashlib.sha256((root/'source.xer').read_bytes()).hexdigest() == body['xerFinancials']['sourceSha256']
revisions = body['xerFinancialRevisions']
assert len(revisions) == 3 and len({r['id'] for r in revisions}) == 3
current = revisions[-1]
projection = {k: v for k, v in body.items() if k not in ('xerFinancials', 'xerFinancialRevisions', 'revision', 'documentId', 'derivedFrom')}
assert hashlib.sha256(canonical(projection)).hexdigest() == current['financials']['projectionSha256']
assert current['financials']['tasks'][0]['actualMinor'] == 14001
assert body['xerFinancials']['tasks'][0]['actualMinor'] == 12280
assert body['progress'] == source['progress'] and body['baselines'] == source['baselines']

latest = {}
entries = {e['id']: e for e in body['periodActuals']}
for e in body['periodActuals']:
    key = (e['taskId'], e['resourceId'], e['date'])
    assert e['previousEntryId'] == (latest[key]['id'] if key in latest else '')
    latest[key] = e
assert sum(e['workUnitMinutes'] or 0 for e in latest.values()) == 25501
assert sum(e['actualMinor'] or 0 for e in latest.values()) == 12581
assert 14001 - 12581 == 1420
for key, e in latest.items():
    if key[1] and key[2] in ('2026-10-19', '2026-10-20'):
        assert e['workUnitMinutes'] is None and e['actualMinor'] is None
assert all(e in body['periodActuals'] for e in source['periodActuals'])

# Fractional native units use a separate Decimal largest-remainder oracle.
for r in revisions:
    expected = {}
    for allocation in r['allocations']:
        p = r['financials']['periods'][allocation['periodIndex']]
        days = sorted(allocation['days'], key=lambda d: d['date'])
        weight = sum(d['weight'] for d in days)
        amounts = [p['work'] if p['resourceId'] else None, p['actualMinor'] if p['resourceId'] else p['expenseMinor']]
        pieces = []
        for total in amounts:
            if total is None:
                pieces.append([None]*len(days))
                continue
            exact = [D(total)*d['weight']/weight for d in days]
            values = [int(v) for v in exact]
            order = sorted(range(len(days)), key=lambda i: (-(exact[i]-values[i]), days[i]['date']))
            for i in order[:total-sum(values)]:
                values[i] += 1
            assert sum(values) == total
            pieces.append(values)
        for i, d in enumerate(days):
            if all(part[i] is None for part in pieces):
                continue
            key = (p['taskId'], p['resourceId'], d['date'])
            assert key not in expected
            expected[key] = (pieces[0][i], pieces[1][i])
    assert len(r['ownedEntryIds']) == len(expected)
    for entry_id in r['ownedEntryIds']:
        e = entries[entry_id]
        assert (e['workUnitMinutes'], e['actualMinor']) == expected[(e['taskId'], e['resourceId'], e['date'])]
        assert all(e[field] == r[field] for field in ('reporter', 'reason', 'recordedAt', 'sourceDocumentId', 'recordedRevision'))

t = tables(root/'export.xer')
a = t['TASKRSRC'][0]
assert D(a['act_reg_cost']) == D('100.13') and D(a['act_ot_cost']) == D('20.34')
assert D(a['act_reg_qty'])*6000 == D('22501.002000')  # Six-place XER representation.
assert sum(D(r['act_cost'] or '0') for r in t['PROJCOST']) == D('19.54')
assert sum(D(r['act_cost'] or '0') for r in t['TRSRCFIN']) == D('120.47')
assert json.loads((root/'export-report.json').read_text(encoding='utf-8'))['actualCosts']['financialAdjustmentCount'] == 3
assert any(r['start_date'] == '2026-10-19 00:00' and r['end_date'] == '2026-10-26 23:59' for r in t['FINDATES'])
assert all(D(r['bcwp']) == D('9999') for r in t['TASKFIN'])

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
        hours = assignments[0].getActualWork().convertUnits(TimeUnit.HOURS, model.getProjectProperties()).getDuration()
        assert abs(D(str(hours))*6000 - 25501) <= D('.003')
        assert sum(reader_money(r.getActualCost()) for r in assignments) == D('120.47')
        tasks = [r for r in model.getTasks() if not r.getSummary()]
        assert reader_money(tasks[0].getActualCost()) == D('140.01')
    finally:
        proxy.close()
finally:
    jpype.shutdownJVM()
print(json.dumps({'status': 'passed', 'schema': 26, 'audit_revisions': 3, 'native_work_units': 25501,
                  'ac_minor': 14001, 'allocated_minor': 12581, 'unallocated_minor': 1420,
                  'original_snapshot_unchanged': True, 'readers': ['Decimal weighted oracle', 'native and projection SHA256', 'TSV', 'MPXJ16.10.0 strict'],
                  'external_p6_execution': 'not run'}, indent=2))
