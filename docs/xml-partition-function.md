# XML 계산식 Partition

2026-11-14 · XPT-01~06. 작업·프로젝트·자원·배정 계산식에서 수치를 구간 Text로 분류한다.

```text
Partition(number,start,stop,interval)
```

| 식 | 결과(따옴표 안 공백 포함) |
|---|---|
| `Partition(12,0,99,10)` | `" 10: 19"` |
| `Partition(-1,0,99,5)` | `"   : -1"` |
| `Partition(100,0,99,5)` | `"100:   "` |
| `Partition(1010,100,1010,20)` | `"1000:1010"` |
| `Partition(2.5,0,9,2.5)` | `" 2: 3"` |
| `Partition(3.5,0,9,2.5)` | `" 4: 5"` |

XPT-01: 네 인수는 Number 또는 Boolean의 숫자 값이다. **모든 인수를 짝수 쪽 정수 반올림**한 뒤 start>=0, stop>start, interval>=1을 검사한다. 예를 들어 interval0.5는0이 되어 오류, 1.5/2.5는2가 된다. native 소수6자리와 각 인수의 전체 수치 범위±9,000,000,000,000을 지원한다. Double/Variant 전체 범위와 암묵 문자열/Date 변환은 지원하지 않는다. 숫자 문자열은 CDbl 등으로 명시 변환한다. True=-1, False=0이다.

XPT-02: start부터 interval개 정수씩 구간을 나누고 number가 속한 하한:상한을 반환한다. 마지막 구간은 stop에서 끝난다. interval1이면 범위 안에서 number:number다. number<start이면 하한은 빈칸, 상한은 start-1이며 number>stop이면 하한은 stop+1, 상한은 빈칸이다. 범위 밖 판정을 interval1보다 먼저 적용한다. 전체 구간을 열거하지 않고 정수 연산으로 계산하므로 큰 범위도 일정한 작업량이다.

XPT-03: 양쪽 폭은 stop+1과 start-1의 십진 문자열 길이 중 큰 값이다. 짧은 경계와 없는 경계는 왼쪽 공백으로 채운다. **앞뒤 공백은 반환 Text의 일부**이며 저장·XML/XER·XLSX/PDF에서 보존한다. 필요하면 호출자가 Trim을 명시한다. Number 필드에 구간 문자열을 강제로 넣지 않으며 Len(Partition(...))을 Number 필드로 계산해 기존 native 숫자 식에서 참조할 수 있다.

XPT-04: 어느 인수든 Null이면 미설정 결과다. Null은 인수 값의 형식/구간 검증보다 먼저 전파하지만 인수 식은 기존 규칙대로 모두 평가한다. 따라서 Partition(Null,-1,0,0)은 미설정이고 Partition(Null,1/0,99,5)은 계산 오류다. 미설정·오류와 저장된 스냅샷을 구별하고 원본 값이나0으로 대체하지 않는다. 대소문자·쉼표/세미콜론 구분자를 지원한다.

XPT-05: 활성 Partition이 현재·기준선·시나리오·일정 채택·통화/ID 이력 전후 사본에 있으면 조건부 **schema71**다. schema70 이하는 활성 새 식을 거절하며 비활성 원문은 기존 schema44부터 보존한다. native 필드는 추가하지 않는다. 안정 ID·이름 변경·undo/redo·저장/재열기·복구·읽기 전용·취소 checkpoint와 편집기 예시/worker 미리보기를 연결한다. Cost 참조는 통화 전환 후 다시 구간 분류한다.

XPT-06: 네 범위·요약·roll-down·native 숫자 식 연결과 XML 정의/현재 값/재수입 실행, XER 정적 값, XLSX/PDF의 오류·미설정을 검사한다. 경계·큰 수·interval1·혼합 입력5,332건을 Python 나머지 연산으로 독립 판독하고 Windows .NET Microsoft.VisualBasic.Interaction.Partition과 정확한 공백까지 비교한다. 직접 식은 별도로 반올림·Null·오류를 검사한다. 실제 Project/P6에서의 실행 왕복과 전체 VBA 호환성은 별도 잔여다.

구문 기준은 Microsoft [VBA Partition](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/partition-function)이다. Windows 비교에는 [.NET Partition](https://learn.microsoft.com/en-us/dotnet/api/microsoft.visualbasic.interaction.partition?view=net-10.0)을 사용한다. [검증 기록](validation/xml-partition-2026-11-14/README.md). 다른 함수·번호별 기준선·요약 기간과 기존 GAP도 계속 잔여다.
