# XML 기준선 채택·표준 내보내기

2026-11-15 후속: [NBT-01~06 native 상세 시간분포](native-baseline-timephased.md)를 추가했다. 아래의 native 상세 시간분포 제외는 최초 증분의 경계이며, 현재는 동결 실행 공수·일별 원가를 다섯 범위의 부모 위치에 표준 출력하고 명시 재채택/병합 시 schema77로 위치 정보를 보존한다.

2026-11-15 후속: [XBM-01~06 기존 문서 기준선 병합](xml-baseline-merge.md)을 추가했다. 아래의 ‘기존 문서 병합 제외’는 최초 새 사본 채택 증분의 경계이며, 현재는 별도 메뉴의 번호별 전체 교체·명시 연결/제외·전후 이력/schema76 계약을 따른다.

2026-11-15 · XBE-01~07. 작업·프로젝트 요약(UID0)·WBS·자원·배정의 Baseline0~10을 다룬다. [번호별 참조](xml-numbered-baselines.md)와 [요약 기간](xml-summary-durations.md)의 외부 자료 경계를 확장한다.

- XBE-01: XML을 읽으면 번호별 후보를 만들며 기본은 미채택이다. 가져오기 확인 창에서 `XML 번호별 기준선을 별도 검토한 뒤 새 사본에 채택`을 선택하고 번호·검토자·사유를 확인한다. 선택 번호의 모든 객체를 하나로 채택한다. 같은 객체/번호 중복·잘못된 값이 있으면 그 번호를 채택할 수 없으며 선택하지 않은 다른 번호는 영향을 받지 않는다. 기존 문서의 승인본을 덮어쓰는 병합은 포함하지 않는다.
- XBE-02: Start/Finish·Duration·Work·Cost의 명시 값과0/미설정을 보존한다. 날짜는 시간대 없는 ISO civil 일시/소수 초6자리다. 기간/공수는 비음수 ISO PT 시/분/초를 소수6자리 분으로 정확히 표현할 수 있어야 한다. 표현할 수 없는 정밀도는 거절한다. XML 원가 전송 값은 주요 통화 단위의1/100이며 기존 XML Cost 변환과 같은 규칙으로 현재 통화 최소 단위에 정확히 변환한다. 예: USD12.34는 XML1234, JPY1234는 XML123400이다. 최소 단위 아래의 값·한도 초과는 거절한다. 현재 계획·달력/단가/배정률·관측 진척으로 값을 추정하지 않는다.
- XBE-03: Interim·DurationFormat·EstimatedDuration, BCWS·BCWP·작업 FixedCost와 작업/배정 Baseline 안의 TimephasedData를 보존한다. 시간분포의 종류는 번호/객체에 맞는 기준선 Work/Cost여야 한다. 단위·값·선택 날짜와 원본 UID를 보관하고 XML 출력 UID는 현재 객체의 출력 UID에 연결한다. 시간분포로 실제 원장·배정 곡선·native EVM을 자동 생성하지 않는다. 별도 위치의 시간분포·미지원 새 요소는 원본/손실 진단 범위다.
- XBE-04: 채택 번호의 `BaselineN` 참조는 그 외부 명시 값을 사용한다. 번호0은 이름 없는 `Baseline`에도 대응한다. 번호 전체의 자료 출처를 선택하므로 누락 객체/필드를 같은 번호의 native 승인본으로 보충하지 않는다. 채택하지 않은 번호는 기존 native vN+1 조회를 유지한다. 외부 자료는 native 승인 이력과 별도이며 이후 승인본을 저장해도 채택 번호의 출처는 바뀌지 않는다. 프로젝트 기간은 원본 UID0의 명시 Duration이며 없으면 미설정이다. WBS/필터의 계산식 기간은 현재 포함 작업의 날짜 범위를 채택 당시 동결 프로젝트 달력으로 측정한다. XML 원본 WBS의 명시 기준선 값은 별도로 보존·재출력한다.
- XBE-05: 일반/표준 실적 XML 내보내기에 표준 `<Baseline>` 요소를 함께 쓴다. 채택한 외부 값·표시·시간분포를 보존하고, 나머지 슬롯은 native 승인 v1~v11의 동결 작업/프로젝트/WBS 날짜·기간·공수·원가와 자원/배정 공수·원가·실행 배정 날짜를 투영한다. 미확정 원가는 생략한다. 현재 삭제된 객체·v12 이후 승인본·native 기준선 곡선/원가의 상세 시간분포와 전체 이력은 `source.ppm`에 보존하고 진단한다. 원본 프로젝트 전체 기준선 합계가 현재 객체 합계와 다를 수 있다.
- XBE-06: 채택 자료가 현재·기준선·시나리오·채택/통화/ID 이력 사본에 있으면 조건부 schema75다. schema74 이하는 새 자료를 거절하며 없는 파일의 최소 writer는 유지한다. 저장/재열기·복구·명시 통화 전환(추가 원가/시간분포 원가 포함)·작업/자원 ID 이관을 연결한다. 원본 SHA/검토 정보를 보존하고 새 교환 폴더 게시 전에 원본 XML·후보·선택·결과를 다시 대조한다. 진척 채택과 함께 사용할 때 진척→기준선 순서를 유지한다. 취소/stale 시 기존 문서·원본·게시 대상에 부분 반영하지 않는다.
- XBE-07: 번호별/범위별 수치·부분/0·중복·잘못된 값·원본 위변조·채택/진척 공존·내보내기/재채택, native 승인본의 표준 투영, 형식/복구/통화/ID와 UI를 회귀 검증한다. 독립 XML/십진수 판독과 보고서에서 같은 수치를 확인한다. 실제 MS Project/P6 재계산·재저장 동등성은 별도 검증 대상이다.

표준 근거: Microsoft [Baseline 요소](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/baseline-element?view=project-client-2016), [TimephasedDataType](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/xml-schema-for-the-timephaseddatatype-complex-type?view=project-client-2016).

[검증 기록](validation/xml-baseline-exchange-2026-11-15/README.md): 집중 회귀42/42·파서960개, 독립 XML/Decimal·XLSX/PDF와 MPXJ 원가/공수 판독. 소수 초 판독 제한과 실제 제품 왕복 미실행을 포함한다.
