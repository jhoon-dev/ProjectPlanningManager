# UX-W07 열 보기·자원 메뉴·완료일 탐색 검증

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/workspace-navigation-2026-11-18)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

2026-11-18 · [기획·수용조건](../../planning/21-weekly-update-ux-review.md) · [사용법](../../workspace-navigation.md).
기준 커밋 `36fdb3c` 이후 변경이다. 엔진·도메인·저장 schema를 변경하지 않았다.

## 결과와 범위

일정 핵심·주간 편차/보고·자원 확인을 선택하고 이전 열 순서·폭·숨김으로 돌아갈 수 있다.
선택 기준선 완료·근무일 편차·상태일 보고는 조회 전용이며 기존 사용자 필드와 코드의 열 번호를 유지한다.
주간 조회 열은 저장된 보기·상세 인쇄에 넣지 않는다. 회의용 출력의 기존 요약은 유지한다.
자원 메뉴는 기존17개 QAction을 재사용해 주간 검토·기간별 공수/실적·자재/역할·고급 설정으로 묶는다.
표시/선택 범위와 전체 계산·자원 비교 범위, 전체 완료와 표시 작업 완료를 구분한다.
표시 완료일 이동은 가로 시간축만 변경한다. 선택 보기·전체 맞춤의 기존 단축키도 유지한다.

Windows 네이티브 앱을 직접 조작했다. 주 창은1562×932, 어두운 테마이며 합성 사례를 사용했다.
초기 파일 선택 때 다른 앱이 전면을 가져갔지만 창을 다시 확인한 뒤 재개해 아래 흐름을 모두 완료했다.
실제 PM 무도움 사용자 시험·macOS·OS DPI 변경·외부 MSP/P6 왕복·전체 테스트 모음은 수행하지 않았다.

## 실제 화면 점검

[review.ppm](../../../tests/fixtures/native-history/workspace-navigation-2026-11-18/review.ppm)은 이전 UX-W06의 같은 파일을 바이트 그대로 복사한 것이다.
작업5개·보고 원장6건(보고 작업3개)·기준선1개·결정3건, 상태일11-16 09:00을 사용했다.
기준선 완료11-17 18:00, 자원 조정 채택 후 전체 완료11-24 18:00이다.
이번 수동 점검에서는 입력·채택·명시 저장을 하지 않았고 종료 후 저장 바이트가 같음을 확인했다.

| 증거 | 직접 확인한 동작 |
|---|---|
| [manual-01](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/workspace-navigation-2026-11-18/manual-01-calculated-outside.png) | 파일 열기·F9, 전체 완료11-24와 화면 밖 안내 |
| [manual-02](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/workspace-navigation-2026-11-18/manual-02-weekly-columns.png) | 주간 열: A 완료 보고, B 보고 있음, C 미착수 보고, D/E 미보고·미정; E +5d(+40h) |
| [manual-03](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/workspace-navigation-2026-11-18/manual-03-finish-visible.png) | 표시 완료일 보기로11-24를 화면 안에 표시 |
| [manual-04](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/workspace-navigation-2026-11-18/manual-04-filter-and-selection-scope.png) | 조립 검색·C 선택: 표시1/5·선택1·ID C, 표시 완료11-19와 전체 완료11-24 구분 |
| [manual-05](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/workspace-navigation-2026-11-18/manual-05-resource-menu.png) | 자원 기본5명령과3개 하위 메뉴. 캡처는 메뉴 팝업 영역 |
| [manual-06](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/workspace-navigation-2026-11-18/manual-06-resource-advanced-menu.png) | 기존 고급6명령. 캡처는 하위 메뉴 영역 |
| [manual-07](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/workspace-navigation-2026-11-18/manual-07-resource-columns.png) | 자원 확인: C 2d·설치 기술자100%·평준화 임계, 필터/선택 유지 |
| [manual-08](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/workspace-navigation-2026-11-18/manual-08-schedule-columns.png) | 일정 핵심: C 기간·선행·시작·완료 표시 |
| [manual-09](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/workspace-navigation-2026-11-18/manual-09-original-columns-restored.png) | 전체·직접 배치로 원래 열 복원, 필터/선택 유지 |
| [manual-10](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/workspace-navigation-2026-11-18/manual-10-resource-full-project-scope.png) | 필터1/5 상태에서 자원 비교는 전체5개·보고3/5·배정2/5, 원본 과부하16h→현재0h |
| [manual-11](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/workspace-navigation-2026-11-18/manual-11-return-preserves-filter.png) | 비교창을 닫은 뒤 조립 필터·C 선택·저장됨 유지 |

## 자동 검사

