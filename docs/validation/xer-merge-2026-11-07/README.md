# XER 원본·현재 원장 병합 검증

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-merge-2026-11-07)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

2026-11-07 Windows x64, Qt6.8.3/MinGW13.1, Release/Ninja의 미커밋 작업 폴더에서 [XFM-01~06](../../xer-ledger-merge.md)을 검증했다. 최종 소스 지문·바이너리/증거 해시는 [verification-summary.json](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-merge-2026-11-07/verification-summary.json)에 남긴다.

최종 전체 회귀 **127/127(134.30초)**, XER/기간 실적/파서/공개 표본 집중 **14/14(15.80초)**, 파서 **312 seeds(schema1~27)**, C++ 형식 검사 **319파일**을 통과했다. 최종 빌드는 경고/오류 없이 완료했다. [전체 CTest](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-merge-2026-11-07/ctest.txt) · [JUnit](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-merge-2026-11-07/ctest.xml) · [모든 테스트 출력](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-merge-2026-11-07/all-test-output.txt) · [집중 검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-merge-2026-11-07/focused.txt) · [초기 집중 검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-merge-2026-11-07/early-focused.txt) · [전체 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-merge-2026-11-07/build.txt) · [최종 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-merge-2026-11-07/final-build.txt) · [형식 검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-merge-2026-11-07/format.json)를 보존한다.

## 원장 불변과 독립 대사

[원본 XER](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-merge-2026-11-07/source.xer)의 배정 누계22500u·11046센트, 작업 AC12280센트를 유지한다. 현재 원장에 별도 날짜 자원1800u/500센트와 직접비700센트를 입력하고 이름·상태일을 바꾼 [병합 전 본문](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-merge-2026-11-07/before.json)과 [병합 후 본문](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-merge-2026-11-07/merged.json)을 비교한다. revision/독립 사본 ID·병합 이력을 제외한 입력과 원장 전체가 같다. CostEntry/기간 실적/진척·기준선의 값·작성자·기록 ID를 새로 쓰지 않는다. 원본 스냅샷도 같다.

native 날짜 원가는4534센트, 누계 AC12280센트, 미배분7746센트다. native 날짜 공수9300u는 그대로 유지한다. 병합 자원 기간은21300u·10546센트, 경비 명세1234센트이며 배정 누계11046센트 안의 미배분 자원 원가500센트/공수1200u가 남는다. 추가 날짜를 원본 누계에 다시 더하지 않는다. 반복 병합2건도 native 기록 수와 금액을 늘리지 않는다.

[독립 검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-merge-2026-11-07/independent-merge.json)는 별도 native 전체 입력/원장 동일성·envelope/투영/원본 SHA256, 상태일 및 제출 revision 이전의 최신 참조 oracle를 사용한다. 각 병합의 소유 ID가 당시 최신 날짜 기록과 일치하며 그 공수/원가가 단일 날짜 배분값과 같은지 확인한다. 실제 누계 AC는 원장 보고로 대조한다. [schema27 문서](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-merge-2026-11-07/merged.ppm)에는 원본·두 병합과 근거를 담았다.

## 충돌·외부 교환

집계 기간 안의 새 날짜 기록은 자동 합산하지 않고 미해결 충돌로 표시한다. 명시 기간 교체·현재 AC가 낮아진 경우의 누계 선택·정규/초과근무 누계 부족·새 작업/명시0, 기존 배분 정정/철회, 원장 누락/한도·삭제/배정/단위/통화/상태일 오류를 검사했다. 병합 후 기존 집계 편집도 실행했다. 취소·권한 회수·같은 ID/revision의 새 session·stale/읽기 전용, 저장/복구/독립 사본과 과거 병합의 참조/AC 변조·불완전 연결 거절을 포함한다.

[재내보낸 XER](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-merge-2026-11-07/export.xer)는 배정 공수3.75h, 자원 누계110.46 USD+직접비12.34 USD=작업 AC122.80 USD를 유지한다. [변환 보고](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-merge-2026-11-07/export-report.json)의 병합2건·재내보내기 판정도 확인했다. 별도 [경비 충돌 정정 본문](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-merge-2026-11-07/expense-merged.json)과 [XER](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-merge-2026-11-07/expense-export.xer)는 외부 EV 구간19~26일을 유지하고 그 안의20일 경비200센트를 독립 차원으로 연결한다. 같은 작업/재무기간 TASKFIN 키는 한 행으로 합치며 중복 키가 없다.

TSV/Decimal와 고정 MPXJ16.10.0/JPype1.6.0 strict reader로 일반/경비 XER 모두를 다시 읽었고 무시된 오류가 없다. [기존 배분](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-merge-2026-11-07/independent-allocation.json) · [보존 스냅샷](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-merge-2026-11-07/independent-snapshot.json) · [실적·원가](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-merge-2026-11-07/independent-actual-costs.json) · [변조하지 않은 공개7개 표본](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-merge-2026-11-07/independent-public.json)도 재검사했다. MPXJ 이진 합계에만1e-8 이내인지 검사한 뒤 센트로 반올림하며 원시 TSV/native 정수는 정확히 비교한다.

## Windows 화면

[native UI 로그](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-merge-2026-11-07/native-ui.txt), [기본 병합 대사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-merge-2026-11-07/merge.png), [800×600](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-merge-2026-11-07/merge-small.png), [원본/현재 값의 충돌 비교](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-merge-2026-11-07/conflict.png), [명시 선택 후 대사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-merge-2026-11-07/resolved.png), [연결할 원장](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-merge-2026-11-07/ledger.png)을 보존했다. worker 미리보기 전후 원장 불변, 초안 변경 시 제출 차단, 닫기 취소·실제 제출·MainWindow 메뉴 연결을 자동 조작했다. 가로/세로 스크롤과 고정된 제출/취소/닫기 footer, 통화·상태일·한글/원가 표시를 Windows 플랫폼에서 확인했다.

## 재현

저장소 루트 PowerShell에서 Qt/MinGW bin을 PATH 앞에 추가한다. 공개 reader 출력 디렉터리는 새 경로를 사용한다.

```powershell
$taskRoot=(Get-Location).Path
$env:PATH="$taskRoot/.deps/Qt/6.8.3/mingw_64/bin;$taskRoot/.deps/Qt/Tools/mingw1310_64/bin;$env:PATH"
cmake --build build-governance --parallel 8
ctest --test-dir build-governance --output-on-failure --parallel 4
ctest --test-dir build-governance -R 'xer_|primavera_xer|parser_seed_replay|period_actual|primavera_public_corpus' --output-on-failure --parallel 2
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xer-merge.py build-governance
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xer-allocation.py build-governance
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xer-financials.py build-governance
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xer-actual-costs.py build-governance
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-p6-corpus.py --build-dir build-governance --corpus tests/fixtures/p6-public --output build-governance/xfm-public-readers-new
python scripts/check-cpp-quality.py format --tool .deps/cpp-quality/Lib/site-packages/clang_format/data/bin/clang-format.exe
```

새 외부 원본 버전의 이름 기반 ID 재연결, 통화/인력-장비 단위 전환, 관측하지 않은 날짜 생성, 내부 기간 EVM·MPP reader·실제 Oracle P6 재계산/재저장은 이번 완료 범위가 아니다. 이번은 개발 빌드 검증이며 PC 설치본은 교체하지 않았다.
