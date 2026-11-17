# 개념 데이터모델

2026-11-17 후속: [SMAT-01~07](../shared-material-actuals.md)은 계산 중 원장 ID/정정 사슬을 구분해 연결하고 리비전으로 안정 정렬한다. IntegratedResult.materials에 원본 프로젝트/자재·리비전·확인 재고·잔여 소모·미입고 잔량을 보존한다. 실제 원장을 새 원본 기록으로 합산 저장하지 않는다.

2026-11-17 후속: [MAT-01~08](../actual-materials.md)은 Project/LevelingScenario.materialActuals의 안정 거래 ID·정정 사슬·상태일/기록 메타데이터·동결 개시 기준과 ResourcePlan.autoDelayRemainingForReceipts를 추가한다. 실제 잔고/명시 잔여 수량에서 파생 공급을 구성하며 실제 기록을 계획 소모·원가로 다시 합산하지 않는다.

2026-11-17 후속: [자동 입고 대기](../material-arrival-leveling.md)는 ResourcePlan.autoDelayForReceipts와 새 직렬 정책이다. MaterialReservations는 엔진별 후보 날짜에서 순서대로 자재 소모를 예약한다. 내부 부분 예약은 외부의 전체 프로젝트 재고 조회와 구분하며 공급을 새로 만들지 않는다.

2026-11-17 후속: [계획 자재 재고](../material-inventory.md)는 Material.stockAt와 ResourcePlan.receipts를 저장하고,MaterialInventoryQuery가 불변 계획 일정의 소모를 투영한다. 개시 직전 재고·이후 입고/소모·미정·음수·최초 부족을 구분하며 실제 원장과 분리한다.

2026-11-16 후속: [공유 가용기간](../shared-resource-availability.md)은 SharedResource의 nullable availableFrom/To와 AvailabilityPeriod 목록이다. 공통 공급 어댑터로 분 시점/변경 이벤트/통합 평준화에 같은 우선순위를 적용한다. 일별 부하의 variableCapacity는 복수 설정값과 미정 상수를 구분하는 파생 값이다.

2026-11-16 후속: [공유 잔여 가용량](../shared-remaining-availability.md)은 SharedAvailabilityQuery의 불변 작업공간·프로젝트별 ResourceAvailabilityQuery로 파생한다. 공유 공급을 한 번만 적용하고 미정 전체/확인된 하한/프로젝트별 기여·입력 출처를 구분한다. 새 영속 필드는 없다.

2026-11-16 후속: [잔여 가용량](../remaining-resource-availability.md)은 ResourceAvailabilityQuery의 불변 공급/달력·누적 배정 수요 스냅샷으로 파생한다. 새 영속 자원 필드 없이 활성 부하 함수에만 조건부 schema80을 적용한다. 계산 문맥은 공유 query를 보유하며 실제 원장을 생성하지 않는다.

2026-11-16 후속: [가용기간 모델](../resource-availability.md)은 Resource.availableFrom/availableTo와 AvailabilityPeriod.from/to/units를 추가한다. 양끝 분 포함·빈 경계 무기한, 전체 경계 밖0·날짜→기간→기본값의 우선순위와 조건부 schema79를 사용한다. 계산용 용량 타임라인은 모델을 변경하지 않고 파생한다.

2026-11-15 후속: [Units·가용률 참조](../xml-unit-fields.md)는 기존 Assignment.units/Resource.maxUnits/CapacityDay를 읽는다. 안정 builtin 키·명시 날짜 확장 함수·범위별 Null과 캐시 입력을 추가하고 새 영속 모델 없이 조건부 schema78을 사용한다.

2026-11-15 후속: [native 상세 시간분포](../native-baseline-timephased.md)는 동결 실행 구간·일별 원가에서 파생하며 native 승인본을 수정하지 않는다. 명시 채택/병합 시 기존 XmlBaselineRow.timephased와 새 timephasedOutside 위치 정보를 보존하고 조건부 schema77을 사용한다. 자원 시간분포는 표준 부모 위치에 출력한다.

2026-11-15 후속: [XML 기준선 병합](../xml-baseline-merge.md)은 XmlBaselineSet.origins에 번호별 원본·검토·동결 달력을 보존하고 Project.xmlBaselineMerges에 수신 자료·일대일 연결/제외·교체 확인·당시 통화·전후 집합을 남긴다. 현재/동결 값은 통화·ID 전환을 따르고 감사 기록은 당시 값으로 보존한다.

