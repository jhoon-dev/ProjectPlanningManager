# XML 계산식의 최초 기준선 참조

2026-11-15 후속: [XBE-01~07](xml-baseline-exchange.md)의 외부 기준선 채택·표준 출력을 지원한다. 채택 번호의 참조는 외부 명시 값을 사용하며 번호0은 이름 없는 Baseline에도 대응한다. 채택하지 않은 번호는 기존 native 승인본을 사용한다. 아래 XML 기준선 미교환/재가져오기 미설정 표기는 기본 미채택 상태에만 유지하며 검토 후 명시 채택할 수 있다.

2026-11-15 후속: [번호별 Baseline0~10](xml-numbered-baselines.md)은 native 승인 v1~v11에 고정 대응하며, [요약 기간](xml-summary-durations.md)은 프로젝트/WBS/그룹의 동결 프로젝트 달력으로 계산한다. 아래 최초 v1의 작업별 계약은 유지하고 번호별 참조·프로젝트/그룹 기간 미지원 표기는 새 계약으로 확장한다.

2026-11-14 후속: [Baseline Cost](xml-baseline-cost.md)도 최초 native v1의 동결 원가로 지원한다. 네 범위·그룹/전파·동결 날짜별 단가/직접비를 연결하며 새 바인딩은 조건부 schema62다. 아래 네 기존 참조의 최소50은 유지한다.

2026-11-12 · XMLB-01~05.

- XMLB-01: `[Baseline Start]`/`[BaselineStart]`와 `[Baseline Finish]`/`[BaselineFinish]`는 최초 native 기준선(v1)의 동결 일정이다. 작업·프로젝트 범위를 지원하고 배정에서는 `[Task.Baseline Start]`/`[Task.Baseline Finish]`로 소유 작업을 참조한다. `[Project.Baseline Start]`/`[Project.Baseline Finish]`는 모든 범위에서 전체 동결 프로젝트를 참조한다. 화면의 비교 기준선 선택이나 후속 v2/v3 저장에 따라 값이 바뀌지 않는다.
- XMLB-02: `[Baseline Duration]`/`[BaselineDuration]`은 동결 작업의 시작~완료 사이를 동결 작업 달력·중단으로 적분한 근무 분이다. 고정 공수 역산의 실제 기간을 사용하며 원래 입력 기간으로 대체하지 않는다. 작업과 배정의 `[Task.Baseline Duration]`만 지원한다. 프로젝트·자원·WBS/그룹의 내장 기준선 기간은 Null이며 병렬 기간을 합산하지 않는다.
- XMLB-03: `[Baseline Work]`/`[BaselineWork]`는 동결 배정 실행 구간의 자원분이며 작업·자원·배정·프로젝트 네 범위를 지원한다. Task/Resource/Project 접두사는 허용 범위에서 소유 객체 또는 전체 동결 프로젝트를 참조한다. 동결 달력·중단·곡선·고정 공수의 마지막 부분 단위를 반영하고 관측 실제 공수·현재 계획을 사용하지 않는다. 기준선에 존재한 미배정 작업·미사용 자원은0, 이후 추가된 객체/배정은 Null이다. 프로젝트 합계는 이후 삭제된 작업을 포함한 동결 전체 값이다.
- XMLB-04: WBS/작업·자원 그룹은 현재 그룹에 포함된 객체 ID를 동결 객체와 연결한다. 시작/완료는 최소/최대, 공수는 합계이며 기준선에 없는 포함 행은 묵시 제외하지 않고 Null을 전파한다. 빈 그룹은 Null이다. 기준선 없음은 사유를 가진 Null, 잘못된 첫 버전·중복/누락 일정·잘못된 날짜/완료일·합계 초과는 오류다. 현재 일정의 미계산/stale/충돌이나 상태일은 기준선 조회를 막지 않는다. 취소는 상위 worker로 전파한다.
- XMLB-05: `builtin:baselinestart/baselinefinish/baselineduration/baselinework`가 현재·비활성·동결·시나리오·XER 채택·통화/ID 전후 사본에 있으면 조건부 schema50을 사용한다. schema49 이하는 새 바인딩을 거절하고 기존 최소 writer는 유지한다. 표·worker 미리보기·출처·참조 삽입, 저장/복구·전체 사본과 XML/XER 정적 결과·XLSX/PDF 보고서를 연결한다.

이 지원 부분은 **최초 native 기준선(v1)**에 고정된다. 외부 XML 기준선이나 MS Project의 Baseline0~10 슬롯을 자동 채택·대응하지 않으며 번호별 참조는 후속이다. XML에는 계산식 정의와 정적 사용자 필드 결과를 내보내고 native 기준선 자체는 `source.ppm`에 보존한다. 따라서 XML 재가져오기 후 기준선 수식은 사유를 가진 Null이다. 외부 제품에서 재계산한 값이 동일하다고 보장하지 않으며 내보내기 진단에도 이 경계를 표시한다.

후속 [Early/Late 참조](xml-early-late-references.md)의 지원 부분을 구현했다. 실제 MS Project/P6 재계산·재저장, XML 실적 원장 자동 채택과 나머지 GAP 잔여는 유지한다.

[검증 기록](validation/xml-baseline-references-2026-11-12/README.md), [C++ 검사](../tests/XmlBaselineTests.cpp), [독립 검사기](../scripts/verify-xml-baseline-references.py).
