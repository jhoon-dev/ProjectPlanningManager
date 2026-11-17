"""Compare RGB with Windows VBScript and QBColor with .NET Microsoft.VisualBasic."""
import argparse
import json
import os
from pathlib import Path
import subprocess

parser=argparse.ArgumentParser()
parser.add_argument('root',type=Path)
root=parser.parse_args().root.resolve()
load=lambda n:json.loads((root/n).read_text(encoding='utf-8-sig'))
rows=[(i,r) for i,r in enumerate(load('vectors.json')) if r['function']=='rgb']
selected=[(i,r) for i,r in enumerate(load('cases.json')) if 'qbcolor' not in r['source'].lower() and not r['source'].startswith('IIf(')]
inputs=['\t'.join(str(v) for v in (i,r['red'],r['green'],r['blue'])) for i,r in rows]
(root/'vbscript-input.tsv').write_text('\n'.join(inputs)+'\n',encoding='utf-16')
program='''Option Explicit
Dim fso,input,stream,fields,value
SetLocale 1033
Set fso=CreateObject("Scripting.FileSystemObject")
Set input=fso.OpenTextFile(WScript.Arguments(0) & "\\vbscript-input.tsv",1,False,-1)
Set stream=fso.CreateTextFile(WScript.Arguments(0) & "\\vbscript-output.tsv",True,True)
Do Until input.AtEndOfStream
 fields=Split(input.ReadLine,vbTab)
 value=RGB(CInt(fields(1)),CInt(fields(2)),CInt(fields(3)))
 stream.WriteLine fields(0) & vbTab & value
Loop
input.Close
stream.Close
Set stream=fso.CreateTextFile(WScript.Arguments(0) & "\\vbscript-scalars.tsv",True,True)
'''
for i,row in selected:
    literal='"'+row['source'].replace(';',',').replace('"','""')+'"'
    program+=f'stream.WriteLine "{i}" & vbTab & CStr(Eval({literal}))\n'
program+='stream.Close\nOn Error Resume Next\n'
errors=[('RGB(-1,0,0)',5),('RGB(0,-1,0)',5),('RGB(0,0,-1)',5),('RGB(32768,0,0)',6),
        ('RGB(32767.5,0,0)',6),('RGB(Null,0,0)',94),('RGB(0,Null,0)',94),('RGB(0,0,Null)',94)]
for i,(source,code) in enumerate(errors):
    program+=f'Err.Clear\nvalue={source}\nIf Err.Number <> {code} Then WScript.Quit {i+2}\n'
program+='On Error GoTo 0\nWScript.Echo "PASS: RGB matrix, scalar expressions, 8 errors; LCID1033"\n'
(root/'vbscript-oracle.vbs').write_text(program,encoding='utf-16')
system=Path(os.environ['WINDIR'])/'System32'
run=subprocess.run([str(system/'cscript.exe'),'//Nologo',str(root/'vbscript-oracle.vbs'),str(root)],capture_output=True,text=True,errors='replace',timeout=180)
(root/'vbscript.txt').write_text(run.stdout+run.stderr,encoding='utf-8')
assert run.returncode==0,(run.returncode,run.stdout,run.stderr)
for filename,expected in [('vbscript-output.tsv',rows),('vbscript-scalars.tsv',selected)]:
    lines=(root/filename).read_text(encoding='utf-16').splitlines()
    assert len(lines)==len(expected)
    for line,(i,row) in zip(lines,expected):
        index,value=line.split('\t',1)
        assert int(index)==i and value==str(row['value']),(row,value)
assert len(rows)==5593 and len(selected)==17
# VBScript has no QBColor built-in. Use the Windows .NET Visual Basic implementation.
program='''param([string]$OutputFile)
$ErrorActionPreference='Stop'
[void][Reflection.Assembly]::Load('Microsoft.VisualBasic, Version=10.0.0.0, Culture=neutral, PublicKeyToken=b03f5f7f11d50a3a')
$rows=for ($i=0; $i -le 15; $i++) { "$i`t$([Microsoft.VisualBasic.Information]::QBColor($i))" }
$rows | Set-Content -LiteralPath $OutputFile -Encoding UTF8
Write-Output 'PASS: Microsoft.VisualBasic.Information.QBColor 16-entry palette'
'''
(root/'palette-oracle.ps1').write_text(program,encoding='utf-8-sig')
run=subprocess.run([str(system/'WindowsPowerShell/v1.0/powershell.exe'),'-NoProfile','-NonInteractive','-File',str(root/'palette-oracle.ps1'),str(root/'palette-output.tsv')],capture_output=True,text=True,errors='replace',timeout=60)
(root/'palette.txt').write_text(run.stdout+run.stderr,encoding='utf-8')
assert run.returncode==0,(run.returncode,run.stdout,run.stderr)
palette=[r for r in load('vectors.json') if r['function']=='qbcolor']
lines=(root/'palette-output.tsv').read_text(encoding='utf-8-sig').splitlines()
assert len(lines)==len(palette)==16
for line,row in zip(lines,palette):
    i,value=map(int,line.split('\t'))
    assert (i,value)==(row['index'],row['value']),(line,row)
print(json.dumps(dict(status='PASS',rgb_cases=len(rows),scalar_cases=len(selected),error_cases=len(errors),palette_cases=len(palette),
    rgb_engine='Windows VBScript RGB; SetLocale1033',palette_engine='Windows .NET Microsoft.VisualBasic.Information.QBColor',
    lcid=1033,external_product_run=False),ensure_ascii=False,indent=2))
