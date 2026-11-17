"""Compare whole-second en-US display with VBScript and record deliberate profile differences."""
import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('root', type=Path)
root = parser.parse_args().root.resolve()
rows = json.loads((root / 'vectors.json').read_text(encoding='utf-8-sig'))
cases = json.loads((root / 'cases.json').read_text(encoding='utf-8-sig'))
selected = [(i, row) for i, row in enumerate(cases)
            if not row['source'].startswith('IIf(')
            and not re.search(r'T\d\d:\d\d:\d\d\.', row['source'])
            and not any(int(y) < 1601 for y in re.findall(r'#(\d{4})-', row['source']))]
inputs = ['\t'.join([row['date'], str(row['format'])]) for row in rows]
(root / 'vbscript-input.tsv').write_text('\n'.join(inputs) + '\n', encoding='utf-16')
program = '''Option Explicit
Dim fso, input, stream, fields, i, parts, day, value, seconds, errorCode
SetLocale 1033
Set fso = CreateObject("Scripting.FileSystemObject")
Set input = fso.OpenTextFile(WScript.Arguments(0) & "\\vbscript-input.tsv", 1, False, -1)
Set stream = fso.CreateTextFile(WScript.Arguments(0) & "\\vbscript-output.tsv", True, True)
i = 0
Do Until input.AtEndOfStream
 fields = Split(input.ReadLine, vbTab)
 parts = Split(Replace(Replace(fields(0), "T", "-"), ":", "-"), "-")
 day = DateSerial(CLng(parts(0)), CLng(parts(1)), CLng(parts(2)))
 seconds = CLng(parts(3))*3600+CLng(parts(4))*60+CLng(parts(5))
 day = DateAdd("s", seconds, day)
 value = FormatDateTime(day, CLng(fields(1)))
 stream.WriteLine CStr(i) & vbTab & value
 i = i + 1
Loop
input.Close
stream.Close
Set stream = fso.CreateTextFile(WScript.Arguments(0) & "\\vbscript-scalars.tsv", True, True)
'''

def date_expression(raw):
    d = datetime.fromisoformat(raw)
    return f'DateAdd("s",{d.hour*3600+d.minute*60+d.second},DateSerial({d.year},{d.month},{d.day}))'

for i, row in selected:
    source = re.sub(r'#([^#]+)#', lambda m: date_expression(m[1]), row['source'])
    # Construct ISO values explicitly; do not rely on the VBScript regional text parser.
    source = re.sub(r'"(\d{4}-\d\d-\d\d(?:T\d\d:\d\d:\d\d)?)"', lambda m: date_expression(m[1]), source)
    source = source.replace(';', ',')
    literal = '"' + source.replace('"', '""') + '"'
    program += f'stream.WriteLine "{i}" & vbTab & CStr(Eval({literal}))\n'
program += 'stream.Close\nOn Error Resume Next\n'
errors = [('FormatDateTime(Null)', 13), ('FormatDateTime(DateSerial(2024,1,1),Null)', 94),
          ('FormatDateTime(DateSerial(2024,1,1),5)', 5), ('FormatDateTime(DateSerial(2024,1,1),-1)', 5)]
for i, (expression, code) in enumerate(errors):
    program += f'Err.Clear\nvalue={expression}\nIf Err.Number <> {code} Then WScript.Quit {i+2}\n'
program += '''Err.Clear
value=FormatDateTime(DateSerial(2024,2,29)+0.6/86400,0)
If Err.Number <> 0 Then WScript.Quit 10
Set stream=fso.CreateTextFile(WScript.Arguments(0) & "\\vbscript-differences.tsv",True,True)
stream.WriteLine value
Err.Clear
value=FormatDateTime(DateSerial(1600,1,1),2)
errorCode=Err.Number
stream.WriteLine CStr(errorCode)
stream.Close
If errorCode <> 5 Then WScript.Quit 11
On Error GoTo 0
WScript.Echo "PASS: 7370 whole-second displays, scalar expressions and 4 errors; LCID1033"
'''
(root / 'vbscript-oracle.vbs').write_text(program, encoding='utf-16')
run = subprocess.run([str(Path(os.environ['WINDIR']) / 'System32/cscript.exe'), '//Nologo',
                      str(root / 'vbscript-oracle.vbs'), str(root)], capture_output=True, text=True,
                     errors='replace', timeout=180)
(root / 'vbscript.txt').write_text(run.stdout + run.stderr, encoding='utf-8')
assert run.returncode == 0, (run.returncode, run.stdout, run.stderr)
lines = (root / 'vbscript-output.tsv').read_text(encoding='utf-16').splitlines()
assert len(lines) == len(rows) == 7370
for i, (line, row) in enumerate(zip(lines, rows)):
    index, value = line.split('\t', 1)
    assert int(index) == i and value == row['value'], (row, value)
lines = (root / 'vbscript-scalars.tsv').read_text(encoding='utf-16').splitlines()
assert len(lines) == len(selected) == 19
for line, (i, row) in zip(lines, selected):
    index, value = line.split('\t', 1)
    assert int(index) == i and value == row['value'], (row, value)
differences = (root / 'vbscript-differences.tsv').read_text(encoding='utf-16').splitlines()
assert differences == ['2/29/2024 12:00:01 AM', '5'], differences
native = {row['source']: row['value'] for row in cases}
assert native['FormatDateTime(#2024-02-29T00:00:00.6#)'] == '2/29/2024'
assert native['FormatDateTime(#1600-01-01#,2)'] == '1/1/1600'
print(json.dumps(dict(status='PASS', display_cases=len(rows), scalar_cases=len(selected), error_cases=len(errors),
                      engine='Windows VBScript FormatDateTime; SetLocale1033', lcid=1033,
                      differences=dict(subsecond=dict(native='2/29/2024', vbscript=differences[0]),
                                       year1600=dict(native='1/1/1600', vbscript_error=5)),
                      external_product_run=False), ensure_ascii=False, indent=2))
