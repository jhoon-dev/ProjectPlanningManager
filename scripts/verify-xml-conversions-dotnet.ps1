param([Parameter(Mandatory=$true)][string]$EvidenceDirectory)
$ErrorActionPreference='Stop'
$taskCulture=[System.Globalization.CultureInfo]::InvariantCulture
$taskComparisons=0
foreach($taskRow in (Get-Content -Raw -LiteralPath (Join-Path $EvidenceDirectory 'vectors.json') | ConvertFrom-Json)) {
    $taskDecimal=[decimal]::Parse($taskRow.input,$taskCulture)/1000000
    $taskSingle=[single]::Parse($taskDecimal.ToString($taskCulture),$taskCulture)
    # Widen first: directly casting Single to Decimal keeps only seven significant digits.
    $taskScaled=[decimal][double]$taskSingle*1000000
    $taskRounded=[long][Math]::Round($taskScaled,0,[MidpointRounding]::AwayFromZero)
    if($taskRounded -ne [long]::Parse($taskRow.output,$taskCulture)) {
        throw "Single conversion mismatch for $($taskRow.input): $taskRounded versus $($taskRow.output)"
    }
    $taskComparisons++
}
@{status='PASS';comparisons=$taskComparisons;runtime=[System.Environment]::Version.ToString();culture='Invariant';
  scope='Single.Parse of exact native decimal input, widened Single then native fixed-six half-away rounding; no VBA Variant/NLS claim.';
  external_product_run=$false} | ConvertTo-Json
