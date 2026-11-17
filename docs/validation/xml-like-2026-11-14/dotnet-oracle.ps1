param([string]$Root)
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
