"""Compare bounded exact-rational native results with Windows Visual Basic binary64."""
import argparse
from decimal import Decimal, ROUND_HALF_UP
import json
import os
from pathlib import Path
import subprocess

parser=argparse.ArgumentParser()
parser.add_argument('root',type=Path)
root=parser.parse_args().root.resolve()
load=lambda n:json.loads((root/n).read_text(encoding='utf-8-sig'))
vectors=load('vectors.json')
selected=[r for r in vectors if 'value' in r and abs(int(r['value']))<=1000000000000
          and max(int(r['cost']),int(r['salvage']))<=1000000000000]
(root/'windows-input.json').write_text(json.dumps(selected),encoding='utf-8-sig')
program='''param([string]$InputFile,[string]$OutputFile)
$ErrorActionPreference='Stop'
[void][Reflection.Assembly]::Load('Microsoft.VisualBasic, Version=10.0.0.0, Culture=neutral, PublicKeyToken=b03f5f7f11d50a3a')
$culture=[Globalization.CultureInfo]::InvariantCulture
[Threading.Thread]::CurrentThread.CurrentCulture=$culture
$rows=Get-Content -LiteralPath $InputFile -Raw -Encoding UTF8 | ConvertFrom-Json
$output=foreach($r in $rows) {
    $cost=[double]::Parse($r.cost,$culture)/1000000.0
    $salvage=[double]::Parse($r.salvage,$culture)/1000000.0
    $life=[double]::Parse($r.life,$culture)/1000000.0
    $period=[double]::Parse($r.period,$culture)/1000000.0
    if($r.function -eq 'sln') { $value=[Microsoft.VisualBasic.Financial]::SLN($cost,$salvage,$life) }
    else { $value=[Microsoft.VisualBasic.Financial]::SYD($cost,$salvage,$life,$period) }
    $value.ToString('R',$culture)
}
$output | ConvertTo-Json | Set-Content -LiteralPath $OutputFile -Encoding UTF8
$bad=0
try { [void][Microsoft.VisualBasic.Financial]::SLN(1,0,0) } catch { $bad++ }
try { [void][Microsoft.VisualBasic.Financial]::SYD(1,-1,1,1) } catch { $bad++ }
try { [void][Microsoft.VisualBasic.Financial]::SYD(1,0,1,0) } catch { $bad++ }
try { [void][Microsoft.VisualBasic.Financial]::SYD(1,0,1,2) } catch { $bad++ }
try { [void][Microsoft.VisualBasic.Financial]::SYD(1,0,1,-1) } catch { $bad++ }
if($bad -ne 5) { throw 'Expected five domain errors' }
Write-Output 'PASS: Windows Microsoft.VisualBasic.Financial.SLN/SYD and 5 common domain errors'
'''
(root/'windows-oracle.ps1').write_text(program,encoding='utf-8-sig')
system=Path(os.environ['WINDIR'])/'System32/WindowsPowerShell/v1.0/powershell.exe'
run=subprocess.run([str(system),'-NoProfile','-NonInteractive','-File',str(root/'windows-oracle.ps1'),str(root/'windows-input.json'),str(root/'windows-output.json')],capture_output=True,text=True,errors='replace',timeout=120)
(root/'windows.txt').write_text(run.stdout+run.stderr,encoding='utf-8')
assert run.returncode==0,(run.returncode,run.stdout,run.stderr)
actual=load('windows-output.json')
assert len(actual)==len(selected)
differences=[]
for i,(r,value) in enumerate(zip(selected,actual)):
    scaled=int((Decimal(value)*1000000).quantize(Decimal(1),rounding=ROUND_HALF_UP))
    difference=abs(scaled-int(r['value']))
    assert difference<=1,(r,value,difference)
    if difference:differences.append(dict(index=i,function=r['function'],
        scaled_inputs={k:r[k] for k in ('cost','salvage','life','period')},
        native=r['value'],windows=value,delta_scaled=difference))
(root/'windows-differences.json').write_text(json.dumps(differences,indent=2),encoding='utf-8')
print(json.dumps(dict(status='PASS',depreciation_cases=len(selected),exact_rounded=len(selected)-len(differences),
    one_micro_differences=len(differences),max_scaled_difference=max([0]+[d['delta_scaled'] for d in differences]),
    error_cases=5,engine='Windows .NET Microsoft.VisualBasic.Financial.SLN/SYD; invariant culture',
    scope='Nonnegative cost/salvage, positive life/period and bounded values/results; native exact rational vs Windows binary64 with tolerance 0.000001',
    external_product_run=False),ensure_ascii=False,indent=2))
