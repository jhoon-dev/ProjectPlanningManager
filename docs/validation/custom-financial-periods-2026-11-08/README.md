# 사용자 정의 기간·기간 마감 검증 · 2026-11-08

최종 Windows x64 · Qt6.8.3 · MinGW13.1 · Release/Ninja. 전체 **134/134**(163.78초), 집중 **29/29**(36.66초), 파서 **390개 시드**와 clang-format **362개 파일**을 통과했다. [기능·계산/저장 계약](../../custom-financial-periods.md) · [검증 요약/지문](verification-summary.json) · [전체 로그](ctest.txt) · [JUnit](ctest.xml) · [개별 테스트 출력](all-test-output.txt) · [집중 회귀](focused.txt).

## 구현과 지식 마감

기준선별 비중첩 사용자 기간과 작업별 기간 예산 PV·인정 EV를 저장한다. 지정액은 해당 기간의 지표에 사용하며, native 누계와 기준선 BAC·물리 진척·AC·기존 원장·XER 원본을 보존한다. 빈칸/명시0·음수 EV 정정, 미래 인정 EV 거절과 현재 기간 관측, 기준선 WBS 집계·civil 날짜0001/9999를 검사했다.

통제 사례는 2026-10-26~27, 기준선 BAC40.00 USD다. 설계 작업에 기간 PV5.00/EV4.50, 시공 작업에는 native PV30.00/EV21.00을 사용해 기간 합계 PV35.00/EV25.50/AC25.00, CV0.50/SV-9.50, CPI1.02/SPI25.5/35를 얻었다. 별도 native 누계는 PV40.00/EV27.00/AC25.00, EAC38.00/ETC13.00이다. 날짜배분20.00과 보고증감 차이5.00을 이중 합산하지 않았다.

마감은 작업별 native 개시/마감·날짜배분·원장 ID와 `knowledgeRevision`을 고정한다. 뒤 원가·계획 및 외부 XER EV/PV 철회가 과거 마감을 재작성하지 않는다. 누계 원가·진척·Steps의 과거 상태일과 뒤 상태일의 과거 실제 날짜 배분을 잠근다. 가장 뒤 마감부터 재개방한 뒤 정정/재마감하면 새 이력을 추가한다. 이전 AC25.00은 보존하고 재마감 AC26.00/CV-0.50/native EV27.50/EAC38.50을 계산했다.

[채택 전 native 입력](before.json) · [최종 native 본문](final.json) · [schema30 파일](closed.ppm) · [열린 기간](open-report.json) · [첫 마감](closed-report.json) · [재개방](reopened-report.json) · [재마감](reclosed-report.json). 정의/마감/재개방의 불변성, 계획 undo, 독립 사본과 복구, 구 envelope29의 새 필드/미래31 거절·정확한 키/형식/전이/참조/금액과 기록 당시 원장 잠금 위반 검사를 포함한다. 기존 schema25~28 재무 문서가30 envelope에서도 정상 판독됨을 추가 확인했다.

## 독립 계산과 파일 출력

[독립 결과](independent-periods.json)와 [검사 코드](verify-custom-financial-periods.py)는 앱 계산 함수를 호출하지 않는다. native envelope/본문 SHA256, 지식 revision 이하 원장의 상태일·마지막 정정·기준선 예산·Decimal로 마감 스냅샷3건과 프로젝트/작업/WBS **340개 수치**를 대조한다. 기본·Windows 화면의 실제 XLSX에서 추가 **170개 수치**를 검사했다. 사전 원장/기준선은 그대로다.

[첫 마감 XLSX](closed.xlsx) · [재마감 XLSX](reclosed.xlsx) · [화면 XLSX](ui-report.xlsx) · [Windows XLSX](native-ui-report.xlsx), 총 **4개**를 openpyxl로 읽었고 실행 수식은0건이다. [A3 PDF](closed.pdf), [화면 A4 PDF](ui-report.pdf), [Windows A4 PDF](native-ui-report.pdf)는 각각6/8/7페이지다. 기본 실행 PDF는 pypdf strict reader의 한글·숫자·제어 문자 검사를 통과했다.

