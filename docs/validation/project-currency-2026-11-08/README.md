# 프로젝트 전체 통화 전환 검증 · 2026-11-08

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/project-currency-2026-11-08)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

Windows x64 · Qt6.8.3 · MinGW13.1 · Release/Ninja. [기능 계약](../../project-currency-conversion.md) · [결과/소스 지문](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/project-currency-2026-11-08/verification-summary.json) · [전체 CTest](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/project-currency-2026-11-08/ctest.txt)·[JUnit](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/project-currency-2026-11-08/ctest.xml)·[개별 출력](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/project-currency-2026-11-08/all-test-output.txt) · [빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/project-currency-2026-11-08/build.txt)·[최종 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/project-currency-2026-11-08/final-build.txt) · [형식 검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/project-currency-2026-11-08/format.txt).

**최종 전체140/140(200.97초)** · 파서504개(schema1~34) · 형식400파일 · 독립 환산576사례/전체25,444필드 · 실제 Windows 기본/800×600 · XLSX34시트/PDF22페이지 · 독립 MPXJ strict XML/XER4파일 · 별도 패키징28개 통과(플랫폼 조건2개 제외).

## 실제 전환과 원본

실제 XER 테스트 표본에서 현재 단가/고정비·자재/경비, Steps·기준선·시나리오·사용자 기간 마감·재무 병합/재연결·외부 EV/PV를 구성했다. EUR 원본 입력의 기존 USD 변환, 원장 ID 이관 및 WBS 채택 이력을 가진 프로젝트를 USD→KRW로 환산한다. 현재 금액205개 항목의 배율/미정·명시0, 선택 작업 BAC와 미보고가 섞인 전체 BAC의 미정 유지, 날짜/공수·정정/작성·승인 정보 및 원래 이력 불변을 대조했다.

전환 후 일반 편집/undo·stale/취소·읽기 전용, 저장/재열기·복구·독립 사본, XER 재병합, 추가 ID 이관과 KRW→USD 두 번째 전환을 검증한다. 이전 이벤트는 변경하지 않는다. 별도 작은 원장에서 개별 반올림으로 날짜별 합계가 누계를 넘는 경우 제출을 거절하고 문서가 그대로인지 확인했다. 정확한 최소 단위 정책과 평준화 해제, 연속16건 허용·17건째 원자적 거절/저장 재실행을 검사했다.

구/미래 schema, 잘못된 환율·근거/기준일·정책/확인/작성 정보·UTC, 중첩/알 수 없는 필드와 변조한 원본/결과/현재 루트를 거절한다. 최초 [집중 검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/project-currency-2026-11-08/first-focused.txt)는 전체 BAC를 알려진 값으로 가정해 실패했다. 이 표본의 다른 작업은 예산 미보고이므로 전체 미정을 유지하고, 알려진 선택 작업의 BAC를 별도로 검산하도록 [두 번째 검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/project-currency-2026-11-08/second-focused.txt)를 보완했다.

Windows 재실행에서는 테스트 보고서의 기존 출력 경로와 충돌했다. 보고서를 매번 임시 경로에 생성한 뒤 증거 파일로 복사하도록 테스트를 수정했다. [수정 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/project-currency-2026-11-08/repeatable-test-build.txt) 이후 동일 실행 파일로 native 검사를 재실행했고, 전체 회귀의 새 통화 테스트도 수정된 실행 파일로 통과했다. 제품의 기존 출력 파일 보호 동작은 유지한다.

## 독립 검사와 출력

[Python oracle](../../../scripts/verify-project-currency.py)은 앱 라이브러리 없이 Fraction으로 **576개 환산/오버플로 사례**를 확인하고, typed 금액 키를 독립적으로 식별해 **모든 JSON 필드**를 대조한다. 환산 전 전체 원본·현재 결과, raw XER incoming과 이전 ID/통화/일정 이력, 단계별 반올림·원가/개시 누계/마감·수량/날짜/작성 정보와16건 이력의 투영을 검사한다. [검산 결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/project-currency-2026-11-08/independent-oracle.json)와 [증거 폴더](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/project-currency-2026-11-08/project-currency-qa)를 남긴다.

