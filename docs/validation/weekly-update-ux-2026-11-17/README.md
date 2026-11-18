# 주간 갱신 Windows 화면 점검 증거

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-ux-2026-11-17)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

2026-11-17. [확정 과업](../../planning/20-first-user-weekly-update.md) ·
[관찰 결과·우선순위](../../planning/21-weekly-update-ux-review.md).
전문가의 소규모 합성 사례 점검이며 실제 사용자 수용 시험·전체 회귀·macOS·외부 제품 왕복은 아니다.
앱 소스 수정은 없고 기획 문서와 점검 자료를 추가했다.

## 재현 자료

- [initial-plan.ppm](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-ux-2026-11-17/initial-plan.ppm): 외부 준비한 5작업/1자원 최초 입력. UI에서 처음부터 작성한 파일이 아니다.
- [baseline-ready.ppm](../../../tests/fixtures/native-history/weekly-update-ux-2026-11-17/baseline-ready.ppm): 위 입력을 앱에서 열어 기준선 생성 후 UI로 다른 이름 저장한 파일.
  상태일 입력창 장애 중 기본값 11-09 18:00을 적용한 상태다.
- [weekly-prepared-input.ppm](../../../tests/fixtures/native-history/weekly-update-ux-2026-11-17/weekly-prepared-input.ppm): 기준선을 보존한 독립 문서 ID의 후속 점검 사본.
  상태일 11-13 09:00과 실적 2건은 앱 밖에서 준비했다. 정상 상태일 UI 입력 성공 증거가 아니다.
- [weekly-prepared.ppm](../../../tests/fixtures/native-history/weekly-update-ux-2026-11-17/weekly-prepared.ppm): 후속 사본을 앱에서 열어 실적 확인 정정 1건 제출,
  자원 평준화 후보 채택, Ctrl+S 저장한 결과. 닫기·재열기·F9에서 완료일 11-23 18:00을 확인했다.
- [weekly-meeting-default.pdf](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-ux-2026-11-17/weekly-meeting-default.pdf): 앱의 기본 인쇄 설정으로 실제 저장한 7페이지 출력.
  미리보기 페이지 1을 확인했으며 모든 페이지의 PDF 시각 QA를 완료한 자료는 아니다.
- `manifest.json`: 실행 파일·소스 기준점·자료의 SHA-256, 준비/실행 범위와 파일 구조 확인 결과.

준비 사례는 월~금 8시간 달력, 설계 16h → 부품 준비 16h → 조립/설치 준비 각16h → 시운전8h다.
조립·설치 준비에 같은 기술자를 각각100% 배정했다. 상세 원가·자재·WBS·외부 파일은 포함하지 않는다.
부품 준비의 실제16h/잔여16h가 최초 지연 사례이며 보고 범위는2/5다.

## 주요 실제 화면

| 증거 | 확인한 내용 |
|---|---|
| [새 프로젝트 입력](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-ux-2026-11-17/01-new-project-1.png) | 기본 달력 설명·작업명 목록. 관찰 후 취소 |
| [계획 계산](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-ux-2026-11-17/02-plan-calculated-0.png) | 현재 계획 완료11-17, 임계 색상 |
| [추적 메뉴](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-ux-2026-11-17/03-tracking-menu-0.png) | 기준선·상태일·선택 실적·비교 진입 |
| [기준선 생성 직후](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-ux-2026-11-17/05-baseline-created-0.png) | 기준선1개·선택v1, 오래된 결과와 비워진 간트 |
| [상태일 표시 장애](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-ux-2026-11-17/06-status-date-dialog-1.png) | 약132×141 창, 날짜·설명 등 잘림 |
| [크기 조정 시도](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-ux-2026-11-17/07-status-resize-attempt-0.png) | 창 확대가 되지 않아 정상 입력을 복구하지 못함 |
| [실적 입력](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-ux-2026-11-17/10-progress-dialog-1.png) | 실제/잔여960분, 보고 이력·보고자·사유 |
| [보고 저장 직후](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-ux-2026-11-17/11-progress-submitted-0.png) | 오래된 계산·F9 재계산 안내 |
| [기준선 비교](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-ux-2026-11-17/12-baseline-comparison-1.png) | 기준선11-17 / 예측11-19, +16h·보고범위2/5 |
| [자원 메뉴](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-ux-2026-11-17/13-resource-menu-0.png) | 기본 업무와 상세 기능이 긴 목록에 함께 배치 |
| [평준화 비교](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-ux-2026-11-17/14-leveling-comparison-1.png) | 원본 예측11-19 / 후보11-23·부하 검증 |
| [원본 부하](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-ux-2026-11-17/15-original-load-1.png) | 11-17/18 기술자200% 중복 배정 |
| [채택 후](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-ux-2026-11-17/16-leveling-adopted-1.png) | 채택 상태 표시, 원본 부하 탭은 여전히 채택 전 수치 |
| [인쇄 미리보기](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-ux-2026-11-17/17-print-preview-1.png) | 5개 작업·기본 열·7페이지 |
| [재열기](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-ux-2026-11-17/19-reopened-0.png) | 상태일·기준선 보존, 미계산 상태 |
| [재열기 후 계산](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-ux-2026-11-17/20-reopened-calculated-0.png) | 평준화 채택·완료11-23 재현 |
| [전체 맞춤](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-ux-2026-11-17/21-final-fit-0.png) | 전체 날짜와 기준선·예측 간트 확인 |

PNG는 native 창 캡처이며 같은 이름의 TXT는 당시 접근성 트리다. 일부 접근성 텍스트 갱신이
늦거나 비어 있어 화면 캡처와 함께 읽는다. 메뉴/대화상자 캡처는 주 창과 소유 창을 각각 남겼다.
이 디렉터리는 Git 줄바꿈 변환을 끄고 원본 바이트와 manifest 해시를 checkout 간에도 보존한다.
정상 과업 전체 성공으로 판정하지 않는다. 첫 개선은 상태일 창의 표시 복구다.
