# XML 계산식 AscW·ChrW

2026-11-14 · XUC-01~06. 작업·프로젝트·자원·배정 XML 계산식에서 Unicode 문자와 코드의 명시 변환을 지원한다. 기존 Asc/Chr의 표시 가능한 ASCII32~126 계약은 유지한다.

| 식 | 결과 |
|---|---|
| `AscW("A")` | 65 |
| `AscW("가나다")` | -21504 |
| `ChrW(44032)` 또는 `ChrW(-21504)` | 가 |
| `AscW(ChrW(32768))` | -32768 |
| `ChrW(44031.5)` | 가 |

XUC-01: **native/XML 텍스트에 표현 가능한 BMP 문자**를 사용한다. AscW는 문자열 첫 문자의 부호 있는 16비트 코드(0~32767 또는 -32768~-1)를 Number로 반환한다. U+8000 이상은 코드에서65536을 뺀다. UTF-8 바이트 값이나 현재 Windows ANSI 코드페이지를 사용하지 않는다. 문자열 전체는 유효한 UTF-8 BMP여야 하며 빈 입력·4,096 bytes 초과·잘못된 UTF-8·보조 평면·서로게이트는 오류다. 첫 문자 뒤에 있는 잘못된 문자도 거절한다.

XUC-02: ChrW는 숫자를 **짝수 쪽 정수 반올림**한 뒤 -32768~65535를 받는다. 음수에65536을 더한 코드와 양수 코드가 같은 문자를 의미한다. 예를 들어 -21504와44032는 모두 `가`다. 반올림 후 범위를 검사하므로65535.5는 범위 오류다. 문자열/날짜는 숫자로 암묵 변환하지 않는다. 필요한 경우 CDbl 등을 명시한다.

XUC-03: 두 함수는 Null을 오류로 처리한다. 미설정 결과는 `IIf(IsNull([입력]),Null,AscW("A"))` 같은 기존 typed 식으로 표현할 수 있으나 IIf의 모든 인수는 평가된다. `IIf(True,1,ChrW(0))`도 오류다. 숫자/Boolean을 AscW에 넘기려면 CStr를 명시한다. 결과 없음·오류를 저장된 원본 값이나0으로 대체하지 않는다.

지원 문자에서 **C0 제어문자(탭·LF·CR 제외), DEL, U+D800~DFFF, U+FFFE/FFFF**를 제외한다. 이 제약은 함수 중간 값에도 적용한다. 따라서 VBA가 생성할 수 있는 NUL·단독 서로게이트도 이 앱에서는 생성하지 않으며, 두 서로게이트를 결합해 이모지를 만드는 용도로 사용할 수 없다. XML1.0과 native 필드에서 재현 가능한 부분만 지원하며 전체 UTF-16/VBA/Project 동등성을 주장하지 않는다. 같은 제한 안에서 탭·LF·CR은 사용할 수 있고 최종 필드 형식의 기존 검증도 적용한다.

XUC-04: 활성 새 함수가 현재·기준선·시나리오·일정 채택·통화/ID 이력 전후 전체 사본에 있으면 조건부 **schema66**이다. schema65 이하는 활성 새 식을 거절한다. 비활성 원문은 기존 schema44부터 보존하고 다른 기능/참조의 최소 버전도 유지한다. native 데이터 필드는 추가하지 않는다. 안정 ID 바인딩·이름 변경·undo/redo·저장/재열기·복구·읽기 전용과 취소 checkpoint를 연결한다.

XUC-05: 편집기에 예시 삽입과 worker 미리보기를 연결한다. 네 범위·요약 식·roll-down·native 숫자 식과 결합할 수 있다. 통화 전환 후 `AscW(CStr([금액]))`는 환산 입력에서 다시 계산한다. XML은 정의·참조·현재 값을 내보내고 재수입 후 실행한다. XER는 정적 현재 값, source.ppm은 전체 정의/이력, XLSX/PDF는 결과와 오류/미설정을 보존한다.

XUC-06: 지원되는 BMP **63,456개 전체**의 UTF-8 결과와 부호 코드를 Python Unicode codec으로 독립 판독하고, 같은 코드와 음수 별칭을 Windows VBScript AscW/ChrW와 비교한다. 직접 식·반올림·Null/빈 입력·오류·저장/전체 사본·네 범위·교환·보고서·작은 화면도 검사한다. 실제 Microsoft Project/P6에서 열기·재계산·재저장은 별도 잔여다.

구문과 개념은 Microsoft [Asc/AscW](https://learn.microsoft.com/en-gb/office/vba/language/reference/user-interface-help/asc-function)와 [Chr/ChrW](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/chr-function)를 기준으로 한다. [검증 기록](validation/xml-unicode-2026-11-14/README.md). 다른 VBA/Project 함수·번호별 기준선·요약 기간·로캘/코드페이지와 기존 GAP는 계속 잔여다.
