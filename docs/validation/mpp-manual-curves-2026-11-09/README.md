# MPP 수동 계획 곡선 검증

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/mpp-manual-curves-2026-11-09)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

2026-11-09 · Windows x64 / Qt6.8.3 / MinGW13.1 Release / MPXJ16.10.0 / Java21. [기능 계약](../../mpp-manual-curves.md)을 따른다.

## 최종 결과

- 전체 **145/145 통과 · 250.91초**: [CTest](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/mpp-manual-curves-2026-11-09/ctest.txt) · [JUnit](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/mpp-manual-curves-2026-11-09/ctest.xml) · [검사별 출력](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/mpp-manual-curves-2026-11-09/all-test-output.txt).
- 파서 **566개** 결정적 시드, C++ 형식 **417파일**, Java 투영 **16개 assertion** 통과.
- 독립 원본/자원분 대조 **9배정·10,080분**, [XML/XER strict reader](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/mpp-manual-curves-2026-11-09/independent-exports.json) 각9개 곡선 공수 일치·무시 오류0.
- 별도 패키징 **28개 통과/플랫폼 조건2개 제외**, protocol2 reader **216파일/대응 소스** 검증: [bundle 검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/mpp-manual-curves-2026-11-09/reader-verification.json).
- Windows 기본/800×600 UI 통과. [소스·증거 SHA256 요약](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/mpp-manual-curves-2026-11-09/verification-summary.json).

## 검증 범위와 근거

[집중 검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/mpp-manual-curves-2026-11-09/refined-focused.txt)는4/4 통과했다. 기존 MPP8/9/12/14의 원본 보존·손상/암호/timeout/cancel 검사, 원래 UI와 새 수동 곡선 UI, 새 실제 binary 곡선 계약을 함께 실행했다. [Windows UI](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/mpp-manual-curves-2026-11-09/native-ui.txt)는 실제 Windows plugin에서 파일 선택·명시 통화·미리보기/확인·취소·stale/session·권한 철회·읽기 전용 원본에서 새 사본 열기를 검사했다. [기본900×650](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/mpp-manual-curves-2026-11-09/mpp-qa/preview.png)·[800×600](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/mpp-manual-curves-2026-11-09/mpp-qa/preview-small.png)을 직접 확인했다. 요약 수치와 확인/제출 버튼이 보이며 상세 내용은 스크롤한다.

[공개 원본](../../../tests/fixtures/mpp-public/manifest.json)의 timephased-multi-rate.mpp는 실제 MPP14다. 정규 계획9배정/13구간을 채택하고 초과근무3배정은 제외한다. 수동2곡선의16h·0구간과 분할로 인한2개 기간 창 확장을 검사했다. 실제/실제 초과근무 표본에서는 원장·기준선·AC를 생성하지 않는다. 복잡한 MPP9/12/14timephasedsegments는 추가 근무일 달력을 지원하는 것으로 오인하지 않고 기존 제한에 따라 거절한다. 모든 기존 제작 버전의 수동 곡선 전체 복원이나 외부 제품 동등성을 주장하지 않는다.

[독립 스크립트](../../../scripts/verify-mpp-curves.py)는 원본을 직접 MPPReader로 읽고 bridge/MSPDIWriter를 거치지 않은 정규 원본 값과 native 구간을 대조했다. 별도 civil-minute 반복으로 작업·자원 달력 교집합과0구간을 계산해9배정/10,080개 자원분의 위치·배정률/총공수를 대조한다. native 논리 일정 날짜에 대한 투영 검산이며 source의 절대 시각 재계산 동등성 검사가 아니다. [결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/mpp-manual-curves-2026-11-09/independent-curves.json).

[Java 계약 검사](../../../tests/java/CurveProjectionContract.java)는 고정 입력16개 assertion으로 최대 나머지의 소수 동률·총공수·원본 row 소유,0/미정 구분, 실제/초과근무/없는 자료 제외, 양수 분 미만 구간 접힘·순서·음수·합계·1,024행 한도·올림 근거를 검사한다. [실행](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/mpp-manual-curves-2026-11-09/java-contract.txt).

## 구현 중 확인한 경계

초기 테스트의 GUI 링크를 불필요하게 요구하던 부분은 QCoreApplication으로 정리했다. 전체 timephasedsegments 표본에는 native에서 지원하지 않는 추가 근무일이 있어 성공 표본으로 축소 해석하지 않고 명시 거절 검사를 유지했다. MSPDI의 전일 휴일 종료23:59:59는 해당 휴일로 읽도록 보완했다.

실제 수동 곡선 표본에서 writer가0값을 생략하는 것을 확인해 MPP protocol2의 검증된 정규 시간대 행만 명시0으로 복원한다. 일반 XML의 누락 값 정책은 유지한다. 실적이 있는 미배정 작업에 writer가 임시 배정을 생성하는 경로를 억제했고, 소수 동률의 float 오차는 십진 정규화 뒤 배분하도록 수정했다. 원본 binary는 수정하지 않는다.

## 재현

```powershell
$taskRoot = (Get-Location).Path
$evidence = Join-Path $taskRoot 'docs/validation/mpp-manual-curves-2026-11-09'
$env:PATH = "$taskRoot/.deps/Qt/6.8.3/mingw_64/bin;$taskRoot/.deps/Qt/Tools/mingw1310_64/bin;$env:APPDATA/Python/Python314/Scripts;$env:PATH"
python scripts/prepare-mpp-reader.py --jdk C:/Users/dow/.jdks/jdk21 --libraries build-governance/mpp-reader/lib --sources build-governance/mpp-reader/sources --output build-governance/mpp-reader
cmake --build build-governance --parallel 12
ctest --test-dir build-governance --output-on-failure --parallel 4 --output-junit "$evidence/ctest.xml" --output-log "$evidence/ctest.txt"
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-mpp-curves.py "$evidence/mpp-curves-qa"
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-mpp-curve-exports.py "$evidence/mpp-curves-qa"
Push-Location $evidence
& "$taskRoot/build-governance/jobs_project_mpp_tests.exe" -platform windows --ui-only --curve-ui
Pop-Location
javac --release 21 -encoding UTF-8 -cp 'build-governance/mpp-reader/bridge.jar;build-governance/mpp-reader/lib/*' -d build-governance/mpp-curve-java-tests tests/java/CurveProjectionContract.java
java '-Dlog4j2.statusLoggerLevel=OFF' -cp 'build-governance/mpp-curve-java-tests;build-governance/mpp-reader/bridge.jar;build-governance/mpp-reader/lib/*' app.toolmoa.ppm.CurveProjectionContract
python scripts/check-cpp-quality.py format --tool .deps/cpp-quality/Lib/site-packages/clang_format/data/bin/clang-format.exe
python tests/test_msix_packaging.py
python tests/test_mac_store_packaging.py
python tests/test_prepare_apple_signing.py
python tests/test_mpp_payload.py
```

실제 MS Project/P6 실행·재저장, macOS/스토어 실행과 새 sanitizer/fuzzer 캠페인은 미실행이다. protocol2 bridge·manifest와 대응 source/license를 검증하며 MPXJ/JDK 의존성 lock은 바꾸지 않았다.
