# XML 외부 계산식 실행

2026-11-16 후속: [현재 수요·잔여 가용량](remaining-resource-availability.md)의 PPMResourceLoad/PPMRemainingAvailability를 지원한다. 자원/배정 범위·명시 시각·전체 프로젝트 수요·1=100%·음수/Null·일정 문맥과 활성 함수 schema80을 따른다.

2026-11-16 후속: [가용기간](resource-availability.md)의 부분일/무기한·자원 전체 경계가 PPMUnitAvailability의 명시 시각에 반영된다. 날짜만 주면00:00이며 기존 일별 자료의 결과는 유지한다. 새 가용기간 데이터는 schema79다.

2026-11-15 후속: [Units·가용률 XUF-01~06](xml-unit-fields.md)의 배정률/기본 가용률(1=100%)과 명시 날짜 PPMUnitAvailability를 지원한다. scope/Null·날짜 예외·XML AvailabilityPeriods의 보존/손실 진단·캐시·조건부 schema78을 따른다.

2026-11-15 후속: [외부 기준선 교환 XBE-01~07](xml-baseline-exchange.md)의 번호별 명시 값을 Baseline 참조에 연결했다. 채택 번호의 외부 값이 우선이며 미보고 객체/필드는 native로 보충하지 않는다. 외부0은 이름 없는 Baseline에도 대응한다. 출처/검토·typed 기간 표시·원본 보존과 조건부 schema75를 따른다.

2026-11-15 후속: [번호별 기준선](xml-numbered-baselines.md)의 Baseline0~10 Start/Finish/Duration/Work/Cost와 [프로젝트·WBS/그룹 요약 기간](xml-summary-durations.md)을 지원한다. native 승인 버전 고정 대응과 동결/현재 프로젝트 달력, 엄격한 미보고/빈 범위·완료 제외를 따른다. 바인딩/요약 설정은 조건부 schema73/74이며 기존 작업별 의미와 과거 파일 읽기를 보존한다.

2026-11-15 후속: [SLN·SYD](xml-depreciation-functions.md)의 명시 비음수 입력·소수 life/period·정확한 중간 계산·최종6자리 반올림과 조건부 schema72를 지원한다. 전체 VBA/Double 동등성은 별도 범위다.

2026-11-14 후속: [Partition](xml-partition-function.md)의 짝수 반올림·구간 분류·정렬용 공백·Null 전파와 조건부 schema71를 지원한다. native 수치 범위의 명시 부분을 실행한다.

2026-11-14 후속: [RGB·QBColor](xml-color-functions.md)의 성분 제한·짝수 반올림·빨강 하위 바이트 정수·16색 팔레트와 조건부 schema70를 지원한다. 숫자/Boolean 입력·Null 오류를 명시한다.

2026-11-14 후속: [숫자 표시3개](xml-numeric-display.md)의 FormatNumber/Percent/Currency·고정 en-US·정확한 십진 표시·TriState와 Null 오류를 지원한다. 활성 새 식/전체 사본은 조건부 schema69이며 Currency의 고정 $는 문서 통화와 별개다.

2026-11-14 후속: [FormatDateTime](xml-format-datetime.md)의 고정 en-US 표시0~4·기준일/자정·옵션 반올림·Null 오류를 지원한다. 소수 초는 표시에서 생략하며 활성 새 식/전체 사본은 조건부 schema68이다.

2026-11-14 후속: [MonthName·WeekdayName](xml-date-name-functions.md)의 고정 en-US 이름/약어·주 시작일·짝수 반올림·Null 오류를 지원한다. 활성 새 식/전체 사본은 조건부 schema67이며 OS 로캘을 자동 추론하지 않는다.

2026-11-14 후속: [AscW·ChrW](xml-unicode-functions.md)의 native/XML-safe BMP·부호 있는16비트 코드/음수 별칭·짝수 반올림·Null/오류를 지원한다. 활성 새 식/전체 사본은 조건부 schema66이며 기존 Asc/Chr의 ASCII 계약은 유지한다.

2026-11-14 후속: [Like 패턴 비교](xml-like-operator.md)의 BMP 정확 비교·와일드카드/목록·범위·부정과 조건부 schema65를 지원한다. Null/전체 패턴 검증·유한 작업량과 기존 문법 보존을 연결한다.

