param([string]$InputFile,[string]$OutputFile)
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
