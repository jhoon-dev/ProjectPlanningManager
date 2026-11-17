"""Compare only the shared non-Null BMP/ASCII subset against Windows VBScript."""
import argparse
import json
from pathlib import Path
import subprocess

parser=argparse.ArgumentParser();parser.add_argument('root',type=Path);root=parser.parse_args().root.resolve()
rows=json.loads((root/'vectors.json').read_text(encoding='utf-8-sig'))
hextext=lambda s:''.join(f'{ord(c):04X}' for c in s)
lines=[]
for i,r in enumerate(rows):
    lines.append('\t'.join([str(i),hextext(r['source']),hextext(r['find']),hextext(r['replacement']),str(r['start']),
        str(r['count']),str(r['reverseStart']),str(r['compare']),hextext(r['replaced']),str(r['position']),hextext(r['reversed'])]))
(root/'vbscript-vectors.tsv').write_text('\n'.join(lines)+'\n',encoding='utf-16')
program=r'''Option Explicit
Function Decode(s)
 Dim i,n,v
 v=""
 For i=1 To Len(s) Step 4
  n=CLng("&H" & Mid(s,i,4))
  If n>32767 Then n=n-65536
  v=v & ChrW(n)
 Next
 Decode=v
End Function
Function Encode(s)
 Dim i,n,v
 v=""
 For i=1 To Len(s)
  n=AscW(Mid(s,i,1))
  If n<0 Then n=n+65536
  v=v & Right("0000" & Hex(n),4)
 Next
 Encode=v
End Function
Dim fso,f,a,s,find,replacement,v,total
Set fso=CreateObject("Scripting.FileSystemObject")
Set f=fso.OpenTextFile(WScript.Arguments(0),1,False,-1)
total=0
Do Until f.AtEndOfStream
 a=Split(f.ReadLine,vbTab)
 s=Decode(a(1)):find=Decode(a(2)):replacement=Decode(a(3))
 v=Replace(s,find,replacement,CLng(a(4)),CLng(a(5)),CInt(a(7)))
 If Encode(v)<>a(8) Then
  WScript.Echo "Replace mismatch row " & a(0) & " actual " & Encode(v) & " expected " & a(8)
  WScript.Quit 1
 End If
 v=InStrRev(s,find,CLng(a(6)),CInt(a(7)))
 If CStr(v)<>a(9) Then
  WScript.Echo "InStrRev mismatch row " & a(0)
  WScript.Quit 1
 End If
 v=StrReverse(s)
 If Encode(v)<>a(10) Then
  WScript.Echo "StrReverse mismatch row " & a(0)
  WScript.Quit 1
 End If
 total=total+3
Loop
f.Close
WScript.Echo "PASS " & total & " non-Null BMP/ASCII comparisons"
'''
(root/'vbscript-oracle.vbs').write_text(program,encoding='utf-16')
run=subprocess.run(['cscript.exe','//nologo',str(root/'vbscript-oracle.vbs'),str(root/'vbscript-vectors.tsv')],
                   capture_output=True,text=True,timeout=30)
(root/'vbscript.txt').write_text(run.stdout+run.stderr,encoding='utf-8')
assert run.returncode==0,(run.returncode,run.stdout,run.stderr)
assert f'PASS {len(rows)*3} ' in run.stdout
print(json.dumps(dict(status='PASS',comparisons=len(rows)*3,engine='Windows VBScript',
    scope='Non-Null BMP character reversal/binary matching and ASCII case-insensitive matching; integer controls.',
    null_difference='VBScript InStrRev(Null,...) errors; native intentionally follows VBA-documented Null propagation.',
    external_product_run=False),ensure_ascii=False,indent=2))
