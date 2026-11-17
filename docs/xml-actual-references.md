# XML 계산식의 실적 일정 참조

2026-11-12 후속: [XPA-01~06](xml-progress-adoption.md)의 새 사본 작업 진척 명시 채택을 추가했다. 아래의 과거 XML 실적 미채택 표기는 기본 계획 변환에는 유지하며, 선택한 실제 날짜·기간의 새 사본 채택은 이 지원 부분으로 대체한다. 기존 문서 원장 병합·실제 공수/원가 채택은 계속 잔여다.

2026-11-12 후속: 실제 기간과 보고 공수의 지원 부분은 [XMLQ-01~05](xml-actual-quantities.md)를 따른다. 아래의 미지원 표기는 이 날짜 참조 증분 당시의 범위다.

2026-11-11 · XMLA-01~05.

XML 계산식에서 `Actual Start`·`Actual Finish`를 참조한다. 데이터는 현재 프로젝트의 실적 원장이며, 상태일 이하의 최신 보고를 선택한다. Microsoft의 [Actual Start 필드](https://support.microsoft.com/en-us/project/actual-start-fields)와 [XML ActualStart 요소](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/actualstart-element?view=project-client-2016)가 사용하는 실제 착수 일시와 계획 날짜의 구별을 따른다. 이 앱의 상태일/원장·요약 정책을 구현한 지원 부분이며 외부 제품과의 계산 동등성을 뜻하지 않는다.

| 참조 | 범위와 값 |
|---|---|
| `[Actual Start]`, `[ActualStart]` | 작업의 실제 시작. 프로젝트 필드에서는 전체 작업의 최초 실제 시작 |
| `[Actual Finish]`, `[ActualFinish]` | 작업의 실제 완료. 프로젝트 필드에서는 전체 작업이 완료 실적을 가진 경우의 최종 완료 |
| `[Project.Actual Start]`, `[Project.ActualStart]` 및 Finish 대응 이름 | 네 범위에서 프로젝트 전체 실적 날짜 |
| `[Task.Actual Start]`, `[Task.ActualStart]` 및 Finish 대응 이름 | 배정 필드에서 소유 작업의 실적 날짜 |

배정 자체의 Actual Start/Finish와 자원의 실제 기간은 별도 원장이 없어 실행하지 않는다. 배정에서 Task 접두사는 작업의 실적을 뜻한다. 계획·예측의 Start/Finish와 실제 날짜는 구별한다. Actual Duration/Work, Remaining/Baseline/Early/Late 등 다른 참조는 이번 범위에 포함하지 않는다.

```text
ProjDateDiff([Actual Start], [Actual Finish])
DateDiff("n", [ActualStart], [ActualFinish])
IIf(IsNull([Actual Finish]), "진행", "완료")
```

첫 식은 [근무 달력 함수](xml-calendar-functions.md)로 근무 분을 계산한다. 두 번째 식은 휴게·야간을 포함한 달력 분이다. 기록된 실제 기간 입력을 대체하거나 추정하지 않는다. 미착수까지 구분하려면 Actual Start의 Null도 함께 검사한다.

## 상태일과 미설정

progressAsOf와 같은 기준을 사용한다. 보고 상태일이 현재 상태일 이하인 항목 중 가장 늦은 상태일을 선택하고, 같은 상태일이면 나중에 추가한 정정 보고를 선택한다. 작성 시각/시스템 현재 시각으로 선택하지 않는다. 미래 상태일 보고는 제외하며, 늦게 추가한 과거 정정이 더 최신 상태일 보고를 덮지 않는다. 최신 보고가 날짜를 비웠으면 이전 보고의 날짜로 채우지 않는다.

상태일·보고·실제 시작/완료가 없으면 사유를 가진 미설정(Null)이다. 0·1900년·계획/예측·원본 필드 스냅샷으로 대체하지 않는다. 진행 중 작업에는 Actual Finish가 없으며, 잔여0인 미착수 이정표도 실적 날짜를 추정하지 않는다. 원장의 원래 날짜는 분 정밀도이며 수식의 일시는 기존 ISO 초 표현으로 정규화한다. 날짜 연산은 기존 초/소수 초 기능과 연결한다.

## 요약과 평가

WBS·프로젝트 루트·필터/그룹의 CalculationType=2 식에서 실제 시작은 포함된 작업 중 알려진 최초 시작이다. 미착수/미보고 작업이 섞여도 알려진 시작은 유지한다. 실제 완료는 **포함된 모든 작업**에 실제 완료가 있어야 표시한다. 비어 있는 그룹은 미설정이다. 완료된 작업만 선택한 필터에서는 그 범위의 완료를 표시한다. Project 접두사는 필터와 관계없이 전체 프로젝트를 참조한다. CalculationType=1의 기존 형식별 집계는 유지한다.

계산 계층은 원장을 한 번 순회해 선택한 실적을 불변 평가 문맥으로 제공한다. 도메인은 스케줄러를 재귀 호출하지 않는다. 실적 참조만 있으면 일정 계산을 요청하지 않는다. 계획의 불완전/충돌/stale/취소 결과와 관계없이 현재 원장의 유효한 실적을 표시한다. 혼합 식의 계획 날짜는 기존 일정 유효성 검사를 따른다. 진행 중 요청의 취소는 전파한다. 프로젝트 시작일 이전의 유효한 실적도 읽는다.

문맥은 정확히 같은 입력 객체에서만 사용한다. 상태일·보고/정정·입력/계산 세대가 바뀌면 문맥/캐시를 다시 만들며 native/XML 혼합 참조도 구분한다. 식 오류나 비정상 날짜는 원본 값으로 대체하지 않는다. UI 미리보기는 worker에서 실행하고 변경된 문서/세션의 결과를 적용하지 않는다.

## 보존·교환

builtin:actualstart/actualfinish와 Task/Project scope를 저장한다. 새 바인딩이 현재·기준선/시나리오·채택·통화/ID 전후 사본에 있으면 조건부 schema47이다. schema46 이하에서 새 바인딩을 거절하고 기존 최소 writer를 유지한다. 계획 기준선에는 정의만 있으며, 실적 원장 자체가 없는 PlanInput 사본에서 날짜를 만들어내지 않는다. 원장과 상태일을 포함한 시나리오·전체 사본/복구는 해당 원장 기준으로 조회한다.

기존 사용자 필드 바인딩은 예약 이름과 같아도 편집 시 안정 ID를 유지한다. 새 식에서는 표준 실적 이름이 우선한다. XML 내보내기는 예약 이름 충돌을 피하고 실제 출력 필드명/범위로 식을 다시 쓴다.

XML/XER의 사용자 필드 정적 결과도 **현재 상태일 기준 실적**이다. 계획 내보내기라는 이유로 실적을 계획 날짜로 바꾸지 않는다. 기존 XML 교환은 실적/진척 원장을 자동 채택하지 않는다. 따라서 XML 재가져오기는 식과 정적 스냅샷을 보존하지만 실제 보고가 없으면 활성 실적 참조는 미설정이다. 실행을 끄면 가져온 정적 스냅샷을 조회할 수 있다. 이 차이를 가져오기/내보내기 진단에 표시한다. 원본 상태일·실적/정정 이력은 source.ppm에 보존한다. 원장을 갖춘 native 재열기는 같은 결과를 재계산한다.

상태일·수식 설정의 undo 규칙은 유지한다. 제출 실적은 기존 원장 정책대로 append-only이며 undo로 지우지 않고 정정 보고를 추가한다. 보고서/필터/표·roll-down은 형식별 값을 사용하고 출처에 실적 기준 상태일을 표시한다. 실제 Microsoft Project/P6 재계산·재저장은 미검증이다.

[검증 기록](validation/xml-actual-references-2026-11-11/README.md), [C++ 검사](../tests/XmlActualTests.cpp), [fixture](../tests/XmlActualFixture.h), [독립 검사기](../scripts/verify-xml-actual-references.py).
