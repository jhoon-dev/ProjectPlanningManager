# XML 근무 달력 함수 검증

2026-11-11 · XMLC-01~05 · Windows / Qt6.8.3 MinGW13.1 Release.

[지원 계약](../../xml-calendar-functions.md), [C++ 검사](../../../tests/XmlCalendarTests.cpp), [fixture](../../../tests/XmlCalendarFixture.h), [독립 검사기](../../../scripts/verify-xml-calendar-functions.py).

## 결과

전체 **165/165 · 359.27초**, 집중 **36/36 · 27.73초**를 통과했다. [전체 CTest](ctest.txt), [JUnit](ctest.xml), [집중 기록](focused.txt), [집중 JUnit](focused.xml). 파서 **738개** 결정적 시드(schema1~46)와 C++ 형식 **457파일**, 차이0이다. [전체 Release 빌드](build.txt), [코어 빌드](core-build.txt), [달력 왕복 보강 빌드](calendar-roundtrip-build.txt), [형식](quality.json).

독립 검사:180개 구간 순회 사례·XML 원본/재내보내기68칸·식 정의30개·XER30값·보고서36칸, XLSX26시트/자원4시트와 PDF16페이지를 확인했다. 통화 전후·기준선 등6개 원본의 프로젝트 달력 ID 보존도 확인했다. MPXJ strict reader는 오류0, 작업4행·배정2개다. [보고서 대조](independent-reports.json), [리더 대조](independent-reader.json).

Windows800×650의 달력/함수 선택·미리보기·저장/undo를 확인했다. [실행](native-ui.txt), [화면](calendar-small.png). [XML](plan.xml), [재내보내기](reexport.xml), [XER](plan.xer), [입력 JSON](native.json), [원본 파일](source.ppm), [통화 전환 이력](archives.json), [예측 XLSX](forecast.xlsx), [자원 XLSX](resources.xlsx), [PDF](forecast.pdf), [180개 대조 사례](vectors.tsv).

[verification-summary.json](verification-summary.json)에 자료와 관련 소스 SHA-256을 기록한다.

## 검사 범위

세 함수의 기본/명시 달력, 프로젝트 휴일과 커스텀 달력의 대체 정책, 휴게/주말/휴일·야간/자정·소수 분/초·역순/음수/0·경과 기간을 검사한다. 작업/프로젝트/자원/배정 및 중첩 필드의 정의 범위와 요약 기본 달력을 검증한다. 원본 스냅샷 미대체, native 혼합 계산·roll-down·달력/휴일 변경 캐시·불명/중복 이름·불가용/범위 제한·취소를 포함한다.

schema46·프로젝트 달력 ID와 XML CalendarUID 왕복, 구버전 거절·리터럴/필드 참조의 함수 이름 오인 방지, 동결/시나리오·통화/ID 전후 사본·복구를 검사한다. 달력/함수 삽입·worker 미리보기·저장/undo·800×650 Windows 화면을 확인한다.

C++의180개 사례를 기존 스케줄러 WorkCalendar의 근무 좌표와 대조한다. 별도의 Python 검사기는 누적량 공식을 재사용하지 않고 날짜별 근무 구간을 직접 순회해 같은180개 차이·더하기/빼기를 검증한다. 입력에서 계획/예측을 독립 계산해 XML 두 파일의68칸과 식 정의30개·XER 정적30값·실제 보고서 필드/요약36칸을 확인한다. 원본999가 source.ppm에 남고 정적 값에 섞이지 않는지도 확인한다. MPXJ strict reader는 XML 파싱만 검증하며 수식 실행 동등성을 뜻하지 않는다.

## 재현과 경계

Qt/MinGW/CMake를 PATH에 준비한다.

```powershell
cmake --build build-governance --parallel 12
ctest --test-dir build-governance --output-on-failure --parallel 4
.deps/report-verification/Scripts/python.exe scripts/verify-xml-calendar-functions.py build-governance/xml-calendar-qa --reports
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xml-calendar-functions.py build-governance/xml-calendar-qa --reader
```

실제 Microsoft Project/P6 열기·재계산·재저장, macOS/스토어 실행은 미검증이다. 전체 VBA/Project 함수, 지역 언어/월 단위·시간대·자원 가용량/배정 구간을 달력으로 계산하는 기능은 지원 범위 밖이다. 날짜 함수의 근무1d는480분이며 커스텀 달력의 하루 길이와 구별한다. 명시 이름은 정확히 일치해야 하고 중복/누락을 추측하지 않는다. 기존 증분의 검증 자료는 변경하지 않는다.
