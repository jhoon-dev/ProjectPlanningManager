# XML 번호별 기준선 참조

2026-11-15 후속: [기존 문서 XML 기준선 병합](xml-baseline-merge.md)은 선택 번호의 외부 출처를 전체 교체한다. 다른 번호의 값·출처·동결 달력과 native 승인본은 보존하며 조건부 schema76을 따른다.

2026-11-15 · XBS-01~05.

- XBS-01: `Baseline0`~`Baseline10`의 Start/Finish/Duration/Work/Cost를 지원한다. 기본은 native 승인 v1~v11에 고정 대응하며 비교 화면 선택·이름·목록 위치로 바뀌지 않는다. [외부 기준선](xml-baseline-exchange.md)을 명시 채택한 번호는 그 외부 값을 우선하고 번호0은 이름 없는 Baseline에도 대응한다. 누락 값을 native 승인본으로 보충하지 않으며 이후 승인본을 추가해도 채택 출처는 바뀌지 않는다.
- XBS-02: `[Baseline1 Start]`, `[Baseline1Duration]`, `[Task.Baseline1 Work]`, `[Resource.Baseline1 Cost]`, `[Project.Baseline10 Finish]`처럼 사용한다. 범위별 날짜/공수/원가 의미는 기존 기준선 계약과 같다. 각 승인본의 동결 달력·중단·곡선·단가를 사용하며 현재 입력으로 대체하지 않는다.
- XBS-03: 없는 승인 버전·이후 추가된 객체는 사유 있는 미설정이다. 중복 버전·잘못된 일정은 해당 번호의 오류다. 다른 번호의 정상 값은 유지한다. 11 이상·음수·01 같은 번호는 지원하지 않는다. 요청한 번호만 계산하고 취소를 전파한다.
- XBS-04: 번호별 바인딩은 활성/비활성 모두 조건부 schema73이다. 현재·기준선·시나리오·채택·통화/ID 이력의 사본을 검사하고 schema72 이하에서 거절한다. 기존 기능의 최소 writer는 유지한다. 새 영속 필드는 없다.
- XBS-05: 편집기의 번호별 참조 삽입·worker 미리보기·출처, 표·요약·전파·보고서와 저장/복구를 연결한다. XML 식은 번호·접두사를 보존하고 표준 Baseline 요소에 지원 값도 출력한다. 재가져오기에서는 기본 미채택이며 별도 검토 후 새 사본에 명시 채택한다. native 전체 승인/곡선/원가 이력은 source.ppm에 보존하고 XER는 정적 사용자 필드 값이다. 실제 Project/P6 재계산 동등성은 별도 검증 대상이다.

기존 [기준선](xml-baseline-references.md)·[원가](xml-baseline-cost.md) 계약을 확장한다.
