# 주간 갱신 통합 재점검과 PM 시험 준비

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

2026-11-18 · 기준 커밋 `64ccb71` 이후.
[과업](../../planning/20-first-user-weekly-update.md) · [UX 기록](../../planning/21-weekly-update-ux-review.md) ·
[사용자 시험 진행안](../../planning/22-weekly-pm-pilot.md).

## 결과

상태일 변경→연속 실적 제출→다음 초안 취소→명시 재예측→기준선/예측 비교→자원 후보 채택→
회의용 PDF→저장·재열기를 연결했다. 수동 점검에서 **중첩 비교창의 자원 채택 후 주 창 표·간트 갱신 누락**을
발견하고 수정했다. MainWindow::showComparison 종료 후 기존 refresh를 호출한다.
엔진·schema·작업 입력·보고/기준선 보존 계약은 변경하지 않았다.

최종 Windows 네이티브 통합 자동 검사는5/30개 작업 모두 통과했다.
관련 회귀7개(6개 묶음+인쇄 계약1개), 채택/해제 네이티브 검사6건,
통합 네이티브 검사4건(실제 사례2개+init/cleanup)을 통과했다.
실제 PM 참여자 시험·시장 수용·외부 MSP/P6 왕복을 통과한 것으로 해석하지 않는다.

## 전문가 수동 점검 — 수정 전 빌드

UX-W07 빌드 `2847322fb6215825364cf7b6c6df2e38fded7e20d0bd911c36d131ae6bbddb28`의
Windows1562×932 어두운 테마에서 직접 조작했다.
[start.ppm](../../../tests/fixtures/native-history/weekly-update-integrated-2026-11-18/start.ppm)은 최초 점검의 [이전 주 준비 파일](../../../tests/fixtures/native-history/weekly-update-ux-2026-11-17/weekly-prepared.ppm)을
그대로 복사했다. WEEK-01/02는 시작 조건으로 제공했고 처음부터 다시 작성하지 않았다.

| 증거 | 확인한 사실 |
|---|---|
| [01](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/01-open-saved-calculation-needed.png) | 저장된 파일 열기, 저장됨과 계산 필요 분리 |
| [02](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/02-status-date-draft.png)·[03](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/03-current-week-previous-and-unreported.png) | 달력으로11-16 09:00 적용, 이전 주2·현재0·미보고3, 이전 완료 제외 확인 대상4 |
| [04](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/04-parts-report-draft.png) | B 실제2d/잔여2d·기존 실제 시작을 확인하고 이번 주 사유 입력 |
| [05](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/05-explicit-not-started-draft.png)·[06](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/06-two-submitted-next-draft-cancelled.png) | C 미착수0d/2d·보고자/사유 제출, 다음 D 초안 취소. 제출2·미보고2 유지 |
| [07](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/07-close-unsaved-needs-calculation.png)·[08](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/08-reforecast-complete-unsaved.png) | 닫기는 미저장/계산 필요 유지, 다시 열어 입력 마치고 재예측하면 완료11-20·미저장 |
| [09](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/09-baseline-plan-forecast-comparison.png)·[10](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/10-parts-dependency-evidence.png) | 기준선/계획11-17, 논리 예측11-20(+3d/+24h), B 실제/잔여와 선행 A 확인 |
| [11](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/11-current-versus-unadopted-candidate.png)·[12](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/12-original-resource-overload.png)·[13](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/13-candidate-resource-load.png) | 현재20/후보24 구분, 기술자11-18/19 각각 과부하8h, 후보0h |
| [14](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/14-adopted-current-resource-load.png)·[15](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/15-current-applied-load.png)·[16](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/16-adoption-decision-history.png) | 사유와 함께 채택, 현재24일·0h와 기존 채택/상태일 변경 무효화/이번 채택3건 |
| [17](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/17-adopted-main-unsaved.png) | 결함 재현: 전체 완료24일인데 주 창 표·간트와 표시 완료20일이 남음 |
| [18](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/18-before-fix-saved.png) | 명시 저장 후 기존 저장 경로의 refresh로 표·간트24일 반영 |

[before-save.ppm](../../../tests/fixtures/native-history/weekly-update-integrated-2026-11-18/before-save.ppm)은 보고·재예측 후에도 디스크가 시작 파일과 같다는 증거다.
[before-fix-saved.ppm](../../../tests/fixtures/native-history/weekly-update-integrated-2026-11-18/before-fix-saved.ppm)과 [review.ppm](../../../tests/fixtures/native-history/weekly-update-integrated-2026-11-18/review.ppm)은 명시 저장 후의 같은 파일이다.
기존 작업5·보고3·기준선1·채택 이력을 보존하고 B/C 보고2건과 무효화/채택 결정2건만 추가됐다.

