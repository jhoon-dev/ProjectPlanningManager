# XML 계산식 SLN·SYD

2026-11-15 · XDP-01~06. 작업·프로젝트·자원·배정 계산식에서 정액법과 연수합계법 감가상각 수치를 계산한다.

```text
SLN(cost,salvage,life)
SYD(cost,salvage,life,period)
```

| 식 | 결과 |
|---|---|
| `SLN(100,10,5)` | 18 |
| `SYD(100,10,5,1)` | 30 |
| `SYD(100,10,5,5)` | 6 |
| `SLN(1,0,3)` | 0.333333 |
| `SLN(0.000001,0,2)` | 0.000001 |
| `SLN(10,100,5)` | -18 |

XDP-01: cost·salvage는0 이상, life는0보다 커야 한다. SYD의 period는0보다 크고 life 이하여야 한다. 모든 인수는 native 소수6자리 Number 또는 Boolean의 숫자 값이고 범위는0~9,000,000,000,000이다. True=-1은 범위 오류, False=0은 해당 위치의 범위 조건을 따른다. 숫자 문자열·Date는 암묵 변환하지 않는다. 문자열 숫자는 CDbl 등으로 명시 변환한다. 이 지원 부분은 음수 입력을 거절하며 .NET SLN의 음수 인수 허용 전체와 같지 않다.

XDP-02: SLN은 `(cost-salvage)/life`, SYD는 `2*(cost-salvage)*(life-period+1)/(life*(life+1))`다. life와 period는 같은 단위여야 한다. 소수 life·period를 그대로 계산하며 정수 반올림이나 달력/현재 시각의 자동 추론은 하지 않는다. salvage>cost이면 음수 결과가 된다. 결과는 Number이며 자산 원장·관측 원가를 자동 기입하지 않는다. 통화와 단위는 입력 값의 명시 의미를 따른다.

XDP-03: 입력을 binary float로 바꾸지 않고 고정 크기 정수의 교차 곱으로 정확한 유리수 값을 계산한다. **중간 반올림 없이 최종 소수6자리에서 절반은 절댓값 증가로 반올림**한다. 반올림한 결과가±9,000,000,000,000을 초과하면 오류다. 큰 수끼리의 작은 차이도 보존한다. 예를 들어 SLN(9000000000000,8999999999999.999999,1)은0.000001이다. 전체 VBA Double/Variant 동등성은 주장하지 않는다.

XDP-04: 모든 위치의 Null은 계산 오류다. 기존 IIf의 모든 인수 평가 규칙도 유지하므로 IIf(True,1,SLN(1,0,Null))은 오류다. 미설정 결과는 IIf(True,Null,SLN(1,0,1))처럼 명시한다. 오류·미설정과 저장된 스냅샷을 구별하고 임의0이나 원본 값으로 대체하지 않는다. 함수 이름의 대소문자·쉼표/세미콜론 구분자를 지원한다.

XDP-05: 활성 새 함수가 현재·기준선·시나리오·일정 채택·통화/ID 이력 전후 사본에 있으면 조건부 **schema72**다. schema71 이하는 활성 새 식을 거절한다. 비활성 원문은 기존 schema44부터 보존하고 다른 기능/참조의 최소 버전도 유지한다. native 필드는 추가하지 않는다. 안정 ID·이름 변경·undo/redo·저장/재열기·복구·읽기 전용·취소 checkpoint와 편집기 예시/worker 미리보기를 연결한다.

XDP-06: 네 범위·요약·roll-down·native 숫자 식, Cost 입력의 통화 전환 후 재계산, XML 정의/현재 값/재수입 실행과 XER 정적 결과, XLSX/PDF의 값·오류·미설정을 검사한다. 최대값·미세 차이·소수 입력·overflow·혼합 입력9,032건을 Python Fraction으로 독립 판독한다. Windows .NET Financial.SLN/SYD 비교는 수치가 제한된 공통 입력 범위에서 수행하며 binary64 연산의 최대0.000001 차이는 별도 기록한다. 실제 Project/P6 실행 왕복은 별도 잔여다.

구문 기준은 Microsoft [SLN](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/sln-function)·[SYD](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/syd-function)다. [검증 기록](validation/xml-depreciation-2026-11-15/README.md). 다른 함수·번호별 기준선·요약 기간과 기존 GAP도 계속 잔여다.
