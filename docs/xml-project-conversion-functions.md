# XML 계산식 Project 날짜·기간 변환

2026-11-14 · XPC-01~06. ProjDateValue/ProjDateConv/ProjDurConv를 고정 native 프로필로 지원한다. 현재 시각·PC 언어·Project 옵션을 조회하지 않는다.

| 함수 | 지원 문법·결과 |
|---|---|
| `ProjDateValue(value)` | ISO 날짜/일시 또는 native Date를 Date로 변환한다. 시각·소수 초6자리를 보존한다. |
| `ProjDateConv(value[, format])` | 같은 입력을 고정 영문 날짜 표시 Text로 변환한다. 기본255는형식0이다. |
| `ProjDurConv(value[, unit])` | 분 숫자/typed Duration/명시 기간 문자열을 단위가 붙은 Text로 변환한다. 기본은3(분)이다. |

```text
ProjDateValue("2026-11-14T09:30:12.123456")
ProjDateConv(#2026-11-14T09:30:00#,20) → 11/14/2026
ProjDurConv(90,5)                    → 1.5 h
ProjDurConv("2w",7)                  → 10 d
ProjDurConv("2ed?",40)               → 2 ed?
```

## 날짜 프로필

ProjDateValue는 완전한 ISO 날짜/일시를 native 민간 시각으로 정규화한다. 날짜만 있으면 자정을 붙인다. 앞뒤 ASCII 공백을 제거한 대소문자 무관 `NA`는 native 미설정이다. Null도 미설정이며 빈 문자열·잘못된 날짜는 오류다. 숫자 OLE 날짜·지역 날짜 표기·Today/Now·시각만 있는 문자열은 추측하지 않는다. 시간대 변환은 기존 PPM 함수로 명시한다.

ProjDateConv의 인수도 같은 의미이며 NA/Null은 미설정이다. 표시 형식은 PjDateFormat의 숫자0~20과255를 받는다. 255와생략은고정0이며 Project의 View 옵션을 읽지 않는다. 이름 상수 토큰을 추가하지 않았으므로 숫자 ID를 입력한다. 연도는2자리/4자리, 월·요일 이름은영문, 시각은12시간 AM/PM과분까지 표시하며 초를 반올림하지 않는다. 원래 Date 값의 초/마이크로초는 변경하지 않는다.

| ID | 예시 입력2026-11-14T09:30:12.123456의 표시 |
|---|---|
| 0 / 255 | 11/14/26 9:30 AM |
| 1 | 11/14/26 |
| 2 / 3 | November 14, 2026 9:30 AM / November 14, 2026 |
| 4 / 5 | Nov 14 9:30 AM / Nov 14, '26 |
| 6 / 7 | November 14 / Nov 14 |
| 8 / 9 | Sat 11/14/26 9:30 AM / Sat 11/14/26 |
| 10 / 11 | Sat Nov 14, '26 / Sat 9:30 AM |
| 12 / 13 / 14 | 11/14 / 14 / 9:30 AM |
| 15 / 16 / 17 | Sat Nov 14 / Sat 11/14 / Sat 14 |
| 18 / 19 | W46/7 / W46/7/26 9:30 AM |
| 20 | 11/14/2026 |

18/19의 주차는 기존 DatePart/Format 프로필과 같은 **일요일 시작·1월1일 포함 주**이며 ISO 주차나 외부 Project 옵션의 결과와 같다고 가정하지 않는다. 주는2자리 이상, 요일은일요일1~토요일7이다. 연말을 다음 해1주로 바꾸지 않는다. Microsoft의 예시 날짜/주차를 하드코딩하지 않고 Gregorian 민간 날짜에서 계산한다.

## 기간 프로필

입력 숫자는 소수6자리 분이다. 문자열은 앞뒤 ASCII 공백·부호·정수부와선택 소수1~6자리, 선택 단위 `m/em/h/eh/d/ed/w/ew/mo/emo`, 선택 끝의 `?`를 받는다. 단위는대소문자 무관이며 생략은분이다. 쉼표·지수·현지 언어·단위 사이의 임의 문자와분수6자리 초과는 거절한다. Boolean/Date를 기간으로 암묵 변환하지 않는다. 입력은64bytes 이내이며 분 환산 뒤 절댓값이9조 이내여야 한다.