2026-11-14 후속: [Eqv/Imp](xml-logical-operators.md)의 Boolean/32비트·형식별 Null·새 식 우선순위와 조건부 schema64를 지원한다. 기존 식은 기존 문법을 유지한다.

2026-11-14 후속: [거듭제곱·정수 나눗셈](xml-arithmetic-operators.md)의 ^/정수 나눗셈과 새 문법의 우선순위·32비트 짝수 반올림을 지원한다. 활성 식/전체 사본은 schema63이다. 새 연산이 없는 기존 식은 기존 문법을 유지한다.

2026-11-14 후속: [Baseline Cost](xml-baseline-cost.md)의 최초 native v1 동결 단가·공수·고정비·자재/경비를 작업·프로젝트·자원·배정/그룹·전파에서 조회한다. 바인딩은 활성/비활성 모두 조건부 schema62이며 번호별 슬롯은 잔여다.

2026-11-14 후속: [Project 날짜·기간 변환3개](xml-project-conversion-functions.md)를 지원한다. ISO/NA와 고정 en-US 날짜 표시·명시 기간 환산을 실행하며 활성 식/전체 사본은 schema61이다. 전체 제품 옵션/NLS 동등성은 주장하지 않는다.

2026-11-14 후속: [추가 형 변환3개](xml-conversion-functions.md)의 CDec/CSng/CVar를 지원한다. native 소수6자리·정확한 binary32 변환·형식/기간 보존과 조건부 schema60이다. 전체 VBA 형식 범위는 주장하지 않는다.

2026-11-14 후속: [문자열 함수3개](xml-string-functions.md)의 Replace/InStrRev/StrReverse를 지원한다. BMP/ASCII 명시 부분과 시작 이후 반환·끝 경계·Null 구분을 실행하며 활성 식/전체 사본은 schema59이다.

2026-11-13 후속: [추가 수학 함수6개](xml-math-functions.md)를 지원한다. Atn/Cos/Exp/Log/Sin/Tan·라디안/자연로그·부동소수 근사/소수6자리와 조건부 schema58을 연결한다. 전체 VBA/제품 실행 동등성은 주장하지 않는다.

2026-11-13 후속: [Str·Val·Hex·Oct](xml-number-text.md)의 명시 지원 부분을 추가했다. 고정 점 소수와 접두부·선행 공백·32비트 진수를 사용한다. 새 활성 식/전체 사본은57이며 기존 함수/비활성 원문과 현재 결과/원본 보존을 유지한다.

2026-11-13 후속: [TimeSerial·TimeValue](xml-time-functions.md)의 명시 지원 부분을 추가했다. 1899-12-30 기준 Date와16비트/짝수 반올림/상대 넘침·음수 OLE,24시간/영문 AM·PM/ISO·소수6자리의 규칙을 따른다. 새 활성 식/보관 사본은56이며 비활성 원문은 기존44부터 보존한다.

2026-11-13 후속: [날짜 함수 확장](xml-date-functions.md)의 DatePart·Weekday와 DateAdd/DateDiff10간격·주 옵션을 지원한다. 아래 과거 d/h/n/s 제한은 새 계약으로 대체한다. 기존 경계 수·정수 횟수·고정 Gregorian/민간 시각 의미는 유지하며, 새 지원 저장은55·기존3인수 문서는 계속 읽는다.

2026-11-13 후속: [Format·StrConv의 명시 지원 부분](xml-format-strconv.md)을 추가했다. 아래의 과거 미지원 표기는 고정 en-US Format·명시 LCID1033/1041/1042의 PPMFormat/StrConv·Unicode16.0 지원 범위로 대체한다. 전체 VBA/NLS·다른 로캘/코드페이지는 잔여다. 활성 식은 schema54를 사용한다.

2026-11-13 후속: [시간대 변환 XTZ-01~06](xml-formula-timezones.md)을 PPM 확장 함수로 지원한다. 명시 고정 오프셋/버전 포함 IANA2026f·중복/누락 정책·마이크로초와 오프셋 Text를 다룬다. 활성 식은 조건부 schema53이며 날짜 결과 자체에는 시간대 표지가 없다. 아래의 시간대 미지원은 이 명시 수식 지원 부분으로 확장하며 일반 일정/날짜 필드의 시간대는 자동 추론하지 않는다.

