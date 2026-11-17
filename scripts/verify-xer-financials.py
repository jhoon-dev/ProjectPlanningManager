"""Independent XER/native financial snapshot checks, without assigning aggregates to invented dates."""
from decimal import Decimal as D
from pathlib import Path
import hashlib
import importlib.metadata
import json
import sys

root = Path(sys.argv[1] if len(sys.argv) > 1 else 'build-governance').resolve() / 'xer-financial-qa'


def reader_money(value):
    # MPXJ adds regular+OT with binary doubles. The source TSV/native amounts stay exact.
    raw = D(str(value or '0'))
    cents = raw.quantize(D('0.01'))
    assert abs(raw - cents) <= D('0.00000001')
    return cents


def tables(path):
    result = {}
    fields = None
    rows = None
    lines = path.read_text(encoding='utf-8-sig').splitlines()
    assert lines[0].startswith('ERMHDR\t') and lines[-1] == '%E'
    for line in lines[1:-1]:
        cells = line.split('\t')
        if cells[0] == '%T':
            assert cells[1] not in result
            rows = result[cells[1]] = []
            fields = None
        elif cells[0] == '%F':
            assert fields is None and len(set(cells[1:])) == len(cells) - 1
            fields = cells[1:]
        else:
            assert cells[0] == '%R' and len(cells) == len(fields) + 1
            rows.append(dict(zip(fields, cells[1:])))
    return result


native = json.loads((root / 'project.ppm').read_text(encoding='utf-8'))
body = native['payload']


def digest(value):
    return hashlib.sha256(json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


assert native['schemaVersion'] == 25 and digest(body) == native['sha256']
s = body['xerFinancials']
assert s['sourceSha256'] == hashlib.sha256((root / 'source.xer').read_bytes()).hexdigest()
projection = {k: v for k, v in body.items() if k not in ('xerFinancials', 'revision', 'documentId', 'derivedFrom')}
assert digest(projection) == s['projectionSha256']
assert s['statusAt'] == '2026-10-28 18:00' and s['currency'] == 'USD'
a = s['assignments'][0]
assert (a['regularWork'], a['overtimeWork'], a['regularMinor'], a['overtimeMinor']) == (19500, 3000, 9012, 2034)
resource = [r for r in s['periods'] if r['resourceId']]
assert sum(r['work'] for r in resource) == 19500
assert a['regularWork'] + a['overtimeWork'] - sum(r['work'] for r in resource) == 3000
assert resource[0]['start'] == '2026-10-19 00:00' and resource[0]['finish'] == '2026-10-26 23:59'
assert len(body['periodActuals']) == 2
assert sum(r['workUnitMinutes'] or 0 for r in body['periodActuals']) == 7500
assert sum(r['actualMinor'] or 0 for r in body['periodActuals']) == 3334
assert not body['baselines']
t = tables(root / 'export.xer')
source = tables(root / 'source.xer')
for name, fields in [('TASKRSRC', ['act_reg_qty', 'act_ot_qty', 'act_reg_cost', 'act_ot_cost']),
                     ('TRSRCFIN', ['act_qty', 'act_cost']), ('TASKFIN', ['act_work_cost', 'act_equip_cost', 'act_mat_cost', 'act_expense_cost', 'bcwp', 'bcws'])]:
    assert [[D(r.get(f) or '0') for f in fields] for r in t[name]] == [[D(r.get(f) or '0') for f in fields] for r in source[name]]
assert [(r['start_date'], r['end_date']) for r in t['FINDATES']] == [(r['start_date'], r['end_date']) for r in source['FINDATES']]
assert sum(D(r['act_cost'] or '0') for r in t['PROJCOST']) == D('12.34')
assert json.loads((root / 'stale-report.json').read_text(encoding='utf-8'))['actualCosts']['preservedFinancials'] == 'stale-source-only'
combined = tables(root / 'combined.xer')
assert len(combined['PROJECT']) == 2
assignments = {r['taskrsrc_id']: r for r in combined['TASKRSRC']}
periods = {r['fin_dates_id'] for r in combined['FINDATES']}
for r in combined['TRSRCFIN']:
    assert r['fin_dates_id'] in periods
    assert (r['task_id'], r['proj_id']) == (assignments[r['taskrsrc_id']]['task_id'], assignments[r['taskrsrc_id']]['proj_id'])

assert importlib.metadata.version('mpxj') == '16.10.0'
assert importlib.metadata.version('JPype1') == '1.6.0'
import jpype
import mpxj
jpype.startJVM()
try:
    from org.mpxj.reader import UniversalProjectReader
    from org.mpxj import TimeUnit
    proxy = UniversalProjectReader().getProjectReaderProxy(str(root / 'export.xer'))
    try:
        proxy.getProjectReader().setIgnoreErrors(False)
        model = proxy.read()
        assert not list(model.getIgnoredErrors())
        assignments = list(model.getResourceAssignments())
        work = assignments[0].getActualWork().convertUnits(TimeUnit.HOURS, model.getProjectProperties()).getDuration()
        assert D(str(work)) == D('3.75')
        assert sum(reader_money(r.getActualCost()) for r in assignments) == D('110.46')
        tasks = [r for r in model.getTasks() if not r.getSummary()]
        assert reader_money(tasks[0].getActualCost()) == D('122.80')
    finally:
        proxy.close()
finally:
    jpype.shutdownJVM()
print(json.dumps({'status': 'passed', 'schema': 25, 'snapshot_work_hours': '3.75', 'source_period_work_hours': '3.25',
                  'undated_work_hours': '0.50', 'native_dated_work_hours': '1.25', 'cumulative_ac_usd': '122.80',
                  'native_allocated_ac_usd': '33.34', 'native_evm_includes_external_bcwp': False,
                  'readers': ['TSV/Decimal', 'native SHA256 and projection SHA256', 'MPXJ16.10.0 strict'],
                  'external_p6_execution': 'not run'}, indent=2))
