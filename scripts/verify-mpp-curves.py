"""Compare MPP source rows and independently allocated native civil-minute curves.

Uses the pinned decoder directly, bypassing the bridge and its writer. This is not
Microsoft Project recalculation. Native dates intentionally use the PPM schedule.
"""
import argparse
from datetime import datetime, timedelta
from decimal import Decimal, ROUND_FLOOR, ROUND_HALF_UP
import hashlib
import json
from pathlib import Path
import jpype
import mpxj  # Registers the pinned decoder classpath.


def ceil_minute(value):
    stamp = datetime.fromisoformat(str(value))
    return stamp.replace(second=0, microsecond=0) + (timedelta(minutes=1) if stamp.second or stamp.microsecond else timedelta())


def working(calendar, stamp):
    minute = stamp.hour * 60 + stamp.minute
    return (stamp.isoweekday() in calendar['weekdays'] and stamp.date().isoformat() not in calendar['holidays']
            and any(p['begin'] <= minute < p['end'] for p in calendar['periods']))


def minutes_between(calendar, begin, end):
    result = 0
    while begin < end:
        result += working(calendar, begin)
        begin += timedelta(minutes=1)
    return result


def verify(root, fixtures):
    from org.mpxj.mpp import MPPReader
    from org.mpxj import TimeUnit
    name = 'timephased-multi-rate.mpp'
    source = fixtures / name
    model = MPPReader().read(str(source.resolve()))
    assert not list(model.getIgnoredErrors())
    native = json.loads((root / (name + '.json')).read_text(encoding='utf-8'))
    report = json.loads((root / (name + '.report.json')).read_text(encoding='utf-8'))
    recorded = json.loads((root / (name + '.spans.json')).read_text(encoding='utf-8'))
    calendars = {c['id']: c for c in native['calendars']}
    tasks = {t['id']: t for t in native['tasks']}
    resources = {r['id']: r for r in native['resources']}
    rows_by_pair = {(r['task'], r['resource']): r for r in recorded}
    default = {'weekdays': [1, 2, 3, 4, 5], 'periods': [{'begin': 540, 'end': 720}, {'begin': 780, 'end': 1080}], 'holidays': native['holidays']}
    anchor = datetime.fromisoformat(native['startDate'])
    checked, checked_minutes, manual = 0, 0, []
    for a in model.getResourceAssignments():
        task_id, resource_id = 'xml-task-' + str(a.getTaskUniqueID()), 'xml-resource-' + str(a.getResourceUniqueID())
        raw = list(a.getRawTimephasedRemainingRegularWork())
        if float(a.getActualWork().getDuration()) or float(a.getRemainingOvertimeWork().getDuration()) or list(a.getRawTimephasedRemainingOvertimeWork()) or not raw:
            continue
        task, resource = tasks[task_id], resources[resource_id]
        task_calendar = calendars.get(task['calendarId'], default)
        resource_calendar = calendars.get(resource['calendarId'], default)
        source_origin = ceil_minute(a.getTask().getStart())
        boundaries = [(minutes_between(task_calendar, source_origin, ceil_minute(r.getStart())),
                       minutes_between(task_calendar, source_origin, ceil_minute(r.getFinish()))) for r in raw]
        values = [Decimal(0) if r.getTotalAmount() is None else Decimal(str(r.getTotalAmount().convertUnits(TimeUnit.MINUTES, model.getProjectProperties()).getDuration())) * 100 for r in raw]
        target = int((Decimal(str(a.getWork().convertUnits(TimeUnit.MINUTES, model.getProjectProperties()).getDuration())) * 100).quantize(Decimal(1), rounding=ROUND_HALF_UP))
        amounts = [int(v.to_integral_value(rounding=ROUND_FLOOR)) for v in values]
        order = sorted(range(len(raw)), key=lambda i: -(values[i] - amounts[i]))
        for i in order[:target - sum(amounts)]:
            amounts[i] += 1
        observed = rows_by_pair[(task_id, resource_id)]
        expected = {}
        begin = datetime.fromisoformat(observed['start'])
        end = datetime.fromisoformat(observed['finish'])
        groups = [[] for _ in raw]
        logical = 0
        while begin < end:
            if working(task_calendar, begin):
                if working(resource_calendar, begin):
                    for i, (start, finish) in enumerate(boundaries):
                        if start <= logical < finish:
                            groups[i].append(int((begin - anchor).total_seconds() // 60))
                logical += 1
            begin += timedelta(minutes=1)
        assert logical == task['durationMinutes']
        for available, amount in zip(groups, amounts):
            assert available or amount == 0
            if available:
                units, extra = divmod(amount, len(available))
                for index, at in enumerate(available):
                    if units + (index < extra):
                        expected[at] = units + (index < extra)
        actual = {}
        for segment in observed['spans']:
            for at in range(segment['start'], segment['finish']):
                assert at not in actual
                actual[at] = segment['units']
        assert actual == expected, (task_id, len(actual), len(expected))
        assert sum(actual.values()) == observed['work'] == target
        checked += 1
        checked_minutes += len(actual)
        if str(a.getWorkContour()) == 'CONTOURED':
            assert target == 96000 and any(v == 0 for v in amounts)
            manual.append(task_id)
    assert checked == 9 and len(manual) == 2
    assert report['source']['sha256'] == hashlib.sha256(source.read_bytes()).hexdigest()
    for name in ('timephased-actual-work-resource.mpp', 'timephased-actual-overtime-work.mpp'):
        project = json.loads((root / (name + '.json')).read_text(encoding='utf-8'))
        metadata = json.loads((root / (name + '.report.json')).read_text(encoding='utf-8'))
        assert metadata['planningCurves']['excludedActual'] > 0
        for key in ('progress', 'periodActuals', 'costEntries', 'baselines'):
            assert project.get(key, []) == []
        assert project['statusAt'] is None
    return dict(status='PASS', assignments=checked, resource_minutes=checked_minutes, manual_curves=manual,
                decoded_source='MPXJ16.10.0 directly; bridge/writer bypassed',
                comparison='independent civil-minute allocation at native scheduled dates; exact totals/zeros/order',
                source_sha256=report['source']['sha256'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('root', type=Path)
    parser.add_argument('--fixtures', type=Path, default=Path(__file__).resolve().parents[1] / 'tests/fixtures/mpp-public')
    args = parser.parse_args()
    jpype.startJVM('-Dlog4j2.statusLoggerLevel=OFF')
    try:
        print(json.dumps(verify(args.root, args.fixtures), indent=2))
    finally:
        jpype.shutdownJVM()
