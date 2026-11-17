# XML 계산식 Eqv·Imp

2026-11-14 · XLG-01~06. `Eqv`(동치)와 `Imp`(함의)를 작업·프로젝트·자원·배정 계산식에서 지원한다. 편집기의 논리 연산 예시 삽입·worker 미리보기, 표·요약·전파, 저장/복구와 XML/XER·XLSX/PDF를 연결한다.

| 식 | 결과 | 형식 |
|---|---|---|
| `True Eqv False` | False | Boolean |
| `False Imp False` | True | Boolean |
| `5 Eqv 3` | -7 | 32비트 비트 동치 |
| `5 Imp 3` | -5 | 32비트 비트 함의 |
| `False Imp Null` | True | Boolean |
| `True Imp Null` | 미설정 | Null |
| `2 Imp Null` | -3 | 숫자와 Null의 규약 |
| `Null Imp 2` | 2 | 숫자와 Null의 규약 |

XLG-01: 함수 호출이 아닌 이항 연산자이며 ASCII 대소문자를 구별하지 않는다. Boolean끼리는 Boolean, 숫자가 섞이면 비트 연산 숫자다. True는-1, False는0으로 대응한다. `Eqv`는 같은 위치의 두 비트가 같으면1, `Imp`는 왼쪽1/오른쪽0일 때만0이다. 결과는 부호 있는32비트 값이며 기간 태그를 전파하지 않는다. Boolean을 Flag에 저장하면 기존1/0 규약, Text에 표시하면 True/False를 사용한다.

XLG-02: 숫자는 **32비트 범위의 정수**만 받는다. 분수·범위 초과·문자열/날짜의 암묵 변환은 하지 않는다. 분수는 `CLng` 등으로 명시 변환한다. `Eqv`/`Imp` 뒤에 숫자·밑줄이 붙은 `1Eqv0`·`0 Imp1` 같은 구문은 거절한다. 문자·날짜 리터럴, 이스케이프된 필드 참조 안의 단어와 Important 같은 긴 이름은 새 연산으로 판정하지 않는다. 실행 구문과 조건부 저장 버전 판정이 일치해야 한다.

XLG-03: `Eqv`는 어느 쪽이 Null이어도 Null이다. `Imp`는 Boolean/숫자 형식에 따른 규약을 적용한다. Boolean에서는 False Imp Null과 Null Imp True가 True이며 True Imp Null·Null Imp False·Null Imp Null은 Null이다. 숫자에서는 **-1 Imp Null은 Null**, 그 밖의 정수 Imp Null은 오른쪽0과의 비트 함의다. **Null Imp 0은 Null**, Null Imp 비영 정수는 그 오른쪽 정수다. 이 결과는 원본 입력을0으로 바꾸거나 저장값으로 보충한 것이 아니다.

기준은 Microsoft의 [Eqv](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/eqv-operator), [Imp](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/imp-operator)와 [MS-VBAL의 숫자/Null 규약](https://learn.microsoft.com/en-us/openspecs/microsoft_general_purpose_programming_languages/ms-vbal/9d1d4bb9-7e95-4d53-8f3c-b696e4924b56)이다. 전체 VBA의 숫자 형식·자동 변환을 구현한 것은 아니다. 모든 피연산자를 평가하므로 `False Imp (1/0)`은 오류다. 미설정·오류를 저장값99/777로 대체하지 않는다. XLSX 값 시트는 결과 없음으로, 별도 `계산식 미설정과 오류` 시트는 상태/사유로 표시한다. 취소와 기존 식 크기·깊이·단계 제한을 유지한다.

XLG-04: **Eqv/Imp를 포함한 식 단위**로 [VBA 우선순위](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/operator-precedence)를 적용한다. 산술 → 결합 → 비교 → Not → And → Or → Xor → Eqv → Imp 순서이며 같은 우선순위는 왼쪽부터다. 산술은 `^` → 단항 부호 → `* /` → `\` → `Mod` → `+ -`다. 기존 And/Or/Xor 등의 형식/Null 지원 범위 자체는 변경하지 않는다.

새 연산이 없는 식은 과거 문법을 유지한다. 예를 들어 기존 `True Or True Xor True`는 True지만, 새 문법의 `(True Or True Xor True) Eqv False`는 True다(괄호 안은 False). 기존 `8 Mod 3*2`는4이며 새 문법의 `8 Mod 3*2 Eqv 2`는-1이다. 기존 필드를 참조해도 그 필드의 식을 새 문법으로 다시 해석하지 않는다. 기존 식에 새 연산을 추가해 혼합할 때는 괄호로 의도를 명시한다. 앞선 ^/정수 나눗셈만 포함한 식의 schema63 문법도 유지한다.

XLG-05: 활성 새 연산이 현재·기준선·시나리오·일정 채택·통화/ID 이력 전후 전체 사본에 있으면 조건부 **schema64**다. schema63 이하는 활성 새 식을 거절한다. 비활성 원문은 기존 schema44부터 보존하며 기존 참조·기능의 최소 버전을 별도로 적용한다. 새 native 데이터 필드는 없다. 안정 ID 바인딩/이름 변경·저장/재열기·복구·읽기 전용·undo/redo를 연결한다. 통화 전환은 입력을 환산하고 식을 다시 실행하므로 비트 결과에 환율을 곱하지 않는다.

XLG-06: XML은 정의·참조·현재 값을 내보내며 재수입 후 다시 실행한다. XER는 정적 결과, 전체 정의/이력은 source.ppm에 보존한다. 독립 비트별 계산4096건과 형식별 진리표200건, Windows VBScript의 같은4296건을 비교한다. 비교 범위는 명시32비트 정수와 Boolean/Null이며 실제 Microsoft Project/P6 실행·전체 VBA 동등성 검증은 아니다.

번호별 기준선 대응·요약 기간·다른 함수/연산·전체 VBA/로캘과 기존 GAP는 잔여다. [검증 기록](validation/xml-logical-2026-11-14/README.md).
