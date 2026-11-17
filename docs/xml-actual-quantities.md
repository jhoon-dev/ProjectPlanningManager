# XML 계산식의 실제 기간·보고 공수 참조

2026-11-15 후속: [요약 기간 XSD-01~06](xml-summary-durations.md)의 프로젝트/WBS/그룹 Actual Duration을 지원한다. 작업별 보고 실제 기간은 그대로 유지하며 요약은 시작~완료/상태일의 프로젝트 근무 범위다. 아래 프로젝트 요약 실제 기간 미지원 표기를 이 지원 부분으로 확장한다.

2026-11-12 후속: [XPA-01~06](xml-progress-adoption.md)의 새 사본 작업 진척 명시 채택을 추가했다. 아래의 과거 XML 실적 미채택 표기는 기본 계획 변환에는 유지하며, 선택한 실제 날짜·기간의 새 사본 채택은 이 지원 부분으로 대체한다. 기존 문서 원장 병합·실제 공수/원가 채택은 계속 잔여다.

후속 [Remaining Duration/Work](xml-remaining-references.md)는 XMLR-01~05를 따른다. 아래의 Remaining 미지원은 이 실적 수량 증분 당시의 범위다.

2026-11-12 · XMLQ-01~05.

## 지원 계약

- XMLQ-01: 작업 `[Actual Duration]`/`[ActualDuration]`은 현재 상태일 이하의 최신 진척 보고에 기록된 `actualDurationMinutes`다. 같은 상태일의 정정은 마지막 추가 보고를 선택한다. 실제 시작/완료의 차이나 계획·잔여 기간으로 추정하지 않는다. 배정에서는 `[Task.Actual Duration]`을 사용한다. 프로젝트/자원 및 WBS·그룹의 내장 실제 기간은 지원하지 않는다. 병렬 작업 기간을 묵시적으로 합산하지 않는다.
- XMLQ-02: `[Actual Work]`/`[ActualWork]`는 상태일 기준 날짜별 실적 원장의 작업·자원·배정·프로젝트별 **보고된 공수 합계**다. 단위는 자원분이며 `workUnitMinutes / 100`을 정확하게 변환한다. 같은 작업/자원/날짜는 상태일·추가 순서로 최신 정정 하나만 포함한다. 미보고 날짜·누계 XER 공수·실제 기간·계획 배정률로 보충하지 않는다. 완전한 전체 실제 공수라는 보장은 없다.
- XMLQ-03: 값이 없으면 Null, 명시 보고 0은 0이다. 최신 정정이 공수를 비우면 이전 값으로 채우지 않는다. Task/Resource/Project 접두사는 허용된 범위에서 명시 대상을 참조한다. 작업/자원 WBS·분류 그룹의 요약 식은 포함된 행의 보고 공수만 합산하며, 모두 미보고인 그룹은 Null이다. 추가 전용 원장을 수정하지 않는다.
- XMLQ-04: 실적 평가는 계획 계산과 독립이며 상태일·정정·입력 객체가 달라지면 기존 평가 문맥을 재사용하지 않는다. 잘못된 값/합계 범위 초과·취소는 오류/취소로 전달한다. 새 바인딩이 현재/동결/이관 사본에 있으면 schema48을 사용하고 schema47 이하는 거절한다. 기존 날짜 참조만 있으면 schema47을 유지한다.
- XMLQ-05: 수식 편집기의 참조 삽입·worker 미리보기, 표/보고서·XML/XER 정적 값과 XML 식 재작성을 연결한다. native 파일·복구·통화/ID 이력은 정의와 원장을 보존한다. XML 재가져오기는 실적 원장을 자동 생성하지 않으며 활성 참조는 Null이다. 비활성 수식의 가져온 정적 값은 조회할 수 있다.

예: `[Actual Duration] / 60`, `[Actual Work] / 60`, `[Task.Actual Work]`, `[Resource.Actual Work]`, `[Project.Actual Work]`.

Remaining/Baseline/Early/Late, 프로젝트 요약 실제 기간, XML 실적 원장 자동 채택과 실제 MS Project/P6 재계산·재저장은 후속 범위다.

[검증 기록](validation/xml-actual-quantities-2026-11-12/README.md), [C++ 검사](../tests/XmlQuantityTests.cpp), [독립 검사기](../scripts/verify-xml-actual-quantities.py).
