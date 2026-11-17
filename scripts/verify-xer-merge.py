"""Independent source/current ledger merge checks and strict MPXJ readback."""
from decimal import Decimal as D
from pathlib import Path
import hashlib
import importlib.metadata
import json
import sys

root = Path(sys.argv[1] if len(sys.argv) > 1 else 'build-governance').resolve()/'xer-merge-qa'


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':')).encode()


def tables(path):
    out, rows, fields = {}, None, []
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
            assert len(cells)-1 == len(fields)
            rows.append(dict(zip(fields, cells[1:])))
    for name, keys in [('TASKFIN', ('task_id', 'fin_dates_id')), ('TRSRCFIN', ('taskrsrc_id', 'fin_dates_id'))]:
        values = [tuple(r[k] for k in keys) for r in out[name]]
        assert len(values) == len(set(values)), f'duplicate {name} key'
    return out


def money(value):
    raw = D(str(value or '0'))
    cents = raw.quantize(D('.01'))
    assert abs(raw-cents) <= D('.00000001')
    return cents


def latest(body, status, revision):
    out = {}
    for e in body['periodActuals']:
        if e['statusAt'] <= status and e['recordedRevision'] < revision:
            key = (e['taskId'], e['resourceId'], e['date'])
            if key not in out or out[key]['statusAt'] <= e['statusAt']:
                out[key] = e
    return out


def verify_references(body):
    previous = body['xerFinancials']
    for r in body['xerFinancialRevisions']:
        assert r['merge']['policy'] == 'native-ledger-union-v1'
        assert r['merge']['baseProjectionSha256'] == previous['projectionSha256']
        at_merge = latest(body, r['financials']['statusAt'], r['recordedRevision'])
        entries = {e['id']: e for e in body['periodActuals']}
        expected = {}
        for a in r['allocations']:
            p = r['financials']['periods'][a['periodIndex']]
            assert len(a['days']) == 1 and a['days'][0]['weight'] == 1
            date = a['days'][0]['date']
            assert p['start'][:10] == date == p['finish'][:10]
            key = (p['taskId'], p['resourceId'], date)
            expected[key] = (p['work'] if p['resourceId'] else None,
                             p['actualMinor'] if p['resourceId'] else p['expenseMinor'])
        assert len(expected) == len(r['ownedEntryIds'])
        for entry_id in r['ownedEntryIds']:
            e = entries[entry_id]
            key = (e['taskId'], e['resourceId'], e['date'])
            assert at_merge[key]['id'] == entry_id
            assert (e['workUnitMinutes'], e['actualMinor']) == expected[key]
        previous = r['financials']


