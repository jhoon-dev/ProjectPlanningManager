# 외부 계산식의 Units·가용률 참조

2026-11-16 후속: [현재 부하 차감](remaining-resource-availability.md)의 두 PPM 함수는 유효 공급(근무 달력 포함)과 평가 일정의 동시 수요를 조회한다. 기존 PPMUnitAvailability/Max Units의 설정 가용률 의미는 유지한다. 활성 새 함수는 schema80이다.

2026-11-16 후속: [RAV-01~06 가용기간](resource-availability.md)을 지원한다. 아래 XUF 당시의 부분일/무기한·자원 경계 미지원은 새 분 단위 모델로 확장한다. PPMUnitAvailability는 명시 시각의 분을 조회하며 날짜만 있으면00:00이다. 기본/일별 예외만 있는 기존 결과는 유지한다. 새 기간/경계 저장은 조건부 schema79이며 XUF의 unit 바인딩 자체는78을 유지한다.

2026-11-15 · XUF-01~06. XML 외부 계산식 편집기에서 배정률과 자원 가용률을 삽입·미리보기·저장할 수 있다. 원래 숫자/날짜/텍스트 필드와 혼합 native/XML 의존성, 보고서와 XML 재가져오기에 연결한다.

| 참조/식 | 범위 | 결과 |
| --- | --- | --- |
| `[Units]`, `[Assignment Units]`, `[Assignment.Units]` | 배정 | native 기본 배정률. 50%→0.5, 150%→1.5 |
| `[Max Units]`, `[MaxUnits]` | 자원·배정 | 자원에 설정된 **기본** 가용률. 75%→0.75 |
| `[Resource.Max Units]` | 배정 | 연결된 자원의 기본 가용률 |
| `PPMUnitAvailability(#2026-11-17#)` | 자원·배정 | 해당 날짜 예외, 없으면 기본 가용률 |
| `PPMUnitAvailability([field:when])` | 자원·배정 | 명시 날짜 필드의 civil 날짜로 조회 |
| `FormatPercent([Units],2)` | 배정 | 50% 배정이면 `50.00%` |

- **XUF-01 배정률:** 1=100%인 무차원 Number를 사용한다. 영문 대소문자·공백 없는 이름을 허용한다. 기간별 배정/곡선의 특정 시점 값이나 최대/평균 배정률로 바꾸지 않는다. 작업·프로젝트·자원의 Units는 Null이며 여러 배정을 합산하지 않는다. 작업에서 자원 Max Units를 추정하거나 임의 자원을 선택하지 않는다.
- **XUF-02 기본 가용률:** `[Max Units]`는 native `Resource.maxUnits`다. 배정 범위에서는 연결된 자원으로 바인딩하며 명시 `Resource.` 참조와 같다. 기본 미설정은 Null, 명시0은0이다. Microsoft Project의 현재 날짜/가용기간에 따라 바뀌는 Max Units 전체 의미와 구별한다.
- **XUF-03 날짜별 조회:** `PPMUnitAvailability`는 **PPM 확장 함수**이며 인자 하나를 받는다. ISO Date/Text의 날짜를 사용하고 같은 날짜의 시각은 무시한다. OS 현재 시각·상태일·시간대에 의존하지 않는다. Null 인자는 Null, 잘못된 날짜/숫자/시간대 오프셋 문자열은 오류다. 단일 자원이 없는 작업·프로젝트·요약 범위는 Null과 설명을 반환한다. 날짜 예외의0/미설정도 그대로 반영한다. 근무시간·휴일이나 현재 수요를 곱하거나 빼서 잔여 가용시간을 만들지 않는다.
- **XUF-04 XML 교환:** Units/MaxUnits의 native 정수 백분율을 XML 소수 배율로 변환한다. 새 기본 참조는 표준 영문 이름과 범위 접두사로 재작성하고 날짜 조회 확장 이름은 유지한다. `AvailabilityPeriods`의 명시 날짜 전체 범위(시작00:00:00, 종료23:59:00 또는23:59:59)는 양끝 날짜 포함 `CapacityDay`로 읽는다. native 날짜 예외는 한 날짜당 한 기간으로 출력한다. 누락 AvailableUnits는 미설정, 명시0은0이다. XML 표준 정적 계산 결과와 XER 정적 UDF 결과도 내보낸다.
- **XUF-05 오류·손실:** 중복/겹침·부분일·무기한/잘못된 기간·자원 자체 AvailableFrom/To 경계·미지원 자식 요소는 해당 자원의 예외 전체를 적용하지 않고 진단한다. 이런 자료가 있으면 가져온 날짜 조회식을 비활성으로 보존하여 원래 값과 원본 XML을 검토할 수 있게 한다. Units/기본 Max Units 식은 유지한다. 날짜 범위 하나는10,000일, 전체 확장은100,000일까지이며 초과 자료도 부분 적용하지 않는다. 취소는 전파한다.
- **XUF-06 저장·변경:** 안정 키 `builtin:units`/`builtin:maxunits` 바인딩은 비활성 상태도 조건부 schema78이다. 활성 날짜 조회식도78이며 비활성 원문만 있는 식은 기존44 최소 writer를 유지한다. 상한78·미지원 미래79다. 현재·승인·시나리오·일정 채택·통화/ID 이관 사본의 새 의미를 검사한다. 배정률/기본 가용률/날짜 예외 변경은 XML 및 혼합 native 식 캐시를 갱신한다. 편집 undo/redo와 저장/복구를 지원한다.

XML 기간 밖에 기본 MaxUnits를 적용하는 것은 PPM의 native 계약이다. 외부 reader/제품은 AvailabilityPeriods가 있을 때 표 밖 날짜를 가용량 미설정으로 해석할 수 있다. 교환 진단에 이 차이를 남기며 외부 제품의 재계산 동등성을 주장하지 않는다. 부분일·무기한 외부 가용기간을 완전한 native 가용량 모델로 채택하지 않는다. 원본 XML·전체 native 입력/이력은 교환 동반 파일을 따른다.

표준 근거: Microsoft [Assignment Units fields](https://support.microsoft.com/en-us/project/assignment-units-fields)의 배정 수치와 작업/자원 빈 필드, [Max Units resource field](https://support.microsoft.com/en-us/project/max-units-resource-field)의 기간별 가용량, [AvailabilityPeriod](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/availabilityperiod-element?view=project-client-2016)와 [Resources XML 스키마](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/xml-schema-for-the-resources-element?view=project-client-2016). 위 표의 native 기본값·명시 날짜 함수는 이 앱이 정한 지원 부분이다.

[검증 기록](validation/xml-unit-fields-2026-11-15/README.md) · `xml_unit_fields_contract` / `xml_unit_fields_ui` · 독립 검증 `scripts/verify-xml-unit-fields.py`.
