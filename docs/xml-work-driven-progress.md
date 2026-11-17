# 공수 기반 작업의 XML 기간 진척 병합

2026-11-13 후속: [다일 실적 비례 배분](xml-multiday-proportional-actuals.md)을 별도 검토로 추가했다. 아래 다일 배분 잔여는 명시 구간 총량/추정 확인과 두 가지 분 가중 기준을 사용하는 새 지원 범위로 대체한다.

2026-11-13 · XWP-01~06. **파일 → XML 작업 진척을 현재 문서에 병합…**에서 현재 native 작업이 공수 기준인 경우도 지원한다. 기존 [작업 진척 병합](xml-progress-merge.md)에 명시 잔여 공수 검토를 연결한다.

- **XWP-01 원본·연결:** 기존과 같이 행은 미선택·대상 미지정으로 시작한다. 작업 ActualStart/Finish와 명시 ActualDuration/RemainingDuration을 읽고, 공수 기준 native 대상에는 작업 RemainingWork를 추가로 읽는다. 공수 고정 또는 현재 `usesWork` 판정에 해당하는 작업이 대상이며 XML 작업 유형으로 현재 계획의 유형을 바꾸지 않는다. 원본을 다시 파싱해 진척 후보·잔여 공수/오류·계획·SHA-256을 재대조한다.
- **XWP-02 명시 잔여:** 진행/미착수 공수 작업에는 양수의 명시 잔여 공수가 필요하다. 완료 날짜가 있는 작업은 잔여 공수 미보고 또는 명시0을 허용하며 미보고를 0으로 저장하지 않는다. 미착수이며 계획 공수와 실제/잔여 기간이 모두0인 작업의 명시0도 보존한다. 날짜·완료·실제/잔여 기간의 기존 일관성 검사를 유지한다. 누락·음수·한도 초과·정확히 표현할 수 없는 값과 완료/양수 잔여의 모순은 전체 선택 batch를 거절한다. 100u=1자원·분, 1u=0.6초이며 기간·단가·배정률로 공수를 추정하지 않는다.
- **XWP-03 기간과 예측:** 수신 실제/잔여 분은 보고된 관측값으로 원장에 보존한다. 예측 잔여 기간은 현재 native 작업/자원 달력·기간별 배정률·중단·날짜별 잔여 목표와 명시 잔여 공수로 별도 계산한다. XML 잔여 분을 그대로 예측 기간으로 사용하거나 현재 계획 공수·배정률을 역산 변경하지 않는다. 서로 다른 달력·주말·곡선·마지막 분의 정확한 공수 배분을 기존 실행 모델로 처리한다.
- **XWP-04 계획 스냅샷·원자성:** 보고 당시 계획 기간은 현재 공수 계획의 계산 결과를 사용한다. 계산에 사용하지 않는 nominal 기간이나 외부 XML Duration을 복사하지 않는다. batch당 한 번 계획을 계산하고 선택한 공수 작업의 스냅샷만 구한다. 계획이 유효하지 않거나 기간 한도를 넘으면 스냅샷은 미설정으로 보존한다. native 계획·실제 공수/원가·기존/미래 진척·날짜별 잔여 목표는 수정하지 않는다. 현재 상태일·이전 보고·세션/revision·마감·읽기 전용·worker 취소를 검사한 뒤 한 revision으로 추가한다.
- **XWP-05 조회·이력:** 검토 화면에 현재 작업의 계산 기준/계획 공수, 현재/수신 잔여 공수의 정확한 u 값과 미보고/오류를 표시한다. 공수와 보고 기간/예측 기간의 차이를 안내한다. 기존 XML 진척 감사 표에 공수 보고가 있으면 ‘보고 당시 계획 분’과 ‘잔여 공수 u’를 추가한다. 원본 해시/UID·작성자/UTC·이전 보고와 기존 정정·미래 이력을 유지하며 동일 출처 재수입은 후속 정정을 되돌리지 않는다. native ID 이관 뒤에도 중복 검사가 유지된다.
- **XWP-06 저장·검증:** 기존 ProgressEntry의 optional remainingWorkUnitMinutes와 planDurationMinutes를 사용하며 새 영속 형식은 추가하지 않는다. 기본 공수 지원은 기존 schema9, 실제 일별 원장이 있는 단순 표본은 schema11, 이번 공수 곡선 표본은 기존 schema24를 사용한다. 지원 상한51은 유지한다. 저장/복구·XLSX/PDF·원본 파일 보존·취소/변경·마감/ID 이관과 독립 분 단위 계산을 검사한다. [검증 기록](validation/xml-work-driven-progress-2026-11-13/README.md).

기간 기준 대상은 기존 날짜/기간만 채택하며 RemainingWork가 없거나 잘못되어도 작업 기간 병합을 막지 않는다. 명시 잔여 공수 채택은 현재 native의 공수 기준 대상에만 적용한다. 기본 XML 계획 가져오기와 새 사본 진척 채택에서 공수 기준 계획으로 자동 전환하거나 잔여 공수를 자동 채택하지 않는다. 작업 ActualWork·ActualCost와 배정/자원 누계를 이 흐름에서 원장에 기록하지 않는다. 실제 일별 원장은 [별도 검토](xml-period-actual-adoption.md)로 처리한다.

날짜별 잔여 목표와 새 총 잔여 공수가 충돌하는 경우 목표를 자동 재배분하거나 삭제하지 않는다. 기존 예측 계산의 검토 사유를 확인하고 명시 조정한다. 기존 기간 일관성 계약에 맞지 않는 원본 기간을 공수로 보정하지 않는다. 원본 XML 바이트는 native 파일에 내장하지 않으므로 원본을 함께 보관한다. Microsoft Project/P6 실제 실행·재계산·재저장, 추가 함수/시간대와 다일 실적 비례 배분은 잔여다.

입력 의미는 Microsoft의 [RemainingWork](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/remainingwork-element?view=project-client-2016), [ActualDuration](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/actualduration-element?view=project-client-2016), [RemainingDuration](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/remainingduration-element?view=project-client-2016)를 따른다. 수신 값의 현장 측정 사실을 인증하거나 외부 제품 계산과의 동등성을 보장하지 않는다.

[채택 준비](../src/persistence/XmlProgress.cpp), [원장 반영](../src/application/Document.cpp), [C++/UI 검사](../tests/XmlWorkProgressTests.cpp), [독립 계산](../scripts/verify-xml-work-progress.py).