before = json.loads((root/'before.json').read_text(encoding='utf-8'))
envelope = json.loads((root/'merged.ppm').read_text(encoding='utf-8'))
assert envelope['schemaVersion'] == 27
body = envelope['payload']
assert hashlib.sha256(canonical(body)).hexdigest() == envelope['sha256']
excluded = {'xerFinancialRevisions', 'revision', 'documentId', 'derivedFrom'}
assert {k: v for k, v in before.items() if k not in excluded} == {k: v for k, v in body.items() if k not in excluded}
assert hashlib.sha256((root/'source.xer').read_bytes()).hexdigest() == body['xerFinancials']['sourceSha256']
assert body['xerFinancials']['tasks'][0]['name'] != body['tasks'][0]['name']
assert body['xerFinancials']['statusAt'] == '2026-10-28 18:00'
assert body['statusAt'] == '2026-10-29 18:00'
assert len(body['xerFinancialRevisions']) == 2
verify_references(body)
working = body['xerFinancialRevisions'][-1]['financials']
projection = {k: v for k, v in body.items() if k not in excluded | {'xerFinancials'}}
assert hashlib.sha256(canonical(projection)).hexdigest() == working['projectionSha256']
assert working['tasks'][0]['name'] == body['tasks'][0]['name']
assert working['resources'][0]['name'] == body['resources'][0]['name']
assert working['assignments'][0] == body['xerFinancials']['assignments'][0]
native = latest(body, body['statusAt'], body['revision']+1)
assert sum(e['actualMinor'] or 0 for e in native.values()) == 4534
assert sum(e['workUnitMinutes'] or 0 for e in native.values()) == 9300
resource_periods = [e for e in working['periods'] if e['resourceId']]
assert sum(e['work'] or 0 for e in resource_periods) == 21300
assert sum(e['actualMinor'] or 0 for e in resource_periods) == 10546
assert sum(e['expenseMinor'] or 0 for e in working['periods']) == 1234
assert 9012+2034+1234 == 12280 and 22500-21300 == 1200
report = json.loads((root/'export-report.json').read_text(encoding='utf-8'))
assert report['actualCosts']['financialMergeCount'] == 2
assert report['actualCosts']['preservedFinancials'] == 'reexported'
t = tables(root/'export.xer')
assert D(t['TASKRSRC'][0]['act_reg_qty'])+D(t['TASKRSRC'][0]['act_ot_qty']) == D('3.75')
assert sum(D(r['act_cost'] or '0') for r in t['PROJCOST']) == D('12.34')
assert sum(D(r['act_cost'] or '0') for r in t['TRSRCFIN']) == D('105.46')
assert sum(D(r['act_expense_cost'] or '0') for r in t['TASKFIN']) == D('12.34')
assert all(D(r['bcwp']) == D('9999') for r in t['TASKFIN'] if r['bcwp'])

# An explicit replacement of only the expense dimension keeps the external EV range.
expense = json.loads((root/'expense-merged.json').read_text(encoding='utf-8'))
verify_references(expense)
assert expense['xerFinancialRevisions'][0]['merge']['replacedPeriods'] == [2]
periods = expense['xerFinancialRevisions'][-1]['financials']['periods']
assert any(e['start'] == '2026-10-19 00:00' and e['bcwp'] == 999900 and e['expenseMinor'] is None for e in periods)
assert any(e['start'] == '2026-10-20 00:00' and not e['resourceId'] and e['expenseMinor'] == 200 for e in periods)
tables(root/'expense-export.xer')

assert importlib.metadata.version('mpxj') == '16.10.0'
assert importlib.metadata.version('JPype1') == '1.6.0'
import jpype
import mpxj
jpype.startJVM()
try:
    from org.mpxj.reader import UniversalProjectReader
    from org.mpxj import TimeUnit
    for name in ('export.xer', 'expense-export.xer'):
        proxy = UniversalProjectReader().getProjectReaderProxy(str(root/name))
        try:
            proxy.getProjectReader().setIgnoreErrors(False)
            model = proxy.read()
            assert not list(model.getIgnoredErrors())
            assignments = list(model.getResourceAssignments())
            work = assignments[0].getActualWork().convertUnits(TimeUnit.HOURS, model.getProjectProperties()).getDuration()
            assert D(str(work)) == D('3.75')
            assert sum(money(a.getActualCost()) for a in assignments) == D('110.46')
            tasks = [t for t in model.getTasks() if not t.getSummary()]
            assert money(tasks[0].getActualCost()) == D('122.80')
        finally:
            proxy.close()
finally:
    jpype.shutdownJVM()
print(json.dumps({'status': 'passed', 'schema': 27, 'merge_audits': 2, 'native_facts_unchanged': True,
                  'native_ac_minor': 12280, 'native_allocated_minor': 4534,
                  'native_dated_work_units': 9300, 'union_period_work_units': 21300,
                  'unallocated_assignment_work_units': 1200, 'taskfin_keys_unique': True,
                  'readers': ['native equality and SHA256', 'as-of/revision ownership oracle', 'TSV/Decimal', 'MPXJ16.10.0 strict'],
                  'external_p6_execution': 'not run'}, indent=2))
