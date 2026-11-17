param([Parameter(Mandatory=$true)][string]$EvidenceDirectory)
$ErrorActionPreference='Stop'
[System.Threading.Thread]::CurrentThread.CurrentCulture=[System.Globalization.CultureInfo]::InvariantCulture
$taskEvidence=(Resolve-Path -LiteralPath $EvidenceDirectory).Path
$taskComparisons=0
foreach($taskRow in (Get-Content -Raw -LiteralPath (Join-Path $taskEvidence 'conversions.json') | ConvertFrom-Json)) {
    $taskNumber=[int]::Parse($taskRow.integer,[System.Globalization.CultureInfo]::InvariantCulture)
    $taskHex=[Microsoft.VisualBasic.Conversion]::Hex($taskNumber)
    $taskOct=[Microsoft.VisualBasic.Conversion]::Oct($taskNumber)
    $taskStr=[Microsoft.VisualBasic.Conversion]::Str($taskNumber)
    $taskVal=[Microsoft.VisualBasic.Conversion]::Val('&H'+$taskRow.hex+'&')
    if($taskHex -cne $taskRow.hex -or $taskOct -cne $taskRow.oct -or $taskStr -cne $taskRow.str -or $taskVal -ne [long]$taskRow.roundtrip) {
        throw "Conversion mismatch: $($taskRow | ConvertTo-Json -Compress)"
    }
    $taskComparisons+=4
}
@{status='PASS';comparisons=$taskComparisons;culture='Invariant';runtime=[System.Environment]::Version.ToString();
  scope='Signed Int32 Hex/Oct, integer Str and explicit Long radix Val. Native fixed-six decimal prefixes are independently checked with Decimal.';
  external_product_run=$false} | ConvertTo-Json
