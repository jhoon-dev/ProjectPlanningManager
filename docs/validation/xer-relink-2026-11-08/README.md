# 새 XER 버전·삭제 ID 재연결 검증

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-relink-2026-11-08)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

2026-11-08 Windows x64, Qt6.8.3/MinGW13.1, Release/Ninja의 미커밋 작업 폴더에서 [XRL-01~06](../../xer-source-relink.md)을 검증했다. 최종 소스 지문·바이너리/증거 해시는 [verification-summary.json](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-relink-2026-11-08/verification-summary.json)에 기록한다.

최종 전체 회귀 **128/128(161.21초)**, XER/기간 실적/파서/공개 표본 집중 **15/15(20.79초)**, 파서 **326 seeds(schema1~28)**, C++ 형식 검사 **327파일**을 통과했다. 최종 빌드는 경고/오류 없이 완료했다. [전체 CTest](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-relink-2026-11-08/ctest.txt) · [JUnit](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-relink-2026-11-08/ctest.xml) · [모든 테스트 출력](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-relink-2026-11-08/all-test-output.txt) · [집중 검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-relink-2026-11-08/focused.txt) · [초기 집중 검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-relink-2026-11-08/early-focused.txt) · [전체 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-relink-2026-11-08/build.txt) · [최종 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-relink-2026-11-08/final-build.txt) · [형식 검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-relink-2026-11-08/format.json)를 보존한다.

## 새 원본과 삭제 ID 연결

[최초 XER](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-relink-2026-11-08/source.xer)의 작업/자원 외부 ID를 모두1000만큼 바꾸고 일부 이름도 바꾼 [후속 XER](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-relink-2026-11-08/version2.xer)를 읽는다. 현재 문서에서 관측값 없는 C SS 작업을 삭제/재생성해 새 UUID로 바꾼 뒤 이전 집계 ID와 새 원본 ID를 모두 현재 대상에 연결한다. 서로 다른 프로젝트 ID는 사용자의 후속 관계 확인이 필요하다. 정확한 이름이 중복되면 후보를 임의 선택하지 않는다.

누락/중복/종류/통화/상태일·명시0의 제외 거절, 관측 없는 항목 제외, 재무 관측/상태일 없는 계획 사전, 빈값 유지/미보고 정책과 다중 프로젝트 선택을 검사했다. 연결표 확인은 변경 시 해제되며, 미해결 기간/초과근무 누계는 기존 병합 검토로 해결해야 제출할 수 있다. 이후 일반 원장 병합·저장/재열기·복구·독립 사본·undo와 취소/session/stale/읽기 전용을 검사했다. schema28 정상 파일과 과거 envelope·미래29/연결표 변조 거절을 포함한다.

## 원본·현재 원장 보존과 독립 판독

[연결 전 본문](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-relink-2026-11-08/before.json)과 [연결 후 본문](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-relink-2026-11-08/relinked.json)을 독립 비교했다. 문서 ID/독립 사본 출처·revision·새 재무 이력을 제외한 전체 native 입력이 같으며 최초 원본 스냅샷도 같다. 현재 작업 이름·WBS/달력/관계/배정·기준선·진척·원가/기간 실제 기록과 작성자/ID를 다시 쓰지 않는다. 현재 AC12280센트, 날짜 배분3334센트와 공수7500u를 유지한다. 새 원본의 기간 관측값을 native 원장에 자동 제출하지 않는다.

[독립 검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-relink-2026-11-08/independent-relink.json)는 별도 canonical native envelope/투영 SHA256와 두 원본 파일 지문, 원시 TSV ID 집합·완전한 일대일 연결표/현재 대상 종류, 삭제 ID의 새 UUID 연결 및 후속 병합의 원장 참조 시점을 대조한다. [schema28 문서](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-relink-2026-11-08/relinked.ppm)는 재연결1건과 후속 일반 병합1건을 담았다. 파일에는 typed 재무/ID 사전과 SHA256를 보존하며 원본 XER 전체 바이트는 별도 파일로 보관한다.

