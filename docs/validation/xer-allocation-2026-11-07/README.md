# XER 집계 편집·날짜별 배분 검증

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-allocation-2026-11-07)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

2026-11-07 Windows x64, Qt6.8.3/MinGW13.1, Release/Ninja에서 미커밋 작업 폴더를 빌드하고 [XAE-01~06](../../xer-aggregate-editing.md)을 검증한다. 최종 실행 수치·소스 지문·바이너리/증거 해시는 [verification-summary.json](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-allocation-2026-11-07/verification-summary.json)에 남긴다.

## 자동 검사

최종 전체 회귀 **126/126(140.54초)**, XER/기간 실적/파서/공개 표본 집중 **13/13(14.73초)**, 파서 **298 seeds**와 C++ 형식 검사 **313파일**을 통과했다. 최종 빌드는 경고/오류 없이 완료했고 Windows 기본/800×600 및 독립 reader 결과를 함께 보존한다.

전체 회귀 [CTest 원문](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-allocation-2026-11-07/ctest.txt) · [JUnit](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-allocation-2026-11-07/ctest.xml) · [전체 테스트 출력](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-allocation-2026-11-07/all-test-output.txt), XER/기간 실적/파서/공개 표본 [집중 검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-allocation-2026-11-07/focused.txt), [초기 집중 검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-allocation-2026-11-07/early-focused.txt), [전체 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-allocation-2026-11-07/build.txt) · [최종 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-allocation-2026-11-07/final-build.txt), [C++ 형식 검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-allocation-2026-11-07/format.json)를 보존한다.

새 `xer_financial_edit_allocation_contract`는 작업/배정/자원 기간/작업 요약의 정정, 미지정 잔량·직접비 잔량의 새 기간, 정수 비중·나머지·최대 금액·null/0·휴일을 확인한다. 원본 불변, 반복 제출의 대체, 날짜 이동/철회, AC-only, 다른 경로의 원장 키 충돌, 원자적 실패·취소/stale/읽기 전용, 계획/기준선/EVM 분리, schema26 저장/복구/독립 사본과 구25·미래27 거절도 검사한다. 파서 replay는 schema1~26의 정상·손상 입력을 실제 decoder/store 경로에 통과시킨다.

## 독립 기대값

[원본](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-allocation-2026-11-07/source.xer)의 22500u·누계 AC12280센트는 보존된다. [정정 문서](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-allocation-2026-11-07/edited.ppm)는 작업 AC14001센트, 배정 정규22501u+초과3000u=25501u·자원 누계12047센트와 새 기간을 가진다. 첫 자원 기간12001u·7013센트를 19일 비중1/20일 비중2로 나누면 4000/8001u와2338/4675센트다. 같은 집계를 반복 제출해도 합산하지 않고, 세 번째 제출에서는 해당 기간을21일로 옮겨19/20일의 자원 원가·공수를 미보고 정정으로 철회한다.

현재 native 전기간 배분은12581센트, 누계 AC14001센트, 미배분1420센트다. 작업 경비와 자원 원가의 날짜 명세를 누계에 다시 더하지 않는다. 원래 Progress/기준선·실제 날짜·일정은 유지하고 외부 EV/PV는 내부 EVM에 사용하지 않는다. [정정 native 본문](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-allocation-2026-11-07/edited-body.json) · [원본 본문](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-allocation-2026-11-07/source-body.json)에서 원래 스냅샷과 이전 기록의 보존을 직접 확인할 수 있다.

[독립 배분 검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-allocation-2026-11-07/independent-allocation.json)는 별도 Python Decimal largest-remainder oracle로 각 정정의 비중과 실제 생성 원장 ID를 대조한다. native envelope·투영 SHA256와 원본 bytes SHA256도 재계산한다. [재내보낸 XER](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-allocation-2026-11-07/export.xer)의 자원 AC120.47 USD+직접/미배분19.54 USD=작업 AC140.01 USD, 수정한 누계 공수와 기간을 TSV/Decimal 및 MPXJ16.10.0/JPype1.6.0 strict reader로 판독한다. [변환 보고](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-allocation-2026-11-07/export-report.json)의 정정3건·재내보내기 판정도 확인한다.

XER의 시간 소수6자리 표현 때문에22501u가3.750167h로 쓰여0.002u 차이가 생긴다. 독립 reader에서는 기존 교환 계약의 반최소 표현 단위(0.003u) 이내인지 검사하고 native 원장은 정확한 정수로 대조한다. 원가 TSV/native는 정확하게 비교하며 MPXJ의 이진 합계만1e-8 이내 오차를 확인하고 센트로 반올림한다.

[기존 스냅샷](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-allocation-2026-11-07/independent-snapshot.json), [기존 실적·원가](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-allocation-2026-11-07/independent-actual-costs.json), [변조하지 않은 공개 P6 7개 표본](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-allocation-2026-11-07/independent-public.json)의 독립 판독도 다시 실행한다.

## Windows 화면

[native UI 로그](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-allocation-2026-11-07/native-ui.txt), [기본 편집기](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-allocation-2026-11-07/editor.png), [800×600 편집기](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-allocation-2026-11-07/editor-small.png), [배분 미리보기](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-allocation-2026-11-07/preview.png), [최신 정정 조회](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-allocation-2026-11-07/corrected-view.png), [가져온 원본 조회](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-allocation-2026-11-07/original-view.png)를 보존한다. 표의 집계 입력·잘못된 정밀도 거절·배정/직접비 잔량 추가·날짜 비중, 미리보기 이후 초안 변경 시 제출 차단·닫기 취소·실제 제출·MainWindow 메뉴 연결을 자동 조작했다. 작은 창의 스크롤과 고정된 제출/취소/닫기 footer, 한국어/통화/상태일·읽기 전용 표를 Windows 플랫폼에서 확인한다.

## 재현

저장소 루트 PowerShell에서 Qt/MinGW bin을 PATH 앞에 추가한다. 독립 reader 출력 디렉터리는 새 경로여야 한다.

```powershell
$taskRoot=(Get-Location).Path
$env:PATH="$taskRoot/.deps/Qt/6.8.3/mingw_64/bin;$taskRoot/.deps/Qt/Tools/mingw1310_64/bin;$env:PATH"
cmake --build build-governance --parallel 4
ctest --test-dir build-governance --output-on-failure --parallel 4
ctest --test-dir build-governance -R 'xer_|primavera_xer|parser_seed_replay|period_actual|primavera_public_corpus' --output-on-failure --parallel 2
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xer-allocation.py build-governance
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xer-financials.py build-governance
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xer-actual-costs.py build-governance
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-p6-corpus.py --build-dir build-governance --corpus tests/fixtures/p6-public --output build-governance/xfa-public-readers-new
python scripts/check-cpp-quality.py format --tool .deps/cpp-quality/Lib/site-packages/clang_format/data/bin/clang-format.exe
```

실제 Oracle P6 재계산/재저장·MPP reader·원본과 다른 원장의 자동 병합은 실행 범위 밖이다. XER에는 최신 정정의 집계 기간을 쓰고 명시 날짜 배분과 모든 이력은 source.ppm에 보존한다. 이번 작업은 개발 빌드이며 PC 설치본을 교체하지 않는다.