2026-11-15 후속: [XBE-01~07 외부 기준선](../xml-baseline-exchange.md)은 native Baseline 승인 이력과 분리한 PlanInput의 선택적 XmlBaselineSet이다. 번호·객체 연결별 명시 값/0/미설정·표시·시간분포와 원본/검토·동결 프로젝트 달력을 보존한다. 채택 번호가 참조 출처를 고정하며 일정·관측 원장·native 승인/EVM을 자동 생성하지 않는다.

2026-11-11 후속: [XML 계산식](../xml-external-formulas.md)의 안정 바인딩과 원본 입력을 보존하고 조회 시 typed 결과를 계산한다. native 식과의 혼합 그래프·roll-up/down 연계를 검증한다.

2026-11-11 후속: [요약 roll-up](../xml-roll-up.md)은 명시 WBS/Task 루트 값과 CustomField.xmlRollUp 규칙을 보존하고, 조회별 RollUpValues로 WBS·작업/자원 그룹의 파생 값을 계산한다.

2026-11-11 후속: [XmlRollDown·RollDownValues](../xml-roll-down.md)를 추가했다. CustomField.xmlRollDown은 배정 정의 ID와 자동 플래그를, Assignment.rollDownMissing은 명시 미설정 키를 보존한다. 유효 배정 값은 원본/직접 상태에서 계산하며 원본을 배정 직접 값에 덧쓰지 않는다.

2026-11-11 후속: [XmlCodeMask·XmlWbsCodes](../xml-code-masks.md)를 추가했다. XmlLookup의 마스크/전체 수준 필수와 GUID 값 생성, PlanInput의 별도 WBS 설정/객체 코드 소유 참조를 보존한다. 생성은 기존 코드를 재번호하지 않으며 저장 형식은 조건부41이다.

2026-11-10 Start/Finish는 native 독립 형식으로, Date는 기존 날짜 또는 시각 값으로 저장한다. 일시는 시간대 없는 civil time과 소수 초6자리까지 보존하며 실제 일정 제약으로 채택하지 않는다. 압축 GUID 날짜는 6초 단위로 다루고 공유 날짜 사전은 필드 역할과 값 사전을 구분한다. [계약](../xml-date-time-fields.md).

2026-11-10 `CustomFieldType`은 Flag·Cost·Duration을 추가한다. Flag 값은 1/0, Cost는 현재 통화의 최소 단위로 정확히 표현되는 주단위 문자열, Duration은 분 수치와 경과/추정 접미사를 보존한다. 금액은 비교/정렬과 숫자 보고서·통화 전환에 연결하되 원가/공수에 합산하지 않는다. [계약](../xml-additional-field-types.md).

2026-11-10 `CustomField.xmlLookup`은 외부 목록의 값 형식·테이블/필드 GUID·다중 선택 한도·leaf-only/기본값과 `XmlLookupValue`의 옵션 ID·외부 값 ID/GUID·원시 값·설명·부모 옵션 ID를 보존한다. Choice의 다중 값은 정렬된 옵션 ID를 길이 접두사 문자열로 저장하며 표시 시 이름을 결합한다. 관측된 열린 목록 직접 값은 `listed=false`로 외부 참조와 구분한다. [계약](../xml-lookup-references.md).

2026-11-03 `PeriodActualEntry`는 PlanInput 밖의 불변 제출 사실이다. 작업+자원+날짜 키, 원래 자원 종류, 공수/원가 nullable 값, 상태일·UTC·작성자/사유·원본/revision·이전 ID와 누계 `CostEntry` ID를 보존한다. `PeriodActualSubmission`은 명시 누계와 여러 날짜 행을 함께 검증/게시한다. 누계는 AC의 단일 출처이고 원장은 포함 배분이며 기준선·진척/잔여 네트워크를 변경하지 않는다. [계약](../period-actuals.md).

2026-11-03 시간축 Usage는 [계산 계약](../timephased-usage.md)에 따른 파생 조회다. `UsageResult`는 불변 입력에서 선택 기간·계획/기준선/잔여의 배정/고정비별 정수 공수·원가와 작업 누적 실제원가를 구성한다. 작업/자원 계층은 같은 고유 명세를 집계한다. 새 영속 엔터티·파일 schema·실제공수 이력은 추가하지 않으며 실제 누계의 기간/자원 배분은 추정하지 않는다.

상태: PROPOSED. DB나 저장 기술의 선택이 아니다.

## 2026-11-02 작업 분류·보기 구현 모델

