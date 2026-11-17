# XML 계산식 날짜 함수 확장

2026-11-13 후속: 다음 후보였던 [TimeSerial·TimeValue](xml-time-functions.md)를 구현했다. 기준일1899-12-30·음수 OLE·영문/ISO 입력·소수6자리의 새 계약과 schema56을 사용하며 아래 DatePart/Weekday·확장 DateAdd/DateDiff의55 및 레거시3인수 읽기를 유지한다.

2026-11-13 · XDT-01~06. `DatePart`·`Weekday`를 추가하고 `DateAdd`·`DateDiff`의 달력 간격과 주 옵션을 확장한다. 사용자 필드의 **XML 계산식…**에서 예시와 주 규칙을 선택하고 현재 결과를 미리 본다. 공수·계획·실적 날짜를 자동으로 변경하지 않는다.

```text
DatePart("q", [Start])
Weekday([Start], 2)
DatePart("ww", [Start], 2, 2)
DateAdd("m", 1, #2024-01-31T09:30:12.123456#)
DateDiff("ww", #2026-11-07T23:59:59#, #2026-11-08#, 1)
```

마지막 두 결과는 `2024-02-29T09:30:12.123456`과1이다. 월말 이동은 유효한 월말로 보정하며 마이크로초를 보존한다. 두 시각의 주 차이는 경과 시간이 아니라 일요일 경계 수다.

## 함수와 간격

| 함수 | 결과와 규칙 |
|---|---|
| `DatePart(interval, date[, firstDay[, firstWeek]])` | 지정한 날짜 부분을 정수 Number로 반환한다. |
| `Weekday(date[, firstDay])` | 주 시작일을1로 한1~7의 요일 번호다. DatePart의 w와 같다. |
| `DateAdd(interval, count, date)` | 연·분기·월을 이동하거나 정수 시간 단위를 더한 Date 값이다. |
| `DateDiff(interval, from, to[, firstDay[, firstWeek]])` | 지정한 달력 경계 수 또는 온전한 주 수다. 역방향은 음수다. |

간격 이름은 ASCII 대소문자를 구분하지 않으며 `yyyy/q/m/y/d/w/ww/h/n/s`를 받는다. 각각 연·분기·월·연중 일·일·요일·주·시·분·초이다. 날짜 인수는 Date/Start/Finish 값이다. 문자열은 완전한 ISO 표기를 CDate로 변환한다. 달력은 Gregorian1~9999년이며 PC의 현재 시각·지역 설정·달력이나 네트워크를 조회하지 않는다.

DatePart의 y는 연중 일 번호, w는 요일, ww는 주 번호다. 소수 초는 Second와 같이 정수 초 부분만 반환한다. DateAdd의 yyyy/q/m은 월을 이동한 뒤 날짜가 없으면 그 달 말일로 보정한다. y/d/w는 모두 달력 일 이동이고 **근무일 계산이 아니다**. ww는7일이다. 횟수는 기존과 같이±10,000,000 정수이며 분수 횟수의 암묵적 반올림을 하지 않는다. 결과 날짜도 native 범위 안에 있어야 한다.

DateDiff의 yyyy/q/m은 경과 연수·월수가 아닌 연·분기·월 경계 수다. y/d/h/n/s는 기존의 달력 경계 규칙을 유지한다. 자정 직전부터 자정까지1마이크로초여도 d는1이다. w는 두 **날짜** 사이의 온전한7일 묶음을0 방향으로 잘라 계산하며 시각을 제외한다. ww는 두 날짜 사이의 주 시작일 경계를 세며 firstWeek에 따라 달라지지 않는다. DatePart의 주 번호끼리 빼지 않으므로 연도 전환에서도 역전하지 않는다. 근무 달력이 필요하면 ProjDateAdd/Sub/Diff를 사용한다.