수정 빌드의 수동 재열기 때 공유 Computer Use의 active request 충돌·다른 앱 전면 이동·요청 경로 대신
다른 텍스트 입력이 반복됐다. 잘못된 경로를 제출하지 않았고 복구 재시도 후 수동 경로를 중단했다.
수정 후 전체 흐름·PDF·재열기는 아래 Qt 자동 검사로 확인했다. **수정 후 전체 수동 점검 완료로 기록하지 않는다.**
작업 폴더 앱 실행 요청이 설치 앱으로 해석된 경우도 확인해 실제 빌드 경로 프로세스로 구분했다.

## 수정과 회귀

[before-fix-test.txt](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/before-fix-test.txt)는 같은 중첩 경로로 채택한 결과11-03에 대해 표가11-02를 남겨 실패했다.
수정 후 OutcomeSummaryUiTests::nestedAdoptionAndReleaseRefreshMainViews는 채택·해제 양쪽에서
문서 결과·표·간트·주간 편차·범위 안내가 같고 필터·선택·열 보기가 유지됨을 확인한다.
원본 작업/기준선과 명시 저장 전 바이트를 보존하고 저장 후 재열기도 검사한다.

- [regression-fixed.log](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/regression-fixed.log): outcome_summary_ui, navigation_ui, workflow_ui,
  weekly_update_ui, meeting_print_ui, task_table_model_contract **6/6 통과**, 10.09초.
- [print-contract.log](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/print-contract.log): schedule_print_contract **1/1 통과**, 9.03초.
- [outcome-tests.txt](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/outcome-tests.txt): offscreen6건. [native-tests.txt](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/native-tests.txt): Windows **6건 통과**,10717ms.
- 최종 제품 빌드 [build-fixed.log](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/build-fixed.log). 기존 영향 타깃을 최종 UI 소스로 다시 링크했다.
  통합 타깃 추가 [build-journey.log](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/build-journey.log), 마지막 테스트 빌드 [build-journey-diagnostic.log](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/build-journey-diagnostic.log).

## 최종 통합 자동 검사 — 실제 Qt 대화상자

[WeeklyJourneyUiTests](../../../tests/WeeklyJourneyUiTests.cpp)는 원본의 임시 사본에서 실제 MainWindow 명령과
상태일·실적·주간 갱신·결과 비교·중첩 자원 비교·인쇄창의 버튼/신호를 실행한다.
파일 선택창은 테스트에서 Qt 비네이티브 창을 사용해 선택 경로를 검증한다. Windows SendInput 수동 조작과 구분한다.
앱의 실제 PDF 내보내기 버튼과 비동기 출력 경로를 사용했으며 별도 수기 PDF를 만들지 않았다.

[journey-native-tests.txt](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/journey-native-tests.txt): **4건 통과**,19551ms. 본문은5개·30개 작업의2개 사례다.
두 사례 모두 상태일 변경, B/C의 d 입력·사유 제출, D의3d 초안 취소, 닫기/재예측/저장 독립,
후보/현재 구분·전체 자원 부하 검증·중첩 채택 후 주 창 갱신, 필터1개 상태의 전체 출력,
다음 주11-23~29 프리셋, PDF 출력, 창 닫기·재열기·재계산을 검사했다.

| 증거 | 5개 작업 | 30개 작업 |
|---|---|---|
| 제출2건·다음 취소 | [five-01](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/five-01-submitted-cancelled.png) | [pilot-01](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/pilot-01-submitted-cancelled.png) |
| 중첩 채택 후 저장 전 표·간트·전체/표시 완료24일 | [five-02](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/five-02-adopted-main-before-save.png) | [pilot-02](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/pilot-02-adopted-main-before-save.png) |
| 전체 범위·다음 주 회의용 미리보기 | [five-03](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/five-03-meeting-next-week-preview.png) | [pilot-03](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/pilot-03-meeting-next-week-preview.png) |
| 저장·재열기 후 재계산24일 | [five-04](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/five-04-reopened-calculated.png) | [pilot-04](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/pilot-04-reopened-calculated.png) |
| 최종 저장 원본 | [five-final.ppm](../../../tests/fixtures/native-history/weekly-update-integrated-2026-11-18/five-final.ppm) | [pilot-final.ppm](../../../tests/fixtures/native-history/weekly-update-integrated-2026-11-18/pilot-final.ppm) |
| 실제 내보낸 PDF | [five-meeting.pdf](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/five-meeting.pdf) | [pilot-meeting.pdf](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/pilot-meeting.pdf) |

