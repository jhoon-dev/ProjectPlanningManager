param([string]$OutputFile)
$ErrorActionPreference='Stop'
[void][Reflection.Assembly]::Load('Microsoft.VisualBasic, Version=10.0.0.0, Culture=neutral, PublicKeyToken=b03f5f7f11d50a3a')
$rows=for ($i=0; $i -le 15; $i++) { "$i`t$([Microsoft.VisualBasic.Information]::QBColor($i))" }
$rows | Set-Content -LiteralPath $OutputFile -Encoding UTF8
Write-Output 'PASS: Microsoft.VisualBasic.Information.QBColor 16-entry palette'