firstDay는1(일요일)~7(토요일), firstWeek는1(1월1일 포함)·2(연초 최소4일)·3(연초 온전한 주)이다. 0은고정 기본값1이다. 생략도1이며 호스트 설정을 사용하지 않는다. 사용하지 않는 옵션도 정수 범위를 검증한다. DatePart/Weekday와 선택 인수가 있는 DateDiff에서 잘못된 옵션은 날짜가 Null이어도 오류다. 나머지 Null은 기존 전파 규칙을 따른다. 오류나 미설정을 저장된 스냅샷으로 대체하지 않는다.

주 번호는 기존 Format의 ww와 같은 고정 계약이다. 연초가 첫 주 이전이면 이전 연도의 마지막 주 번호를 쓰며, 연말을 다음 해1주로 재번호하지 않는다. 따라서 월요일/최소4일 옵션의2018-12-31은53이다. 1월1일 포함 규칙에서는2000-12-31이54일 수 있다. ISO 주차나 ISO 주차 연도와 같다고 가정하지 않는다.

의미를 확인한 원문은 Microsoft의 [Project 함수 목록](https://support.microsoft.com/en-us/project/project-functions-for-custom-fields-in-project-desktop), [DatePart](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/datepart-function), [Weekday](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/weekday-function), [DateAdd](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/dateadd-function), [DateDiff](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/datediff-function)이다. VBA의 전체 Variant/로캘 강제 변환·Hijri·분수 횟수·100년 이전 제한을 재현하는 전체 실행기는 아니다. .NET VB의 시간 경과 차이도 이 경계 수 계약과 다르다. 실제 Microsoft Project 제품 왕복은 미검증이다.

## 저장과 이전 문서

새 DatePart/Weekday·4~5인수 DateDiff 문법은 schema55부터 읽는다. 저장기는 새 간격·선택 인수·동적으로 결정되는 간격을 실행하는 식이나 그 전체 보관 사본이 있으면55를 사용한다. 새 native 필드는 추가하지 않는다. 기준선·시나리오·일정 채택/통화/ID 이력, 복구와 실행 취소/다시 실행에도 같은 규칙을 적용한다.

기존3인수 DateAdd/DateDiff는 과거에도 동적 간격이나 미지원 간격을 가진 활성 식을 저장할 수 있었다. 그 문서는 기존 schema44~54에서 계속 읽고 원문·활성 상태·참조와 입력을 그대로 보존한다. 이번 지원으로 과거 미지원 간격의 계산 오류가 실제 값으로 바뀔 수 있다. 저장할 때는 보수적으로55를 선택한다. 기존 정적 d/h/n/s 식과 다른 기능만 있는 문서의 최소 저장 버전은 유지한다. 비활성 원문은 기존44부터 읽으며 자동 활성화하지 않는다.

## 화면·교환·보고

예시4개, 주 시작일7개와 첫 주 규칙3개를 제공한다. 첫 주 규칙은 주 번호에만 쓰이므로 나머지 예시에서는 해당 선택을 비활성화한다. 비동기 미리보기·형식 검증·저장/취소·문서 변경 방지와 읽기 전용 경로를 유지한다. 새 함수는 네 범위·native 숫자 식 참조·요약·배정 전파를 사용한다.

XML은 식 정의와 현재 결과를 내보내며 참조를 실제 필드 이름으로 재작성한다. 지원 식은 가져오기에서 활성화하고 미지원 식은 기존 비활성 보존 경로를 사용한다. XER는 정적 결과, XLSX/PDF는 결과·정의·오류/미설정을 기록한다. 시간대가 필요하면 PPMTimeZone으로 먼저 날짜를 변환한 뒤 DatePart/Weekday를 적용한다. 날짜 결과 자체에는 시간대 표지가 없다.

다음 후보는 TimeSerial/TimeValue, 추가 수학·문자열 함수다. 현재 시각·난수·외부 참조는 이 증분에 포함하지 않는다.
