"""Compare the valid BMP/ASCII-digit subset with the Windows .NET binary Like engine."""
import argparse
import json
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('root', type=Path)
root = parser.parse_args().root.resolve()
program = r'''param([string]$Root)
$ErrorActionPreference='Stop'
[void][Reflection.Assembly]::Load('Microsoft.VisualBasic, Version=10.0.0.0, Culture=neutral, PublicKeyToken=b03f5f7f11d50a3a')
$rows=Get-Content -LiteralPath (Join-Path $Root 'vectors.json') -Encoding UTF8 -Raw | ConvertFrom-Json
$writer=New-Object IO.StreamWriter((Join-Path $Root 'dotnet-output.tsv'),$false,(New-Object Text.UTF8Encoding($false)))
$i=0
try {
 foreach($row in $rows) {
  $value=[Microsoft.VisualBasic.CompilerServices.LikeOperator]::LikeString([string]$row.text,[string]$row.pattern,[Microsoft.VisualBasic.CompareMethod]::Binary)
  $writer.WriteLine([string]$i+"`t"+[string]$value)
  $i++
 }
}finally{$writer.Close()}
$digit=[Microsoft.VisualBasic.CompilerServices.LikeOperator]::LikeString('١','#',[Microsoft.VisualBasic.CompareMethod]::Binary)
$empty=[Microsoft.VisualBasic.CompilerServices.LikeOperator]::LikeString('','[',[Microsoft.VisualBasic.CompareMethod]::Binary)
@{comparisons=$i;unicode_digit=$digit;empty_invalid_pattern=$empty} | ConvertTo-Json -Compress
'''
(root / 'dotnet-oracle.ps1').write_text(program, encoding='utf-8-sig')
shell = Path(os.environ['WINDIR']) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
run = subprocess.run([str(shell), '-NoProfile', '-NonInteractive', '-File', str(root / 'dotnet-oracle.ps1'), str(root)],
                     capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=180)
(root / 'dotnet.txt').write_text(run.stdout + run.stderr, encoding='utf-8')
assert run.returncode == 0, (run.returncode, run.stdout, run.stderr)
metadata = json.loads(run.stdout)
rows = json.loads((root / 'vectors.json').read_text(encoding='utf-8-sig'))
output = (root / 'dotnet-output.tsv').read_text(encoding='utf-8-sig').splitlines()
assert len(output) == len(rows) == metadata['comparisons'] == 8932
for i, (line, row) in enumerate(zip(output, rows)):
    index, value = line.split('\t')
    assert int(index) == i and (value == 'True') == row['matched'], (i, row, value)
assert metadata['unicode_digit'] is True and metadata['empty_invalid_pattern'] is False
print(json.dumps(dict(status='PASS', comparisons=len(rows), engine='Windows .NET VisualBasic LikeOperator Binary',
                      scope='Valid BMP patterns with ASCII digit matching; no locale text comparison.',
                      differences=dict(non_ascii_digit=dict(native=False, dotnet=True),
                                       empty_invalid_pattern=dict(native='error', dotnet=False)),
                      external_product_run=False), ensure_ascii=False, indent=2))