[regression-verified.log](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/workspace-navigation-2026-11-18/regression-verified.log): 최종 소스 기준 관련 CTest **11/11 통과**, 26.24초.
navigation_ui, task_table_model_contract, workspace_presentation, task_classification_ui,
schedule_print_ui, meeting_print_ui, wbs_gantt_ui, resource_ui, tracking_ui, workflow_ui, dialog_layout이다.
영향받는 실행 파일을 최종 제품 소스로 다시 링크했다.
[native-tests.txt](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/workspace-navigation-2026-11-18/native-tests.txt): Windows 플랫폼 검사 **5건 통과**(본문3개와 init/cleanup), 5285ms.
[navigation-tests.txt](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/workspace-navigation-2026-11-18/navigation-tests.txt)는 offscreen 최종 결과다.
[layout-tests.txt](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/workspace-navigation-2026-11-18/layout-tests.txt)54건·[workspace-tests.txt](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/workspace-navigation-2026-11-18/workspace-tests.txt)10건의 상세 결과도 보존했다.

새 navigation_ui는 QAbstractItemModelTester로 조회 열 추가·제거를 검사한다.
읽기 전용, 기존 열 배치 정확한 복원, 필터·선택 보존, 전체/표시 완료 차이,
17개 자원 명령의 중복/누락과 기존 단축키, 저장/dirty 불변, 상세 인쇄 조회 열 제외를 확인한다.
기준선 없음·오래된 결과에서는 편차를 —로 표시하며 완료일 이동을 비활성화하고 재계산 후 복구한다.
이 변경 검사는 임시 사본에서만 실행했다. 기존 분류·모델 계약 검사로 사용자 필드/코드 번호도 확인했다.

Windows 네이티브 Qt 캡처는 실제 앱 직접 조작 증거와 구분해 보존한다.
[01](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/workspace-navigation-2026-11-18/01-weekly-columns.png)~[06](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/workspace-navigation-2026-11-18/06-original-columns-restored.png)은 열·탐색·복원,
[03b](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/workspace-navigation-2026-11-18/03b-different-displayed-and-project-finish.png)는 표시/전체 완료 차이,
[07](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/workspace-navigation-2026-11-18/07-resource-menu.png)·[08](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/workspace-navigation-2026-11-18/08-resource-advanced-menu.png)은 메뉴,
[09](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/workspace-navigation-2026-11-18/09-stale-no-variance.png)·[10](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/workspace-navigation-2026-11-18/10-recalculated-variance.png)은 임시 변경 후 오래됨/재계산이다.
01·03b·09·10과 수동 흐름의 화면을 시각 확인했다.

초기 실패 기록도 보존했다. build-final.log의 테스트 캐스트 컴파일 오류를 수정했다.
initial-tests.log에서 편집 직후 완료일 버튼이 갱신되지 않는 문제를 찾아 문서 변경 알림에 연결했다.
regression.log/regression-final.log의 직접 문서 변경 후 표 갱신 및 전면 창에 의존하는 F9 테스트는
기존 보기 새로고침과 같은 계산 QAction 버튼 호출로 고쳤다. 최종 판정은 regression-verified.log와 native-tests.txt다.
최종 제품 빌드는 build-feedback.log, 마지막 테스트 빌드는 build-capture.log에 기록했다.

## 보존·재현

`python docs/validation/workspace-navigation-2026-11-18/verify_saved.py`는 원본과 저장 파일 전체 바이트,
schema·작업·보고·기준선·채택·결정·상태일 보존을 읽기 전용으로 확인한다.
[saved-file-check.txt](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/workspace-navigation-2026-11-18/saved-file-check.txt)에 결과를 보존했다.
[manifest.json](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/workspace-navigation-2026-11-18/manifest.json)은 기준 커밋·분기·제품 바이너리·변경 소스 및 증거 SHA-256을 기록한다.
증거는 .gitattributes로 원시 바이트를 보존하며 staging blob과 일치함을 확인했다.
clang-format --dry-run --Werror, git diff --check와 이 문서의 로컬 링크 검사도 통과했다.

자동 검사를 재현하려면 같은 Qt6.8.3/MinGW Release 환경에서 JOBS와 관련 테스트 타깃을 빌드한 뒤 실행한다.

```powershell
ctest --test-dir build-numbered-summary --output-on-failure -R '^(navigation_ui|task_table_model_contract|workspace_presentation|task_classification_ui|schedule_print_ui|meeting_print_ui|wbs_gantt_ui|resource_ui|tracking_ui|workflow_ui|dialog_layout)$'
build-numbered-summary/jobs_navigation_ui_tests.exe -platform windows
```

UX-W01~07 구현과 개별 화면 검증을 완료했다. WEEK-03~07 통합 재점검과 실제 대상 PM 사용자 시험은 다음 과제다.
