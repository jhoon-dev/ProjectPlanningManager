"""Independent TSV/Decimal, native checksum and strict MPXJ checks; not a P6 execution test."""
from decimal import Decimal as D
from pathlib import Path
import hashlib
import importlib.metadata
import json
import sys

root = Path(sys.argv[1] if len(sys.argv) > 1 else 'build-governance').resolve() / 'xer-actual-cost-qa'


def reader_money(value):
    # MPXJ's regular+OT total is a binary double; TSV/native checks above remain exact.
    raw = D(str(value or '0'))
    cents = raw.quantize(D('0.01'))
    assert abs(raw - cents) <= D('0.00000001')
    return cents


def tables(path):
    result = {}
    current = fields = None
    lines = path.read_text(encoding='utf-8-sig').splitlines()
    assert lines[0].split('\t')[0] == 'ERMHDR' and lines[-1] == '%E'
    for line in lines[1:-1]:
        cells = line.split('\t')
        if cells[0] == '%T':
            assert cells[1] not in result
            current = result[cells[1]] = []
            fields = None
        elif cells[0] == '%F':
            assert fields is None and len(cells[1:]) == len(set(cells[1:]))
            fields = cells[1:]
        else:
            assert cells[0] == '%R' and len(cells) == len(fields) + 1
            current.append(dict(zip(fields, cells[1:])))
    return result


t = tables(root / 'export.xer')
assert t['PROJECT'][0]['last_recalc_date'] == '2026-10-28 18:00'
assert t['TASK'][0]['status_code'] == 'TK_Active' and D(t['TASK'][0]['remain_drtn_hr_cnt']) == 4
assert t['TASK'][1]['status_code'] == 'TK_Complete' and t['TASK'][1]['act_end_date'] == '2026-10-28 12:00'
assert D(t['TASK'][0]['phys_complete_pct']) == D('37.25')
assert t['TASKRSRC'][0]['remain_qty'] == '' and D(t['TASKRSRC'][1]['remain_qty']) == 0
assert sum(D(r['act_reg_cost'] or '0') + D(r['act_ot_cost'] or '0') for r in t['TASKRSRC']) == D('110.46')
assert sum(D(r['act_cost'] or '0') for r in t['PROJCOST']) == D('12.34')
assert sum(D(r['act_cost']) for r in t['TRSRCFIN']) == D('100.46')
assert sum(D(r['act_qty']) for r in t['TRSRCFIN']) == D('3.25')
assert sum(D(r['act_expense_cost'] or '0') for r in t['TASKFIN']) == D('5.34')
assert all(D(r['bcwp']) == D('9999') and D(r['bcws']) == D('8888') for r in t['TASKFIN'])
assert all(r['start_date'][:10] == r['end_date'][:10] for r in t['FINDATES'])
assert D(t['RSRCRATE'][0]['cost_per_qty']) == D('25.50')
report = json.loads((root / 'import-report.json').read_text(encoding='utf-8'))
assert report['compatibility']['externalRoundTrip'] == 'unverified'
assert report['actualCosts']['assignmentTotals'][0]['actualWorkUnitMinutes'] == '22500'
native = json.loads((root / 'imported.ppm').read_text(encoding='utf-8'))
payload = native['payload']
compact = json.dumps(payload, ensure_ascii=False, sort_keys=True, separators=(',', ':')).encode()
assert hashlib.sha256(compact).hexdigest() == native['sha256']
assert len(payload['periodActuals']) == 4
cost = next(c for c in payload['costEntries'] if c['taskId'] == 'xer-task-20')
assert int(cost['actualMinor']) == 12280 and int(cost['physicalBasisPoints']) == 3725
combined = tables(root / 'combined.xer')
assert len(combined['PROJECT']) == 2 and len(combined['TRSRCFIN']) == 4
assignments = {r['taskrsrc_id']: r for r in combined['TASKRSRC']}
periods = {r['fin_dates_id'] for r in combined['FINDATES']}
for r in combined['TRSRCFIN']:
    assert r['fin_dates_id'] in periods
    assert assignments[r['taskrsrc_id']]['proj_id'] == r['proj_id']
    assert assignments[r['taskrsrc_id']]['task_id'] == r['task_id']

import jpype
import mpxj  # Makes the pinned validation jars available; never an application dependency.
assert importlib.metadata.version('mpxj') == '16.10.0'
assert importlib.metadata.version('JPype1') == '1.6.0'
jpype.startJVM()
try:
    from org.mpxj.reader import UniversalProjectReader
    from org.mpxj.primavera import PrimaveraXERFileReader
    proxy = UniversalProjectReader().getProjectReaderProxy(str(root / 'export.xer'))
    try:
        reader = proxy.getProjectReader()
        assert isinstance(reader, PrimaveraXERFileReader)
        reader.setIgnoreErrors(False)
        model = proxy.read()
        assert not list(model.getIgnoredErrors())
        leaves = [task for task in model.getTasks() if not task.getSummary()]
        assert str(model.getProjectProperties().getStatusDate()) == '2026-10-28T18:00'
        assert str(leaves[0].getActualStart()) == '2026-10-26T09:00'
        assert str(leaves[1].getActualFinish()) == '2026-10-28T12:00'
        assert D(str(leaves[0].getPhysicalPercentComplete())) == D('37.25')
        assert reader_money(leaves[0].getActualCost()) == D('122.80')
        assert sum(reader_money(a.getActualCost()) for a in model.getResourceAssignments()) == D('110.46')
    finally:
        proxy.close()
finally:
    jpype.shutdownJVM()

print(json.dumps({'result': 'passed', 'independent_readers': ['TSV/Decimal', 'native SHA256', 'MPXJ 16.10.0 strict'],
                  'cumulative_usd': '122.80', 'dated_allocated_usd': '105.80', 'unallocated_usd': '17.00',
                  'dated_work_hours': '3.25', 'source_cumulative_work_hours': '3.75',
                  'external_p6_execution': 'not run'}, indent=2))
