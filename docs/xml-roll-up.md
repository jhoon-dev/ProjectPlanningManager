# XML 사용자 필드의 요약 roll-up

2026-11-11 후속: [외부 계산식 실행](xml-external-formulas.md)은 활성 식의 말단 결과를 roll-up하고 CalculationType=2를 요약 값에서 계산한다. 비활성 식/없는 식은 아래 명시 값 보존 규칙을 따른다.

2026-11-11 · RUP-01~05.

작업 사용자 필드를 프로젝트 루트와 WBS 하위 작업에서 계산하고, 현재 작업 분류 그룹과 자원 그룹에도 같은 규칙을 적용한다. 사용자 필드의 요약이며 일정·공수·원가 지표는 별도로 계산한다. 설정은 사용자 필드 관리의 **요약 roll-up…**에서 변경하고 WBS/자원 미리보기로 확인한다. WBS·작업 분류 그룹의 툴팁, 자원 그룹의 필드 열, XLSX/PDF 보고서에 결과를 표시한다.

## XML 규칙과 계산

Microsoft의 [CalculationType](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/calculationtype-element?view=project-client-2016)과 [RollupType](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/rolluptype-element?view=project-client-2016)을 보존한다. CalculationType 0은 명시 요약 값, 1은 자동 roll-up이다. 2는 [활성 XML 계산식](xml-external-formulas.md)을 요약 값에서 계산하고, 식이 없거나 비활성이면 명시 요약 값을 사용한다. Project/Assignment 범위의 메타데이터는 보존하며 요약 실행은 Task/Resource 범위에 한정한다.

| RollupType | 계산 | 지원 필드 |
|---|---|---|
| 0 | 최댓값; Flag는 OR | Number, Cost, Duration, Date, Start, Finish, Flag |
| 1 | 최솟값; Flag는 AND | 위와 같음 |
| 2 | 자신을 제외한 모든 하위 요약 행과 말단 행 개수 | Number |
| 3 | 말단 전체 합계 | Number, Cost, Duration |
| 4 | 말단 전체 평균 | Number, Cost, Duration |
| 5 | 직접 하위 행/그룹의 값에 같은 가중치를 주는 재귀 평균 | Number, Cost, Duration |
| 6 | 직접 하위 요약/말단 행 개수 | Number |
| 7 | 모든 하위 말단 행 개수 | Number |

Text/Choice는 하위 roll-up 집계(CalculationType=1)를 실행하지 않고 명시 요약 값과 비활성 메타데이터를 보존한다. Text의 CalculationType=2 실행은 위 XML 계산식 계약을 따른다. 지원하지 않는 활성 조합은 거절한다. Cost/Duration을 개수로 변환하지 않는다. 개수의 표시 단위는 개다. 평균의 계층 가중치와 누락 정책은 이 구현의 계약이며 실제 MS Project/P6의 모든 옵션과 실행 동등성을 검증한 것은 아니다.

상위 G에 직접 값 2, 하위 H에 4·8이 있고 루트에 16이 있으면 G의 전체 평균은 4.666667, 직접 하위 평균은 (2+6)/2=4다. 루트의 직접 하위 평균은 (4+16)/2=10이다. 빈 그룹은 개수에는 포함하고 값 평균에서는 제외한다. 숫자·날짜 값이 누락되면 결과도 미설정과 사유로 표시한다. 개수는 값이 없는 행도 센다. Flag는 OR의 true, AND의 false가 누락 값보다 우선하며 나머지 불확정 결과는 미설정이다.

Number/Duration은 소수 6자리, Cost는 프로젝트 통화의 최소 단위로 계산하고 평균은 절반을 0에서 멀어지는 방향으로 반올림한다. 합계는 양/음의 정수 누산을 분리해 큰 중간 합계의 상쇄를 보존한다. 결과가 필드 범위를 넘으면 미설정/오류로 표시한다. Duration의 경과/근무 형식이 섞이면 변환을 추측하지 않고 사유를 표시하며 추정 표시는 보존한다. 날짜 비교는 날짜·초·소수 초를 유지한다. native 숫자 계산식과 활성 XML 계산식은 말단 값을 공급할 수 있다.

## 값 보존과 교환

`CustomField.xmlRollUp`은 계산 방식, `WbsNode.attributes`와 `PlanInput.taskSummaryAttributes`는 명시 Task 범위 요약 값이다. UID0의 Task 값과 Project 값은 분리한다. 자동 계산은 명시 값을 덮어쓰지 않는다. 자동 계산을 끄면 보존한 명시 값이 다시 표시된다. 명시 요약 값의 API 편집은 계산 없음에서만 허용한다.

XML은 활성 요약의 현재 결과와 규칙을 내보낸다. 명시 원본과 다르거나 계산 오류가 있으면 진단에 남긴다. 재가져온 XML의 계산 결과는 새 명시 스냅샷으로 보존하면서 계속 하위 값으로 재계산한다. 따라서 자동 계산 이전 원본 값까지 보존하려면 동반 source.ppm을 사용한다. XER는 요약 규칙/WBS 명시 값을 실행하지 않으며 source.ppm과 진단에 남긴다.

조건부 schema43을 사용한다. 기존 데이터는 기존 최소 writer를 유지하고 구버전 envelope는 새 데이터를 거절한다. 기준선·시나리오·일정 채택·통화/ID 이관의 전체 전후 사본, 복구, undo/redo에도 값을 보존한다. 통화 전환은 말단 Cost와 저장된 명시 요약 Cost를 각각 한 번 환산하고 자동 결과는 환산된 말단에서 다시 계산한다. WBS 이름/위치 변경은 명시 값을 유지한다.

최대 100,000개 요약 조회 셀, 30,001개 조회 그룹, 깊이 64를 허용한다. 원본 객체별 요약 값은 64개다. 조회 그룹에는 같은 말단 행을 중복 포함할 수 없다. 작업/자원 필터 결과의 고유 행과 실제 분류 계층을 사용한다. 취소 체크와 편집의 읽기 전용·stale 차단을 적용한다.

| 요구 | 범위 |
|---|---|
| RUP-01 | CalculationType/RollupType 보존·형식별 활성 검증 |
| RUP-02 | WBS/루트와 Task/Resource 분류 계층, 8개 방식·누락·정밀도 |
| RUP-03 | 명시/자동 값 분리, XML 재교환·XER 손실 진단 |
| RUP-04 | schema43·동결/복구·통화/ID 이관·undo/worker |
| RUP-05 | 설정/미리보기·그룹 화면·보고서·독립 판독 |

[전체159/159·집중17/17·파서698개·형식441파일·독립 판독/Windows UI 검증](validation/xml-roll-up-2026-11-11/README.md)을 통과했다. 실제 Microsoft Project/P6 열기·재계산·재저장, macOS/스토어 실행은 별도 환경의 검증이 필요하다.
