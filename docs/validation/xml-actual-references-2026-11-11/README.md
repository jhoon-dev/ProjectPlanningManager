# XML 실적 일정 참조 검증

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-actual-references-2026-11-11)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

2026-11-11 · XMLA-01~05 · Windows / Qt6.8.3 MinGW13.1 Release.

[지원 계약](../../xml-actual-references.md), [C++ 검사](../../../tests/XmlActualTests.cpp), [fixture](../../../tests/XmlActualFixture.h), [독립 검사기](../../../scripts/verify-xml-actual-references.py).

## 결과

전체 **167/167 · 360.72초**, 집중 **38/38 · 33.23초**를 통과했다. [전체 CTest](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-actual-references-2026-11-11/ctest.txt), [JUnit](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-actual-references-2026-11-11/ctest.xml), [집중 기록](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-actual-references-2026-11-11/focused.txt), [집중 JUnit](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-actual-references-2026-11-11/focused.xml). 파서 **750개** 결정적 시드(schema1~47)·C++ 형식 **459파일** 차이0이다. 마지막 파서 fixture 변수의 이름/범위를 정리한 뒤 해당 파일을 재검사했다. [전체 Release 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-actual-references-2026-11-11/build.txt), [코어 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-actual-references-2026-11-11/core-build.txt), [테스트 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-actual-references-2026-11-11/test-build.txt), [형식](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-actual-references-2026-11-11/quality.json), [최종 파서 형식](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-actual-references-2026-11-11/final-format.txt).

독립 대조: XML122칸(미설정34칸 포함)·식 정의28개·XER25값·보고서61칸과 원장 사본6개, XLSX21시트·PDF17페이지를 확인했다. MPXJ strict reader는 오류0, 요약 포함 작업7행·배정4개다. [보고서 대조](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-actual-references-2026-11-11/independent-reports.json), [리더 대조](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-actual-references-2026-11-11/independent-reader.json).

Windows800×650에서 상태일/수식 undo·정정 보고 갱신·미리보기·출처/비활성 스냅샷을 확인했다. [실행](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-actual-references-2026-11-11/native-ui.txt), [화면](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-actual-references-2026-11-11/actual-small.png). [부분 완료 XML](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-actual-references-2026-11-11/actuals.xml), [전체 완료 XML](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-actual-references-2026-11-11/complete.xml), [XER](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-actual-references-2026-11-11/actuals.xer), [입력 JSON](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-actual-references-2026-11-11/native.json), [전체 완료 입력](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-actual-references-2026-11-11/complete.json), [매핑/진단](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-actual-references-2026-11-11/mapping.json), [원본 파일](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-actual-references-2026-11-11/source.ppm), [원장/통화 이력](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-actual-references-2026-11-11/archives.json), [XLSX](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-actual-references-2026-11-11/fields.xlsx), [자원 XLSX](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-actual-references-2026-11-11/resources.xlsx), [PDF](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-actual-references-2026-11-11/fields.pdf).

[verification-summary.json](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-actual-references-2026-11-11/verification-summary.json)에 자료와 관련 소스 SHA-256을 기록한다.

## 범위

현재 상태일·이전/미래 보고·동일 상태일 정정·과거 정정·최신 날짜 지우기, 미보고/진행/완료·미착수 이정표를 검사한다. 작업·프로젝트·자원의 프로젝트 참조·배정의 작업 참조, 부분 실제 시작/전체 완료 요약과 완료 작업만 선택한 그룹·빈 그룹을 포함한다.

계획 계산 없음/불완전·충돌·stale·취소에서의 실적 독립성, 혼합 계획/실적·스냅샷 주소/캐시 격리·비정상 보고·작업 시작일 이전 실적·요청 취소, 근무/일반 날짜 함수·native 혼합·roll-down을 확인한다. 예약 이름/이전 바인딩, schema47·구버전 거절·동결/시나리오·통화/ID 사본·복구·재열기를 검사한다.

XML 내보내기의 상태일 기준 정적 값과 식, 재가져오기 후 원장이 없으면 미설정, 실행 해제로 가져온 스냅샷 조회를 구별한다. 제출 실적은 기존 정책대로 append-only이며 정정 보고로 변경한다. 상태일/수식 설정의 undo와 표/worker 미리보기·출처·작은 Windows 화면을 확인한다.

Python은 C++ 조회 함수를 호출하지 않고 원장 배열에서 상태일/추가 순서로 독립 선택한다. 부분/전체 완료 XML122칸과 미설정34칸, 식 정의28개·XER 정적25값·보고서61칸을 실제 값과 비교한다. 통화 전후/시나리오 등 원장6개도 독립 대조한다. MPXJ strict reader는 파싱만 확인하며 외부 제품의 계산 동등성을 뜻하지 않는다.

## 재현·경계

Qt/MinGW/CMake를 PATH에 준비한다.

```powershell
cmake --build build-governance --parallel 12
ctest --test-dir build-governance --output-on-failure --parallel 4
.deps/report-verification/Scripts/python.exe scripts/verify-xml-actual-references.py build-governance/xml-actual-qa --reports
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xml-actual-references.py build-governance/xml-actual-qa --reader
```

실제 Microsoft Project/P6 열기·재계산·재저장과 macOS/스토어 실행은 미검증이다. XML 실적 원장 자동 채택, 배정/자원 자체 실적 날짜와 다른 Actual/Remaining/Baseline/Early/Late 참조는 이번 범위 밖이다. XML 재가져오기의 미설정과 원본 source.ppm의 완전한 이력 보존을 구별한다. 이전 증분의 검증 자료는 변경하지 않는다.
