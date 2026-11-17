# XML 일별 실적의 여러 구간 합산

2026-11-13 후속: [다일 실적 비례 배분](xml-multiday-proportional-actuals.md)을 별도 검토로 추가했다. 아래 다일 제한은 기본 일별 합산의 범위이며, 원본 총량/추정 확인·기준 선택·가중치와 정수 배분 근거를 사용하는 새 지원은 별도 계약을 따른다. 같은 종류의 다일/일별 원본 겹침은 중복 채택을 막는다.

2026-11-13 후속: [표준 실적 XML 내보내기](xml-standard-actual-export.md)를 추가했다. 아래의 표준 쓰기 잔여는 이 지원 범위로 대체하며 상태일 당일의 전체 날짜 버킷은 실제 시각 구간과 구별해 다시 채택할 수 있다. 원래 intraday 구간을 복원하지 않고 native 일별 값을 출력한다.

2026-11-12 · XAF-01~05. [XML 일별 공수/원가 채택](xml-period-actual-adoption.md)의 같은 배정·날짜에 여러 명시 구간이 있는 입력을 지원한다.

- **XAF-01 합산 범위:** 같은 source assignment UID·날짜의 실제 공수(Type2)와 실제 원가(Type6)를 각각 합산한다. 기존 일별 Unit2·civil 분 정밀도·통화/정수 수량 계약을 유지한다. 같은 종류의 구간은 `[Start, Finish)` 기준으로 겹치지 않아야 한다. 끝과 다음 시작이 같은 경계는 허용하며, 입력 순서와 구간 사이의 빈 시간은 합계에 영향을 주지 않는다. 서로 다른 종류(공수와 원가)는 같은 시각 범위를 사용해도 된다. 원본의 명시 수치만 더하고 구간 길이·단가·배정률을 곱하지 않는다.
- **XAF-02 오류와 한도:** 같은 종류의 정확한 중복·부분 겹침·포함 관계·길이0은 거절한다. 일자를 식별할 수 있는 잘못된 시각/정밀도/단위/수치가 있으면 그 일자 전체를 오류로 표시하며 정상 구간만 떼어 채택하지 않는다. 구간별 값과 일별 합계 모두 native 공수/금액 한도를 지켜야 한다. 합계 초과 시 부분 수치는 채택 후보로 남기지 않는다. 다른 명시 일자는 별도 선택할 수 있다. 여러 날짜를 포괄하는 값을 자동 배분하지 않는다.
- **XAF-03 검토·출처:** 기존 검토 화면에 공수·원가의 구간 수와 정확한 합계를 표시한다. 선택/대상 배정은 미지정으로 시작하며 현재 관측·수신 합계·누계 AC를 비교해 명시 반영한다. 원본 바이트와 후보의 합계/구간 수를 재대조한다. source SHA-256·배정 UID·native 대상·날짜와 원본 파일로 각 합산의 구성 구간을 추적한다. 원본 XML 파일은 계속 함께 보관해야 한다.
- **XAF-04 native 연결:** 여러 구간도 한 배정·날짜당 하나의 PeriodActualEntry가 된다. 기존 부분 미보고 보존, 작업 누계 AC/날짜별 배분 대사·이전 보고 연결·동일 출처 중복 방지·후속 정정·마감/취소/변경 검사를 그대로 사용한다. 전체 batch는 한 revision으로 반영하며 기존 계획/원장/파일을 보존한다. native 저장 구조·기본 schema11·지원 상한51을 변경하지 않는다.
- **XAF-05 검증:** 맞닿는 구간·역순 입력·빈 시간·다른 종류의 같은 범위·0/미보고·0.6초 공수/센트 금액·중복/겹침/잘못된 시각·정확한 한도/초과를 검사한다. 일별 원장 7건·누계4건과 후속 정정13건·누계8건, 원장/복구·동일 출처/새 원본·누계 부족/worker 취소/변경, 기존 일별/작업 진척 흐름과 작은 화면을 확인한다.

입력 요소의 의미는 Microsoft [TimephasedData](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/timephaseddata-element?view=project-client-2016)·[Type](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/type-element-multiple-parents?view=project-client-2016)·[Unit](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/unit-element?view=project-client-2016)를 따른다. 이번 지원은 단일 날짜 내 명시 값의 합산이며 날짜가 없는 누계, 다일 반복/배분이나 다른 시간 단위의 비례 계산을 의미하지 않는다. 표준 실적 XML 쓰기·공수 기반 작업 기간 병합·추가 함수/시간대와 실제 MS Project/P6 재계산/재저장은 잔여다.

[C++/UI 검사](../tests/XmlPeriodFragmentTests.cpp), [독립 판독](../scripts/verify-xml-period-fragments.py), [검증 기록](validation/xml-daily-actual-fragments-2026-11-12/README.md).
