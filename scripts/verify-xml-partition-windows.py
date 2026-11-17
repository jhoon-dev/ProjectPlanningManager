"""Compare whole-number ranges and rounded scalar arguments with Windows Visual Basic."""
import argparse
from decimal import Decimal, ROUND_HALF_EVEN
import json
import os
from pathlib import Path
import re
import subprocess

parser=argparse.ArgumentParser()
parser.add_argument('root',type=Path)
root=parser.parse_args().root.resolve()
load=lambda n:json.loads((root/n).read_text(encoding='utf-8-sig'))
vectors=load('vectors.json')
cases=load('cases.json')
selected=[]
for row in cases:
    m=re.fullmatch(r'(?i)partition\(([-\d.;,A-Za-z]+)\)',row['source'])
    if not m:continue
    args=m[1].replace(';',',').split(',')
    values=[str(int(Decimal({'true':'-1','false':'0'}.get(v.lower(),v)).quantize(Decimal(1),rounding=ROUND_HALF_EVEN))) for v in args]
    selected.append(dict(args=values,value=row['value']))
inputs=[dict(args=[str(r[k]) for k in ('number','start','stop','interval')],value=r['value']) for r in vectors]+selected
(root/'windows-input.json').write_text(json.dumps(inputs,ensure_ascii=False),encoding='utf-8-sig')
program='''param([string]$InputFile,[string]$OutputFile)
$ErrorActionPreference='Stop'
[void][Reflection.Assembly]::Load('Microsoft.VisualBasic, Version=10.0.0.0, Culture=neutral, PublicKeyToken=b03f5f7f11d50a3a')
[Threading.Thread]::CurrentThread.CurrentCulture=[Globalization.CultureInfo]::InvariantCulture
$rows=Get-Content -LiteralPath $InputFile -Raw -Encoding UTF8 | ConvertFrom-Json
$output=foreach($r in $rows) {
    [Microsoft.VisualBasic.Interaction]::Partition([long]$r.args[0],[long]$r.args[1],[long]$r.args[2],[long]$r.args[3])
}
$output | ConvertTo-Json | Set-Content -LiteralPath $OutputFile -Encoding UTF8
foreach($probeArguments in @(@(1,-1,99,1),@(1,5,5,1),@(1,5,4,1),@(1,0,99,0),@(1,0,99,-1))) {
    $rejected=$false
    try { [void][Microsoft.VisualBasic.Interaction]::Partition($probeArguments[0],$probeArguments[1],$probeArguments[2],$probeArguments[3]) }
    catch { $rejected=$true }
    if(-not $rejected) { throw 'Expected domain rejection' }
}
Write-Output 'PASS: Windows Microsoft.VisualBasic.Interaction.Partition and 5 domain errors'
'''
(root/'windows-oracle.ps1').write_text(program,encoding='utf-8-sig')
system=Path(os.environ['WINDIR'])/'System32/WindowsPowerShell/v1.0/powershell.exe'
run=subprocess.run([str(system),'-NoProfile','-NonInteractive','-File',str(root/'windows-oracle.ps1'),str(root/'windows-input.json'),str(root/'windows-output.json')],capture_output=True,text=True,errors='replace',timeout=120)
(root/'windows.txt').write_text(run.stdout+run.stderr,encoding='utf-8')
assert run.returncode==0,(run.returncode,run.stdout,run.stderr)
actual=load('windows-output.json')
assert len(actual)==len(inputs)
for row,value in zip(inputs,actual):assert row['value']==value,(row,value)
print(json.dumps(dict(status='PASS',partition_cases=len(vectors),scalar_cases=len(selected),error_cases=5,
    engine='Windows .NET Microsoft.VisualBasic.Interaction.Partition; invariant culture',
    scope='Scalar arguments rounded independently with Python Decimal ROUND_HALF_EVEN; exact spaces compared',
    external_product_run=False),ensure_ascii=False,indent=2))
