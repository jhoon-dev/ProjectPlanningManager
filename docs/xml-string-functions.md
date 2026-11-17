# XML 계산식 Replace·InStrRev·StrReverse

2026-11-14 후속: [CDec/CSng/CVar](xml-conversion-functions.md)의 추가 형 변환을 구현했다. 활성 호출/전체 사본은 schema60이며 이 문서의 문자열 계약/최소59를 유지한다.

2026-11-14 · XST-01~06. 사용자 필드의 **XML 계산식…**에서 명시 문자열 치환·뒤에서 검색·문자 순서 반전을 지원한다.

| 함수 | 지원 문법·기본값 |
|---|---|
| `Replace(text, find, replacement[, start[, count[, compare]]])` | start=1, count=-1(전체), compare=0. 왼쪽부터 겹치지 않는 검색어를 치환한다. |
| `InStrRev(text, find[, start[, compare]])` | start=-1(마지막 문자), compare=0. 뒤에서 찾은 첫 일치의1부터 시작하는 위치이며 없으면0이다. |
| `StrReverse(text)` | BMP 문자 순서를 반전한 문자열이다. |

```text
Replace("abcabc", "ab", "X")             → XcXc
Replace("abcabc", "ab", "X", 3)          → cXc
Replace("ABCabc", "ab", "X", 1, -1, 1)  → XCXc
InStrRev("ababa", "aba")                 → 3
InStrRev("ababa", "aba", 4)              → 1
StrReverse("가Ab中")                      → 中bA가
```

## 문자열·위치·비교 계약

위치와 길이는 UTF-8 바이트가 아닌 **BMP 문자** 기준이다. 유효한 UTF-8 BMP만 받고 overlong·잘못된 연속 바이트·서로게이트·BMP 밖 문자(이모지 등)는 명시 오류다. 결합문자도 각각의 문자로 취급한다. 예를 들어 `StrReverse("é")`는 결합 악센트가 e 앞에 온다. 정규화·글자 묶음(grapheme) 반전이나 UTF-16 서로게이트 반전을 수행하지 않는다.

compare=0은 정확한 문자 비교다. compare=1은 **검색 대상과 검색어의 ASCII 대소문자만 무시**하며 두 입력에 비ASCII 문자가 있으면 오류다. 치환할 새 문자열은 BMP여도 된다. -1(Option Compare)·2(Database)·PC 로캘/NLS의 대조 규칙은 지원하지 않는다. 비ASCII 검색은0으로 명시한다. 기존 InStr/StrComp 계약을 바꾸지 않는다.

제어 인수는 native 정수만 받는다. 숫자 문자열·분수의 암묵적 정수 반올림을 하지 않는다. Replace start는1~2147483647, count는-1~2147483647이다. InStrRev start는-1 또는1~2147483647이며0은 오류다. 선택 인수는 순서대로 쓰고 중간 생략 쉼표/이름 있는 인수는 지원하지 않는다. 기본값을 명시해 뒤 옵션을 지정한다.

Replace의 결과는 **start부터 끝까지의 문자열**이다. 앞부분을 보존하지 않는다. 빈 find 또는 count=0이어도 같은 시작 이후 문자열을 반환한다. start가 길이를 넘으면 빈 문자열이다. replacement가 비어 있으면 검색어를 삭제한다. 원본의 겹치지 않는 일치만 치환하며 새 문자열을 다시 검색하지 않는다. 예를 들어 Replace("aaaaa","aa","X")는XXa이다.

InStrRev는 검색어의 **끝 문자 위치가 start 이내**인 일치만 찾는다. start가 길이를 넘으면0이며 길이로 줄이지 않는다. 검색 대상이 비면0이다. 검색어가 비고 start가 유효하면 실제 start를 반환한다. 모든 입력과 compare를 검증한 뒤 빈 입력·횟수0 같은 빠른 반환을 적용한다.

입력과 최종 문자열은 각각 최대4096bytes다. 치환 결과의 증가를 추가 전에 검사하며 잘라내지 않는다. 반복 검색·문자 변환에 취소 검사를 연결한다. 문자열 인수는 Text만 받고 숫자/Boolean/날짜는 CStr로 명시 변환한다.

## Null·오류·저장

Replace의 문자열 인수 셋 중 하나라도 Null이면 오류이고 StrReverse(Null)도 오류다. InStrRev의 검색 문자열이 Null이면 native 미설정으로 전파한다. 제어 인수의 Null·잘못된 시작/횟수/비교는 미설정으로 감추지 않고 먼저 오류를 낸다. 모든 인수를 평가하므로 `IIf(True,1,StrReverse(Null))`도 오류다. 원본에 저장된 필드 스냅샷을 현재 결과나 오류 대신 사용하지 않는다.

활성 새 함수 또는 전체 보관 사본이 있으면 조건부 **schema59**이다. 이전 태그의 활성 식을 거절하며 비활성 원문은 기존 schema44부터 자동 활성화 없이 읽는다. 문자열/날짜 리터럴·참조 이름에 들어 있는 함수명은 호출로 판정하지 않는다. 새 native 데이터 필드는 없으며 기존 기능의 최소 저장 버전을 유지한다. 현재·기준선·시나리오·일정 채택·통화/ID 전체 이력, 저장/복구·읽기 전용·undo/redo를 연결한다.

## 편집·교환·검증

편집기 예시4개, 비동기 미리보기·저장/취소, 네 범위(작업/프로젝트/자원/배정), 요약·배정 전파·native 숫자 식 참조를 지원한다. Cost 입력이 바뀌거나 통화 전환되면 CStr/Replace를 다시 계산하며 문자열의 숫자나 식 상수를 통화 값으로 해석하지 않는다. 원래 입력과 원장을 유지한다.

XML은 정의·참조 이름·현재 값을 교환하고 재가져와 재계산한다. XER는 정적 값이며 XLSX/PDF는 값·정의·오류/미설정을 표시한다. XML에는 native 비활성 설정이 없어 지원 식을 다시 가져오면 활성화될 수 있다. 전체 원본과 이력은 source.ppm을 보관한다.

원문 의미는 Microsoft의 [Replace](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/replace-function), [InStrRev](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/instrrev-function), [StrReverse](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/strreverse-function)를 참고했다. Windows VBScript는 InStrRev(Null,...)에 오류를 내지만 이 앱은 VBA 문서의 Null 전파를 명시 채택한다. VBScript와 비교하는 범위는 Null이 없는 BMP/ASCII·정수 제어 인수다. 전체 VBA/NLS·자동 형 변환·실제 MS Project/P6 왕복의 동등성을 주장하지 않는다.

[검증 기록](validation/xml-string-functions-2026-11-14/README.md). 다음 잔여는 다른 Project/문자열 함수·번호별 기준선/Cost·요약 기간 및 기존 GAP와 실제 제품 왕복이다.
