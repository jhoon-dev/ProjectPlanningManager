# XML 계산식 MonthName·WeekdayName

2026-11-14 · XDN-01~06. 작업·프로젝트·자원·배정 계산식에서 월·요일의 이름과 약어를 반환한다. 기존 Format의 고정 Gregorian/en-US 이름 표를 재사용한다.

| 식 | 결과 |
|---|---|
| `MonthName(11)` | November |
| `MonthName(11,True)` | Nov |
| `MonthName(2.5)` | February |
| `WeekdayName(1)` | Sunday |
| `WeekdayName(1,False,2)` | Monday |
| `WeekdayName(7,True,2)` | Sun |
| `WeekdayName(Weekday(#2024-02-29#,2),False,2)` | Thursday |

XDN-01: `MonthName(month[,abbreviate])`는 월 번호1~12를 받는다. `WeekdayName(weekday[,abbreviate[,firstDay]])`는 요일 번호1~7을 받는다. 반환값은 Text다. abbreviate 생략/False/숫자0은 전체 이름, True/0 이외 숫자는 세 글자 약어다. May는 약어도 May다. 텍스트를 숫자·Boolean으로 암묵 변환하지 않으며 필요한 경우 CDbl/CBool 등을 명시한다.

XDN-02: 월·요일·주 시작일 번호는 소수6자리 숫자를 **짝수 쪽 정수 반올림한 후** 범위를 검사한다. MonthName(1.5)와 MonthName(2.5)는 모두 February, MonthName(12.5)는 December이며12.500001은 오류다. abbreviate의 숫자는 반올림하지 않고0 여부로 판정한다.

XDN-03: firstDay는1(일)~7(토), 생략과0은 이 앱의 고정 기본값1(일요일)이다. WeekdayName의 첫 번호는 해당 주 시작일에서 센 상대 번호다. Weekday와 조합할 때는 두 함수에 같은 주 시작일을 지정한다. PC의 NLS 설정이나 현재 로캘을 읽지 않는다. VBA의 시스템 로캘/Calendar 옵션 전체를 재현하지 않으며13개월 달력은 지원하지 않는다. 다른 언어의 날짜 표시는 기존 명시 LCID PPMFormat 지원 범위를 사용할 수 있다.

모든 인수의 Null은 오류이며 미설정으로 바꾸지 않는다. 미설정 Text 결과는 `IIf(True,Null,MonthName(1))`처럼 기존 typed 식으로 만들 수 있으나 IIf의 모든 인수를 평가하므로 `IIf(True,1,MonthName(13))`도 오류다. 범위·형식·인수 개수 오류는 원본 스냅샷이나0으로 대체하지 않는다. 새 함수가 없는 기존 날짜 함수·반올림·표시 규칙은 유지한다.

XDN-04: 활성 새 함수가 현재·기준선·시나리오·일정 채택·통화/ID 이력 전후 전체 사본에 있으면 조건부 **schema67**이다. schema66 이하는 활성 새 식을 거절한다. 비활성 원문은 기존 schema44부터 보존하며 다른 참조/기능의 최소 버전도 유지한다. native 데이터 필드는 추가하지 않는다. 안정 ID 바인딩·이름 변경·저장/재열기·복구·읽기 전용·undo/redo와 취소 checkpoint를 연결한다.

XDN-05: 편집기에 예시 삽입·worker 미리보기를 제공한다. 네 범위·요약 식·roll-down과 native 숫자 식을 연결한다. 예를 들어 월 이름의 Len 결과를 native 숫자 식에서 참조할 수 있다. 통화 전환 후 `MonthName(Int([금액]))`는 환산 입력에서 다시 계산한다. XML은 정의·참조·현재 값과 재수입 실행, XER는 정적 결과, source.ppm은 전체 정의/이력, XLSX/PDF는 값과 별도 오류/미설정을 보존한다.

XDN-06: 월 이름/약어24건·요일/주 시작일112건과2024년 윤년366일×8개 주 시작일×2개 약어 옵션5,856건, 합계 **5,992건**을 Python calendar/datetime과 Windows VBScript(SetLocale1033)로 독립 대조한다. 직접 식·반올림·Null/범위 오류·저장/전체 사본·교환·보고서·작은 화면을 함께 확인한다. 실제 Project/P6 실행 왕복과 전체 VBA/로캘 동등성은 잔여다.

구문 기준은 Microsoft [MonthName](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/monthname-function)·[WeekdayName](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/weekdayname-function)이다. [검증 기록](validation/xml-date-names-2026-11-14/README.md). 다른 함수·번호별 기준선·요약 기간과 기존 GAP도 계속 잔여다.