**Windows 네이티브 PDF의 글자는 Qt 폰트 엔진이 벡터 윤곽선으로 출력한다.** 해당 파일의 검색 가능한 텍스트 판독을 통과했다고 주장하지 않는다. strict PDF 구조·전 페이지 벡터 내용 검사와 PDFium 렌더의 한글/수치/배치 검토로 확인했다. 같은 Windows 실행의 XLSX 수치는 독립 대조했다. [A3 첫 장](pdf-core-first.png) · [A3 근거](pdf-core-audit.png) · [A4 첫 장](pdf-ui-first.png) · [A4 근거](pdf-ui-audit.png) · [Windows 첫 장](pdf-native-first.png) · [Windows 근거](pdf-native-audit.png). 기간내 날짜를 보간하지 않으며 native 누계 차트는 선택 마감의 한 점이다.

기존 [native 기간 EVM](independent-native-period.json) · [외부 EV/PV 채택](independent-external-adoption.json) · [일반 보고](independent-classic-reports.txt) · [기간 실제](independent-period-actuals.json) · [Usage](independent-usage.json) · [활동/Steps](independent-activities.json)와 XER [실적/원가](independent-xer-actual-costs.json) · [스냅샷](independent-xer-snapshots.json) · [배분](independent-xer-allocation.json) · [병합](independent-xer-merge.json) · [재연결](independent-xer-relink.json)도 독립 판독했다.

## Windows UI와 실행 식별

최종 UI 바이너리의 SHA256이 같은 복사본을 `-platform windows`에서 실행했다. [로그](native-ui.txt) · [정의](definition.png) · [800×600](definition-small.png) · [마감 검토](close-preview.png) · [불변 이력](closed-history.png) · [기간 보고](report.png) · [작은 보고 화면](report-small.png). 메뉴→정의→검토/확인→마감→재개방→과거 이력 선택→실제 파일 출력, 오류 사유 안내·잘못된 금액·초안 검증 해제·취소·문서 교체/권한·읽기 전용을 검사했다. 1만 작업에 행별 편집 위젯을 만들지 않는 가상 표도 확인했다.

[소스 manifest](source-manifest.txt)의 최종 SHA256은 `99de13ad61d596b35280ae2ce9c29c2577f1596331a957257fee3b81bafef24c`다. HEAD와 별도로 현재 미커밋 구현 전체를 포함한다. [최초 전체 빌드](build.txt) · [최종 빌드](build-final.txt)와 실행 파일/테스트/reader/출력 지문은 요약 JSON에 있다. 설치본 교체·패키지 배포·실제 P6/MS Project 재계산/재저장은 수행하지 않았다.

## 초기 검사에서 조정한 사항

최초 CMake 등록 순서와 테스트의 누락 include를 수정했다. 복구 스냅샷은 미저장 변경이 필요하다는 기존 계약과, 동일 입력을 일반 저장한 뒤에는 준비 결과를 올바른 dirty 상태로 반영할 수 있다는 공유 worker 계약을 테스트에 반영했다. 반복 실행은 테스트가 생성한 정확한 고정 산출물만 정리하며 기존 파일 덮어쓰기 거절 정책을 유지한다. 기존 XER 검사4건의 미래30 기대를31로 갱신하고30 판독을 추가했다. 첫 전체134/134 이후 오류 안내도 구체 사유로 보완해 최종 전체 회귀를 다시 실행했다. 초기 로그도 보존했다. 신규 coverage-guided fuzz/sanitizer나 외부 제품 실행으로 확대하지 않는다.

## 재현

저장소 루트 PowerShell에서 아래를 실행한다. 같은 Ninja 폴더에서 빌드를 겹치지 않는다. reader 의존성은 openpyxl3.1.5/pypdf6.10.0, XER는 MPXJ16.10.0/JPype1.6.0/Java21이며 PNG 렌더는 pypdfium2 4.30.0/Pillow다.

```powershell
$taskRoot=(Get-Location).Path
$env:PATH="$taskRoot/.deps/Qt/6.8.3/mingw_64/bin;$taskRoot/.deps/Qt/Tools/mingw1310_64/bin;$env:APPDATA/Python/Python314/Scripts;$env:PATH"
cmake --build build-governance --parallel 8
ctest --test-dir build-governance --output-on-failure --parallel 4 --output-junit cfp-regression.xml
.deps/report-verification/Scripts/python.exe scripts/verify-custom-financial-periods.py build-governance
.deps/report-verification/Scripts/python.exe scripts/verify-native-period-evm.py build-governance
.deps/report-verification/Scripts/python.exe scripts/verify-external-xer-evm.py build-governance
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
