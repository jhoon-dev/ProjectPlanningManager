"""Compare fixed en-US date names, weekday composition and scalar cases with Windows VBScript."""
import argparse
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
selected = [(i, row) for i, row in enumerate(cases) if not row['source'].startswith('IIf(')]
inputs = ['\t'.join([row['kind'], str(row['index']), str(row['abbreviate']), str(row['firstDay']), row.get('date', '-')]) for row in rows]
(root / 'vbscript-input.tsv').write_text('\n'.join(inputs) + '\n', encoding='utf-16')
program = '''Option Explicit
Dim fso, input, stream, fields, i, idx, shortName, first, value, monthValue, day, parts
SetLocale 1033
Set fso = CreateObject("Scripting.FileSystemObject")
Set input = fso.OpenTextFile(WScript.Arguments(0) & "\\vbscript-input.tsv", 1, False, -1)
Set stream = fso.CreateTextFile(WScript.Arguments(0) & "\\vbscript-output.tsv", True, True)
i = 0
Do Until input.AtEndOfStream
 fields = Split(input.ReadLine, vbTab)
 idx = CLng(fields(1))
 shortName = (fields(2) = "True")
 first = CLng(fields(3))
 monthValue = ""
 If fields(0) = "month" Then
  value = MonthName(idx, shortName)
 ElseIf fields(0) = "weekday" Then
  value = WeekdayName(idx, shortName, first)
 Else
  parts = Split(fields(4), "-")
  day = DateSerial(CLng(parts(0)), CLng(parts(1)), CLng(parts(2)))
  idx = Weekday(day, first)
  value = WeekdayName(idx, shortName, first)
  monthValue = MonthName(Month(day), shortName)
 End If
 stream.WriteLine CStr(i) & vbTab & value & vbTab & monthValue & vbTab & CStr(idx)
 i = i + 1
Loop
input.Close
stream.Close
Set stream = fso.CreateTextFile(WScript.Arguments(0) & "\\vbscript-scalars.tsv", True, True)
'''
for i, row in selected:
    source = re.sub(r'#(\d{4})-(\d{2})-(\d{2})#', lambda m: 'DateSerial(' + ','.join(str(int(x)) for x in m.groups()) + ')', row['source'])
    source = source.replace(';', ',')
    literal = '"' + source.replace('"', '""') + '"'
    program += f'stream.WriteLine "{i}" & vbTab & CStr(Eval({literal}))\n'
program += 'stream.Close\nOn Error Resume Next\n'
errors = [('MonthName(13)', 5), ('WeekdayName(8)', 5), ('WeekdayName(1,False,8)', 5),
          ('MonthName(Null)', 94), ('MonthName(1,Null)', 94), ('WeekdayName(Null)', 94),
          ('WeekdayName(1,Null)', 94), ('WeekdayName(1,False,Null)', 94)]
for i, (expression, code) in enumerate(errors):
    program += f'Err.Clear\nvalue = {expression}\nIf Err.Number <> {code} Then WScript.Quit {i+2}\n'
program += 'On Error GoTo 0\nWScript.Echo "PASS: 5992 name cases, scalar expressions and 8 error cases; LCID1033"\n'
(root / 'vbscript-oracle.vbs').write_text(program, encoding='utf-16')
run = subprocess.run([str(Path(os.environ['WINDIR']) / 'System32/cscript.exe'), '//Nologo',
                      str(root / 'vbscript-oracle.vbs'), str(root)], capture_output=True, text=True,
                     errors='replace', timeout=180)
(root / 'vbscript.txt').write_text(run.stdout + run.stderr, encoding='utf-8')
assert run.returncode == 0, (run.returncode, run.stdout, run.stderr)
lines = (root / 'vbscript-output.tsv').read_text(encoding='utf-16').splitlines()
assert len(lines) == len(rows) == 5992
for i, (line, row) in enumerate(zip(lines, rows)):
    index, value, month_value, weekday = line.split('\t')
    assert int(index) == i and value == row['value'] and month_value == row.get('monthValue', '') and int(weekday) == row['index'], (row, line)
lines = (root / 'vbscript-scalars.tsv').read_text(encoding='utf-16').splitlines()
assert len(lines) == len(selected) == 26
for line, (i, row) in zip(lines, selected):
    index, value = line.split('\t', 1)
    assert int(index) == i and value == row['value'], (row, value)
print(json.dumps(dict(status='PASS', name_cases=len(rows), scalar_cases=len(selected), error_cases=len(errors),
                      engine='Windows VBScript MonthName/WeekdayName; SetLocale1033', lcid=1033,
                      external_product_run=False), ensure_ascii=False, indent=2))