고정 환산은1h=60m, 근무1d=480m·1w=2400m·1mo=9600m(20근무일), 경과1ed=1440m·1ew=10080m·1emo=43200m(30일)이다. 달력의 하루 길이·실제 월 길이·PC/Project HoursPerDay/DaysPerMonth 옵션을 추측하지 않는다. 월은 일정 이동이 아닌 명시 표시 환산이다.

출력 단위 번호는3~12가각각 m/em/h/eh/d/ed/w/ew/mo/emo이며35~44는같은 단위에추정 `?`를 붙인다. 단위 인수는0~255 정수로 검증한 뒤 이20개만 허용한다. **출력 인수가 경과·추정 표지를 선택**한다. 생략3은입력의표지와무관한 m이며, 예를 들어 ProjDurConv("2ed?",5)는48 h이다. 단위 번호를 명시해 필요한 표시를 선택한다. 원래 typed Duration이나 원장을 수정하지 않는다.

정수 분 척도를 단위 분 값으로 나누고 소수6자리까지 절반은0에서 멀어지는 방향으로 반올림한다. 소수 뒷0은제거하며 결과는 `숫자 공백 단위[?]`다. 표시 변환은 반올림 손실이 있을 수 있어 다시 파싱한 분이 원래와 같다는 보증은 없다. 작은 값은0단위로 보일 수 있다. 입력/중간 범위 초과·잘못된 단위는 오류이며 포화/잘라내기나 저장 스냅샷 대체를 하지 않는다.

기존 ProjDurValue는 native의 m/em 문법을 유지한다. h/d/w 표시 문자열은 기존 ProjDateAdd/Sub의 지원 단위와 조합할 수 있지만 mo/emo를 그 함수에 새로 추가하지 않는다. ProjDurConv의 Text 결과를 Duration 필드에 자동 채택하지 않는다. 숫자만 필요하면 Val을 명시해 단위 환산 의미를 식에서 처리한다.

## 저장·화면·교환

활성 호출 또는 전체 보관 사본은 조건부 **schema61**이다. 이전 태그의 활성 식을 거절하고 비활성 원문은 기존44부터 읽으며 자동 활성화하지 않는다. 문자열/날짜 리터럴·참조 이름은 호출로 판정하지 않는다. 새 native 필드는 없다. 기존 기능의 최소 버전과 현재·기준선·시나리오·일정 채택·통화/ID 전체 사본, 저장/복구·읽기 전용·undo/redo를 유지한다.

예시4개·비동기 미리보기·저장/취소, 네 범위·요약·배정 전파·native 숫자 식을 연결한다. 통화 전환 때 일반 숫자/날짜 입력이나 수식 상수·단위 문자열을 통화로 환산하지 않는다. XML은정의·참조 이름·현재 값과 재가져오기 재계산, XER는정적 값, XLSX/PDF는값·정의·오류/미설정을 기록한다. XML의 비활성 설정 부재 때문에 지원 식을 다시 가져오면 활성화될 수 있다. 전체 원본 이력은 source.ppm에 보관한다.

원문은 [Project 함수 목록](https://support.microsoft.com/en-us/project/project-functions-for-custom-fields-in-project-desktop), [PjDateFormat](https://learn.microsoft.com/en-us/office/vba/api/project.pjdateformat), [PjFormatUnit](https://learn.microsoft.com/en-us/office/vba/api/project.pjformatunit), [DurationFormat](https://learn.microsoft.com/en-us/office/vba/api/project.application.durationformat)이다. 위 고정 표시/근무시간·월 길이·NA/Null·ISO 입력은이 앱의 명시 계약이며 전체 제품/NLS 동등성을 주장하지 않는다.

[검증 기록](validation/xml-project-conversions-2026-11-14/README.md). 번호별 기준선/Cost·요약 기간·다른 Project 함수와 실제 제품 왕복/기존 GAP는잔여다.