`PlanInput.customFields`는 ID·이름·형식·단위·목록 옵션 ID/이름, `codeTypes`는 종류 ID와 코드 노드 ID·이름·부모 ID를 갖는다. `Task.attributes`는 `field:<ID>` 또는 `code:<종류 ID>`와 정규화된 값/옵션 ID/코드 ID를 연결한다. 값 부재, 빈 문자열, 숫자 0은 다르다. 기준선/평준화 시나리오의 PlanInput에도 사전과 값이 동결된다.

`Project.savedTaskViews`는 안정 ID·이름과 WBS/검색/기본 필터·정렬/분류 필터/그룹 키·선택 경로/분류 정렬/열 ID별 배치를 보존한다. 현재 보기 적용은 세션 상태이며 저장된 보기 자체만 편집 이력으로 관리한다. 자세한 제한·파일 매핑은 [분류·보기 계약](../task-classification.md)을 따른다. 이 절 이후의 표는 전체 목표의 개념 모델이며 전부 구현됐다는 뜻은 아니다.

| 객체 | 주요 필드·관계 | 불변조건 |
|---|---|---|
| Workspace | ID, 구성원, 정책 | 프로젝트별 접근 경계 |
| Project | ID, 명칭, 시간대, 계획 시작, 상태일, 기본 달력 | 표시명과 ID 분리 |
| WBSNode | ID, projectId, parentId, 코드, 순서 | 순환 금지, 작업과 집계 구분 |
| Activity | ID, WBS ID, 유형, 기간, 달력, 제약, 입력 방식 | 요약/WBS와 실행 작업 구분 |
| Dependency | 선행/후행 ID, FS/SS/FF/SF, lag, lagUnit, lagCalendarPolicy | 자기참조·순환 진단 |
| Calendar | ID, 버전, 시간대, 주간 근무구간, 예외 | 겹침·빈 근무·DST 정책 명시 |
| Resource | ID, 종류, 역할, 역량, 달력, 기간별 가용량 | 단가/등급≠생산성 |
| Assignment | activityId, resourceId, 배정 곡선, 작업량, 단위 | 작업량·기간·배정 단위 구분 |
| Rate/CostItem | 적용기간, 통화, 단가, 고정비, 발생 기준 | 통화 변환일·환율 근거 보존 |
| Baseline | ID, 승인자, 입력·결과 스냅샷, 생성일 | 수정 대신 새 버전 |
| ProgressEntry | 상태일, 실제 시작/완료, 작업량, 잔여량, 진행률 유형 | 실제/추정 출처 명시 |
| Forecast | 입력 버전, 정책 버전, 계산 결과 | 실적·기준선 덮어쓰기 금지 |
| CalculationRun | ID, 입력 해시, 엔진 버전, 진단, 결과 | 동일 입력 재현 가능 |
| Scenario | 원본 버전, 변경 집합, 비교 결과 | 실제 계획에 명시적 반영 |
| Risk/Issue | 영향 대상, 확률/영향, 담당자, 상태 | 위험 확률과 확정 이슈 구분 |
| AuditEvent | 행위자, 시각, 대상, 변경 전후, 사유 | 권한 있는 열람과 보존 정책 |
| ExternalMapping | 외부 형식·버전·원본 ID·내부 ID | 누락·변환·손실 추적 |

## 계획·기준선·실적·예측
계획은 현재 승인/편집 대상 입력이다. 기준선은 특정 시점의 승인 비교 기준이다. 실적은 발생한 사실과 보고된 측정값이다. 예측은 상태일과 잔여 입력으로 계산한 미래 결과다. 네 층은 별도 ID/버전으로 연결한다.
삭제·병합·WBS 이동이 기준선과 감사 참조를 깨뜨리지 않게 tombstone 또는 이력 표현을 검토한다.
동시 편집은 객체 버전 충돌을 드러내고 무음 덮어쓰기를 금지하는 방향을 제안한다.

## 수치 표현
기간·작업량은 단위가 붙은 값으로 저장한다. 작업일을 고정 8시간으로 환산하지 않는다. 날짜와 실제 순간, 현지 근무 시각을 구분한다. 금액은 통화와 소수 정밀도를 갖는다. 표시 반올림이 후속 계산 입력이 되지 않도록 한다.
진척률에는 duration/work/physical 유형과 산식·측정 주체가 필요하다. 물리 진척은 원가 또는 작업량 비율로 자동 대체하지 않는다.

## 평준화 채택 설정 (2026-11-03)

