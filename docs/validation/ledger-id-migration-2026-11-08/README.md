# 관측 원장 ID 이관 검증 · 2026-11-08

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/ledger-id-migration-2026-11-08)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

Windows x64 · Qt6.8.3 · MinGW13.1 · Release/Ninja. 전체 **137/137 통과, 172.66초**, 파서 **478개 시드**. [기능 계약](../../ledger-id-migration.md). 형식 **384개 파일**, 별도 패키징21개와 Windows 기본/800×600 조작을 통과했다. 독립 oracle은16,239개 필드 검사를, MPXJ strict reader는 ignored error 없이6개 작업의 계획 기간·자원을 대사했다. 전체 회귀의 최종 수치와 지문은 [검증 요약](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/ledger-id-migration-2026-11-08/verification-summary.json), 전체 [CTest 로그](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/ledger-id-migration-2026-11-08/ctest.txt)·[JUnit](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/ledger-id-migration-2026-11-08/ctest.xml)·[개별 출력](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/ledger-id-migration-2026-11-08/all-test-output.txt), [전체 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/ledger-id-migration-2026-11-08/build.txt)·[최종 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/ledger-id-migration-2026-11-08/final-build.txt), [형식 검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/ledger-id-migration-2026-11-08/format.txt)에 남긴다.

## 구현과 보존

실제 XER 표본에서 Steps·기준선·저장 시나리오·잠긴 사용자 기간·외부 EV/PV·재무 병합/재연결을 준비하고 새 일정 채택으로 추가한 미관측 작업과 새 자원으로 이관한다. 기존 대상 정의는 유지하고 원래 중복 정의를 제거한다. 별도로 새 ID 부여, 연속 이관, 날짜별 잔여 목표·자재/비용/역할·네트워크 참조와 평준화 자동 해제를 검사한다.

원장의 기록 ID·값·날짜·작성/출처·정정 연결, 기준선 계산값과 마감 수치를 보존한다. 과거 일정 채택 전체와 재연결 raw incoming 원본은 변경하지 않는다. 현재 native 참조는 일관되게 바꾸고 최초 이관 이벤트는 이후 일반 편집·사본·재병합·두 번째 이관에서도 동일하게 보존한다.

이미 관측된 대상, 다른 자원 종류, 중복/연쇄/자기 연결, 동결 계획이 이미 사용하는 대상, 중복 예산/배정을 거절한다. 잘못된 확인/작성 정보·UTC, 구/미래 schema, 알 수 없는 필드/정책·중첩 이력·변조한 전후 문서/현재 루트도 거절한다. 이력16건은 지원하고17건째 거부에서 문서가 변하지 않는지 확인한다.

schema33 저장·재실행·재열기·복구·독립 사본, 준비 단계 불변·취소/stale/읽기 전용, 일반 편집/undo와 XER 재병합·내보내기를 검사한다.

## 독립 대조

[Python oracle](../../../scripts/verify-ledger-id-migration.py)은 앱 라이브러리를 사용하지 않는다. envelope SHA256과 원본 스냅샷을 확인하고 typed 참조를 식별해 **모든 JSON 필드**를 대조한다. 소유 ID를 제외한 네 원장 전체의 동일성, 미정/0·금액/공수 합계, 외부 원본 공간과 이전 이벤트의 불변성, 마지막 저장 루트와 검증 결과 및 평준화 해제 원장을 별도로 확인한다. 결과는 [독립 대조](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/ledger-id-migration-2026-11-08/independent-oracle.json)에 보존한다.

MPXJ16.10.0/JPype1.6.0 strict reader로 새 ID 내보내기의 작업·계획 기간과 자원을 확인한다. 이 판독은 실제 P6/MS Project의 재계산/재저장 동등성 검증과 구분한다.

## Windows 화면과 회귀

Qt windows 플랫폼에서 기본/800×600의 연결·분석·전후 조회·확인·제출, 입력 변경 시 확인 무효화, 닫기 취소와 이력 재조회를 조작한다. [native 실행](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/ledger-id-migration-2026-11-08/native-ui.txt), [기본 연결](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/ledger-id-migration-2026-11-08/ledger-id-qa/editor.png)·[작은 창](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/ledger-id-migration-2026-11-08/ledger-id-qa/editor-small.png), [미리보기](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/ledger-id-migration-2026-11-08/ledger-id-qa/preview.png)·[작은 미리보기](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/ledger-id-migration-2026-11-08/ledger-id-qa/preview-small.png), [원본](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/ledger-id-migration-2026-11-08/ledger-id-qa/originals.png)·[이력](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/ledger-id-migration-2026-11-08/ledger-id-qa/history.png)을 남긴다.

전체 회귀에는 이전 XER 통화/자원 전환·일정/WBS/달력 채택·원본 연결/병합/배분/외부 EVM·공개 표본, XML·원가/마감/보고, 공수·평준화·저장/복구·서버 HTTP/실시간 UI가 포함된다. 별도 패키징은 MSIX8개·macOS10개·Apple 서명 준비3개를 통과했고 macOS 플랫폼 조건2개는 Windows에서 건너뛰었다.

초기 확장 검사에서 날짜별 목표용 독립 fixture에 공수 프로필을 지정하지 않아 거절된 기록은 [focused-new.txt](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/ledger-id-migration-2026-11-08/focused-new.txt)에 남겼다. 해당 fixture를 지원 프로필·통화로 수정했다. 이어 하루 작업의 목표가 다음 날로 벗어난 [거부 기록](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/ledger-id-migration-2026-11-08/initial-dated-window.txt)을 확인하고 목표 날짜를 지원 실행 구간 안으로 바로잡았다. 최종 검증에 두 경계의 정상 자료를 포함했다.

실제 P6/MS Project 실행, MPP 판독, 다른 OS 실행과 새 sanitizer/fuzzer 캠페인은 미실행이다. 파서는 저장된 유효/손상 시드의 결정적 재생을 수행한다.

## 재현

```powershell
$taskRoot = (Get-Location).Path
$evidence = Join-Path $taskRoot 'docs/validation/ledger-id-migration-2026-11-08'
$env:PATH = "$taskRoot/.deps/Qt/6.8.3/mingw_64/bin;$taskRoot/.deps/Qt/Tools/mingw1310_64/bin;$env:APPDATA/Python/Python314/Scripts;$env:PATH"
cmake --build build-governance --parallel 8
ctest --test-dir build-governance --output-on-failure --parallel 4 --output-junit "$evidence/ctest.xml" --output-log "$evidence/ctest.txt"
python scripts/verify-ledger-id-migration.py build-governance/ledger-id-qa
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-ledger-id-migration.py build-governance/ledger-id-qa --mpxj
python scripts/check-cpp-quality.py format --tool .deps/cpp-quality/Lib/site-packages/clang_format/data/bin/clang-format.exe
python tests/test_msix_packaging.py
python tests/test_mac_store_packaging.py
python tests/test_prepare_apple_signing.py
```

native 화면은 증거 폴더를 작업 디렉터리로 `build-governance/jobs_ledger_identity_tests.exe -platform windows`를 실행한다. 이 폴더의 `ledger-id-qa`에 원본/저장 파일·내보내기와 화면이 생성된다.
