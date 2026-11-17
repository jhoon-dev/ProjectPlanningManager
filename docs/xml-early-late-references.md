# XML 계산식의 Early/Late 날짜 참조

2026-11-12 · XMLE-01~05.

- XMLE-01: `[Early Start]`/`[EarlyStart]`, `[Early Finish]`/`[EarlyFinish]`, `[Late Start]`/`[LateStart]`, `[Late Finish]`/`[LateFinish]`를 지원한다. 작업·프로젝트 범위와 모든 범위의 `Project.` 참조, 배정의 `Task.` 소유 작업 참조를 지원한다. 자원 자체·배정 자체의 내장 날짜는 지원하지 않는다.
- XMLE-02: 계획의 Early는 현재 전진 배치 날짜이며 ALAP·채택 평준화를 적용한 현재 결과를 따른다. 별도의 무제약 ASAP 배치를 만들어 대체하지 않는다. 상태일 예측의 Early Start는 미완료 작업의 잔여 재개, Early Finish는 예측 완료다. 실제 시작을 미래 시작으로 재사용하지 않고 완료 실적의 네 날짜는 사유를 가진 Null이다. 일반 `[Start]/[Finish]`와 관측 `[Actual Start]/[Actual Finish]`의 기존 의미는 유지한다.
- XMLE-03: Late는 기존 검증된 후진 계산의 좌표를 같은 계산 시계로 날짜화한다. 기본 CPM은 근무분 시계, PDM/공수는 civil-minute 시계다. 좌표가 현재 경계와 같으면 비근무 경계의 이정표를 포함해 정확한 현재 날짜를 보존한다. 논리·잔여 후진 또는 평준화 자원 순서 후진의 가용 플래그를 확인하며 미산정 Late를0좌표나 논리 여유로 대체하지 않는다. 미산정은 사유를 가진 Null, 잘못된 좌표·역전·현재 배치 이전·프로젝트 완료일 초과는 오류다. Late만 잘못되거나 미산정이면 유효한 Early를 유지한다.
- XMLE-04: 프로젝트·WBS·작업 분류/필터 그룹은 시작의 최소·완료의 최대를 사용한다. 예측에서는 완료 실적을 대상 행에서 제외하며 모두 완료되었거나 빈 범위이면 Null이다. 포함된 미완료 작업의 알 수 없는 값·오류를 묵시 제외하지 않는다. 미계산/불완전/취소는 사유를 가진 Null, 충돌·stale·다른 문서/입력/상태일·누락/중복 행은 오류다. 계산식 도메인은 스케줄러를 호출하지 않고 worker가 제공한 불변 현재 결과를 사용한다.
- XMLE-05: 네 `builtin:earlystart/earlyfinish/latestart/latefinish` 바인딩이 현재·비활성·동결·시나리오·XER 채택·통화/ID 전후 사본에 있으면 조건부 schema51이다. schema50 이하는 새 바인딩을 거절하며 이전 최소 writer는 유지한다. 표·worker 미리보기/취소·참조 삽입·출처, 저장/복구·실행 취소와 XML/XER·XLSX/PDF에 연결한다. XML/XER 내보내기는 기존 계획 모드를 사용하며 보고서의 상태일 예측과 구분한다. XER Start/Finish 사용자 필드는 기존 시각 보존 텍스트 투영을 따른다.

예: `ProjDateDiff([Early Start],[Late Start])`, `[Task.Late Finish]`, `[Project.EarlyFinish]`.

후진 날짜는 현재 native 계산 정책의 배치 가능 날짜다. 상용 제품의 무제약 logical early·음수 여유·자원 재최적화와 같은 의미라고 확대하지 않는다. 실제 MS Project/P6 재계산·재저장 동등성은 미검증이다. XML 진척 원장을 자동 채택하지 않으므로 XML 재가져오기 후 계획/예측 문맥이 원본과 다를 수 있다. 전체 원본은 교환 패키지의 `source.ppm`에 보존한다.

기존 잔여인 추가 Project 함수·시간대 변환, XML 실적의 명시적 native 원장 채택과 나머지 GAP 범위는 유지한다. [검증 기록](validation/xml-early-late-references-2026-11-12/README.md), [C++ 검사](../tests/XmlEarlyLateTests.cpp), [독립 검사기](../scripts/verify-xml-early-late.py).
