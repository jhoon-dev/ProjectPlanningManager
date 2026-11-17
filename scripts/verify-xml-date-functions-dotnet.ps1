param([Parameter(Mandatory=$true)][string]$EvidenceDirectory)
$ErrorActionPreference = 'Stop'
[System.Threading.Thread]::CurrentThread.CurrentCulture = [System.Globalization.CultureInfo]::InvariantCulture
$taskEvidence = (Resolve-Path -LiteralPath $EvidenceDirectory).Path
$taskIntervals = @{yyyy='Year';q='Quarter';m='Month';y='DayOfYear';d='Day';w='Weekday';ww='WeekOfYear';h='Hour';n='Minute';s='Second'}
$taskComparisons = 0
$taskJsonOptions = @{}
if ((Get-Command ConvertFrom-Json).Parameters.ContainsKey('DateKind')) { $taskJsonOptions.DateKind = 'String' }
foreach ($taskRow in (Get-Content -Raw -LiteralPath (Join-Path $taskEvidence 'parts.json') | ConvertFrom-Json @taskJsonOptions)) {
    $taskDate = [datetime]::Parse($taskRow.input, [System.Globalization.CultureInfo]::InvariantCulture)
    $taskInterval = [Microsoft.VisualBasic.DateInterval]$taskIntervals[$taskRow.interval]
    $taskActual = [Microsoft.VisualBasic.DateAndTime]::DatePart($taskInterval, $taskDate,
        [Microsoft.VisualBasic.FirstDayOfWeek]$taskRow.firstDay, [Microsoft.VisualBasic.FirstWeekOfYear]$taskRow.firstWeek)
    if ($taskActual -ne [long]$taskRow.value) { throw "DatePart mismatch: $($taskRow | ConvertTo-Json -Compress) / $taskActual" }
    $taskComparisons++
}
foreach ($taskRow in (Get-Content -Raw -LiteralPath (Join-Path $taskEvidence 'adds.json') | ConvertFrom-Json @taskJsonOptions)) {
    $taskDate = [datetime]::Parse($taskRow.input, [System.Globalization.CultureInfo]::InvariantCulture)
    $taskInterval = [Microsoft.VisualBasic.DateInterval]$taskIntervals[$taskRow.interval]
    $taskActual = [Microsoft.VisualBasic.DateAndTime]::DateAdd($taskInterval, [double]$taskRow.count, $taskDate)
    $taskText = $taskActual.ToString('yyyy-MM-ddTHH:mm:ss.ffffff', [System.Globalization.CultureInfo]::InvariantCulture).TrimEnd('0').TrimEnd('.')
    if ($taskText -cne $taskRow.value) { throw "DateAdd mismatch: $($taskRow | ConvertTo-Json -Compress) / $taskText" }
    $taskComparisons++
}
foreach ($taskRow in (Get-Content -Raw -LiteralPath (Join-Path $taskEvidence 'diffs.json') | ConvertFrom-Json @taskJsonOptions)) {
    $taskDateFrom = [datetime]::Parse($taskRow.from, [System.Globalization.CultureInfo]::InvariantCulture)
    $taskDateTo = [datetime]::Parse($taskRow.to, [System.Globalization.CultureInfo]::InvariantCulture)
    $taskInterval = [Microsoft.VisualBasic.DateInterval]$taskIntervals[$taskRow.interval]
    $taskActual = [Microsoft.VisualBasic.DateAndTime]::DateDiff($taskInterval, $taskDateFrom, $taskDateTo,
        [Microsoft.VisualBasic.FirstDayOfWeek]$taskRow.firstDay, [Microsoft.VisualBasic.FirstWeekOfYear]::Jan1)
    if ($taskActual -ne [long]$taskRow.value) { throw "DateDiff mismatch: $($taskRow | ConvertTo-Json -Compress) / $taskActual" }
    $taskComparisons++
}
@{status='PASS';comparisons=$taskComparisons;culture='Invariant Gregorian';runtime=[System.Environment]::Version.ToString();
  scope='Integer additions/parts; difference vectors use identical time-of-day. VB.NET elapsed subday differences are not the PPM boundary contract.';
  external_product_run=$false} | ConvertTo-Json