대상 KRW의 XML/XER와 추가 전환 뒤 USD XML/XER를 실제 내보내고 다시 읽는다. 현재 통화·누계 AC 정밀도와 전환 건수를 검사한다. 실제 [XLSX](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/project-currency-2026-11-08/project-currency-qa/report.xlsx)·[PDF](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/project-currency-2026-11-08/project-currency-qa/report.pdf)에는 환산된 KRW 금액과 전환 이력/환율/기준일/작성 근거를 포함한다. 별도 판독 결과는 [산출물 검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/project-currency-2026-11-08/artifact-verification.json)에 남긴다.

[산출물 검산 스크립트](../../../scripts/verify-project-currency-artifacts.py)는 openpyxl3.1.5/pypdf6.10.0으로 실제 원가6행의 알려진 금액·명시0·미정, 자재 단가, 환산 이력의 전체 메타데이터를 대조한다. PDF22페이지에서 통화/환율·근거·원가와 페이지 구조를 확인했다. MPXJ16.10.0 strict reader는 KRW0자리/USD2자리 XML·XER4개를 오류 누락0건으로 읽었다. XML은 통화 정밀도/작업 이름을, XER는 추가로 계획 기간·자원 이름과 누계 AC6행씩을 대조했다. 이 판독을 실제 외부 제품 실행으로 확대하지 않는다.

## Windows와 회귀

실제 windows 플랫폼에서 금액 표·환율/작성 근거·재분석/확인 무효화·원본 조회·닫기 취소·제출·이력·읽기 전용 메뉴를 조작했다. [기본 창](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/project-currency-2026-11-08/project-currency-qa/preview.png), [800×600](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/project-currency-2026-11-08/project-currency-qa/preview-small.png), [전후 원본](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/project-currency-2026-11-08/project-currency-qa/originals.png), [이력 조회](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/project-currency-2026-11-08/project-currency-qa/history.png)와 [native 로그](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/project-currency-2026-11-08/native-ui.txt)를 남긴다. 작은 창의 본문 스크롤·금액/확인란과 고정 제출/닫기 footer를 확인했다.

전체 회귀는 이전 MPP·XER 통화/종류 전환·일정 채택·ID 이관·재연결/병합/외부 EVM·공개 표본, XML·원가/마감/보고·공수/평준화·저장/복구·서버/실시간 UI를 포함한다. 별도 MSIX8개·macOS10개·Apple 서명 준비3개·MPP payload7개를 실행한다. Windows에서 macOS 플랫폼 조건2개는 제외한다.

실제 P6/MS Project 재계산/재저장, 다른 OS·스토어 실행과 새 sanitizer/fuzzer 캠페인은 미실행이다. 파서 검사는 결정적 시드 재생이다.

## 재현

```powershell
$taskRoot = (Get-Location).Path
$evidence = Join-Path $taskRoot 'docs/validation/project-currency-2026-11-08'
$env:PATH = "$taskRoot/.deps/Qt/6.8.3/mingw_64/bin;$taskRoot/.deps/Qt/Tools/mingw1310_64/bin;$env:APPDATA/Python/Python314/Scripts;$env:PATH"
cmake --build build-governance --parallel 8
ctest --test-dir build-governance --output-on-failure --parallel 4 --output-junit "$evidence/ctest.xml" --output-log "$evidence/ctest.txt"
python scripts/verify-project-currency.py build-governance/project-currency-qa
.deps/report-verification/Scripts/python.exe scripts/verify-project-currency-artifacts.py "$evidence/project-currency-qa"
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-project-currency-artifacts.py "$evidence/project-currency-qa" --mpxj
python scripts/check-cpp-quality.py format --tool .deps/cpp-quality/Lib/site-packages/clang_format/data/bin/clang-format.exe
python tests/test_msix_packaging.py
python tests/test_mac_store_packaging.py
python tests/test_prepare_apple_signing.py
python tests/test_mpp_payload.py
Push-Location $evidence
& "$taskRoot/build-governance/jobs_project_currency_tests.exe" -platform windows
Pop-Location
```
