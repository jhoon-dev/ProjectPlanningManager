# native 기준선 상세 공수·원가 XML 내보내기

2026-11-15 · NBT-01~06. 일반 **Project XML 내보내기…**와 **표준 실적 포함 Project XML 내보내기…**에 native 기준선의 상세 시간분포를 함께 쓴다. 별도의 채택이나 원장 생성은 하지 않는다.

- NBT-01: native 승인 v1~v11을 Baseline0~10에 대응하고, 동결 PlanInput과 저장된 승인 일정으로만 분포를 계산한다. 현재 계획의 날짜·달력·단가·배정률·진척으로 재평가하지 않는다. 이미 외부 출처를 채택한 번호는 그 명시 값/시간분포를 그대로 출력하며 native 자료로 보충하지 않는다. v12 이후와 전체 이력은 `source.ppm`에 보존하고 진단한다.
- NBT-02: 공수는 native 실행 배정 구간의 정수 배정률×civil 분을 사용한다. 작업/자원 달력의 교집합·휴일·중단·사용자 곡선·고정 공수/단위의 마지막 부분 공수를 따른다. 같은 범위의 겹치는 배정을 구간 경계에서 합산해 일정한 공수 밀도의 비중첩 블록을 만들고 내부 유휴 구간은 명시0으로 쓴다. `Unit0`의 Value는 해당 구간 총공수이며 소수6자리 분에 정확히 대응하는 ISO PT 값이다. native 공수는 100u=1자원·분이다. 구간 합계와 동결 총공수를 정확히 대조한다.
- NBT-03: 원가는 native `plannedCostDays`의 배정별/일별 단가·반올림을 먼저 적용한 뒤 합산한다. 고정비의 시작/완료/균등 인식, 마일스톤 고정비와 자재/경비를 작업·WBS·프로젝트에 포함한다. 자원·배정에는 배정 원가만 포함하고 직접비를 가상 자원으로 만들지 않는다. `Unit2`, 당일00:00~다음 날00:00의 civil-day 버킷이며 기존 XML 금액 전송 규칙(주요 통화 단위의1/100)을 사용한다. 예: USD12.34→XML1234, JPY1234→XML123400. 알려진0은0이며 미확정 일별 원가는 Value를 생략한다. 알려진 총원가와 일별 합계는 정확히 대조한다.
- NBT-04: 작업·프로젝트 UID0·WBS·자원·배정의 부모 바로 아래에 표준 `TimephasedData`를 쓴다. 자원 `<Baseline>` 안에 시간분포를 넣지 않는다. Type은 번호·범위·Work/Cost에 맞는4/5·7/8·9/10과16~75이며 UID는 현재 XML 부모 UID로 재연결한다. 일반 내보내기와 실적 내보내기의 기준선 분포는 같다. 실제/잔여 시간분포의 Type1/2/6과 구별한다.
- NBT-05: 프로젝트/자원 전체 합계에는 현재 삭제된 동결 작업의 기여도 남긴다. 삭제된 객체를 현재 XML 행으로 복원하지 않으며 원본/생략 진단을 남긴다. WBS는 기존 기준선 요약 참조와 같은 현재 구성원을 사용한다. 동결 기준선이 없는 현재 구성원이 있거나 빈 요약이면 완전한 상세 분포를 만들지 않고 진단한다. 현재 출력 객체 합계와 동결 프로젝트 전체 합계가 다를 수 있다.
- NBT-06: 부모 위치의 기준선 시간분포도 번호별 후보로 읽어 명시 채택·기존 문서 병합·재내보내기에 보존한다. 스칼라 Baseline이 없으면 분포에서 총량을 추정하지 않는다. 잘못된 부모 UID/범위, 같은 종류의 중복/겹침, Baseline 내부와 부모 위치의 중복은 해당 번호를 거절한다. 새 `timephasedOutside` 위치 정보가 저장될 때만 조건부 schema77이며, 과거 내부 시간분포의 schema75 및 병합 schema76 최소 writer는 유지한다. 현재/승인/시나리오/일정 채택/통화·ID 이관/병합 이력 전체 사본에서 버전을 검사한다.

시간분포는 전체 출력 기준100,000개, 구간 경계/일별 집계는 native 기준선당400,000개 이내다. 기존 XML16MiB·노드/경로·native 저장 한도도 적용한다. 초과하면 잘라서 출력하지 않고 작업을 거절한다. 준비/취소는 입력을 변경하지 않으며 게시 전 오류가 나면 새 교환 폴더에 부분 결과를 남기지 않는다. 분포를 native 실제 원장·EVM이나 새 승인본으로 자동 채택하지 않는다.

원가의 시간 해상도는 native 계산이 정의하는 일별 금액이다. 그보다 세밀한 분 단위 원가를 임의로 배분하지 않는다. 실제 MS Project/P6 실행 환경이 없어 제품 재계산/재저장 동등성은 미검증이다.

표준 근거: Microsoft [TimephasedDataType](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/xml-schema-for-the-timephaseddatatype-complex-type?view=project-client-2016), [Resources 스키마](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/xml-schema-for-the-resources-element?view=project-client-2016), [Baseline 요소](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/baseline-element?view=project-client-2016). 구간 총량 표현은 기존 계획 곡선 출력 및 고정 MPXJ16.10.0의 [MSPDIWriter](https://github.com/joniles/mpxj/blob/v16.10.0/src/main/java/org/mpxj/mspdi/MSPDIWriter.java)의 total amount 표현을 따른다.

[검증 기록](validation/native-baseline-timephased-2026-11-15/README.md): 집중48/48·원가/Usage3/3·파서988개, 독립98시간분포/7,273분 지점과 MPXJ 배정6건.
