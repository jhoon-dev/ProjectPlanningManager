# 새 XER 일정·WBS·달력 채택 검증 · 2026-11-08

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-schedule-adoption-2026-11-08)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

Windows x64 · Qt6.8.3 · MinGW13.1 · Release/Ninja. 전체 **136/136 통과, 184.47초**, 파서 **458개 시드**, clang-format **376개 파일** 통과. [기능 계약](../../xer-schedule-adoption.md) · [요약/소스 지문](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-schedule-adoption-2026-11-08/verification-summary.json) · [전체 로그](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-schedule-adoption-2026-11-08/ctest.txt) · [JUnit](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-schedule-adoption-2026-11-08/ctest.xml) · [개별 출력](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-schedule-adoption-2026-11-08/all-test-output.txt) · [제품 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-schedule-adoption-2026-11-08/final-build.txt) · [최종 테스트 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-schedule-adoption-2026-11-08/final-test-build.txt).

## 구현과 보존

기존 작업 ID 연결·명시 신규 추가·로컬 작업 보존과 일정/WBS/달력의7개 범위 조합을 검사했다. 기간·관계/지연·제약·시작일, 변경된 WBS 부모 관계·소속, 7시간 근무 달력·추가 휴일을 채택한다. 새 WBS·달력에는 별도 ID를 사용하며 이전 정의와 자원 달력 참조를 유지한다.

기준선·진행/원가/날짜별 관측 원장·**마감된 사용자 재무기간**·기존 XER 재무 원본을 보존했다. 기존 평준화 선택은 해제하고 결정 이력에 invalidate를 기록한다. 채택 후 일반 기간 편집/undo·임시 계산, 반복 채택 준비, worker 취소/stale/읽기 전용, 저장/재열기·복구·독립 사본과 XER 내보내기를 통과했다.

schema32 원본·연결표·전후 계획 재실행, 연속 revision 연결과 저장 직후 현재 입력의 대사를 검증한다. 임시 계산의 계획 변경은 persisted 문서 대사와 구분한다. 일반 undo로 채택 이력을 지우지 않는다.

## 독립 검사

[독립 Python oracle](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-schedule-adoption-2026-11-08/independent-oracle.json)은 **133개 검사**를 통과했다. 실제 후속 파일/envelope SHA256, 원장/기준선/마감의 전체 동일성, 작업 ID와 관계, WBS 부모·달력 정의의 ID 변환을 대조했다. native 달력 코드를 사용하지 않고 civil minute를 열거해 **8개 작업의 시작/완료16개**를 확인했다. FS/SS/FF/SF·양수/음수 지연·휴일·근무 구간·자원 달력 교집합·마일스톤을 포함한다.

MPXJ16.10.0/JPype1.6.0 strict reader가 ignored error 없이 계획 기간·WBS 부모·모든 달력의 근무 구간/휴일을 대사했다. 계획은 `getPlannedDuration()`으로 확인한다. 보존한 과거 실적과 미래 계획이 공존하는 설계 작업의 `getDuration()`은 null이며 reader가 실제 소요 기간을 새 달력으로 재해석할 수 있다. 이를 native 관측 이력 보존이나 계획 필드 판독과 혼동하지 않으며 실제 P6 재계산 동등성을 주장하지 않는다.

[증거 폴더](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-schedule-adoption-2026-11-08/xer-schedule-qa)에 실제 후속 XER, typed 원본 계획, 채택 전 native 본문, schema32 파일, 계획 날짜, 손상12종과 재내보낸 XER를 보존한다. [독립 스크립트](../../../scripts/verify-xer-schedule-adoption.py)를 재실행할 수 있다.

## Windows 화면

Qt windows 플랫폼에서 실제 파일 읽기·작업 연결/신규 추가·범위/작성자/사유·미리보기·확인·제출을 조작했다. 설정 변경 시 확인/제출 무효화, 닫기 취소와 실제 단일 제출·이력 재조회를 통과했다. [native 로그](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-schedule-adoption-2026-11-08/native-ui.txt), [기본 연결 창](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-schedule-adoption-2026-11-08/xer-schedule-qa/editor.png), [800×600](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-schedule-adoption-2026-11-08/xer-schedule-qa/editor-small.png), [날짜 전후](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-schedule-adoption-2026-11-08/xer-schedule-qa/preview.png), [WBS/달력 정의](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-schedule-adoption-2026-11-08/xer-schedule-qa/structure.png), [작은 정의 표](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-schedule-adoption-2026-11-08/xer-schedule-qa/structure-small.png), [이력 조회](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-schedule-adoption-2026-11-08/xer-schedule-qa/history.png)를 남겼다. 한국어, 본문 스크롤과 고정된 제출/닫기 footer를 확인했다.

## 회귀와 보완

전체 CTest에는 이전 XER 통화/종류 전환·재연결/병합/배분·공개 표본, XML, 원가/기간 EVM·외부 EV/PV·사용자 기간 마감, 공수/평준화·보고, 저장/복구·서버 HTTP/실시간 UI가 포함된다. 별도 패키징은 MSIX8개·macOS10개·Apple 서명 준비3개가 통과했고 macOS 플랫폼 조건2개는 Windows에서 건너뛰었다.

[첫 집중 검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-schedule-adoption-2026-11-08/initial-focused.txt)에서 배정 구간이 새 기간을 넘는 사례의 명시 차단이 빠진 것을 발견해 보완했다. [두 번째](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-schedule-adoption-2026-11-08/second-focused.txt)와 [확장 집중 검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-schedule-adoption-2026-11-08/third-focused.txt)는 통과했다. 이후 실제 WBS 부모 교체·정의 표·마감 보존·평준화 해제·일반 편집까지 포함한 전체136개와 native 실행이 통과했다.

실제 P6/MS Project 재계산/재저장, 관측 원장 ID 이관, 다른 OS 실행과 새 sanitizer/fuzzer 캠페인은 미실행이다. 원본 XER 전체 바이트가 필요하면 선택 파일을 별도로 보관한다.

## 재현

```powershell
$taskRoot = (Get-Location).Path
$evidence = Join-Path $taskRoot 'docs/validation/xer-schedule-adoption-2026-11-08'
$env:PATH = "$taskRoot/.deps/Qt/6.8.3/mingw_64/bin;$taskRoot/.deps/Qt/Tools/mingw1310_64/bin;$env:APPDATA/Python/Python314/Scripts;$env:PATH"
cmake --build build-governance --parallel 8
ctest --test-dir build-governance --output-on-failure --parallel 4 --output-junit "$evidence/ctest.xml" --output-log "$evidence/ctest.txt"
python scripts/verify-xer-schedule-adoption.py build-governance/xer-schedule-qa
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xer-schedule-adoption.py build-governance/xer-schedule-qa --mpxj
python scripts/check-cpp-quality.py format --tool .deps/cpp-quality/Lib/site-packages/clang_format/data/bin/clang-format.exe
python tests/test_msix_packaging.py
python tests/test_mac_store_packaging.py
python tests/test_prepare_apple_signing.py
```

native 화면은 별도 작업 폴더에서 `build-governance/jobs_xer_schedule_adoption_tests.exe -platform windows`를 실행한다. 해당 폴더의 `xer-schedule-qa`에 결과를 쓴다.
