# XER 통화·자원 종류 전환 검증 · 2026-11-08

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-source-conversion-2026-11-08)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

Windows x64 · Qt6.8.3 · MinGW13.1 · Release/Ninja. 전체 **135/135 통과, 160.15초**, 파서 **424개 시드**, clang-format **366개 파일** 통과. [기능 계약](../../xer-source-conversion.md) · [검증 요약과 소스 지문](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-source-conversion-2026-11-08/verification-summary.json) · [전체 로그](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-source-conversion-2026-11-08/ctest.txt) · [JUnit](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-source-conversion-2026-11-08/ctest.xml) · [개별 테스트 출력](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-source-conversion-2026-11-08/all-test-output.txt) · [최종 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-source-conversion-2026-11-08/final-build.txt).

## 기능과 보존

새/이전 XER 원본별 명시 주통화 환율, 인력·장비 종류별 공수 환산비, 기준일·근거를 입력한다. 금액은 통화 정밀도별 half-up, 공수는 정확한 1/100 근무분 정수다. 동시에 종류를 교환·통합하고 미보고/0을 구분한다. 대사 위반·공수 소수·한도 초과·잘못된 규칙을 거절한다. 새 원본의 종류가 다르면 자동 연결하지 않고 사용자가 대상을 지정한다.

schema31 원본/규칙·연결표/병합 선택의 재실행, 당시 원장/누계와의 대사, 최초·후속 원본과 현재 계획·원장·기준선의 보존을 검사했다. 저장/재열기·독립 사본·새 문서 ID를 만드는 복구, 후속 병합과 재내보내기, worker 취소/stale/읽기 전용 거절을 통과했다. 현재 관측 원장의 종류나 단위를 재분류하는 작업은 이 전환에 포함하지 않는다. 해당 원장과 현재 종류가 다른 경우 기존 병합 보호 규칙을 유지한다.

## 독립 수치와 교환 검사

[독립 검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-source-conversion-2026-11-08/independent-oracle.json)는 Python Fraction으로 네 통화의 **760개** 반올림·큰 정수·넘침 사례를 확인하고, 전체 **844개** 수치/원본·본문·출력 검사를 통과했다. 합성 원본의 양방향 통화/공수·종류 교환, 모든 기간 지표, 현재 native 입력·원장 보존과 XER TSV를 검사한다. MPXJ16.10.0/JPype1.6.0 strict reader는 재내보낸 XER의 공수3.75h·자원 원가110.46·작업 AC122.80을 확인했다. Java Double 판독값은 1e-8 이내의 표현 오차만 허용하고 통화 최소 단위로 비교하며, native 정수 oracle에는 허용 오차를 사용하지 않는다.

[증거 폴더](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-source-conversion-2026-11-08/xer-conversion-qa)에 수치 사례, 최초/합성 원본, native 전후 JSON, schema31 파일, 손상12종과 실제 XER를 보존했다. [검증 스크립트](../../../scripts/verify-xer-source-conversion.py)는 별도의 설치 없이 표준 Python 수치 검사를 실행하며 MPXJ는 선택 플래그로 실행한다.

## Windows 화면

Qt windows 플랫폼에서 실제 파일을 비동기로 읽어 EUR→USD와 인력/장비 전환을 입력했다. [기본 창](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-source-conversion-2026-11-08/xer-conversion-qa/editor.png), [800×600](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-source-conversion-2026-11-08/xer-conversion-qa/editor-small.png), [전후 값](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-source-conversion-2026-11-08/xer-conversion-qa/preview.png), [병합 검토](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-source-conversion-2026-11-08/xer-conversion-qa/merge.png), [저장된 전환 이력](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-source-conversion-2026-11-08/xer-conversion-qa/history.png) 및 [native 실행 로그](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-source-conversion-2026-11-08/native-ui.txt)를 보존한다. 입력 변경 시 확인 해제, 미리보기·닫기 취소의 무변경, 최종 분석/제출과 저장 후 원본/전환값 재조회가 통과했다. 작은 창의 본문 스크롤과 고정된 제출/닫기 footer, 한국어와 통화/공수비를 확인했다.

## 기타 회귀와 개발 중 수정

전체 CTest에는 기존 XER/XML·원가·native 기간 EVM·외부 EV/PV 채택·사용자 기간 마감·공수/평준화·보고/저장/복구·서버 HTTP/실시간 UI가 포함된다. 추가 [패키징 검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-source-conversion-2026-11-08/packaging-tests.json)는 Windows MSIX8개, macOS 패키징10개, Apple 서명 준비3개가 통과했다. macOS 플랫폼 조건 검사2개는 Windows에서 건너뛰었다. [형식 검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-source-conversion-2026-11-08/format.json)도 통과했다.

개발 중 집중 실행 로그도 보존한다. schema31 지원에 맞춰 미래 버전 거절 기대값을32로 바꾸고, 종류 불일치 시 추정하지 않는 연결표, dirty 문서만 허용하는 복구·새 문서 계보를 테스트에 반영했다. [초기](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-source-conversion-2026-11-08/initial-focused.txt), [두 번째](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-source-conversion-2026-11-08/second-focused.txt), [세 번째](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-source-conversion-2026-11-08/third-focused.txt), [네 번째](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-source-conversion-2026-11-08/fourth-focused.txt) 기록 이후 [집중 검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-source-conversion-2026-11-08/focused.txt)와 최종 전체135개가 통과했다. 이러한 거절을 완화하지 않았다.

실제 P6 재계산/재저장, 다른 OS 실행, 새 sanitizer/fuzzer 캠페인은 이번 검증에 포함하지 않는다.

## 재현

```powershell
$taskRoot = (Get-Location).Path
$evidence = Join-Path $taskRoot 'docs/validation/xer-source-conversion-2026-11-08'
$env:PATH = "$taskRoot/.deps/Qt/6.8.3/mingw_64/bin;$taskRoot/.deps/Qt/Tools/mingw1310_64/bin;$env:APPDATA/Python/Python314/Scripts;$env:PATH"
cmake --build build-governance --parallel 8
ctest --test-dir build-governance --output-on-failure --parallel 4 --output-junit "$evidence/ctest.xml" --output-log "$evidence/ctest.txt"
python scripts/verify-xer-source-conversion.py build-governance/xer-conversion-qa
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xer-source-conversion.py build-governance/xer-conversion-qa --mpxj
python scripts/check-cpp-quality.py format --tool .deps/cpp-quality/Lib/site-packages/clang_format/data/bin/clang-format.exe
python tests/test_msix_packaging.py
python tests/test_mac_store_packaging.py
python tests/test_prepare_apple_signing.py
```

native 화면 재현은 증거용 별도 작업 폴더에서 `build-governance/jobs_xer_source_conversion_tests.exe -platform windows`를 실행한다. 테스트는 해당 작업 폴더의 `xer-conversion-qa`에 결과를 쓴다.
