# XML 계산식 FormatNumber·FormatPercent·FormatCurrency

2026-11-14 · XNF-01~06. 작업·프로젝트·자원·배정 계산식에서 숫자·백분율·통화 표시 문자열을 만든다. 기존 Format의 정확한 십진 문자열 표시 경로를 재사용한다.

```text
FormatNumber(value[,digits[,leading[,parentheses[,grouping]]]])
FormatPercent(value[,digits[,leading[,parentheses[,grouping]]]])
FormatCurrency(value[,digits[,leading[,parentheses[,grouping]]]])
```

| 식 | 결과 |
|---|---|
| `FormatNumber(1234.5)` | 1,234.50 |
| `FormatPercent(0.125,1)` | 12.5% |
| `FormatCurrency(-1234.5)` | ($1,234.50) |
| `FormatNumber(-0.5,2,0,-1,0)` | (.50) |
| `FormatNumber(1.25,1)` | 1.3 |
| `FormatNumber(0,0,0)` | 빈 문자열 |

XNF-01: 입력은 native 소수6자리 Number 또는 Boolean의 숫자 값(True=-1/False=0)이다. 숫자 문자열·Date를 암묵 변환하지 않으며 필요한 경우 CDbl 등을 명시한다. Percent는 표시에서100배 하고 `%`를 붙이며 원본 수치는 바꾸지 않는다. **Currency는 고정 en-US `$` 표시이며 문서 통화나 환산을 의미하지 않는다.** 통화 기호를 바꾸려면 기존 명시 프로필 PPMFormat 또는 원하는 리터럴 표시를 사용한다. 세 함수의 결과는 모두 Text다.

XNF-02: digits 생략/-1은 고정 기본2자리, 그 외는0~6자리다. leading·parentheses·grouping은 -2(고정 기본), -1(사용), 0(해제)의 TriState이다. 기본은 선행0 사용·세 자리 쉼표 구분이며 음수 괄호는 Currency에서만 사용한다. Number/Percent 기본 음수는 마이너스다. 소수점은`.`이며 OS 로캘·지역 설정은 조회하지 않는다. 중간 인수의 빈 자리 문법은 지원하지 않으므로 뒤 옵션을 지정할 때 앞 옵션도 명시한다.

옵션 수치는 **짝수 쪽 정수 반올림 후** 범위를 검사한다. digits1.5/2.5는2, leading-0.5는0, -1.5는-2가 된다. True/False는 각각-1/0으로 사용할 수 있다. digits7·TriState1·범위 초과·텍스트 옵션·모든 인수의 Null은 오류다. IIf의 모든 인수 평가 규칙도 유지하므로 `IIf(True,1,FormatCurrency(Null))`은 오류다. 미설정 결과는 안전한 기존 typed 식으로 명시할 수 있다.

XNF-03: 표시 반올림은 기존 Format과 같은 **정확한 십진 반올림(절반은 절댓값 증가)**이다. 기존 Round/CInt의 짝수 반올림과 구별한다. FormatNumber(2.5,0)는3이다. 소수6자리 입력을 binary float로 바꾸지 않으며 Percent의100배 표시도 문자열 자리 이동으로 처리해 native 최대 입력±9,000,000,000,000을 표시할 수 있다. 소수부의 지정 자릿수와 후행0을 유지한다.

원본 입력이 음수면 표시 반올림 후0이 되어도 마이너스/괄호를 유지한다. 예: FormatNumber(-0.001,2)는 `-0.00`이다. 선행0을 끄면 소수부 앞0을 생략하며, 자릿수0과 값0의 조합은 빈 문자열이 될 수 있다. 빈 문자열·미설정·오류를 구별하고 저장된 스냅샷이나 임의0으로 대체하지 않는다. 전체 VBA의 소수 자릿수·Double/Variant·로캘 동등성은 주장하지 않는다.

XNF-04: 활성 새 함수가 현재·기준선·시나리오·일정 채택·통화/ID 이력 전후 전체 사본에 있으면 조건부 **schema69**다. schema68 이하는 활성 새 식을 거절한다. 비활성 원문은 기존 schema44부터 보존하며 다른 기능/참조의 최소 버전도 유지한다. native 필드는 추가하지 않는다. 안정 ID·이름 변경·undo/redo·저장/재열기·복구·읽기 전용과 취소 checkpoint를 연결한다.

XNF-05: 편집기 예시·worker 미리보기, 네 범위·요약·roll-down·native 숫자 식을 지원한다. 예를 들어 Len(FormatNumber([입력수]))를 native 숫자 식에서 참조한다. 통화 전환 후 Cost 입력의 표시도 다시 계산하지만 Currency의 고정 `$` 기호는 바뀌지 않는다. XML은 정의·참조·현재 값 및 재수입 실행, XER는 정적 결과, source.ppm은 전체 정의/이력, XLSX/PDF는 값과 별도 오류/미설정을 보존한다.

XNF-06: 세 함수×8개 자릿수 옵션×27개 TriState 조합×25개 수치, 합계 **16,200건**을 Python Decimal로 독립 검증한다. 그중 binary float에서 정확한 값7,128건과 직접 식은 Windows VBScript(SetLocale1033)로 비교한다. native6자리 한도로 digits7을 거절하는 차이는 별도로 남긴다. XML/XER/XLSX는 Text의 후행0·괄호·구분자를 수치 동등성으로 뭉개지 않고 정확히 검사한다. 실제 Project/P6 실행 왕복은 별도 잔여다.

구문 기준은 Microsoft [FormatNumber](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/formatnumber-function)·[FormatPercent](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/formatpercent-function)·[FormatCurrency](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/formatcurrency-function)다. [검증 기록](validation/xml-numeric-display-2026-11-14/README.md). 다른 함수·번호별 기준선·요약 기간과 기존 GAP도 계속 잔여다.
