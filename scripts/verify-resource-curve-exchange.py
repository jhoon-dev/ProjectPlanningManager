"""Strict independent MPXJ reads of native-produced XML and XER planning curves."""
import argparse
import importlib.metadata
import json
from pathlib import Path
import jpype
import mpxj  # Registers the bundled classpath.

parser=argparse.ArgumentParser();parser.add_argument('root',type=Path);args=parser.parse_args()
assert importlib.metadata.version('mpxj')=='16.10.0'
jpype.startJVM('-Dlog4j2.statusLoggerLevel=OFF')
try:
    from org.mpxj.reader import UniversalProjectReader
    from org.mpxj import TimeUnit, LocalDateTimeRange
    from java.time import LocalDateTime
    from java.util import ArrayList
    results=[]
    for filename,work in [('curve.xml',16),('curve.xer',16),('manual.xml',16.5)]:
        proxy=UniversalProjectReader().getProjectReaderProxy(str((args.root/filename).resolve()))
        reader=proxy.getProjectReader()
        if hasattr(reader,'setIgnoreErrors'):reader.setIgnoreErrors(False)
        model=proxy.read();assert not list(model.getIgnoredErrors())
        assignment=list(model.getResourceAssignments())[0]
        assert abs(assignment.getWork().convertUnits(TimeUnit.HOURS,model.getProjectProperties()).getDuration()-work)<1e-8
        result=dict(file=filename,ignored_errors=0,total_hours=work)
        if filename.startswith('curve'):
            expected=json.loads((args.root/'curved.json').read_text(encoding='utf-8'))['assignments'][0]['nativeCurve']['weights']
            actual=[round(float(v)*1000000) for v in assignment.getWorkContour().getCurveValues()]
            assert actual==expected
            result['curve_points']=len(actual)
        if filename.endswith('.xml'):
            ranges=ArrayList()
            for day in (2,3):ranges.add(LocalDateTimeRange(LocalDateTime.of(2026,11,day,0,0),LocalDateTime.of(2026,11,day+1,0,0)))
            actual=[float(v.getDuration()) for v in assignment.getTimephasedWork(ranges,TimeUnit.HOURS)]
            expected=[8,8] if filename=='curve.xml' else [4.5,12]
            assert all(abs(a-b)<1e-8 for a,b in zip(actual,expected)) and len(actual)==2,(filename,actual)
            result['daily_hours']=actual
        results.append(result)
    print(json.dumps(dict(status='PASS',reader='MPXJ16.10.0',jpype=importlib.metadata.version('JPype1'),files=results),indent=2))
finally:jpype.shutdownJVM()
