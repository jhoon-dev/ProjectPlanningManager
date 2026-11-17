"""Compare binary numeric operations, excluding VBA/VBScript parser precedence differences."""
import argparse
from decimal import Decimal, ROUND_HALF_UP
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('root', type=Path)
root = parser.parse_args().root.resolve()
rows = json.loads((root / 'vectors.json').read_text(encoding='utf-8-sig'))
lines = []
for i, row in enumerate(rows):
    lines.append('\t'.join([str(i), row['op']] +
                           [format(Decimal(row[k]) / 1000000, 'f') for k in ('left', 'right')]))
(root / 'vbscript-input.tsv').write_text('\n'.join(lines) + '\n', encoding='utf-16')
program = r'''Option Explicit
SetLocale 1033
Dim fso,f,o,a,v
Set fso=CreateObject("Scripting.FileSystemObject")
Set f=fso.OpenTextFile(WScript.Arguments(0),1,False,-1)
Set o=fso.CreateTextFile(WScript.Arguments(1),True,True)
Do Until f.AtEndOfStream
 a=Split(f.ReadLine,vbTab)
 If a(1)="^" Then
  v=CDbl(a(2)) ^ CDbl(a(3))
 Else
  v=CDbl(a(2)) \ CDbl(a(3))
 End If
 o.WriteLine a(0) & vbTab & CStr(v)
Loop
f.Close
o.Close
WScript.Echo "VBScript -2^2=" & CStr(Eval("-2^2"))
'''
(root / 'vbscript-oracle.vbs').write_text(program, encoding='utf-16')
run = subprocess.run(['cscript.exe', '//nologo', str(root / 'vbscript-oracle.vbs'),
                      str(root / 'vbscript-input.tsv'), str(root / 'vbscript-output.tsv')],
                     capture_output=True, text=True, timeout=60)
(root / 'vbscript.txt').write_text(run.stdout + run.stderr, encoding='utf-8')
assert run.returncode == 0, (run.returncode, run.stdout, run.stderr)
output = (root / 'vbscript-output.tsv').read_text(encoding='utf-16').splitlines()
assert len(output) == len(rows) == 4096
maximum = exact = 0
for line, row in zip(output, rows):
    index, raw = line.split('\t')
    assert rows[int(index)] == row
    actual = int((Decimal(raw) * 1000000).to_integral_value(rounding=ROUND_HALF_UP))
    difference = abs(actual - int(row['output']))
    assert difference <= (1 if row['op'] == '^' else 0), (row, raw)
    exact += difference == 0
    maximum = max(maximum, difference)
assert run.stdout.strip() == 'VBScript -2^2=4'
print(json.dumps(dict(status='PASS', comparisons=len(rows), exact_values=exact,
                      maximum_difference_scaled=maximum, engine='Windows VBScript',
                      scope='Binary fixed-six numeric operands, no implicit text/date coercion or parser comparison.',
                      precedence_difference=dict(expression='-2^2', native=-4, vbscript=4,
                                                 rule='Native uses documented VBA precedence in new-operator expressions.'),
                      external_product_run=False), ensure_ascii=False, indent=2))
