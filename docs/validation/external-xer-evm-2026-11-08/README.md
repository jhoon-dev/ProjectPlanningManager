# 외부 XER EV/PV native 채택 검증

2026-11-08 Windows x64, Qt6.8.3/MinGW13.1, Release/Ninja의 미커밋 작업 폴더에서 [XEA-01~06](../../external-xer-evm-adoption.md)을 검증했다. [verification-summary.json](verification-summary.json)에 소스·테스트·바이너리·증거 지문을, [source-manifest.txt](source-manifest.txt)에 전체 계산/앱 소스 목록을 보존한다.

최종 전체 회귀 **132/132(167.06초)**, EVM/원가·보고·기간 실적·XER/파일/파서 집중 **27/27(34.64초)**, 파서 **348 seeds(schema1~29)**, C++ 형식 **350파일**을 통과했다. 최종 전체 빌드는 경고/오류 없이 완료했다. [전체 CTest](ctest.txt) · [JUnit](ctest.xml) · [모든 테스트 출력](all-test-output.txt) · [집중 검사](focused.txt) · [초기3개 검사](early-focused.txt) · [초기 핵심 검사](early-core.txt) · [전체 빌드](build.txt) · [형식](format.json)을 보존한다.

## 원본·원장 불변과 채택 수치

[통제된 원본 XER](source.xer)는 TASKFIN의 첫 기간 EV12.00/PV10.00 USD, 두 번째 기간 EV8.00/PV15.00 USD다. 첫 개시0/0, 두 번째 개시12.00/10.00을 명시 채택했다. [채택 전 native 본문](before.json) · [채택 후 본문](adopted.json) · [schema29 파일](adopted.ppm)을 비교한다.

새 채택 이력과 revision을 제외한 전체 native 입력이 같다. 최초 재무 스냅샷·파일 SHA256와 native 원가/기간 실제/진척/Steps·기준선·작성자/기록 ID를 바꾸지 않았다. 기존 XER 실적·원가 재사용용 투영 SHA256도 같다. 동결 BAC100.00, native 물리 진척37.25%, AC122.80 USD를 유지하며 EV20.00/PV25.00으로 대체한다. CV-102.80, SV-5.00, CPI20/122.80, SPI0.8이고 예산 잔여식의 ETC80.00/EAC202.80 USD다. 채택 EV를 native 물리 EV37.25에 더하지 않는다.

[월별](monthly.json) · [주별](weekly.json) · [일별](daily.json) 프로젝트/작업/WBS를 출력한다. 원본 여러 날짜 구간 내부는 미정이며 마지막 단일 날짜 기간은 EV8.00/PV15.00을 계산한다. 첫 기간 마감과 다음 기간 개시를 둘 다 사용자가 확인했기 때문에 그 사이 날짜의 순증감0은 알려진 값이다. 명시 개시 없이 누락 기간을0으로 채운 결과가 아니다.

추가 계약 검사에서 EV 단독/PV 단독·원본 공란/명시0, 공유 경계 불일치·중복/한도/깨진 참조·통화·상태일, 다른 기준선 분리·작업 BAC 불일치를 검사했다. 개별 작업의 외부 금액이 BAC를 넘으면 프로젝트 BAC 합계 안에 들어도 EAC/ETC를 미정으로 전파한다. 후속 재무 병합·새 XER 재연결이 기존 채택값을 조용히 바꾸지 않으며 명시 재채택 때만 새 원본 값을 사용한다. AC·날짜 원장은 그대로다.

## 불변 이력·파일·worker

재채택은 기존 범위를 대체하는 새 이력이며 철회는 native 측정법을 복원한다. schema29의 저장/재열기·복구·독립 사본·계획 undo를 검사했고, 구 envelope28의 새 필드와 미래30을 거절했다. 원본/재무 revision·기준선·확인/형식·recordedRevision·native 상태일·기간 참조/개시 금액 변조도 거절했다. native 사실이 없어도 채택 이력으로 참조한 작업은 삭제 보호된다.

준비 worker는 현재 문서를 바꾸지 않는다. 취소·stale 문서·같은 revision의 다른 session·권한 회수·읽기 전용, 초안 및 잘못된 소수 입력 후의 확인/검증 해제를 검사했다. 실제 적용은 검증된 하나의 제출이며 일반 undo로 원본/채택/철회 기록을 제거하지 않는다.

## 독립 reader와 출력

[독립 결과](independent-adoption.json)와 [검사 코드](verify-external-xer-evm.py)는 원시 TSV/Decimal의 TASKFIN·native envelope/본문/원본 SHA256·전체 native 동일성, 재무기간 참조/명시 개시/마감·원장 상태일/ID oracle로 **2,106개 수치 cell**을 대조한다. 앱의 계산 함수를 호출하지 않는다. 날짜·주·월 및 프로젝트/작업/기준선 WBS의 누락·기간/누계·비율과 AC 대사를 확인한다.

