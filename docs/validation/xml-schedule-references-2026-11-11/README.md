# XML 계산식의 계산된 일정 참조 검증

2026-11-11 · XMLS-01~05 · Windows / Qt6.8.3 MinGW13.1 Release.

[지원 계약](../../xml-schedule-references.md), [C++ 계약/UI 검사](../../../tests/XmlScheduleTests.cpp), [독립 검사기](../../../scripts/verify-xml-schedule-references.py)를 따른다.

## 결과

전체 **163/163 · 279.28초**: [CTest](ctest.txt), [JUnit](ctest.xml). 집중 **26/26 · 14.09초**, 미래 버전 거절 검사 **8/8 · 14.15초**를 통과했다. 파서 **724개** 결정적 시드(schema1~45), C++ 형식 **453파일** 차이0이다. [빌드](build.txt), [최종 테스트 빌드](schema-test-build.txt), [형식](quality.json).

첫 전체 실행은 155/163이었다. 기존 테스트8개가 새 지원 버전45를 미래 버전으로 간주하여 실패했으며 미래 버전 거절 대상을46으로 갱신했다. [첫 실행](initial-ctest.txt)과 최종 전체 재실행을 함께 보존한다. 구현의 추가 변경은 없다.

독립 대조는 일정4행·XML20칸/식 정의7개·XER16값, XLSX21시트의 실제 계산/요약 **22칸**, PDF13페이지를 확인했다. MPXJ strict reader는 오류0, 작업4행·배정2개를 읽었다. [보고서 대조](independent-reports.json), [리더 대조](independent-reader.json).

Windows800×650에서 미리보기와 표/객체 창 갱신을 통과했다. [실행](native-ui.txt), [화면](schedule-small.png). [XML 계획](plan.xml), [XER 계획](plan.xer), [예측 XLSX](forecast.xlsx), [PDF](forecast.pdf), [원본 파일](source.ppm), [계획/예측 일정](schedule.json)을 보존한다.

[verification-summary.json](verification-summary.json)에 자료와 관련 소스 SHA-256을 기록한다.

## 검사 범위

작업의 Start/Finish, 네 범위의 프로젝트 날짜와 배정 소유 작업 날짜를 검사한다. FS/SS 지연·휴일·FixedWork·마일스톤·상태일 예측과 완료 실적을 다룬다. WBS/루트·필터 요약과 roll-down, native/XML 혼합 참조·문맥 없는 평가·중첩 문맥/캐시 분리·부분/중복/오래된 결과·취소/충돌/불완전을 검사한다.

schema45 파일/구버전 거절, 기존 바인딩 우선, 동결/시나리오·복구, XML 재가져오기, 활성 예측 보고서와 XML/XER 계획 정적 값의 구별을 확인한다. 표의 선행 변경·계산 세대 갱신, 비동기 미리보기·작은 Windows 화면과 객체 필드 창의 취소 결과 갱신을 검사한다.

독립 Python 검사기는 앱을 호출하지 않고 평일 09~12/13~18시 달력을 분 단위로 진행해 계획/예측 4행을 계산한다. XML의 실제 값·정의·요약·프로젝트/자원/배정, XER UDFVALUE를 기대값과 비교한다. openpyxl로 실제 사용자 필드/요약의 예측 결과를 확인하고 pypdf로 보고서의 내용·출처를 확인한다. MPXJ strict reader로 XML 파싱 오류도 확인한다.

## 경계

실제 Microsoft Project/P6에서 열기·재계산·재저장, macOS/스토어 실행은 미검증이다. MPXJ 판독은 외부 제품과의 수식 실행 동등성을 뜻하지 않는다. Actual/Early/Late/Baseline 참조, 배정/자원 자체 Start/Finish와 근무 달력 함수는 지원 범위에 포함하지 않는다. DateDiff는 휴게/야간을 포함한 달력 시간이다. 계산된 날짜/평가 문맥은 세션 값이며 원본 필드 스냅샷을 덮어쓰지 않는다.

## 재현

Qt/MinGW/CMake를 PATH에 준비한다.

```powershell
cmake --build build-governance --parallel 12
ctest --test-dir build-governance --output-on-failure --parallel 4
.deps/report-verification/Scripts/python.exe scripts/verify-xml-schedule-references.py build-governance/xml-schedule-qa --reports
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xml-schedule-references.py build-governance/xml-schedule-qa --reader
```

이전 증분의 검증 자료는 변경하지 않는다.
