"""Compare exact-binary numeric inputs/options with Windows VBScript, without relying on ambient locale."""
import argparse
import json
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('root', type=Path)
root = parser.parse_args().root.resolve()
vectors = json.loads((root / 'vectors.json').read_text(encoding='utf-8-sig'))
rows = [(i, row) for i, row in enumerate(vectors) if row['windowsComparable']]
cases = json.loads((root / 'cases.json').read_text(encoding='utf-8-sig'))
selected = [(i, row) for i, row in enumerate(cases) if not row['source'].startswith('IIf(')]
inputs = ['\t'.join([str(i), row['kind'], row['input']] + [str(row[k]) for k in ('digits', 'leading', 'parentheses', 'grouping')]) for i, row in rows]
(root / 'vbscript-input.tsv').write_text('\n'.join(inputs) + '\n', encoding='utf-16')
program = '''Option Explicit
Dim fso, input, stream, fields, value, n, digits, leading, parens, groups
SetLocale 1033
Set fso=CreateObject("Scripting.FileSystemObject")
Set input=fso.OpenTextFile(WScript.Arguments(0) & "\\vbscript-input.tsv",1,False,-1)
Set stream=fso.CreateTextFile(WScript.Arguments(0) & "\\vbscript-output.tsv",True,True)
Do Until input.AtEndOfStream
 fields=Split(input.ReadLine,vbTab)
 n=CDbl(fields(2))/1000000
 digits=CInt(fields(3))
 leading=CInt(fields(4))
 parens=CInt(fields(5))
 groups=CInt(fields(6))
 Select Case fields(1)
  Case "number":value=FormatNumber(n,digits,leading,parens,groups)
  Case "percent":value=FormatPercent(n,digits,leading,parens,groups)
  Case "currency":value=FormatCurrency(n,digits,leading,parens,groups)
 End Select
 stream.WriteLine fields(0) & vbTab & value
Loop
input.Close
stream.Close
Set stream=fso.CreateTextFile(WScript.Arguments(0) & "\\vbscript-scalars.tsv",True,True)
'''
for i, row in selected:
    source = row['source'].replace(';', ',')
    literal = '"' + source.replace('"', '""') + '"'
    program += f'stream.WriteLine "{i}" & vbTab & CStr(Eval({literal}))\n'
program += 'stream.Close\nOn Error Resume Next\n'
errors = [('FormatNumber(Null)', 13), ('FormatPercent(Null)', 13), ('FormatCurrency(Null)', 13),
          ('FormatNumber(1,Null)', 94), ('FormatNumber(1,2,Null)', 94),
          ('FormatNumber(1,2,1)', 5), ('FormatNumber(1,-2)', 5), ('FormatNumber(1,2,-1,0,1)', 5)]
for i, (expression, code) in enumerate(errors):
    program += f'Err.Clear\nvalue={expression}\nIf Err.Number <> {code} Then WScript.Quit {i+2}\n'
program += '''On Error GoTo 0
Set stream=fso.CreateTextFile(WScript.Arguments(0) & "\\vbscript-differences.tsv",True,True)
stream.WriteLine FormatNumber(1,7)
stream.Close
WScript.Echo "PASS: 7128 exact-binary inputs/options, scalar expressions and 8 errors; LCID1033"
'''
(root / 'vbscript-oracle.vbs').write_text(program, encoding='utf-16')
run = subprocess.run([str(Path(os.environ['WINDIR']) / 'System32/cscript.exe'), '//Nologo',
                      str(root / 'vbscript-oracle.vbs'), str(root)], capture_output=True, text=True,
                     errors='replace', timeout=180)
(root / 'vbscript.txt').write_text(run.stdout + run.stderr, encoding='utf-8')
assert run.returncode == 0, (run.returncode, run.stdout, run.stderr)
lines = (root / 'vbscript-output.tsv').read_text(encoding='utf-16').splitlines()
assert len(lines) == len(rows) == 7128
for line, (i, row) in zip(lines, rows):
    index, value = line.split('\t', 1)
    assert int(index) == i and value == row['value'], (i, row, value)
lines = (root / 'vbscript-scalars.tsv').read_text(encoding='utf-16').splitlines()
assert len(lines) == len(selected) == 28
for line, (i, row) in zip(lines, selected):
    index, value = line.split('\t', 1)
    assert int(index) == i and value == row['value'], (row, value)
differences = (root / 'vbscript-differences.tsv').read_text(encoding='utf-16').splitlines()
assert differences == ['1.0000000'], differences
print(json.dumps(dict(status='PASS', numeric_cases=len(rows), scalar_cases=len(selected), error_cases=len(errors),
                      engine='Windows VBScript FormatNumber/FormatPercent/FormatCurrency; SetLocale1033', lcid=1033,
                      differences=dict(digits7=dict(native='error', vbscript=differences[0])),
                      scope='Exact-binary values/options; exact native decimal cases are checked independently with Python Decimal',
                      external_product_run=False), ensure_ascii=False, indent=2))