[재내보낸 XER](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-relink-2026-11-08/export.xer)는 배정 실제공수3.75h, 자원 누계110.46 USD와 작업 AC122.80 USD를 유지한다. [변환 보고](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-relink-2026-11-08/export-report.json)는 `sourceRelinkCount=1`과 재내보내기 적용을 기록한다. TSV/Decimal 및 고정 MPXJ16.10.0/JPype1.6.0 strict reader로 다시 읽었고 무시된 오류가 없다. 재생성 작업이 WBS 밖에 배치되어 reader의 작업 순서가 바뀌므로 대조할 작업은 고유 이름으로 찾는다. 초기 verifier의 첫 행 가정을 이 방식으로 수정한 후 모든 검사를 다시 실행했다.

[기존 병합](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-relink-2026-11-08/independent-merge.json) · [날짜 배분](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-relink-2026-11-08/independent-allocation.json) · [보존 스냅샷](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-relink-2026-11-08/independent-snapshot.json) · [실적·원가](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-relink-2026-11-08/independent-actual-costs.json) · [변조하지 않은 공개7개 표본](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-relink-2026-11-08/independent-public.json)도 독립 재검사했다. MPXJ 부동소수 금액은 센트 반올림 전 오차1e-8 이내를 확인하며 TSV/native 정수는 정확히 비교한다.

## Windows 화면과 실행 이슈

[native UI 로그](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-relink-2026-11-08/native-ui.txt), [1100×700 연결표](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-relink-2026-11-08/mapping.png), [800×600 연결표](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-relink-2026-11-08/mapping-small.png), [재무 병합 검토](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-relink-2026-11-08/review.png)를 보존했다. 최종 테스트 바이너리를 복사해 Windows 플랫폼으로 실행했고 원본 바이너리와 SHA256가 같다. 실제 비동기 파일 읽기·프로젝트 선택·취소, 확인/연결 변경, 검증·제출·메뉴 연결을 검사했다. 가상 표·공유 선택 목록·검색, 스크롤과 고정 footer를 확인했다.

초기에는 같은 Ninja 폴더의 두 빌드가 겹쳐 재생성 권한 오류가 발생했다. [실패 로그](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xer-relink-2026-11-08/initial-concurrent-build-failure.txt)를 남겼으며 이후 빌드를 직렬화했다. 최종 전체 빌드·집중 검사·전체 회귀·native UI는 모두 같은 최종 소스로 통과했다.

## 재현

저장소 루트 PowerShell에서 Qt/MinGW와 CMake/CTest를 PATH에 추가한다. 같은 빌드 폴더에서 빌드를 겹쳐 실행하지 않는다. 공개 reader 출력 폴더는 새 경로를 사용한다.

```powershell
$taskRoot=(Get-Location).Path
$env:PATH="$taskRoot/.deps/Qt/6.8.3/mingw_64/bin;$taskRoot/.deps/Qt/Tools/mingw1310_64/bin;$env:APPDATA/Python/Python314/Scripts;$env:PATH"
cmake --build build-governance --parallel 8
ctest --test-dir build-governance --output-on-failure --parallel 4
ctest --test-dir build-governance -R 'xer_|primavera_xer|parser_seed_replay|period_actual|primavera_public_corpus' --output-on-failure --parallel 2
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xer-relink.py build-governance
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xer-merge.py build-governance
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xer-allocation.py build-governance
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xer-financials.py build-governance
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xer-actual-costs.py build-governance
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-p6-corpus.py --build-dir build-governance --corpus tests/fixtures/p6-public --output build-governance/xrl-public-readers-new
python scripts/check-cpp-quality.py format --tool .deps/cpp-quality/Lib/site-packages/clang_format/data/bin/clang-format.exe
```

새 버전 일정/WBS/달력의 자동 채택, 관측 원장 ID 이관·통화/단위 전환, native 기간 EVM·MPP reader 및 실제 Oracle P6 재계산/재저장은 후속 범위다. 실제 P6 실행은 미실행이며 이번 개발 빌드로 PC 설치본을 교체하지 않았다.