[native 기간 XLSX](report.xlsx) · [일반 원가 XLSX](generic.xlsx) · [화면 출력 XLSX](ui-report.xlsx)의 숫자·음수·미정과 채택 근거를 openpyxl로 읽었고 실행 수식은0건이다. [PDF A3](report.pdf) 5페이지와 [화면 PDF A4](ui-report.pdf) 9페이지는 pypdf strict reader로 한글·금액·제어 문자와 제목/열을 검사했다. Qt 합성 굵은 글씨가 인접한 여러 단어의 같은 출력 run을 반복 추출하는 경우만 정규화한다. 숫자 oracle와 XLSX는 이를 사용하지 않고 원시 값으로 대조한다.

PDFium으로 [A3 첫 장](pdf-core-first.png) · [A3 출처/기간](pdf-core-audit.png) · [A3 추이](pdf-core-chart.png), [A4 첫 장](pdf-ui-first.png) · [A4 채택 이력](pdf-ui-audit.png) · [A4 추이](pdf-ui-chart.png)를 렌더했다. 한글/금액·미정/음수, 원본 파일/투영 지문·revision·작성자, 마감 경계·표의 줄바꿈과 footer를 확인했다.

[기존 native 기간 EVM](independent-native-period.json) · [일반 보고서](independent-classic-reports.txt) · [기간 실제](independent-period-actuals.json) · [Usage](independent-usage.json) · [Steps/활동](independent-activities.json)도 독립 판독했다. 기존 XER [실적·원가](independent-xer-actual-costs.json) · [스냅샷](independent-xer-snapshots.json) · [배분](independent-xer-allocation.json) · [병합](independent-xer-merge.json) · [재연결](independent-xer-relink.json)의 고정 MPXJ/TSV/SHA256 검사도 재실행했다.

## Windows 화면과 초기 검사

최종 테스트 바이너리의 복사본을 실제 Windows 플랫폼에서 실행했고 [로그](native-ui.txt)와 원본 바이너리 SHA256 일치를 보존했다. [1100×760 기간/개시](source.png) · [800×600](source-small.png) · [현재/채택 비교](preview.png) · [기간 EVM](report.png)을 확인했다. 실제 원가 메뉴·가상 표/검색·가로/세로 스크롤·확인/검증 해제·제출·철회와 XLSX/PDF 저장 버튼을 자동 조작했다. 고정 footer로 출력/닫기 접근을 유지한다. 화면/파일은 별도 통제 실행이므로 문서/보고서 UUID는 실행마다 다르다. [화면 실행의 native 사본](ui-adopted.ppm)도 포함한다.

초기 [파서 시드 빌드](initial-seed-build-failure.txt)의 기존 지역 변수 이름 충돌을 수정했다. [초기 파서 기대값](initial-parser-expectation.txt)은 구 envelope를 파일 decoder가 거절해도 정상 본문은 별도 raw schema29 경로에서 통과한다는 기존 harness 계약을 반영했다. 파일 거절과 본문 검증을 분리해 직접 검사한 후3/3·최종 전체132/132를 통과했다. 신규 coverage-guided fuzz/sanitizer·외부 제품 실행은 이번 실행에 포함하지 않는다.

## 재현

저장소 루트 PowerShell에서 실행한다. 동일 Ninja 폴더의 빌드를 겹치지 않는다. reader는 openpyxl3.1.5/pypdf6.10.0, XER는 MPXJ16.10.0/JPype1.6.0/Java21을 사용했다. 선택 PNG 렌더는 pypdfium2 4.30.0과 Pillow다.

```powershell
$taskRoot=(Get-Location).Path
$env:PATH="$taskRoot/.deps/Qt/6.8.3/mingw_64/bin;$taskRoot/.deps/Qt/Tools/mingw1310_64/bin;$env:APPDATA/Python/Python314/Scripts;$env:PATH"
cmake --build build-governance --parallel 8
ctest --test-dir build-governance -R 'external_xer_evm|native_period_evm|^cost_|^reports_|dialog_layout|period_actual|xer_|primavera_xer|parser_seed_replay|primavera_public_corpus|structured_diagnostics|file_lifecycle' --output-on-failure --parallel 2
ctest --test-dir build-governance --output-on-failure --parallel 4 --output-junit xea-regression.xml
.deps/report-verification/Scripts/python.exe scripts/verify-external-xer-evm.py build-governance
.deps/report-verification/Scripts/python.exe scripts/verify-native-period-evm.py build-governance
.deps/report-verification/Scripts/python.exe scripts/verify-report-fixtures.py build-governance
.deps/report-verification/Scripts/python.exe scripts/verify-period-actual-exports.py build-governance
.deps/report-verification/Scripts/python.exe scripts/verify-usage-exports.py build-governance
.deps/report-verification/Scripts/python.exe scripts/verify-activity-extensions.py build-governance
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xer-actual-costs.py build-governance
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xer-financials.py build-governance
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xer-allocation.py build-governance
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xer-merge.py build-governance
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xer-relink.py build-governance
python scripts/check-cpp-quality.py format --tool .deps/cpp-quality/Lib/site-packages/clang_format/data/bin/clang-format.exe
```

사용자 정의 기간 예산/EV 입력·임의 일별 배분, 당시 지식 revision 마감 잠금·통화/단위 변환, 새 일정 병합·MPP reader와 실제 P6/MS Project 재계산·재저장은 후속 범위다. native 채택/철회 정책은 XER/XML에 매핑하지 않고 source.ppm에 보존한다. 이번 개발 빌드로 PC 설치본은 교체하지 않았다.
