# FixedWork/FixedUnits 날짜 고정 잔여 목표 검증

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/dated-effort-2026-11-08)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

2026-11-08 · Windows x64 · Qt6.8.3 · MinGW13.1 · Release/Ninja. [기능 계약](../../dated-effort-targets.md) · [결과/지문](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/dated-effort-2026-11-08/verification-summary.json) · [전체 CTest](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/dated-effort-2026-11-08/ctest.txt)·[JUnit](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/dated-effort-2026-11-08/ctest.xml)·[개별 출력](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/dated-effort-2026-11-08/all-test-output.txt) · [빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/dated-effort-2026-11-08/build.txt)·[최종 수정 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/dated-effort-2026-11-08/repair-build.txt) · [형식 검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/dated-effort-2026-11-08/format.txt).

**최종 전체141/141(202.41초)** · 집중4/4(26.90초) · 파서532개(schema1~35) · 형식402파일 · 독립72개 공수/완료·3개 전수 여유 · 전체 비대상 필드 보존 · 실제 XLSX23/Usage6시트·PDF11페이지 · Windows 기본/800×600 · 패키징28개 통과(플랫폼 조건2개 제외).

## 계산과 보존

`dated_effort_contract`는 FixedWork/FixedUnits의 공수 기준과 FixedUnits의 기간 기준을 검증한다. 상태일/관측 실제 시작·명시 잔여 총공수를 유지하고 고정 목표와0을 배분한 뒤, 미입력 날짜의 기존 곡선으로 나머지를 채워 완료를 계산한다. 기존 예상 완료 뒤의 목표, 같은 작업 여러 자원의 합계 초과·과부하·주말·stale 상태일·완료 뒤0을 검사한다.

72개 합성 사례는8분 작업 달력·부분 자원 달력·휴일·수동 중단·부분 상태 시각·명시0·상대 곡선과 두 자원을 조합한다. 별도 civil-minute 원장으로 각 날짜의 예약 목표와 나머지 자유 공수를 계산하고 모든 반환 수요·정확 총공수·최초 가능한 완료를 대조했다. 사례 입력과 실제 실행 구간은 [oracle.json](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/dated-effort-2026-11-08/dated-effort-qa/oracle.json)에 있다.

별도4분 달력3사례는12근무분 안의 모든 배치를 열거해 실제 용량과 날짜 목표를 만족하는 최대 총/자유여유를 대조한다. 목표200/300/400일 때 목표 작업의 총/자유여유는 각각2/1/0분, 후속 자원 작업은4분이다. 생산 후보 탐색·TaskExecution을 oracle로 재사용하지 않는다. [전수 배치 결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/dated-effort-2026-11-08/dated-effort-qa/float-oracle.json).

동일 작업의 여러 배정이 오래된 상태일을 가진 경우 FixedDuration/FixedWork 모두 하나씩 수정할 수 있음을 검사한다. 첫 배정만 수정하면 다른 배정의 원래 입력을 보존하고 전체 예측은 충돌 상태를 유지한다. 공수 기준은 모든 현재 목표의 합계를 함께 검사하므로 두 번째 목표가 총공수를 넘으면 문서를 그대로 유지하고 거절한다. 다른 작업의 오래된 입력도 자동 채택하지 않는다.

미리보기·준비 취소/stale·읽기 전용·한 revision·undo/redo·저장/재열기·복구 사본, 평준화·전체 부하/증인·채택/무효화·동결 시나리오, schema35와 구 envelope 거절을 검사했다. 기존 계획 입력/곡선·관측 진척/실제공수·기준선은 전체 구조 비교로 보존한다. 날짜 입력을 해제해도 과거 시나리오/평준화 정책의 새 의미가 있으면 schema35를 유지한다.

## 독립 판독과 Windows

[독립 Python 검산](../../../scripts/verify-dated-effort.py)은 앱 라이브러리 없이 datetime 달력 교집합·휴일/중단·배정 곡선, 날짜 예약과 자유 공수의 분 단위 원장을 재현한다. 72사례의 양수 자원분2,078개와3개 전수 여유 사례, 전환 전후 **모든 비대상 JSON 필드**의 동일성을 검사했다. [결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/dated-effort-2026-11-08/independent-oracle.json)와 [변경 전](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/dated-effort-2026-11-08/dated-effort-qa/before.json)·[변경 후](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/dated-effort-2026-11-08/dated-effort-qa/after.json)를 보존한다.

실제 [보고서 XLSX](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/dated-effort-2026-11-08/dated-effort-qa/report.xlsx)23시트·[Usage XLSX](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/dated-effort-2026-11-08/dated-effort-qa/usage.xlsx)6시트·[PDF](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/dated-effort-2026-11-08/dated-effort-qa/report.pdf)11페이지를 생성했다. openpyxl3.1.5/pypdf6.10.0은 날짜별 목표2시간/명시0, 계획/기준선16시간·잔여8시간, 잔여 일별2/6시간과 유효 단가 원가240/720·합계960을 대조한다. PDF에서 제목·목표 날짜/설명과 페이지 구조를 확인한다. [판독 결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/dated-effort-2026-11-08/report-verification.json).

