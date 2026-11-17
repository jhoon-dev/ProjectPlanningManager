# 자원 생산성·외부 고유 곡선 교환 검증

2026-11-09, Windows x64·Qt6.8.3/MinGW13.1 Release·Python3.14에서 수행했다. [기능 계약](../../resource-productivity-curves.md)과 실행 로그를 함께 보존한다.

## 최종 결과

- 전체 CTest **143/143 통과 · 223.86초**: [실행 로그](ctest.txt) · [JUnit](ctest.xml) · [검사별 출력](all-test-output.txt).
- 파서 **566개** 결정적 시드(schema1~36), clang-format18.1.8 **416파일**, 별도 패키징 **28개 통과/플랫폼 조건2개 제외**.
- Windows 도메인/UI 검사 통과, 독립 Fraction25개·달력72개/3,146자원분 및 변경 외 전체 필드 대조 통과: [검산 결과](independent-oracle.json).
- MPXJ16.10.0 strict reader의 XML/XER 원본 곡선/총공수·XML 일별 공수 일치(무시 오류0): [외부 교환](independent-exchange.json). 두 XLSX 각26시트와 PDF13/15페이지에서 물량120m3·생산성30/h·산정4h·현재 예산3600/관측AC12345·21값을 확인: [보고서 판독](report-verification.json).
- [최종 빌드](adopted-final-build.txt), [집중3/3](final-focused.txt), [소스/증거 SHA256 요약](verification-summary.json).

## 범위

생산성 카탈로그 저장만으로 기존 공수/기간을 바꾸지 않는지, FD/FW/FU의 명시 유지값·다중 자원 보존과 기존 native 계획의 effort 전환을 검사한다. 미리보기/확인·한 revision·undo/redo·취소/stale/권한/읽기 전용, schema36 저장·구 envelope 거절·전환 사본/복구, 원장·AC·기준선·시나리오 보존을 전체 구조로 대조한다. 생산성/곡선 근거만 바뀌면 평준화 채택을 유지하고 물리 수요가 바뀌면 해제하는 회귀도 포함한다.

곡선은8개 기본 MS 형식의 XML/XER 읽기/쓰기, 수동 XML 시간대별 계획 공수, 명시0공수와0기간 역산 작업, 소수점6자리 XER 원본 비중, 잘못된21값/수치/참조를 검사한다. 파서는 schema1~36의 결정적566개 시드를 재실행한다. 보고서의 실제 XLSX/PDF를 독립 reader로 판독한다.

[독립 검산 스크립트](../../../scripts/verify-resource-planning.py)는 Fraction 올림25개와 civil-minute 달력 교집합72개를 검사한다. 자원 휴일·작업 중단·부분 근무·20구간 최대 나머지 배분을 직접 구현하고3,146개 양수 자원분의 위치/배정률·총공수 및 변경을 허용한 구간/근거 외 전체 JSON 필드를 대조한다. [외부 reader 검사](../../../scripts/verify-resource-curve-exchange.py)는 MPXJ16.10.0/JPype1.6의 strict reader로 native 출력 XML/XER의 곡선21값과 총공수, XML 일별 공수를 대조한다. XER의 reader 시간대 재계산은 사용하지 않는다.

## 화면과 실행 기록

[Windows UI](native-ui.txt)는 기본1100×900·800×600의 실제 Qt Windows plugin을 사용했다. [생산성](resource-planning-qa/productivity.png)·[작은 화면](resource-planning-qa/productivity-small.png)·[곡선](resource-planning-qa/curve.png)·[작은 곡선](resource-planning-qa/curve-small.png)을 직접 확인했다. 작은 화면은 본문을 스크롤하고 하단 실행 버튼을 유지한다. 새 보고서 표의 [PDF14페이지](resource-planning-qa/curve-report-page-14.png)도 렌더링해21개 비중·산정 근거의 줄바꿈/표 범위를 확인했다.

초기 빌드에서 공수/날짜의 서로 다른 auto 선언과 XML export 배치 오류를 수정했다. 첫 집중 검사에서72개 곡선 사례를 통과했다. 후속 집중 검사의 평준화 fixture는 revision0을 adoption 근거로 사용해 기존 검증에 거절됐으므로 revision1의 정상 사전 상태로 교정했다. [첫 전체 회귀](preliminary-ctest.txt)는141/143을 통과하고 평준화 연계와 기존 곡선 비지원 진단을 기대하던 검사에서 실패했다. 물리 계획 검산용 사본에서 이전 채택/결정 이력을 분리하고 실제 문서 적용 시 무효화/이력 추가를 수행하도록 보완했다. 기존 구간 교환 검사는 XML 계획 시간대/ XER21값 및 집계 손실의 존재를 확인하도록 갱신했다. [후속 집중 검사](final-focused.txt)는3/3을 통과했다. offscreen PDF는 native Windows 글꼴 출력과 달라 독립 PDF 판독에는 `-platform windows` 산출물을 사용한다.

## 재현

```powershell
$taskRoot = (Get-Location).Path
$evidence = Join-Path $taskRoot 'docs/validation/resource-productivity-curves-2026-11-09'
$env:PATH = "$taskRoot/.deps/Qt/6.8.3/mingw_64/bin;$taskRoot/.deps/Qt/Tools/mingw1310_64/bin;$env:APPDATA/Python/Python314/Scripts;$env:PATH"
cmake --build build-governance --parallel 8
ctest --test-dir build-governance --output-on-failure --parallel 4 --output-junit "$evidence/ctest.xml" --output-log "$evidence/ctest.txt"
Push-Location $evidence
& "$taskRoot/build-governance/jobs_resource_planning_tests.exe" -platform windows
& "$taskRoot/build-governance/jobs_resource_planning_ui_tests.exe" -platform windows
Pop-Location
python scripts/verify-resource-planning.py "$evidence/resource-planning-qa"
.deps/report-verification/Scripts/python.exe scripts/verify-resource-planning.py "$evidence/resource-planning-qa" --reports
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-resource-curve-exchange.py "$evidence/resource-planning-qa"
python scripts/check-cpp-quality.py format --tool .deps/cpp-quality/Lib/site-packages/clang_format/data/bin/clang-format.exe
python tests/test_msix_packaging.py
python tests/test_mac_store_packaging.py
python tests/test_prepare_apple_signing.py
python tests/test_mpp_payload.py
```

실제 MS Project/P6 실행·재저장, macOS/스토어 실행과 새 sanitizer/fuzzer 캠페인은 수행하지 않았다. 독립 reader 판독은 해당 제품 재계산 동등성의 증거로 사용하지 않는다.
