"""Compare the supported BMP subset and scalar expressions with Windows VBScript."""
import argparse
import json
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('root', type=Path)
root = parser.parse_args().root.resolve()
cases = json.loads((root / 'cases.json').read_text(encoding='utf-8-sig'))
selected = [(i, row) for i, row in enumerate(cases) if not row['source'].startswith('IIf(')]
program = '''Option Explicit
Dim fso, stream, i, allowed, value, aliasMatch
Set fso = CreateObject("Scripting.FileSystemObject")
Set stream = fso.CreateTextFile(WScript.Arguments(0) & "\\vbscript-output.tsv", True, True)
For i = 0 To 65535
 allowed = Not (i < 32 And i <> 9 And i <> 10 And i <> 13) And i <> 127 And _
           Not (i >= 55296 And i <= 57343) And i < 65534
 If allowed Then
  value = AscW(ChrW(i))
  aliasMatch = True
  If i >= 32768 Then aliasMatch = (ChrW(i-65536) = ChrW(i))
  stream.WriteLine CStr(i) & vbTab & CStr(value) & vbTab & CStr(aliasMatch)
 End If
Next
stream.Close
Set stream = fso.CreateTextFile(WScript.Arguments(0) & "\\vbscript-scalars.tsv", True, True)
'''
for i, row in selected:
    literal = '"' + row['source'].replace('"', '""') + '"'
    program += f'stream.WriteLine "{i}" & vbTab & CStr(Eval({literal}))\n'
program += '''stream.Close
On Error Resume Next
value = AscW(Null)
If Err.Number <> 94 Then WScript.Quit 2
Err.Clear
value = ChrW(Null)
If Err.Number <> 94 Then WScript.Quit 3
Err.Clear
value = AscW("")
If Err.Number <> 5 Then WScript.Quit 4
Err.Clear
On Error GoTo 0
WScript.Echo "PASS: 63456 BMP code pairs; signed aliases, scalars and Null/empty errors"
'''
(root / 'vbscript-oracle.vbs').write_text(program, encoding='utf-16')
shell = Path(os.environ['WINDIR']) / 'System32/cscript.exe'
run = subprocess.run([str(shell), '//Nologo', str(root / 'vbscript-oracle.vbs'), str(root)],
                     capture_output=True, text=True, errors='replace', timeout=180)
(root / 'vbscript.txt').write_text(run.stdout + run.stderr, encoding='utf-8')
assert run.returncode == 0, (run.returncode, run.stdout, run.stderr)
vectors = json.loads((root / 'vectors.json').read_text(encoding='utf-8-sig'))
lines = (root / 'vbscript-output.tsv').read_text(encoding='utf-16').splitlines()
assert len(lines) == len(vectors) == 63456
for line, row in zip(lines, vectors):
    code, signed, alias = line.split('\t')
    assert int(code) == row['input'] and int(signed) == row['asc'] and alias == 'True', (row, line)
lines = (root / 'vbscript-scalars.tsv').read_text(encoding='utf-16').splitlines()
assert len(lines) == len(selected) == 24
for line, (i, row) in zip(lines, selected):
    index, value = line.split('\t', 1)
    assert int(index) == i and value == row['value'], (row, value)
print(json.dumps(dict(status='PASS', code_pairs=len(vectors), scalar_cases=len(selected),
                      engine='Windows VBScript AscW/ChrW', null_and_empty_errors=True,
                      scope='Native/XML-safe BMP characters; signed 16-bit AscW and ChrW aliases',
                      external_product_run=False), ensure_ascii=False, indent=2))
