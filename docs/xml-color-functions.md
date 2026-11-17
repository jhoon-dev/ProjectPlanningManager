# XML 계산식 RGB·QBColor

2026-11-14 · XCL-01~06. 작업·프로젝트·자원·배정 계산식에서 색상의 정수 코드를 만든다.

```text
RGB(red,green,blue)
QBColor(index)
```

| 식 | 결과 |
|---|---|
| `RGB(255,0,0)` | 255 |
| `RGB(0,255,0)` | 65280 |
| `RGB(0,0,255)` | 16711680 |
| `RGB(1,2,3)` | 197121 |
| `Hex(RGB(1,2,3))` | 30201 |
| `RGB(256,1,2)` | 131583 |
| `RGB(1.5,2.5,3.5)` | 262658 |
| `QBColor(7)` | 12632256 |
| `QBColor(15)` | 16777215 |

XCL-01: RGB의 각 성분은 native 소수6자리 Number 또는 Boolean의 숫자 값이다. 먼저 **짝수 쪽 정수 반올림**하고 **0~32767** 범위를 검사한 뒤 **255 초과를255로 제한**한다. 255.5는255, -0.5는0, 32767.499999는255가 되며 -1·32767.5·32768은 오류다. 숫자 문자열·Date는 명시적으로 CDbl/CInt 등으로 변환한다. False=0은 사용할 수 있고 True=-1은 범위 오류다.

XCL-02: 결과는 `red + green*256 + blue*65536`, 범위0~16777215의 Number다. **빨강이 하위 바이트**다. CSS의 `#RRGGBB` 문자열과 바이트 순서가 다르므로 Hex 결과를 그대로 CSS 색상으로 사용하면 안 된다. 계산된 색상 번호는 사용자 필드 값이며 화면의 글자색·채우기색을 자동 변경하지 않는다.

XCL-03: QBColor는 짝수 반올림한0~15의 번호에 해당하는 기존 Basic 16색 정수 값을 반환한다. 0~7은 검정·짙은 파랑·짙은 초록·짙은 청록·짙은 빨강·짙은 자홍·짙은 노랑·밝은 회색, 8~15는 회색·파랑·초록·청록·빨강·자홍·노랑·흰색이다. 2.5는2, 14.5는14, 15.499999는15이며 15.5·16·-1은 오류다. 검정은0, 밝은 회색은각 성분192, 회색은각 성분128이다.

XCL-04: 모든 인수의 Null은 오류다. 기존 IIf의 모든 인수 평가 규칙을 유지하므로 `IIf(True,1,QBColor(Null))`도 오류다. 미설정 결과는 `IIf(True,Null,RGB(0,0,0))`처럼 별도로 표현한다. 오류·미설정과 저장된 스냅샷을 구별하고 임의0으로 대체하지 않는다. 함수 이름은 대소문자를 구별하지 않고 쉼표/세미콜론 구분자를 지원한다. 전체 VBA Variant·암묵 변환 동등성은 주장하지 않는다.

XCL-05: 활성 새 함수가 현재·기준선·시나리오·일정 채택·통화/ID 이력 전후 전체 사본에 있으면 조건부 **schema70**다. schema69 이하는 활성 새 식을 거절한다. 비활성 원문은 기존 schema44부터 보존하며 다른 기능/참조의 최소 버전도 유지한다. native 필드는 추가하지 않는다. 안정 ID·이름 변경·undo/redo·저장/재열기·복구·읽기 전용·취소 checkpoint를 연결한다. 편집기 예시와 worker 미리보기, 네 범위·요약·roll-down·native 숫자 식과 통화 전환 후 재계산을 지원한다.

XCL-06: XML은 정의·참조·현재 값 및 재수입 실행, XER는 정적 결과, source.ppm은 전체 정의/이력, XLSX/PDF는 값과 별도 오류/미설정을 보존한다. Python의 little-endian 바이트 결합·독립 팔레트 계산으로5,609건을 검사한다. RGB5,593건·직접 식17건·오류8건은 Windows VBScript, QBColor16색은 Windows .NET Microsoft.VisualBasic과 비교한다. VBScript 자체에는 QBColor가 없다. 실제 Project/P6 실행 왕복은 별도 잔여다.

구문 기준은 Microsoft [RGB](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/rgb-function)·[QBColor](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/qbcolor-function)·[MS-VBAL QBColor 표](https://learn.microsoft.com/en-us/openspecs/microsoft_general_purpose_programming_languages/ms-vbal/4b7087c6-20fc-4d90-8d83-730a0c6a4aad)다. [검증 기록](validation/xml-colors-2026-11-14/README.md). 다른 함수·번호별 기준선·요약 기간과 기존 GAP도 계속 잔여다.
