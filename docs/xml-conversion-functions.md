# XML 계산식 CDec·CSng·CVar

2026-11-14 후속: [Project 날짜·기간 변환](xml-project-conversion-functions.md)을 구현했다. ProjDateValue/ProjDateConv/ProjDurConv 활성 식/전체 사본은 schema61이며 이 문서의 CDec/CSng/CVar 계약·최소60을 유지한다.

2026-11-14 · XCV-01~06. 사용자 필드의 XML 계산식에서 추가 형 변환3개를 지원한다.

| 함수 | 이 앱의 지원 계약 |
|---|---|
| `CDec(value)` | native 소수6자리 숫자로 변환한다. 숫자·Boolean·엄격한 점 소수 숫자 문자열을 받는다. |
| `CSng(value)` | 같은 입력을 가장 가까운 IEEE754 binary32 값으로 변환하고 소수6자리로 되돌린다. |
| `CVar(value)` | Number/Text/Boolean/Date/Null과 기간의 근무·경과/추정 구분을 그대로 보존한다. |

예를 들어 CDec("1.234567")=1.234567, CSng(16777217)=16777216, CSng(16777219)=16777220이다. 음수에도 대칭으로 적용한다. CVar("001.20")는 숫자로 파싱하지 않고 원문을 유지하며 CVar(True)는 Boolean이다. 날짜 값에는 날짜 추출을 계속 적용할 수 있다. CVar(ProjDurValue("1440 em?"))는 경과1440분·추정 표지를 유지한다. 일정 원장이나 원래 필드 값을 수정하지 않는다.

## 정밀도·범위

중간 값은 기존 ±9조·소수6자리다. CDec는 28자리 Decimal 형식을 새로 만들지 않는다. 6자리를 넘는 소수나 지수/쉼표/통화 표시 숫자 문자열은 기존 CDbl 파서와 같이 거절한다. True=-1, False=0이며 날짜를 숫자로 암묵 변환하지 않는다. CVar는 native 형식만 보존하며 VBA의 전체 Variant 하위 형식·배열·객체·Empty·Error 값을 추가하지 않는다.

CSng는 **정확한 십진 입력을 binary32로 최근접/짝수 반올림**한 뒤, 그 정확한 이진 값을 native 소수6자리로 절반은0에서 멀어지는 방향으로 반올림한다. 양쪽 단계를 정수 비트·나머지 연산으로 수행하며 OS·부동소수 반올림 설정·로캘에 의존하지 않는다. 결과를 다시 사용하는 연산은 native 고정점 연산이며 Single 연산 체계 전체가 아니다. 작은 값에서는 native6자리로 인해 원래 값과 같아질 수 있다. native 최소 비영 값은0.000001이므로 Single의 subnormal 범위를 표현하지 않는다.

반올림 전 단정밀도 결과가 native 범위를 넘으면 오류다. 예를 들어 입력9조는 native 범위 안이지만 CSng 결과9,000,000,159,744는 범위 밖이라 거절한다. NaN/Infinity/음의0을 저장하지 않는다. 최종 Number/Cost/Duration 필드의 기존 범위는 별도로 적용한다. 큰 중간값을 Text/CStr로 볼 수 있어도 최종 Number 한도를 초과하면 오류다.

CDec/CSng는 기간 표지를 숫자로 제거하며 CVar는 보존한다. 따라서 경과 기간을 ProjDateAdd에 넘길 때 CVar를 쓰면 경과 시계가 유지된다. 문자열의 숫자를 자동 변환하지 않아 CVar("12")+1은 형식 오류이며 CDec로 명시 변환한다. 세 함수의 Null은 기존 native 미설정으로 전파한다. 모든 인수를 평가하므로 선택되지 않은 분기의 범위 오류도 감추지 않는다. 저장된99/777 스냅샷을 현재 결과·오류 대신 사용하지 않는다.

## 저장·편집·교환

활성 새 호출 또는 전체 보관 사본이 있으면 조건부 **schema60**이다. 과거 태그의 활성 호출은 거절하고 비활성 원문은 기존44부터 자동 활성화 없이 읽는다. 문자열/날짜 리터럴과 참조 이름의 함수명은 호출로 보지 않는다. 새 native 필드는 없다. 기준선·시나리오·일정 채택·통화/ID 전후 전체 사본과 저장/복구·읽기 전용·undo/redo를 연결한다. 기존 기능의 최소 버전은 유지한다.

편집기 예시4개와 비동기 미리보기·저장/취소, 네 범위·요약·배정 전파·native 숫자 식을 지원한다. Cost 입력을 통화 전환하면 CDec 결과를 다시 계산하며 식의 상수·숫자 문자열을 통화 금액으로 취급하지 않는다. XML은 식 정의·참조 이름·현재 값과 재계산, XER는 정적 값, XLSX/PDF는 값·정의·오류/미설정을 기록한다. XML에는 native 비활성 표지가 없어 지원 식의 재가져오기에서 활성화될 수 있다. 전체 이력은 source.ppm에 보관한다.

원문은 Microsoft의 [Project 함수 목록](https://support.microsoft.com/en-us/project/project-functions-for-custom-fields-in-project-desktop)과 [VBA 형 변환 함수](https://learn.microsoft.com/en-us/office/vba/language/concepts/getting-started/type-conversion-functions)다. 이 문서의 고정점·입력 파서·형식 보존은 이 앱의 명시 지원 부분이며 전체 VBA Decimal/Single/Variant·NLS 또는 실제 제품 왕복의 동등성을 주장하지 않는다.

[검증 기록](validation/xml-conversions-2026-11-14/README.md). 다른 Project 함수·번호별 기준선/Cost·요약 기간과 기존 GAP/실제 제품 검증은 잔여다.
