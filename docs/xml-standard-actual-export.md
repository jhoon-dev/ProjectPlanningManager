# 표준 실적 Project XML 내보내기

2026-11-13 후속: [다일 비례 배분](xml-multiday-proportional-actuals.md)으로 기록한 날짜별 수량도 출력한다. conversion.json에 공수/원가별 추정 기원·원래 배분 보고 ID/사유를 남기며 부분 수동 정정 뒤에도 그대로 이어받은 척도의 기원을 보존한다. XML만으로 추정 구분을 복원할 수 없으므로 source.ppm·영수증을 함께 보관한다.

2026-11-13 · XAE-01~06. 파일 메뉴의 **표준 실적 포함 Project XML 내보내기…**에서 현재 상태일의 보고를 검토하고 새 교환 폴더를 만든다. 기존 계획 전용 내보내기는 기본 동작을 유지한다.

- **XAE-01 시점과 이력:** 명시된 현재 상태일이 필요하다. 작업 진척·누계 원가·배정/날짜별 원장의 상태일 이하 최신 보고를 사용하며 같은 상태일은 마지막 정정을 택한다. 미래 보고·이전 보고·원래 계획을 포함한 전체 native 문서는 `source.ppm`에 보존한다. 현재 문서·원본 파일·revision·undo를 변경하지 않는다.
- **XAE-02 작업 표준 필드:** 보고된 ActualStart/ActualFinish와 명시 ActualDuration/RemainingDuration, 저장된 RemainingWork를 출력한다. 보고 작업의 XML Duration은 실제+잔여 기간이며 원래 계획 기간과의 차이를 미리보기에 표시한다. 작업 ActualCost는 최신 누계 AC다. 날짜별 원가를 누계에 더하지 않는다. 실제 날짜·잔여 공수·진척률·초과 공수/원가를 추정하지 않는다.
- **XAE-03 배정과 자원:** 현재 배정별 일별 보고의 실제 공수/원가 합계를 ActualWork/ActualCost로 출력한다. 해당 척도에 명시 미보고가 하나라도 있거나 배정의 보고가 없으면 그 합계는 생략한다. 작업/자원 ActualWork는 모든 현재 배정의 합계를 알 때만 출력한다. 이 합계는 관측된 보고의 합이며 모든 날짜의 보고가 완결됐다는 증명이 아니다. 자원 누계 원가와 배정 실제 시작/완료는 출력하지 않는다.
- **XAE-04 일별 정밀도:** 배정의 TimephasedData Type2(실제 공수)·Type6(실제 원가), Unit2(일)와 `00:00~다음 날00:00` 반열린 날짜 버킷을 쓴다. 날짜 버킷은 원장 수치의 일자를 표현하며 실제 착수/완료 시각을 뜻하지 않는다. 100u=1자원·분, 1u=0.6초를 정확히 변환하고 통화의 최소 단위를 유지한다. 명시0은 출력하며 미보고는 생략한다. 상태일 당일 버킷은 기존 수동 채택 흐름에서 허용하지만 미래 날짜·상태일 이후의 실제 시각 구간은 거절한다. 계획 Type1 시간대 분포와 잔여=전체계획인 배정 stub는 실적 모드에서 생략한다.
- **XAE-05 검토와 게시:** 미리보기에 기준일·원래/투영 기간·실제 날짜·일별 공수와 금액·누계/배분/미배분 잔액을 표시한다. `project.xml`, `source.ppm`, `conversion.json`을 새 폴더에 원자적으로 게시하고 기존 폴더를 덮어쓰지 않는다. 변환 기록에는 안정 ID→XML UID 매핑·선택 원장 ID·정수 수치·XML 해시·전체 native 입력 해시·한계 진단을 담는다. 게시 전 native로 XML과 기록 전체를 다시 생성해 대조하고 미리보기 이후 문서 변경·취소를 검사한다.
- **XAE-06 검증:** 최신/동일 상태일 정정·미래 이력 보존, 0/미보고·누계/배분 차이·0.6초/센트·미보고 합계 억제·재입력 날짜 버킷·시각 초과·계획 곡선 분리·파일/문서 불변·변조/취소/오래된 미리보기·작은 화면을 검사한다. native 저장 구조는 바뀌지 않으며 기본 표본 schema11·최대 지원51을 유지한다. [검증 기록](validation/xml-standard-actual-export-2026-11-13/README.md).

현재 native에 관측된 자원 없는 작업 직접 배분이나 현재 계획에서 제거된 배정의 알려진 실적은 표준 배정으로 표현할 수 없어 실적 내보내기를 거절한다. 먼저 해당 배정을 명시 복구하거나 기존 계획 전용 내보내기를 사용한다. 삭제된 배정을 임의의 배정률로 만들어내지 않는다. 합계·기간 한도나 다음 날 경계를 표현할 수 없는 입력도 실패하며 부분 파일을 게시하지 않는다.

원가 wire 단위는 통화의 표시 금액×100이다. 예를 들어 USD 13.50은 `1350`, KRW 1,350은 `135000`으로 출력한다. 기존 UDF Cost와 같은 정확한 변환을 실제 원가 scalar·Type6 읽기/쓰기에 공통 적용한다. 이전 일별 채택 초안의 표시 금액 단위 해석도 이 규칙으로 바로잡았다. 기존 native 원장은 자동 재환산하지 않으며, 이전 빌드에서 XML 원가를 채택한 문서는 원본과 대사해 명시 정정해야 한다. MPXJ의 [통화 변환 구현](https://github.com/joniles/mpxj/blob/v16.10.0/src/main/java/org/mpxj/mspdi/DatatypeConverter.java)과 독립 판독으로 달러/센트 수치를 확인한다.

작업 Duration 투영과 계획 기준 사용자 필드/수식 결과는 다른 계산 범위일 수 있다. 외부 제품이 재계산하면 그 결과가 달라질 수 있다. 기준선·시나리오·단가 이력·출처/정정·마감·통화 이관·수동 중단 등 전체 native 의미를 표준 XML만으로 복원하지 않는다. 재입력은 기본 계획 변환이며 실제 원장 채택은 기존 별도 검토 절차를 따른다. 미보고 누계의 자동 복구, 날짜 없는 수치의 배분, 다일 비례 계산·추가 시간대·공수 기반 작업 기간 병합과 실제 MS Project/P6 재계산/재저장 검증은 잔여다.

표준 요소는 Microsoft [Task 스키마](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/xml-schema-for-the-tasks-element?view=project-client-2016), [Assignment](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/assignment-element?view=project-client-2016), [TimephasedData](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/timephaseddata-element?view=project-client-2016), [Project](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/project-element?view=project-client-2016)를 기준으로 한다. 명시 원가는 EditableActualCosts=1·SpreadActualCost=0으로 출력한다. 자체 재입력과 MPXJ 판독은 실제 Microsoft Project/P6 왕복 실행과 다르다.

[투영](../src/persistence/XmlActualExport.cpp), [표준 writer](../src/persistence/ProjectXml.cpp), [C++/UI 검사](../tests/XmlActualExportTests.cpp), [독립 판독 검사](../scripts/verify-xml-actual-export.py).
