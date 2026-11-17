"""Compare OLE serial/whole-second clocks with installed Windows VBScript.

Only validated numeric constants and clock strings are placed in the generated
script. No formula source, Eval, user code or system locale dates are executed.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

parser=argparse.ArgumentParser();parser.add_argument('root',type=Path);args=parser.parse_args();root=args.root
load=lambda n:json.loads((root/n).read_text(encoding='utf-8-sig'))
lines=['Option Explicit', 'Function Stamp(d)',
       'Stamp = Right("0000" & Year(d),4) & "-" & Right("0" & Month(d),2) & "-" & Right("0" & Day(d),2) & "T" & Right("0" & Hour(d),2) & ":" & Right("0" & Minute(d),2) & ":" & Right("0" & Second(d),2)',
       'End Function']
expected=[]
for row in load('serials.json'):
    values=[row[k] for k in ('hour','minute','second')]
    assert all(type(n) is int and -32768<=n<=32767 for n in values)
    lines.append('WScript.Echo Stamp(TimeSerial('+','.join(str(n) for n in values)+'))')
    expected.append(row['value'])
rounding_cases=0
for row in load('cases.json'):
    match=re.fullmatch(r'TimeSerial\((-?\d+(?:\.\d+)?),(-?\d+(?:\.\d+)?),(-?\d+(?:\.\d+)?)\)',row['source'])
    if match:
        lines.append('WScript.Echo Stamp(TimeSerial('+','.join(match.groups())+'))')
        expected.append(row['value']);rounding_cases+=1
for row in load('values.json'):
    value=re.sub(r'^\d{4}-\d{2}-\d{2}T','',row['input'])
    value=re.sub(r'(?<=\d)\.\d{1,6}','',value)
    assert re.fullmatch(r'\d{1,2}:\d{2}:\d{2}(?: [AP]M)?',value),value
    lines.append('WScript.Echo Stamp(TimeValue("'+value+'"))')
    expected.append(row['value'].split('.')[0])
with tempfile.TemporaryDirectory(prefix='ppm-time-oracle-') as directory:
    script=Path(directory)/'clock.vbs';script.write_text('\r\n'.join(lines)+'\r\n',encoding='ascii')
    result=subprocess.run([str(Path(os.environ['WINDIR'])/'System32/cscript.exe'),'//nologo',str(script)],
                          capture_output=True,timeout=60,check=True)
    actual=result.stdout.decode('ascii').splitlines()
assert len(actual)==len(expected),(len(actual),len(expected),result.stderr)
for i,(a,b) in enumerate(zip(actual,expected)):assert a==b,(i,a,b)
print(json.dumps(dict(status='PASS',comparisons=len(expected),rounding_cases=rounding_cases,
                     scope='OLE negative/carry/integer binding; whole-second clock extraction. Native microseconds and ISO date validation are independently checked in Python.',
                     external_product_run=False),indent=2))
