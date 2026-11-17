# native 기간 EVM 검증

2026-11-08 Windows x64, Qt6.8.3/MinGW13.1, Release/Ninja의 미커밋 작업 폴더에서 [NEV-01~06](../../native-period-evm.md)을 검증했다. 소스·테스트·바이너리 및 증거 지문은 [verification-summary.json](verification-summary.json), 전체 계산 소스 목록은 [source-manifest.txt](source-manifest.txt)에 남긴다.

최종 전체 회귀 **130/130(167.66초)**, 원가/보고·기간 실적·XER/파서 집중 **21/21(26.85초)**, 파서 **326 seeds(schema1~28)**, C++ 형식 **336파일**을 통과했다. 전체·최종 빌드는 경고/오류 없이 완료했다. [전체 CTest](ctest.txt) · [JUnit](ctest.xml) · [모든 출력](all-test-output.txt) · [집중 검사](focused.txt) · [초기 집중 검사](early-focused.txt) · [전체 빌드](build.txt) · [최종 빌드](final-build.txt) · [형식 검사](format.json)를 보존한다.

## 계산과 원장 보존

기준선 작업2개의 동결 BAC는1000+3000=4000센트다. 개시 명시0 이후 누계 보고와 동일 상태시각 정정·뒤늦게 추가한 과거 상태일 보고, 이후 상태일의 과거 날짜 배분을 제출한다. [native 파일](source.ppm) · [본문](source.json)과 [일별](daily.json) · [주별](weekly.json) · [월별](monthly.json) 계산을 보존했다.

10월26~30일 기간 EV는1900,800,-50,0,미정이고 기간 AC는1800,700,-50,0,미정이다. 현재 조회 상태일에 알려진 날짜 배분은1400,600,50,0,미정이다. 음수 정정을 지우거나 배분을 AC에 다시 더하지 않는다. 개시 미보고/공란은 미정이며 명시0과 다르다. 마감 EAC/ETC는 기간 효율 대신 누계와 선택 식으로 계산한다. 프로젝트/WBS 효율은 합계 금액의 비율이다.

자정 정각의 보고는 다음 기간에 속한다. 상태일이10월26일12:00이면 PV는 작업별 반올림188+563=751센트, EV700·AC300센트이며 이후 날짜 보고/배분은 적용하지 않는다. 추가 계약 검사에서 동결 단가·휴일과 고정비 귀속/반올림, Steps 가중 진척·완료0/100, 현재 예산/단가 변경 시 기준선 불변, 삭제된 기준선 작업·현재 WBS 교집합, 통화/수치 한도·최초/최후 civil 날짜·기간/행 한도·취소를 확인했다.

조회와 출력 전후 전체 native 문서 동일성·dirty 보존을 검사했다. 저장/재열기·읽기 전용 독립 사본·복구 후 같은 수치를 얻었으며, 재무 XER fixture의 외부 BCWP/BCWS 스냅샷 유무를 바꿔도 native EVM 결과는 같다. 새 저장 상태/schema는 추가하지 않았다. 예제 native 파일은 기존11, 지원 최대 버전은 기존28이다.

## 독립 판독과 PDF

[독립 결과](independent-period-evm.json)와 [검사 코드](verify-native-period-evm.py)는 Python civil-minute/Decimal half-up, 상태일과 같은 시각의 마지막 원장 기록·보고 ID, native envelope SHA256와 전체 본문 동일성으로 프로젝트/작업/WBS의 **810개 수치 cell**을 대조한다. 앱 내부 계산 함수를 호출하지 않는다.

[XLSX](report.xlsx) · [PDF A3](report.pdf), 실제 화면의 저장 버튼으로 생성한 [UI XLSX](ui-report.xlsx) · [UI PDF A4](ui-report.pdf)를 별도 openpyxl/pypdf strict reader로 읽었다. 금액·음수·미정/0·비율 숫자 셀과 실행 수식0건을 확인했다. PDF는 A3 7페이지·A4 12페이지다. 일부 Qt 합성 굵은 글씨의 중복 추출 단어만 인접 중복을 제거한 뒤 제목/열을 검사하며, 출력에 잘못 매핑된 제어 문자가 없는지도 확인한다.

