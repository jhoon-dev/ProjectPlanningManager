"""Read native MPP curve exports with an independent strict XML/XER reader."""
import argparse
import json
from pathlib import Path
import jpype
import mpxj

parser = argparse.ArgumentParser()
parser.add_argument('root', type=Path)
args = parser.parse_args()
native = json.loads((args.root / 'timephased-multi-rate.mpp.json').read_text(encoding='utf-8'))
tasks = {t['id']: t['name'] for t in native['tasks']}
resources = {r['id']: r['name'] for r in native['resources']}
expected = {(tasks[a['taskId']], resources[a['resourceId']]): a['nativeCurve']['workUnitMinutes'] / 6000
            for a in native['assignments'] if a.get('nativeCurve')}
assert len(expected) == 9
jpype.startJVM('-Dlog4j2.statusLoggerLevel=OFF')
try:
    from org.mpxj.reader import UniversalProjectReader
    from org.mpxj import TimeUnit
    output = []
    for suffix in ('xml', 'xer'):
        proxy = UniversalProjectReader().getProjectReaderProxy(str((args.root / ('export.' + suffix)).resolve()))
        reader = proxy.getProjectReader()
        if hasattr(reader, 'setIgnoreErrors'):
            reader.setIgnoreErrors(False)
        model = proxy.read()
        assert not list(model.getIgnoredErrors())
        checked = 0
        for a in model.getResourceAssignments():
            pair = (str(a.getTask().getName()), str(a.getResource().getName()))
            if pair in expected:
                amount = float(a.getWork().convertUnits(TimeUnit.HOURS, model.getProjectProperties()).getDuration())
                assert abs(amount - expected[pair]) < 0.000001, (suffix, pair, amount, expected[pair])
                checked += 1
        assert checked == len(expected)
        output.append(dict(format=suffix, ignored_errors=0, matched_planning_curves=checked))
    print(json.dumps(dict(status='PASS', reader='MPXJ16.10.0', exports=output), indent=2))
finally:
    jpype.shutdownJVM()
