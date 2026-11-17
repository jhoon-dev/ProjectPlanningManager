# XML 계산식 추가 함수

2026-11-13 후속: [Str·Val·Hex·Oct](xml-number-text.md)를 추가했다. 소수6자리·형식 접미사 지원 부분·32비트 진수·현재 결과/선행 공백 보존과 조건부 schema57을 사용한다.

2026-11-13 후속: [TimeSerial·TimeValue](xml-time-functions.md)를 추가했다. 기존 DateValue/DatePart·근무 달력/시간대 의미를 변경하지 않고 새 시각 함수의 명시 지원 부분과 조건부 schema56을 사용한다.

2026-11-13 후속: [날짜 함수 확장](xml-date-functions.md)을 추가했다. DatePart/Weekday·DateAdd/DateDiff의10간격/주 옵션과 기존 함수/저장 버전의 호환 규칙을 따르며 새 기능은 조건부 schema55다.

2026-11-13 후속: [Format·StrConv](xml-format-strconv.md)의 별도 지원 부분을 추가했다. 아래의 과거 미지원 표기는 새 계약으로 대체하며 기존13개 함수·schema52 동작은 유지한다. 새 활성 함수는 schema54이다.

2026-11-13 후속: [명시 시간대 변환](xml-formula-timezones.md)을 PPM 확장 함수로 추가했다. 기존 DateValue 등은 시간대 없는 민간 시각 의미를 유지하며, 명시 오프셋 문자열은 PPMParseUtc로 먼저 UTC화한다. 활성 시간대 식은 조건부 schema53이다. 아래의 시간대 잔여는 이 지원 부분으로 대체한다.

2026-11-13 · XFN-01~06. 사용자 필드의 **XML 계산식…**에서 선택·검색·비교·변환·날짜 추출·제곱근 함수13개를 추가로 실행한다. 기존 계산식의 네 범위(Task/Project/Resource/Assignment), 형식별 결과·참조 바인딩·요약/전파·미리보기·교환을 사용한다. 편집기의 **추가 함수 예시 삽입**에서 함수를 선택할 수 있다.