2026-11-13 후속: [추가 함수13개 XFN-01~06](xml-additional-functions.md)를 지원한다. Choose·InStr/StrComp·LTrim/RTrim·Space/String·Asc/Chr·CByte/CCur·DateValue·Sqr의 명시 BMP/ASCII/ISO·정수/고정점 부분을 실행한다. 활성 새 함수는 조건부 schema52와 전체 사본/복구를 사용하고, 과거 비활성 식은 기존 최소 버전에서 읽는다. 아래 최초 함수 목록과 schema44 범위는 후속 지원 부분으로 확장한다.

2026-11-11 · XMLF-01~05.

MSPDI ExtendedAttribute의 Formula를 원본과 안정 ID 참조로 보존하고, 지원 문법·함수의 식을 이 앱에서 실행한다. Task/Project/Resource/Assignment의 Text·Number·Flag·Cost·Duration·Date·Start·Finish 결과를 기존 필드 조회·그룹/필터·보고서에 연결한다. 외부 식과 native 숫자 식은 별도 문법이며, 한 필드에서 동시에 정의하지 않는다. Choice 결과/참조는 실행하지 않는다.

기준은 Microsoft의 [Formula 요소](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/formula-element?view=project-client-2016), [Project 사용자 필드 함수](https://support.microsoft.com/en-au/project/project-functions-for-custom-fields-in-project-desktop), [IIf 평가](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/iif-function), [Round](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/round-function), [형식 변환](https://learn.microsoft.com/en-us/office/vba/language/concepts/getting-started/type-conversion-functions)이다. 지원 부분의 실행 계약이며 전체 VBA/Project 함수·로캘·실제 제품의 계산 동등성을 뜻하지 않는다.

## 설정과 값 보존

사용자 필드 관리에서 **XML 계산식…**을 연다. 식 편집·필드 참조 삽입·실행 여부·최대20행 미리보기·식 제거를 지원한다. 저장은 문서 ID/revision/session·읽기 전용을 다시 확인한다. 활성 계산값은 직접 입력/값 제거를 차단한다. native 숫자 식이 있는 필드는 기존 식을 먼저 제거해야 한다.

CustomField.xmlFormula는 원본 source, token/key/scope 참조 바인딩, enabled를 저장한다. 이름을 바꿔도 바인딩은 같은 필드 ID를 가리킨다. 안정 ID의 대소문자는 구별하고 참조 네임스페이스와 영어 이름만 ASCII 대소문자를 정규화한다. 원래 Task/Project/Resource/Assignment 속성과 WBS/루트 명시 값은 스냅샷으로 남기며 계산 결과로 덮어쓰지 않는다. 비활성 또는 식 제거 후에는 원본 값을 표시한다. 계산 오류/미설정을 원본 스냅샷이나0으로 대체하지 않는다.

XML 가져오기는 전체 정의를 읽은 뒤 FieldName·Number1 같은 표준 슬롯 이름·Alias를 바인딩한다. 표준 이름을 Alias보다 우선하며 모호한 이름은 추측하지 않는다. 지원식을 활성화하고 미지원 함수/문법·미해결 참조·Choice는 비활성으로 보존한다. 활성 사전의 순환/범위 검증이 실패하면 가져온 XML 식 전체를 비활성으로 보존해 부분 실행을 피한다. 빈 Formula는 수동 필드로 취급한다. 비활성 사전에서도 보존한 유효 참조는 삭제를 보호한다.

XML 내보내기는 식과 현재 결과를 함께 기록한다. 실제로 출력한 필드 이름으로 바인딩을 다시 쓰며 예약 이름이나 중복 이름은 외부 FieldID 기반 이름으로 구별한다. 매핑표는 실제 출력 FieldName을 기록한다. native의 비활성 설정을 표준 XML로 표현할 수 없으므로 비활성 식도 정의에 보존하고 진단한다. 다시 가져오면 지원식은 활성화될 수 있다. 원본 이름·실행 설정·과거 값까지 보존하려면 source.ppm을 사용한다.

## 참조

| 참조 | 의미 |
|---|---|
| `[Number1]`, `[Text1]`, `[사용자 필드 이름]` | 같은 범위의 정의된 필드. 슬롯/FieldName 우선, Alias 후순위 |
| `[field:ID]` | 이 앱 편집기의 같은 범위 안정 ID 참조 |
| `[Project.이름]`, `[project:ID]` | 프로젝트 사용자 필드. 다른 범위에서도 참조 가능 |
| `[Task.이름]`, `[task:ID]`, `[Resource.이름]`, `[resource:ID]` | 배정 식에서 해당 배정의 작업/자원 필드 |
| `[Name]` | 작업/자원/프로젝트의 현재 입력 이름. 배정·요약 그룹 이름은 지원하지 않음 |
| `[Duration]` | 작업의 명시 기간 입력을 분으로 사용. 계산된 기간을 추정하지 않음 |
| `[Work]` | 작업의 명시 공수 입력을 분으로 사용. 배정/기간에서 추정하지 않음 |
| `[Project Start]` | 명시 프로젝트 시작일 |
| `[Start]`, `[Finish]`, `[Project.Start]`, `[Project.Finish]`, `[Task.Start]`, `[Task.Finish]` | [계산된 일정 참조](xml-schedule-references.md)의 범위·계획/예측 기준 |
| `[Summary]` | 일반 작업 false, 요약 식 true |

Project./Task./Resource.와 안정 ID 네임스페이스는 이 앱에서 구별하는 참조 규칙이며 외부 제품의 해석은 별도 검증 대상이다. [계산된 Start/Finish](xml-schedule-references.md)는 후속 구현 범위를 따른다. [Actual Start/Finish](xml-actual-references.md)는 후속 지원 범위를 따른다. Baseline/remaining/actual·달력·Units·가용률은 위 후속 문서의 명시 지원 부분을 따른다. 외부 파일/서버 참조는 지원하지 않는다. 지원하지 않는 표준 이름을 사용자 Alias로 오인해 실행하지 않는다. FieldID 토큰은 `\]`, `\\` 이스케이프를 지원한다. 참조는 같은 범위/프로젝트, 배정에서 작업·자원으로만 연결한다.

## 문법과 함수

- `+ - * / Mod`, 단항 부호, 괄호, `= <> < <= > >=`, `&`, `And Or Xor Not`, True/False/Null을 지원한다. 함수 이름과 영어 연산자는 ASCII 대소문자를 구별하지 않는다. 쉼표 또는 세미콜론 인수 구분자를 받는다.
- 함수는 IIf/Switch, Abs/Int/Fix/Sgn/Round/Min/Max, CStr/CDbl/CInt/CLng/CBool, IsNull/IsNumeric, Len/Left/Right/Mid/Trim/UCase/LCase, CDate/DateSerial/DateAdd/DateDiff, Year/Month/Day/Hour/Minute/Second/IsDate, ProjDurValue를 지원한다. Min/Max는 숫자 인수2~16개다.
- IIf/Switch와 논리 연산은 모든 인수를 평가한다. `IIf(True,1,1/0)`도 오류다. Null은 일반 연산에 전파하고 IIf의 Null 조건은 미설정으로 둔다. False And Null, True Or Null은 확정값을 사용한다. 문자열 `&`는 하나의 Null을 빈 문자열로, 둘 다 Null이면 Null로 둔다.
- True의 숫자 값은-1, False는0이다. Bool끼리 논리 연산을 하고 숫자 피연산자의 And/Or/Xor/Not은32비트 정수 비트 연산이다. 분수나 범위 초과를 추측해 정수화하지 않는다. Mod도 정수만 받는다.
- 문자열은 큰따옴표를 겹쳐 이스케이프한다. `+`는 숫자 연산이며 문자열 결합에는 `&`를 쓴다. 암묵적인 문자열→숫자 변환은 하지 않는다. CDbl은 점 소수6자리만 받는다. Len/Left/Right/Mid는 BMP 문자, 대소문자 변환은 ASCII만 지원한다. Trim은 양끝 공백을 제거한다. 결과 문자열은4,096 bytes 및 native 텍스트 제약을 따른다. 문자열/날짜 포맷은 로캘 의존 형식을 추측하지 않는다.
- 날짜 리터럴 `#YYYY-MM-DD[THH:MM:SS[.소수6자리]]#`와 CDate의 ISO 문자열을 받는다. DateSerial은 정수의 유효한 연/월/일만 받고 월/일 넘침을 이월하지 않는다. DateAdd는 d/h/n/s 간격과 정수 횟수, DateDiff는 그 단위의 달력 경계 수를 계산한다. [근무 달력 함수](xml-calendar-functions.md)는 후속 지원 범위를 따른다. 시간대·현재 시각/난수는 지원하지 않는다. ProjDurValue의 문자열은 기존 분 단위 `m`/`em` 및 추정 표시 형식을 사용한다.
- 중간 수치는 소수6자리 고정점과 ±9,000,000,000,000 범위다. 곱/나눗셈과 Cost 변환의 절반은0에서 멀어지는 방향으로 반올림한다. Round/CInt/CLng는 짝수 쪽 반올림이며 Round 자릿수0~6, CInt/CLng는 각각16/32비트 범위를 따른다. 최종 Number·Cost·Duration·날짜는 각 native 필드 범위/정밀도를 다시 검증한다. VBA의 전체 Double/Variant/로캘 강제 변환과 같다고 가정하지 않는다.

식2,048 bytes·128개 값/연산·함수 인수16개·문법 중첩32·필드 참조64단계·native/XML 통합 평가128프레임·개별 평가32,768단계로 제한한다. 공유 캐시는1,024항목, 키별64KiB·키 합계16MiB까지 저장하며 큰 키는 재계산한다. 캐시를 생략해도 결과는 같다. 취소는 협력적으로 전달하며 순환·너무 깊은 식·0 나눗셈·범위/형식 오류는 상태로 표시한다. 셸·VB·JavaScript 등의 임의 코드를 실행하지 않는다.

## 요약·배정·저장·출력

CalculationType=1은 계산식의 말단 결과를 기존 roll-up 규칙으로 집계한다. CalculationType=2는 활성 XML 식을 요약 필드 값에서 계산한다. 같은 그룹의 다른 자동 요약/명시 값을 참조하고 결과는 `XML 요약 계산식` 출처로 표시한다. 요약 행의 Duration/Work·Name은 추정하지 않는다. XML 식이 비활성/없으면 기존 명시 요약 값을 보존한다.

Task/Resource XML 식은 roll-down 원본이 될 수 있다. 새 배정 수신 필드는 식/요약 규칙을 복제하지 않는 수동 필드로 만든다. 배정 식은 해당 작업/자원 필드를 참조하고 직접 수정은 차단한다. native 숫자 식과 XML 숫자 식의 양방향 참조도 계산하며 혼합 순환을 거절한다.

기본 외부 식은 조건부 schema44, [계산된 일정 바인딩](xml-schedule-references.md)은 조건부 schema45를 사용한다. 이전 데이터는 이전 최소 writer를 유지하고 schema43 이하 envelope의 새 메타데이터는 거절한다. 현재/기준선/시나리오·일정 채택·통화/ID 이관 전후 전체 사본과 복구·undo/redo에 정의/바인딩/원본 값을 보존한다. 통화 전환은 저장된 Cost 입력/스냅샷을 한 번 환산하고 식에서 재계산한다. 식의 숫자 상수는 환율로 바꾸지 않는다. 따라서 `금액필드*2`와 `숫자필드*10`처럼 상수 금액을 만드는 식의 통화 전환 결과는 서로 다를 수 있다.

XLSX/PDF는 현재 결과와 정의·활성 여부·오류/미설정을 표시한다. XLSX에 실행 수식/외부 링크를 넣지 않는다. XER는 현재 결과를 정적 값으로 투영하며 식/바인딩/실행 설정은 source.ppm과 진단에 남긴다. 계산식이 만든 날짜의 시각도 실제 결과를 기준으로 XER 텍스트 형식으로 분류한다. 일정·공수·원가 입력 지표에 계산 결과를 자동 합산하지 않는다.

| 요구 | 범위 |
|---|---|
| XMLF-01 | Formula 원본·안정 바인딩·활성 설정과 형식별 실행 |
| XMLF-02 | 연산·함수·정밀도·Null/오류·순환/깊이/취소 |
| XMLF-03 | XML 이름 대응·원본/현재 값·재교환·XER 정적 투영 |
| XMLF-04 | schema44·동결/복구·통화/ID 이관·worker·undo·읽기 전용 |
| XMLF-05 | 설정/미리보기·필드/그룹·roll-up/down·보고서·독립 판독 |

[전체161/161·최종집중21/21·파서714개·형식448파일·독립 판독/Windows UI 검증](validation/xml-formulas-2026-11-11/README.md)을 통과했다. 실제 Microsoft Project/P6 열기·재계산·재저장과 macOS/스토어 실행은 미실행이다.