통합 검사 초기에는 Qt 파일 선택창의 selectFile이 기본 파일명을 남겨 PDF가 임시 폴더의 기본 이름으로 저장됐다.
[journey-first-tests.txt](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/journey-first-tests.txt)는 기대 경로 확인 실패와 watchdog 종료 기록이다.
파일명 입력창과 selectedFiles를 검증하도록 테스트를 수정했다. 제품 출력 코드는 변경하지 않았다.
[journey-second-tests.txt](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/journey-second-tests.txt)에서는5개 사례를 통과하고30개 fixture의 기준선 revision 오류로
loader가 거절했다. [자료 생성기](../weekly-pm-pilot-2026-11-18/build_case.py)를 수정하고 두 사례 모두 다시 통과했다.
실패를 지우지 않고 최종 결과와 구분했다.

## 독립 대조·PDF 시각 확인

[verify_journey.py](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/verify_journey.py)와 [independent-check.txt](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/independent-check.txt)는
수동/자동 저장의 전체 payload 차이·기존 원장 prefix·기준선·작업/배정·취소 대상 미제출과
의도한 상태일·보고·결정만 추가됐는지 읽기 전용으로 확인한다.
주말 제외 수기 계산은 B16/17, C18/19, 조정 D20/23, E24다. 기준선17→논리20은+3d/+24h,
채택24는+5d/+40h다. 중복2일×8h=16h,6개 기술자=96h이며 자동 검사 화면 값과 대조했다.

PDF 구조는5개 사례 A4가로3쪽(데이터1+출처2),30개 사례6쪽(데이터3+출처3)이다.
Poppler로1400px PNG를 만들고 **9페이지 전부** 확인했다.
[five-1](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/five-pdf-1.png)·[2](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/five-pdf-2.png)·[3](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/five-pdf-3.png),
[pilot-1](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/pilot-pdf-1.png)·[2](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/pilot-pdf-2.png)·[3](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/pilot-pdf-3.png)·[4](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/pilot-pdf-4.png)·[5](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/pilot-pdf-5.png)·[6](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/pilot-pdf-6.png).
행1~5/1~30·반복 머리글·보고3/5와13/30·완료24일·+5d/+40h·채택/출처·다음 주 막대를 확인했고 겹침·잘림은 없었다.
이번 테스트에서 등록한 맑은 고딕은 PDF에서 주로 벡터 윤곽선으로 출력돼 텍스트 추출로 사실을 검증하지 않았다.
PDF 구조 검사와 모든 페이지의 시각 대조를 구분한다. 이 파일의 검색·텍스트 선택은 보장하지 않는다.

## 남은 관찰과 다음 단계

- P2 / PILOT-PRINT-01: 30개 사례의 긴 작업명 줄바꿈으로 데이터 페이지가14/15/1행으로 나뉜다.
  유실은 없지만 마지막 한 행의 여백이 크다. 사용자 시험에서 회의 자료 밀도와 열 폭을 관찰한다.
- P2 / PILOT-TERM-01: 회의용 출력은 C의 명시 미착수도 “보고 있음”으로 요약한다.
  주간 입력창의 “미착수 보고”와 같은 의미로 이해하는지 관찰하고 필요하면 용어를 통일한다.
- 실제 대상 PM 시험은 미실행이다. [진행자 계획](../../planning/22-weekly-pm-pilot.md),
  [30개 원본·과업지·양식](../weekly-pm-pilot-2026-11-18/README.md)을 준비했다.
  수정 빌드의 전체 흐름 직접 수동 재열기, 다른 DPI/macOS, 물리 프린터, 외부 제품 왕복과 전체 테스트 모음은 별도다.

## 재현·무결성

같은 Qt6.8.3/MinGW Release 환경에서 JOBS, jobs_outcome_summary_ui_tests와 jobs_weekly_journey_ui_tests를 빌드한다.

```powershell
build-numbered-summary/jobs_weekly_journey_ui_tests.exe -platform windows
ctest --test-dir build-numbered-summary --output-on-failure -R '^(weekly_journey_ui|outcome_summary_ui)$'
python docs/validation/weekly-update-integrated-2026-11-18/verify_journey.py
```

JOBS_JOURNEY_AUDIT_DIR를 비어 있는 전용 폴더로 지정하면 창 캡처·최종ppm·PDF를 복사한다.
기존 같은 이름의 결과가 있으면 사본 복사가 실패하므로 원본 증거와 분리해 재현한다.
[manifest.json](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/weekly-update-integrated-2026-11-18/manifest.json)에 두 빌드 구간·변경 소스·합성 자료와 증거 SHA-256을 기록한다.
clang-format --dry-run --Werror, git diff --check, 로컬 문서 링크와 staged 증거 바이트 일치를 확인했다.
