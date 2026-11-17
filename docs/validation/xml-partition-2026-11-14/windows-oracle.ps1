param([string]$InputFile,[string]$OutputFile)
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
