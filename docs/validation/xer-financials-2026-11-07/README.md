# XER 재무기간·누계 스냅샷 검증

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-financials-2026-11-07)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

2026-11-07 Windows x64, Qt6.8.3/MinGW13.1, Release/Ninja에서 현재 미커밋 작업 폴더를 빌드했다. [XFS-01~06 계약](../../xer-financial-snapshots.md)을 검증하며, 실행 소스 지문·바이너리 해시·실제 결과는 [verification-summary.json](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-financials-2026-11-07/verification-summary.json)에 기록한다.

## 판정

최종 전체 회귀 **125/125(153.51초)**, XER/파서/공개 표본 집중 **10/10(17.99초)**, 독립 판독과 Windows UI를 통과했다. [CTest 원문](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-financials-2026-11-07/ctest.txt), [JUnit](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-financials-2026-11-07/ctest.xml), [집중 검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-financials-2026-11-07/focused.txt), [모든 테스트 출력](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-financials-2026-11-07/all-test-output.txt)에 실행 기록을 남긴다. 파서 seed replay는 284개, C++ 형식 검사는 305개 파일이다.

첫 전체 회귀는 124/125였다. 공개 resource-rates 표본의 기존 중복 배정 오류보다 새 스냅샷 검증이 먼저 실행되어 진단 코드가 바뀌었다. native 문서를 먼저 검증하도록 순서를 수정했고 표본/기대 코드는 바꾸지 않았다. [첫 실패 기록](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-financials-2026-11-07/first-regression.txt)과 수정 후 전체 회귀를 함께 보존한다. [전체 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-financials-2026-11-07/build.txt), [호환 수정 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-financials-2026-11-07/compat-build.txt), [형식 검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-financials-2026-11-07/format.json)를 확인할 수 있다.

두 번째 전체 실행에서는 기존 서버 long-poll 검사가 3초 대기 중 변경 준비까지 수행해 준비 완료 전에 poll이 만료됐다. 테스트의 preview 준비를 poll 시작 전으로 이동해 커밋 알림 자체를 검사하도록 고쳤다. 제품의 대기 시간이나 서버 동작은 바꾸지 않았다. [두 번째 실행](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-financials-2026-11-07/second-regression.txt)과 [부하 중 단독 재검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-financials-2026-11-07/long-poll-focused.txt)를 보존한다.

## 수치와 독립 판독

[원본 XER](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-financials-2026-11-07/source.xer)의 누계 공수3.75h는 정규3.25h+초과근무0.50h다. 재무기간 명세3.25h, 기간 미지정0.50h와 여러 날짜의 첫 구간2026-10-19~26을 native schema25에 보존한다. 첫 구간2h를 날짜별로 추정하지 않아 native 날짜별 원장은1.25h/33.34 USD다. 누계 AC122.80 USD에 보존 금액을 중복 합산하지 않는다.

[재내보낸 XER](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-financials-2026-11-07/export.xer)는 원래 구간, 정규/초과근무 공수와 원가, 외부 BCWP/BCWS를 보존한다. 자원 누계110.46 USD+직접비12.34 USD=작업 AC122.80 USD이며 native EVM은 외부 EV9999/PV8888을 사용하지 않는다. [두 프로젝트 결합](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-financials-2026-11-07/combined.xer)의 기간/배정 참조도 독립 검사한다.

[독립 검사 결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-financials-2026-11-07/independent-financials.json)는 별도 TSV/Decimal, native 본문과 투영 SHA256, MPXJ16.10.0/JPype1.6.0 strict reader를 사용한다. [기존 실적·원가 reader](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-financials-2026-11-07/independent-actual-costs.json)와 [변조하지 않은 공개7개 표본](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-financials-2026-11-07/independent-public.json)도 다시 확인한다. MPXJ의 이진 부동소수점 합계 오차에만 1e-8 이내인지 검사한 뒤 통화 최소단위로 반올림하며 원시 TSV/native 정수 검사는 정확히 비교한다.

## 문서 수명과 화면

schema25 저장/재열기·복구·독립 사본·undo/redo, 변경된 문서의 원본 보존과 재내보내기 제외, 삭제된 작업/자원의 과거 이름 조회, 잘못된 참조·겹친 기간·소수/음수/초과 금액·지문·구 버전 envelope·미래 schema26 거절을 자동 검사했다. [native 문서](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-financials-2026-11-07/project.ppm), [스냅샷 JSON](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-financials-2026-11-07/snapshot.json), [변경 문서 판정](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-financials-2026-11-07/stale-report.json)을 남긴다.

최종 실행 파일의 Windows 플랫폼에서 기본1000×620과800×600, 검색/가로 스크롤·기간 탭·외부 EV/PV·stale 표시와 MainWindow 메뉴 실행을 확인했다. [native UI 로그](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-financials-2026-11-07/native-ui.txt), [기본 화면](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-financials-2026-11-07/view.png), [작은 화면](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-financials-2026-11-07/view-small.png), [기간 표](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-financials-2026-11-07/periods.png), [외부 EV/PV](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-financials-2026-11-07/external-ev.png)를 참고한다.

## 재현

저장소 루트 PowerShell에서 Qt/MinGW bin을 PATH 앞에 추가한다. 독립 reader 환경은 `.deps/xer-reader-verification`에 설치한 고정 버전을 사용한다. 공개 reader의 출력 디렉터리는 매 실행 새 경로여야 한다.

```powershell
$taskRoot=(Get-Location).Path
$env:PATH="$taskRoot/.deps/Qt/6.8.3/mingw_64/bin;$taskRoot/.deps/Qt/Tools/mingw1310_64/bin;$env:PATH"
cmake --build build-governance --parallel 4
ctest --test-dir build-governance --output-on-failure --parallel 4
ctest --test-dir build-governance -R 'xer_|primavera_xer|parser_seed_replay|primavera_public_corpus' --output-on-failure --parallel 2
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xer-financials.py build-governance
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xer-actual-costs.py build-governance
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-p6-corpus.py --build-dir build-governance --corpus tests/fixtures/p6-public --output build-governance/financials-public-readers-new
python scripts/check-cpp-quality.py format --tool .deps/cpp-quality/Lib/site-packages/clang_format/data/bin/clang-format.exe
```

실제 Oracle P6 재계산/재저장과 MPP reader는 실행하지 않았다. 집계 편집/날짜 배분·현재 원장과 자동 병합·native 기간 EVM은 후속 범위다. 개발 빌드 결과이며 PC 설치본은 이 증분에서 교체하지 않았다.
