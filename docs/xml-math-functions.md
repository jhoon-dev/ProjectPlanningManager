# XML 계산식 추가 수학 함수

2026-11-14 후속: 다음 후보였던 [문자열 함수3개](xml-string-functions.md)를 구현했다. Replace/InStrRev/StrReverse 활성 식/전체 사본은 schema59이며 이 문서의 수학 함수 계약/최소58을 유지한다.

2026-11-13 · XMH-01~06. 사용자 필드의 **XML 계산식…**에서 Atn/Cos/Exp/Log/Sin/Tan을 실행한다. 기존 Abs/Int/Fix/Sgn/Round/Sqr와 함께 사용할 수 있다.

| 함수 | 의미와 입력 |
|---|---|
| `Atn(number)` | 역탄젠트. 결과는 -π/2~π/2 라디안이다. |
| `Cos(number)` | 라디안 각도의 코사인. |
| `Sin(number)` | 라디안 각도의 사인. |
| `Tan(number)` | 라디안 각도의 탄젠트. 극점 근처에서는 매우 민감한 근삿값이다. |
| `Exp(number)` | 자연상수 e의 거듭제곱. |
| `Log(number)` | 0보다 큰 값의 자연로그. 밑10 로그는 `Log(x)/Log(10)`으로 구성한다. |

각 함수의 인수는 정확히1개다. 예를 들어 `Atn(1)`은0.785398, `Sin(1)`은0.841471, `Cos(0)`은1, `Tan(1)`은1.557408, `Exp(1)`은2.718282, `Log(2)`는0.693147이다. `4*Atn(1)`은 중간 단계에서 반올림된 Atn 결과를 사용한다. 이를 정확한 π 상수로 간주하지 않는다.

## 수치 계약

입력/결과의 중간 표현은 기존 ±9조·소수6자리다. 숫자·Boolean(True=-1/False=0)·기존 CDbl과 같은 엄격한 점 소수 숫자 문자열을 받는다. 날짜를 자동 숫자화하거나 시간/공수/통화를 추론하지 않는다. `Log(True)`·`Log(0)`·음수의 Log는 정의역 오류다. Null은 기존 native 규칙대로 전파하고 모든 인수를 평가한다. `IIf(True,1,Log(0))`도 오류다.

초월함수는 플랫폼 C++ `long double` 수학 라이브러리의 **근사 계산**이다. 큰 정수와 소수 부분을 나누고 FMA 잔차와 각도 합/로그 보정으로 입력 소수의 손실을 줄인다. 결과를 소수6자리로 반올림하며 절반은0에서 멀어지는 방향이다. 이는 소수6자리의 정확한 초월함수 반올림 보증이 아니다. 큰 Exp 값·큰 라디안 입력·Tan 극점 부근에서는 마지막 소수 자리가 다를 수 있고 플랫폼의 부동소수 정밀도에 따라 차이가 날 수 있다. 완전한 VBA Double 비트 동일성이나 제품 실행 동등성을 주장하지 않는다.

유한하지 않거나 반올림 전후의 절댓값이9조를 넘으면 명시 오류다. Exp의 허용 상한은 약29.828245693이며 소수6자리 입력29.828245는 허용하고29.828246은 거절한다. 매우 작은 결과는0으로 반올림할 수 있다. 예를 들어 Exp(-14.508658)=0, Exp(-14.508657)=0.000001이다. -16 이하의 Exp는 부동소수 underflow를 일으키지 않고0을 반환한다. 음의0/NaN/Infinity는 저장하지 않는다.

최종 Number/Cost/Duration 필드의 기존 범위·단위 규칙은 별도로 적용된다. 큰 중간값을 Text/CStr로 확인할 수 있어도 최종 Number 범위를 초과하면 오류다. 저장된 필드 스냅샷으로 오류나 미설정을 대체하지 않는다. 계산 결과가 일정·공수·원가 원장에 자동 반영되지 않는다.

## 저장·편집·교환

새 함수의 활성 호출 또는 전체 보관 사본이 있으면 조건부 **schema58**을 사용한다. 과거 태그의 활성 호출을 거절하며 비활성 원문은 기존 schema44부터 자동 활성화 없이 읽는다. 문자열/날짜 리터럴과 참조 이름에 들어 있는 함수명은 호출로 간주하지 않는다. 새 native 데이터 필드는 없다. 기준선·시나리오·일정 채택·통화/ID 전체 이력과 복구·읽기 전용·undo/redo를 연결한다. 다른 기능의 최소 저장 버전은 유지한다.

편집기는 예시6개와 비동기 미리보기·저장/취소를 제공한다. 작업/프로젝트/자원/배정 네 범위, 요약 roll-up·배정 roll-down·native 식 연결을 지원한다. 통화 전환은 Cost 입력을 환산한 뒤 Log 등의 결과를 다시 계산하며 식의 상수·라디안 값을 환산하지 않는다.

XML은 정의·현재 값·참조 이름을 교환하고 다시 가져와 재계산한다. XER는 정적 값을 기록하며 XLSX/PDF는 값·정의·오류/미설정을 표시한다. XML에는 native 비활성 설정이 없어 지원 식을 재가져오면 활성화될 수 있다. 전체 원본 이력은 source.ppm을 보관한다.

의미의 원문은 Microsoft의 [Atn](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/atn-function), [Cos](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/cos-function), [Exp](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/exp-function), [Log](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/log-function), [Sin](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/sin-function), [Tan](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/tan-function)이다. 위 native 범위·입력/결과 반올림과 근사 계산은 이 앱의 명시 지원 계약이다.

[검증 기록](validation/xml-math-functions-2026-11-13/README.md). 다음 잔여는 추가 Project/문자열 함수와 실제 제품 왕복 및 기존 GAP 항목이다. 현재 시각·난수·외부 파일/서버 참조는 지원하지 않는다.