`PlanInput.planLeveling`은 계획 선택을 기준선/시나리오와 동결한다. `Project.remainingLeveling`은 상태일의 잔여 예측에만 적용한다. 우선순위·직렬 정책·탐색일·사유·UTC·원본 ID/revision을 저장하고 설정을 제거한 입력으로 재생한다. 일반 계획 편집이며 별도 불변 결정 원장은 아니다. 입력 변경 무효화·실적 보존·schema12는 [채택 계약](../leveling-adoption.md)을 따른다.

진척 정책 선택은 Project.progressPolicy의 선택적 설정이다. 채택 기록은 현재 설정의 근거이며 별도 불변 승인 원장이 아니다. 작업/실적 수정 뒤에도 유지하고 재계산하며 상태일 부재 시 비활성으로 보존한다. 기준선의 PlanInput에는 넣지 않는다. [정책 채택 계약](../progress-policy-adoption.md).

LevelingScenario는 PlanInput 외부에 선택적 progressPolicy를 동결한다. 없으면 기존 기본 잔여 논리 유지, 있으면 해당 정책의 ID·담당자·사유·UTC·검토 출처로 재생한다. Project의 현재 정책과 현재 평준화 채택은 함께 존재할 수 있다. 정책 변경/해제는 기존 평준화를 해제하고 입력 수정에 의한 평준화 무효화에 정책 변경도 포함한다. [조합 계약](../progress-policy-leveling.md).

ScheduleResult의 levelingFloatCalculated·levelingFloatReason·resourceOrderLinks는 비영속 파생 정보다. 원본 Dependency와 구분하며 기준선 TaskSchedule의 수치는 승인 엔진1.0~1.8에 따라 재현한다. 엔진1.3~1.8의 TaskSchedule 최후 날짜는 작업별 증인 값이며 동시에 실행할 공통 일정이 아니다. 후보 집합·고정/이동 가능 작업 수와 생략된 현재 날짜 증인도 파생 정보다. 1.5 이상의 후보 결합 단위와 자원 수요 포괄 범위·진단 미리보기, 1.6의 가역 제외 상태·끝점 캐시·인증 공유/계수와1.7의 점별 필수 수요·연쇄 지지/부하 전파와1.8의 비용 추정·실패/효과·재시도 상태는 세션 파생 정보이며 입력에 추가하지 않는다. ResourceOrderLink.releaseAt/acquireAt는 실제 인계 시각이며 원본 관계나 프로젝트 파일에 저장하지 않는다. [후진 계약](../leveling-float.md).

ScheduleResult.levelingWitnesses는 worker가 성공한 엔진1.4~1.8 계산에서 제공하는 `shared_ptr<const LevelingFloatWitnesses>`다. 작업별로 선택 작업 ID와 변경된 동료의 LevelingWitnessDates(ID/시작/완료/잔여 시작)를 보존한다. 선택 작업은0여유라도 포함한다. 생략된 작업은 현재 날짜로 재구성하고 실제 날짜는 원본 상태일 원장으로 확인한다. 1.5 이상의 진단 미리보기 생략 여부와 무관하게 전체 구조화 날짜를 제공한다. 결과 사본 사이에 불변 증인을 공유하며 입력·기준선 저장 데이터에는 추가하지 않는다. [탐색 화면 계약](../leveling-witness.md).

2026-11-05 객체 사용자 필드: PlanInput.projectFields/resourceFields는 범위별 CustomField 사전이며 projectAttributes와 Resource.attributes에 field:<ID> 값을 둔다. 작업 customFields/Task.attributes 및 작업 코드·보기는 종전 범위를 유지한다. 같은 ID는 범위로 구분한다. PlanInput 복사·기준선/시나리오·undo·복구가 사전과 값을 함께 보존한다. [계약](../object-custom-fields.md).

2026-11-05 native Assignment.periods는 `{fromMinute,toMinute,units}`의 정렬·비중복 반열린 작업 근무분 구간이다. 빈 구간은 기본 units, 0%는 명시 수요 없음이다. 곡선 프리셋은 이 배열로 물질화하고 계획/기준선/시나리오가 같은 입력을 동결한다. 실제 실적 원장과 분리한다. [정수 면적·잔여 꼬리·한도](../assignment-periods.md).

Project.printPreferences/networkPositions는 PlanInput 밖의 표시 정보다. schema18에서 정수 설정과 안정 작업 ID의 좌표를 보존하고 dirty/undo/복구를 지원한다. 기준선/시나리오 입력과 실적/비용·평준화 결정 원장은 바꾸지 않는다. [계약](../print-network-extension.md).