실제 Windows 플랫폼에서 FixedWork/FixedUnits를 각각 열고 기존2행·예상 완료 뒤 날짜 추가·검산 완료 시각·총공수 초과 거절·수정 시 인증 무효화·문서 적용/undo·저장/읽기 전용을 조작했다. 기존 취소·stale·권한 회수·완료/오래된 입력·메뉴 테스트도 함께 실행한다. [FixedWork 기본](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/dated-effort-2026-11-08/dated-effort-qa/fixed-work.png)·[800×600](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/dated-effort-2026-11-08/dated-effort-qa/fixed-work-small.png), [FixedUnits 기본](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/dated-effort-2026-11-08/dated-effort-qa/fixed-units.png)·[800×600](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/dated-effort-2026-11-08/dated-effort-qa/fixed-units-small.png), [native UI 로그](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/dated-effort-2026-11-08/native-ui.txt)·[native domain 로그](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/dated-effort-2026-11-08/native-domain.txt).

XML/XER은 실제 내보내고 날짜 목표 손실 안내와 `source.ppm` 보존 계약을 유지한다. 원본 파일은 [XML](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/dated-effort-2026-11-08/dated-effort-qa/export.xml)·[XER](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/dated-effort-2026-11-08/dated-effort-qa/export.xer)에 있다. 실제 Microsoft Project/P6 재계산·재저장, 다른 OS/스토어 실행, 새 sanitizer/fuzzer 캠페인은 미실행이다.

## 실행 기록과 수정

초기 빌드는 테스트의 잘못된 프로젝트 이름 편집 호출을 작업 이름 편집으로 고쳤다. [첫 집중 검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/dated-effort-2026-11-08/first-focused.txt)의 undo 검사는 revision까지 과거로 돌아간다고 가정해 실패했으므로 기존 단조 증가 revision·저장점/입력 복원을 따르도록 수정했다. [두 번째](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/dated-effort-2026-11-08/second-focused.txt)는 기존에 금지된 기본 배정률0을 사용한 표본을 교정했다. [집중4/4](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/dated-effort-2026-11-08/final-focused.txt)를 통과한 뒤 전체141/141(201.84초·[기록](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/dated-effort-2026-11-08/preliminary-ctest.txt)), 다시141/141(183.29초·[기록](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/dated-effort-2026-11-08/second-ctest.txt))을 통과했다.

별도 native PDF 판독은 등록한 글꼴이 글자 대신 벡터 윤곽선으로 출력되는 것을 확인했다. 테스트에서 실제 데스크톱처럼 설치된 Malgun Gothic을 직접 사용하고 PdfOptions에 명시했다. 도메인·보고서 실행을 다시 수행해 추출 가능한 텍스트와 숫자를 확인했다. 제품의 계산 의미는 이 수정에서 바뀌지 않았다.

마지막 연계 검토에서 동일 작업의 오래된 다중 배정을 순차 수정할 수 있도록 검산용 사본만 분리하고, 현재 작업의 유효 목표는 합계 검사에 포함하도록 보완했다. [수정 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/dated-effort-2026-11-08/repair-build.txt)·[집중 검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/dated-effort-2026-11-08/repair-focused.txt)·최종 전체 회귀 결과를 위 최신 CTest/JUnit에 남긴다. 전체는 MPP/XER·통화/ID 이관/기간 마감·공수/곡선/평준화·보고/저장/복구·서버/실시간 UI를 포함한다. 파서532개 결정적 시드(schema1~35), 형식402파일과 별도 패키징28개를 검증한다. macOS 플랫폼 조건2개는 Windows에서 제외한다.

## 재현

```powershell
$taskRoot = (Get-Location).Path
$evidence = Join-Path $taskRoot 'docs/validation/dated-effort-2026-11-08'
$env:PATH = "$taskRoot/.deps/Qt/6.8.3/mingw_64/bin;$taskRoot/.deps/Qt/Tools/mingw1310_64/bin;$env:APPDATA/Python/Python314/Scripts;$env:PATH"
cmake --build build-governance --parallel 8
ctest --test-dir build-governance --output-on-failure --parallel 4 --output-junit "$evidence/ctest.xml" --output-log "$evidence/ctest.txt"
python scripts/verify-dated-effort.py "$evidence/dated-effort-qa"
.deps/report-verification/Scripts/python.exe scripts/verify-dated-effort.py "$evidence/dated-effort-qa" --reports
python scripts/check-cpp-quality.py format --tool .deps/cpp-quality/Lib/site-packages/clang_format/data/bin/clang-format.exe
python tests/test_msix_packaging.py
python tests/test_mac_store_packaging.py
python tests/test_prepare_apple_signing.py
python tests/test_mpp_payload.py
Push-Location $evidence
& "$taskRoot/build-governance/jobs_dated_remaining_ui_tests.exe" -platform windows
& "$taskRoot/build-governance/jobs_dated_effort_tests.exe" -platform windows
Pop-Location
```