Microsoft의 [Project 사용자 필드 함수 목록](https://support.microsoft.com/en-us/project/project-functions-for-custom-fields-in-project-desktop)에 있는 함수의 명시 지원 부분이다. 전체 VBA·언어별 문자 코드/정렬·로캘 강제 변환·실제 제품의 계산 동등성은 이 계약과 다르다. 기존 [외부 계산식](xml-external-formulas.md)의 식 크기·깊이·고정점 범위·취소·필드 결과 제약은 유지한다.

## 함수와 경계

| 함수 | 지원 규칙 |
|---|---|
| `Choose(index, choice1, …)` | 1부터 시작하는 인덱스로 선택한다. 분수는 짝수 쪽 반올림한다. 범위 밖·Null 인덱스·선택한 Null은 미설정이다. 인덱스 포함 최대16인수, 즉 최대15선택지이다. |
| `InStr([start,] string1, string2 [,compare])` | BMP 문자 위치를1부터 반환하며 없으면0이다. 2인수는 두 문자열, 3인수는 시작·두 문자열, 4인수는 비교까지이다. 비교를 지정하려면 시작도 필요하다. 시작은1~2147483647 정수, 기본1이다. 빈 원문·원문 밖 시작은0이고 나머지 빈 검색어는 시작 위치다. |
| `StrComp(string1,string2[,compare])` | 앞 문자열이 작음/같음/큼에 따라-1/0/1이다. |
| `LTrim(string)`, `RTrim(string)` | 왼쪽 또는 오른쪽 U+0020 공백만 제거한다. 반대쪽 공백과 탭은 유지한다. |
| `Space(number)` | 0~4096 정수 개의 공백을 만든다. |
| `String(number,character)` | BMP 문자열의 첫 문자를 반복한다. 숫자 문자는 ASCII32~126 정수 코드만 받는다. 빈 반복 문자는 오류이며 개수0~4096·최종4096bytes 한도를 적용한다. |
| `Asc(string)`, `Chr(code)` | 표시 가능한 ASCII32~126의 문자/코드를 변환한다. Asc는 첫 문자를 사용한다. 다른 코드 페이지·제어문자·숫자 코드의 모듈로 변환은 추측하지 않는다. |
| `CByte(expression)` | 숫자 또는 기존 점 소수6자리 문자열을 짝수 쪽 반올림해0~255로 변환한다. 반올림 후 범위 밖은 오류다. |
| `CCur(expression)` | 숫자 또는 같은 숫자 문자열을 소수4자리에서 짝수 쪽 반올림한다. 최종 Cost 필드는 통화 최소단위로 다시 검증/반올림한다. 통화 환산 함수는 아니다. |
| `DateValue(date)` | 날짜 값 또는 완전한 ISO 날짜/일시 문자열의 날짜를 취해 자정으로 만든다. Date 결과는 날짜, Start/Finish 결과는 자정 일시이다. 시각만 있는 문자열·지역 날짜 표기·시간대는 지원하지 않는다. |
| `Sqr(number)` | 비음수의 제곱근을 정수 연산으로 구해 소수6자리로 반올림한다. 큰 중간 곱을 직접 만들거나 부동소수점으로 계산하지 않는다. 음수는 오류다. |

`InStr`/`StrComp`의 기본 비교는0(BMP의 정확한 대소문자 구분 비교)이다. 비교1은 ASCII 대소문자 무시만 지원하며 비ASCII 입력은 오류로 표시한다. 비교-1/2·LCID 등은 지원하지 않는다. 검색 문자열의 Null은 미설정이지만 시작/비교 인수의 Null·범위 오류는 오류다. 위치·길이·문자 코드 인수의 분수를 자동 정수화하지 않는다.

`Choose`는 IIf/Switch와 같이 **모든 인수를 평가**한다. `Choose(1,10,1/0)`은 오류이고 `Choose(1,10,Null)`은10이다. 나머지 새 함수는 명시한 제어 인수 검사를 제외하면 기존 Null 전파를 사용한다. 오류나 미설정 결과를 원래 저장값·0으로 대체하지 않는다. 문자·숫자·Flag·Cost·Duration·Date·Start·Finish 결과는 기존 native 필드 제약을 다시 통과해야 한다.

Sqr의 중간 입력은 기존 고정점 한도 ±9,000,000,000,000이고, 일반 Number 필드의 최종 한도는 ±999999999.999999이다. 예를 들어 `Sqr(9000000000000)`의 결과3000000은 유효하지만 큰 중간 입력 자체를 Number 필드 결과로 저장할 수 있다는 뜻은 아니다. 최대6자리 고정점·CCur4자리·최종 Cost 최소단위는 서로 다른 반올림 단계다.

## 저장·기존 문서·교환

새 함수를 **활성 실행**하는 현재 문서 또는 기준선·시나리오·일정 채택·통화/ID 이관의 전체 사본이 있으면 조건부 **schema52**를 쓴다. 지원 상한을52로 올리되 기존 함수만 사용하는 문서는 이전 최소 writer를 유지한다. 새 영속 필드를 추가하지 않는다. 문자열 리터럴·필드 참조 안의 `Sqr(` 같은 글자를 함수 호출로 오인하지 않는다.

과거에 미지원 식으로 비활성 보존한 새 함수는 schema44부터 원본·바인딩·비활성 설정 그대로 읽을 수 있다. native 파일을 연다고 자동 활성화하지 않는다. 활성 새 함수가 담긴 schema51 이하 문서는 거절한다. 이후 편집기에서 명시 활성화하면 필요한 writer가52로 올라간다. 새 함수의 비활성 원본만 남고 다른 고급 기능이 없으면 기존 최소 writer44를 사용할 수 있다.

XML 가져오기는 지원 문법을 기존 규칙대로 활성화한다. 알 수 없는 함수는 원본/스냅샷과 함께 비활성 보존한다. 내보내기는 원본 바인딩을 실제 XML 필드 이름으로 다시 쓰고 현재 결과를 기록한다. XML에는 native 비활성 설정을 표현하지 못하므로 다시 가져오면 지원식이 활성화될 수 있다. 전체 원본/설정·이력은 source.ppm을 함께 보관한다. XER는 현재 결과를 정적 값으로 출력한다.

활성 계산값의 직접 입력 제한, 입력 필드 이름 변경 뒤 안정 ID 참조, native 계산식과의 연결, 요약 roll-up/요약 식·배정 roll-down을 유지한다. 통화 전환은 원래 Cost 입력을 환산한 뒤 식을 다시 계산하며 식의 숫자 상수는 환산하지 않는다. 미리보기는 작업 스냅샷에서 비동기 실행하고 취소/세션·revision 변경을 검사한다. 저장·복구·undo·읽기 전용과 XLSX/PDF의 값·정의·오류/미설정 표시도 기존 경로를 사용한다.

## 검증과 남은 범위

| 계약 | 검사 |
|---|---|
| XFN-01 | 13개 함수·네 범위·모든 결과 형식·BMP/ASCII 위치·비교/변환 경계 |
| XFN-02 | Null/오류/전체 인수 평가·정수 제약·반올림·512개 큰 수 제곱근 독립 계산 |
| XFN-03 | 활성52/기존44·비활성 레거시 읽기·오래된 태그 거절·동결/전체 사본·복구 |
| XFN-04 | XML 정의/현재 값·미지원 식 보존·재계산·XER 정적 값·진단 |
| XFN-05 | 예시 삽입·비동기 미리보기/저장/취소/변경·native/요약/전파 연결 |
| XFN-06 | 원본 스냅샷·통화/ID 이관·XLSX/PDF·파서/회귀·Windows 실행 패키지 |

Format/StrConv·다른 수학/날짜 함수·추가 날짜 간격·현재 시각/난수·지역별 정렬/문자 코드·시간대 변환과 실제 Microsoft Project/P6 열기·재계산·재저장은 잔여다. 이번 지원으로 모든 Project/VBA 함수를 실행한다고 해석하지 않는다.

공식 세부 의미는 [Choose](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/choose-function), [InStr](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/instr-function), [StrComp](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/strcomp-function), [String](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/string-function)을 참고한다. 위의 ASCII/BMP·정수·고정점 제약은 이 앱의 명시 지원 부분이다.

[검증 기록](validation/xml-additional-functions-2026-11-13/README.md) · [구현](../src/domain/XmlFormula.cpp) · [검사](../tests/XmlAdditionalFunctionTests.cpp) · [독립 계산](../scripts/verify-xml-additional-functions.py)
