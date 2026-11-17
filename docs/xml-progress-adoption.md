# XML 작업 진척의 새 사본 원장 채택

후속: 기존 native 문서의 작업 진척 병합은 [XPM-01~06](xml-progress-merge.md)에서 별도 제공한다. 아래 계약은 새 사본 채택 동작과 당시 검증 범위다.

2026-11-12 · XPA-01~06. 기존 XML 계획 변환에 **선택한 작업 진척을 새 사본에 명시 채택**하는 흐름을 추가한다. 현재 native 문서에 외부 원장을 병합하는 기능과 구분한다.

## 입력과 선택

- XPA-01: 파일 메뉴의 XML 가져오기에서 기본값은 계획만 변환한다. 변환 확인 창에서 진척 검토를 선택하면 leaf 작업의 원본 UID→새 작업 ID, 이름·실제 시작/완료·실제/잔여 분과 누락/형식 사유를 표시한다. 프로젝트/WBS 요약 실적은 작업 원장에 중복 계상하지 않는다. 작업 선택은 모두 해제된 상태에서 시작하며 상태일·작성자·사유와 선택 행을 명시한다.
- XPA-02: 작업 `ActualStart`, `ActualFinish`, `ActualDuration`, `RemainingDuration`의 명시 값을 사용한다. 실제 날짜 없음/NA는 미설정이며 0과 구별한다. 실제/잔여 기간은 둘 다 필요하고 날짜 차이·계획 기간·진척률로 보충하지 않는다. 분 단위 ISO civil 시각과 정수 근무 분만 지원한다. 초/소수 초·시간대가 있는 날짜와 분 미만/음수 기간은 해당 후보를 거절하며 원본에 보존한다.
- XPA-03: XML `StatusDate`가 지원 형식이면 상태일 후보로 표시한다. 없거나 미지원이면 빈 값과 사유를 표시하고 사용자가 명시해야 한다. 시스템 현재 시각·현재 문서 상태일로 대체하지 않는다. 사용자가 다른 채택 상태일을 명시하면 원본 XML 상태일과 채택 상태일을 구분해 기록한다. 실제 시작/완료 순서·상태일 이후 실적·날짜 창보다 큰 실제 기간·완료와 잔여의 모순은 선택 batch 전체를 거절한다. 미착수 이정표의 명시0도 실제 완료 날짜로 추정하지 않는다.

Microsoft의 [ActualDuration](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/actualduration-element?view=project-client-2016), [RemainingDuration](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/remainingduration-element?view=project-client-2016), [StatusDate](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/statusdate-element?view=project-client-2016) 요소를 읽는다. 원본 수치가 작성 제품에서 계산된 값일 수 있으며 현장 측정 사실이나 상용 제품의 재계산 동등성을 검증했다는 뜻은 아니다.

## 보존·재가져오기

- XPA-04: 원본 XML을 다시 파싱해 계획·후보가 검토본과 같은지 대조하고 새 사본의 기존 구조 `ProgressEntry`에 한 번에 기록한다. 원본 SHA-256·UID·파일명과 사용자 사유는 불변 보고 사유에, UID/새 작업/보고 ID 연결과 상태일·작성자·UTC·선택 목록은 `conversion.json`에 남긴다. 원본 바이트는 교환 폴더의 `original.xml`로 보존한다. 게시 직전에도 원본으로 채택 결과·출처·매핑을 재생해 검사한다.
- 동일 검토본과 메타데이터로 재실행하면 같은 준비 결과를 얻으며 행을 중복 추가하지 않는다. 이미 채택한 사본·변경된 계획·기존 원장이 있는 문서는 이 API로 다시 채택할 수 없다. 같은 XML을 다시 가져오면 별도 문서 ID의 새 사본을 만들며 이전 원장과 병합하지 않는다. UID 중복은 기존 XML 파서가 거절한다. 기존 교환 폴더는 덮어쓰지 않고 임시 폴더 검증 후 원자적으로 게시한다.
- XPA-05: 검토·worker 취소와 문서/세션 변경을 감지하면 현재 문서·파일을 보존한다. 게시 후 새 사본을 열며 기존 native 파일의 관측 원장을 바꾸지 않는다. 이후 native 실적 정정은 원래 채택 보고를 수정/삭제하지 않고 `previousEntryId`로 이어지는 새 보고를 추가한다. 기존 원장·상태일의 undo 규칙, 기준선/시나리오·통화/ID 이관·복구를 재사용한다. 새 영속 속성/schema는 추가하지 않는다. 기본 입력은 기존 schema9, 실제 기간/공수 수식이 있으면 기존 schema48 등 기존 최소 writer를 따른다. 지원 상한51도 유지한다.
- XPA-06: 채택 후 실제 날짜/기간 계산식·표·예측·보고서는 기존 native 원장을 읽는다. XML 채택 작업의 진척 원장 보고서는 최초 출처와 후속 정정·미래 상태일을 포함한 전체 이력, 현재 상태일에서 선택한 보고를 구별한다. 실제 공수·원가가 보고되지 않았으면 Null이며 0이나 기간×배정률로 추정하지 않는다. native 파일만 복사하면 원본/변환 기록 동반 파일은 자동 복제되지 않으므로 출처 검증에는 교환 폴더를 함께 보존해야 한다.

## 잔여 범위

기존 native 문서의 사용자 지정 ID 매핑·중복/정정/잠금 대사를 거친 병합, 배정·자원·기간별 실제 공수/원가 채택과 실적 XML 재내보내기는 후속이다. 이번 증분은 native 원장 값을 사용자 필드로 내보내는 기존 계약을 유지한다. 추가 Project 함수·시간대 변환과 다른 GAP 잔여도 유지하며 실제 MS Project/P6 열기·재계산·재저장과 환경별 수용 검증은 미완료다.

[검증 기록](validation/xml-progress-adoption-2026-11-12/README.md), [C++ 검사](../tests/XmlProgressTests.cpp), [독립 검사기](../scripts/verify-xml-progress-adoption.py).
