"""Compare the shared 32-bit/Boolean/Null subset against Windows VBScript."""
import argparse
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('root', type=Path)
root = parser.parse_args().root.resolve()
load = lambda name: json.loads((root / name).read_text(encoding='utf-8-sig'))
vectors, truth = load('vectors.json'), load('truth.json')
expected = [r['output'] for r in vectors]
expected += [r['value'] for r in truth]
operands = vectors + truth
assert len(operands) == 4296
(root / 'vbscript-input.tsv').write_text('\n'.join('\t'.join([str(i), r['op'].lower(), r['left'], r['right']])
                                                   for i, r in enumerate(operands)) + '\n',
                                        encoding='utf-16')
program = r'''Option Explicit
SetLocale 1033
Function Operand(s)
 If s="True" Then
  Operand=True
 ElseIf s="False" Then
  Operand=False
 ElseIf s="Null" Then
  Operand=Null
 Else
  Operand=CLng(s)
 End If
End Function
Dim fso,f,o,a,v,total,x,y
Set fso=CreateObject("Scripting.FileSystemObject")
Set f=fso.OpenTextFile(WScript.Arguments(0),1,False,-1)
Set o=fso.CreateTextFile(WScript.Arguments(1),True,True)
total=0
Do Until f.AtEndOfStream
 a=Split(f.ReadLine,vbTab)
 x=Operand(a(2)):y=Operand(a(3))
 If a(1)="eqv" Then
  v=x Eqv y
 Else
  v=x Imp y
 End If
 If IsNull(v) Then
  o.WriteLine a(0) & vbTab & "__NULL__"
 Else
  o.WriteLine a(0) & vbTab & CStr(v)
 End If
 total=total+1
Loop
f.Close
o.Close
WScript.Echo "PASS " & total
WScript.Echo "Legacy logical order=" & CStr(Eval("True Or True Xor True"))
WScript.Echo "Eqv logical order=" & CStr(Eval("(True Or True Xor True) Eqv False"))
'''
(root / 'vbscript-oracle.vbs').write_text(program, encoding='utf-16')
run = subprocess.run(['cscript.exe', '//nologo', str(root / 'vbscript-oracle.vbs'),
                      str(root / 'vbscript-input.tsv'), str(root / 'vbscript-output.tsv')],
                     capture_output=True, text=True, timeout=60)
(root / 'vbscript.txt').write_text(run.stdout + run.stderr, encoding='utf-8')
assert run.returncode == 0, (run.returncode, run.stdout, run.stderr)
output = (root / 'vbscript-output.tsv').read_text(encoding='utf-16').splitlines()
assert len(output) == len(expected)
for i, (line, want) in enumerate(zip(output, expected)):
    index, value = line.split('\t')
    assert int(index) == i
    actual = None if value == '__NULL__' else value
    assert actual == want, (operands[i], actual, want)
assert run.stdout.splitlines() == ['PASS 4296', 'Legacy logical order=False', 'Eqv logical order=True']
print(json.dumps(dict(status='PASS', comparisons=len(expected), bitwise_vectors=len(vectors),
                      typed_truth_cases=len(truth), engine='Windows VBScript',
                      scope='32-bit integer and Boolean/Null operands; no fractional or text coercion.',
                      legacy_precedence_difference=dict(expression='True Or True Xor True',
                                                        native_legacy='True', vbscript='False',
                                                        native_with_eqv_profile='VBA order'),
                      external_product_run=False), ensure_ascii=False, indent=2))
