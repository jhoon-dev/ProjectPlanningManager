# XML 계산식 TimeSerial·TimeValue

2026-11-13 후속: 다음 후보인 추가 변환 함수 [Str·Val·Hex·Oct](xml-number-text.md)를 구현했다. 아래 TimeSerial/TimeValue의 기준일·OLE·입력/정밀도·schema56 의미는 유지하며 새 함수는 조건부 schema57이다.

2026-11-13 · XTM-01~06. 사용자 필드의 **XML 계산식…**에서 시각을 구성하거나 날짜/문자열의 시각만 추출한다. 결과는 기존 Date 형식이며 **기준일은1899-12-30**이다. 현재 날짜·계획 시작일을 자동으로 붙이지 않는다. bare 시각 텍스트가 필요하면 Format으로 표시한다.

```text
TimeSerial(16, 35, 17)
TimeSerial(0, 75, 0)
TimeValue("2:24PM")
TimeValue("2024-02-29T09:30:12.123456")
Format(TimeValue([Start]), "hh:nn:ss")
```

앞의 결과는 각각 기준일16:35:17,01:15:00,14:24:00과09:30:12.123456이다. 사용자 필드의 날짜/시각을 계산하며 실제 일정·공수·원가 입력이나 실적 원장은 변경하지 않는다.

## TimeSerial

`TimeSerial(hour, minute, second)`는 각 숫자 인수를 정수로 짝수 쪽 반올림한 뒤 -32768~32767 범위를 검증한다. 예를 들어1.5와2.5는 모두2가 된다. 문자열의 숫자를 암묵 해석하지 않으며 필요하면 CInt/CDbl로 명시 변환한다. 기존 Boolean의 숫자 의미는 유지한다.

분·초가 일반적인 범위를 넘거나 음수여도 상대 값을 합산한다. 75분은1시간15분이며24시간은다음 날이다. **음수 합계는 OLE DATE 규칙**을 사용한다. 합계를86400초로 나눈 정수 일은0 방향으로 자르고, 나머지 시각은 절댓값으로 표현한다.

| 식 | Date 결과 |
|---|---|
| `TimeSerial(24,0,0)` | 1899-12-31T00:00:00 |
| `TimeSerial(-1,0,0)` | 1899-12-30T01:00:00 |
| `TimeSerial(-24,0,0)` | 1899-12-29T00:00:00 |
| `TimeSerial(-25,0,0)` | 1899-12-29T01:00:00 |

따라서 음수 TimeSerial을 기준일부터의 일반적인 경과 시간 빼기로 해석하지 않는다. 날짜를 실제로 이동하려면 DateAdd를 사용한다. TimeValue를 다시 적용하면 위 결과의 날짜는 제거되고 기준일에 시각만 남는다.

## TimeValue

`TimeValue(time)`는 날짜 값 또는 명시 문자열을 받는다. Date/Start/Finish의 시각과 최대6자리 소수 초를 보존하고 날짜를 기준일로 바꾼다. 문자열은 다음 고정 규칙을 사용하며 PC 로캘·현재 연도·코드페이지를 조회하지 않는다.

- 24시간 표기 `H:M`, `H:M:S`, `H:M:S.소수1~6자리`를 받는다. 각 시·분·초는1~2자리 ASCII 숫자다.
- 영문 AM/PM은 대소문자를 구분하지 않으며 `2:24PM`, `4:35:17 PM`, `2 PM`처럼 쓴다. 표기한 시간은0~12이며12AM은0시,12PM은12시다. 0AM/0PM도 같은 주기로 해석한다.
- 앞뒤 ASCII 공백·탭·개행은 제거한다. 숫자와 콜론 사이의 공백이나 임의 구분자는 받지 않는다.
- ISO 날짜 `YYYY-MM-DD`만 있으면 자정이다. 날짜 뒤의 `T` 또는공백과 시각도 받는다. **날짜를 버리기 전에 그 날짜를 검증**하므로 잘못된 윤일은 오류다. 달력은 기존 Gregorian1~9999년이다.
- 24:00·윤초·7자리 소수 초·잘못된 AM/PM·숫자 OLE serial의 암묵 변환·지역 날짜 표기·한국어 오전/오후·시간대 오프셋은 거절한다. 시간대를 바꾸려면 PPMTimeZone의 Date 결과를 먼저 구한다. 전각 숫자는 필요하면 명시 StrConv로 반각 변환한다.

예를 들어 TimeValue의 `09:30:12.123400`은 기준일09:30:12.1234로 정규화한다. `CStr(TimeValue(...))`는 날짜와 소수 초를 보존한다. 기존 Format의 시각 토큰은 정수 초까지만 표시하므로 소수 초가 필요한 교환에는 CStr 또는 Date 결과를 사용한다.

Null은 기존 DateSerial 등과 같은 native 전파 규칙을 사용하고, 모든 식 인수는 평가한다. `TimeSerial(Null,1/0,0)`은 나눗셈 오류다. 형식/범위 오류와 미설정은 구분하며 저장된 스냅샷으로 대체하지 않는다. TimeValue 문자열 입력은4096bytes까지다. 기존 식 크기·깊이·평가 단계·취소·형식별 출력 제한을 유지한다.

## 저장·화면·교환

활성 TimeSerial/TimeValue 또는 그 전체 보관 사본이 있으면 조건부 schema56을 사용한다. 이전 태그의 활성 호출은 거절하고, 비활성 원문은 기존44부터 자동 활성화 없이 읽는다. 새 native 필드는 추가하지 않는다. 기준선·시나리오·일정 채택·통화/ID 이력, 저장/복구·실행 취소/다시 실행과 읽기 전용을 연결한다. 기존 날짜 함수·레거시3인수 읽기 규칙과 다른 문서의 최소 writer는 유지한다.

편집기는 예시6개를 제공하고 기준일·음수 규칙을 설명한다. 비동기 미리보기·저장/취소·문서 변경 방지와 네 범위·요약/배정 전파·native 숫자 식을 사용한다. XML은 식과 현재 결과를 교환하고 참조 이름을 재작성한다. XER는 정적 결과, XLSX/PDF는 값·정의·오류를 기록한다. 기준일1899년은시각 표현용 사용자 필드 값이며 계획/실제 작업 날짜가 아니다.

의미를 확인한 원문은 Microsoft의 [Project 함수 목록](https://support.microsoft.com/en-us/project/project-functions-for-custom-fields-in-project-desktop), [TimeSerial](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/timeserial-function), [TimeValue](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/timevalue-function)이다. 고정 입력 문법·native Null·마이크로초는 이 명시 지원 계약이며 전체 VBA/Variant/NLS·실제 제품 왕복을 구현했다는 뜻이 아니다. Windows VBScript 비교는 OLE 음수/넘침·반올림과 정수 초 시각에 한정한다. ISO 날짜 검증과 native 마이크로초는 별도의 Python 검사로 확인한다.

다음 후보는 추가 수학·문자열 함수다. 현재 시각·난수·외부 파일/서버 참조는 지원하지 않는다.