기간 지표·마감 누계·날짜 대사·보고 근거를 각각 나눠 넓은31열을 압축하지 않는다. PDFium으로 렌더한 [A3 첫 장](pdf-a3-first.png) · [A3 추이](pdf-a3-chart.png) · [A4 첫 장](pdf-a4-first.png) · [A4 추이](pdf-a4-chart.png)에서 한글·음수/미정·숫자/제목·여백과 상태일 이후 EV/AC 단절을 확인했다.

[기존 프로젝트 보고서](independent-classic-reports.txt) · [기간 실제](independent-period-actuals.json) · [Usage](independent-usage.json) · [Steps/활동](independent-activities.json) · [XER 병합](independent-xer-merge.json) · [XER 재연결](independent-xer-relink.json)도 독립 reader로 재검사했다.

## Windows 화면과 초기 검사

[Windows 로그](native-ui.txt), [1100×760](daily.png), [800×600](daily-small.png)을 보존했다. 최종 UI 테스트 바이너리의 복사본을 Windows 플랫폼으로 실행했고 지문이 원본과 같다. 실제 원가 메뉴, 일/주/월·WBS 전환·마지막 비동기 결과만 게시, 계산/파일 선택 취소, stale 문서·같은 revision의 session 변경 차단과 읽기 전용을 검사했다. 기간 PV/EV/AC를 먼저 배치하고 가상 표·가로 스크롤·열 이동과 고정 출력/닫기 버튼을 확인했다.

초기 [컴파일 오류](initial-build-failure.txt)는 KPI 구조체 초기화 순서를 수정했다. [경계 기대값 검사](initial-boundary-expectation.txt)는751센트가 맞는데 합산 뒤 반올림한750을 기대한 시험값을 수정했다. [복구 시험](initial-recovery-fixture.txt)은 편집 가능·dirty 상태의 실제 체크포인트 조건을 갖추도록 수정했고, [반복 시험](initial-repeat-artifact-fixture.txt)은 이전 실행의 출력 fixture를 재생성하도록 정리했다. PDF 독립 판독에서 한글 글꼴·미정 문자열/합성 굵은 글씨 추출을 반영했고, 출력의 빼기 기호 매핑과 넓은 열의 가독성을 보완한 뒤 최종 전체 검사를 다시 실행했다.

## 재현

저장소 루트 PowerShell에서 실행한다. 같은 Ninja 폴더의 빌드는 직렬로 실행한다. XLSX/PDF reader에는 openpyxl3.1.5/pypdf6.10.0, 선택적 PNG 렌더에는 pypdfium2 4.30.0과 Pillow를 사용했다.

```powershell
$taskRoot=(Get-Location).Path
$env:PATH="$taskRoot/.deps/Qt/6.8.3/mingw_64/bin;$taskRoot/.deps/Qt/Tools/mingw1310_64/bin;$env:APPDATA/Python/Python314/Scripts;$env:PATH"
cmake --build build-governance --parallel 8
ctest --test-dir build-governance -R 'native_period_evm|^cost_|^report_|dialog_layout|period_actual|xer_|primavera_xer|parser_seed_replay|primavera_public_corpus' --output-on-failure --parallel 2
ctest --test-dir build-governance --output-on-failure --parallel 4 --output-junit nev-regression.xml
.deps/report-verification/Scripts/python.exe scripts/verify-native-period-evm.py build-governance
.deps/report-verification/Scripts/python.exe scripts/verify-report-fixtures.py build-governance
.deps/report-verification/Scripts/python.exe scripts/verify-period-actual-exports.py build-governance
.deps/report-verification/Scripts/python.exe scripts/verify-usage-exports.py build-governance
.deps/report-verification/Scripts/python.exe scripts/verify-activity-extensions.py build-governance
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xer-merge.py build-governance
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xer-relink.py build-governance
python scripts/check-cpp-quality.py format --tool .deps/cpp-quality/Lib/site-packages/clang_format/data/bin/clang-format.exe
```

기간별 사용자 예산/EV 입력·마감 잠금/당시 지식 revision 고정, 외부 XER EV의 native 채택·통화 전환, MPP reader와 실제 P6/MS Project 재계산/재저장은 후속 범위다. 실제 외부 제품 실행은 미실행이며 PC 설치본은 이번에 교체하지 않았다.
