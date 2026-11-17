# XML 계산식의 잔여 기간·공수 참조

2026-11-15 후속: [요약 기간 XSD-01~06](xml-summary-durations.md)의 프로젝트/WBS/그룹 Remaining Duration을 지원한다. 완료 작업 제외·모두 완료0·빈 그룹 미설정과 미완료 잔여 시작~완료의 프로젝트 근무 범위를 사용한다. 아래 요약 잔여 기간 미지원 표기를 이 지원 부분으로 확장한다.

2026-11-12 · XMLR-01~05.

- XMLR-01: 작업 `[Remaining Duration]`/`[RemainingDuration]`은 현재 상태일 예측의 잔여 시작부터 완료까지 작업 달력의 근무 분이다. 작업 달력·중단과 공수 역산의 실제 완료를 반영한다. 배정에서는 `[Task.Remaining Duration]`을 사용한다. 프로젝트/자원·WBS/그룹 내장 잔여 기간은 지원하지 않으며 병렬 작업 기간을 합산하지 않는다.
- XMLR-02: `[Remaining Work]`/`[RemainingWork]`는 같은 잔여 예측의 실행 배정 구간을 적분한 자원분이다. 작업·자원·배정·프로젝트 범위를 지원한다. Task/Resource/Project 접두사는 허용 범위에서 소유 작업/자원 또는 전체 프로젝트를 참조한다. 공통 Usage의 resourceOccupancies와 같으며, 계획 공수에서 관측 실제 공수를 빼서 추정하지 않는다. 완료 실적은0, 배정 없는 작업/미사용 자원도 배정 공수0이다. 자재 수량·직접비를 공수에 더하지 않는다.
- 미보고 작업은 기존 잔여 엔진의 계획 입력 유지 정책을 따른다. 보고 실제 공수처럼 미보고를 Null로 두는 조회와 구별한다. 수동 중단이 있는 계획 곡선의 XML 투영은 중단이 없는 외부 달력에서 전체 시간대 창을 담는 기간을 사용하며, 원본 작업 중단과 native 기간은 source.ppm에 보존한다.
- XMLR-03: 상태일 없음·예측 없음/불완전/취소는 사유를 가진 Null이다. 계획 결과를 예측으로 사용하지 않는다. 충돌/stale·다른 입력·잘못된 행/구간·합계 초과는 오류다. 요약 기간은 Null, 작업/자원 그룹의 공수는 포함된 행을 합산하며 알 수 없는 행을 묵시적으로 제외하지 않는다. 빈 그룹은 Null이다.
- XMLR-04: 선택 진척/잔여 평준화·FixedDuration/FixedWork/FixedUnits·곡선/자원 달력·날짜 고정 잔여 목표를 공통 예측 엔진에 맡긴다. 수식 도메인은 스케줄러를 호출하지 않는다. 표는 현재 결과를 사용하고 worker 미리보기만 누락 계산을 수행한다. XML/XER의 계획 날짜 투영에서도 Remaining은 별도의 현재 잔여 예측을 사용하며 출처를 구분한다. 계획 평준화가 활성화되어 잔여 예측을 제공하지 않는 경우는 Null이다.
- XMLR-05: builtin:remainingduration/remainingwork의 현재/동결/채택·통화/ID 전후 사본은 조건부 schema49이며 schema48 이하는 새 바인딩을 거절한다. 기존 최소 writer·undo·저장/복구와 원장을 유지한다. XML 식 재작성·정적 결과·미리보기·XLSX/PDF를 연결한다. XML 재가져오기는 진척 원장을 자동 생성하지 않으므로 원본 native와 다른 잔여 예측이 가능하다. 원본은 source.ppm에 보존한다.

예: `[Remaining Duration]/60`, `[Remaining Work]/60`, `[Task.Remaining Work]`, `[Resource.RemainingWork]`.

[최초 native 기준선 참조](xml-baseline-references.md)의 지원 부분은 후속 증분으로 구현했다. Early/Late, 요약 잔여 기간, 외부 제품 재계산 동등성은 후속 범위다.

[검증 기록](validation/xml-remaining-references-2026-11-12/README.md), [C++ 검사](../tests/XmlRemainingTests.cpp), [독립 검사기](../scripts/verify-xml-remaining-references.py).
