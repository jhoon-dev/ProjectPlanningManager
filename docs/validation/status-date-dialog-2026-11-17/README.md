# UX-W01 상태일 입력창 수정·검증

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/status-date-dialog-2026-11-17)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

2026-11-17 Windows 11 / Qt6.8.3 / MinGW13.1 Release.
[최초 UX 점검](../../planning/21-weekly-update-ux-review.md)의 P0 상태일 입력창 표시 장애만 수정했다.

## 수정과 원인

`MainWindow::editStatusAt()`은 초기 크기를 지정하지 않아 공통 편집 UI가 본문을 스크롤 영역으로
옮긴 뒤 창이 약132×140으로 열렸다. 날짜·시각과 설명이 잘려 첫 주간 갱신을 진행하기 어려웠다.
다른 편집창과 동일하게 상태일 창에도 최소 초기 크기560×280을 먼저 지정했다.
확대 글꼴에서는 해제 옵션의 실제 너비와 글꼴 높이를 반영해 더 크게 연다.
기존 화면 크기 제한·본문 스크롤·하단 확인/취소 배치는 그대로 사용한다.
공통 편집 UI·엔진·상태일 의미·원장·schema는 변경하지 않았다.

## 회귀 검사

- `before-fix-layout.txt`: 실제 MainWindow 진입점을 열고 테스트에서 크기를 바꾸지 않은 상태로
  수정 전 실패를 재현했다. 기본 글꼴의 가로 스크롤 최대값은247이었다.
- `dialog-layout-after.txt`: 최종 레이아웃 검사48개 통과. 상태일 전용3행은 기본/125%/150%로
  QApplication 글꼴을 바꾸고 실제 입력기에도 적용됐는지 확인한다. 초기 가로·세로 스크롤 없음,
  날짜·해제 옵션의 전체 표시, 설명의 줄 높이, 확인/취소의 가시성을 검사한다.
- `native-layout.txt`: 위 상태일3행을 offscreen이 아닌 Windows 기본 렌더링에서도 실행했다.
  실제 테스트3행과 초기화/정리 포함5개 모두 통과했다. OS DPI 배율을 변경한 검사는 아니다.
- `regression.log`: `dialog_layout`, `tracking_ui`, `tracking_contract`, `tracking_file`,
  `recovery_ui`, `undo_contract`의 관련 CTest6개 모두 통과했다.
- `layout-regression.log`: 글꼴 적용 확인을 보강하면서 offscreen150%의 가로 넘침21px도
  발견했다. 글꼴에 맞춰 초기 크기를 계산한 최종 버전은 `regression.log`에서 다시 검사했다.
- `tracking_ui`에는 적용 후 재진입, 날짜·해제 초안 취소 시 문서 전체 불변, 실제 해제 시
  기준선/실적 보존, 해제 후 재진입·취소·재설정을 추가했다.

빌드 대상은 `JOBS`, `jobs_dialog_layout_tests`, `jobs_tracking_ui_tests`다.
전체 suite·macOS·서로 다른 OS DPI/작은 모니터의 수용 시험은 이번 범위가 아니다.
테스트 실행 시 Qt platform plugin 경로를 `.deps/Qt/6.8.3/mingw_64/plugins/platforms`로 명시했다.

## 실제 앱에서 확인한 동작

수정된 `build-numbered-summary/PPM.exe`를 실행해 이전 합성 주간 갱신 파일의 사본을 열었다.
기준선1개·실적3건이 있는 기존 자료다. 이번 상태일 변경은 파일을 외부 편집하지 않고 UI에서 수행했다.

| 단계 | 실제 입력/행동 | 확인 결과와 화면 |
|---|---|---|
| 최초 진입 | 추적 → 상태일 설정 | 날짜11-13 09:00·설명·해제 옵션·버튼이 잘리지 않음. [화면](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/status-date-dialog-2026-11-17/01-initial-layout-1.png) |
| 입력·적용 | 날짜/시각을 `2026-11-16 18:00`으로 입력 → OK | [입력](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/status-date-dialog-2026-11-17/02-entered-date-1.png), [주 창 적용](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/status-date-dialog-2026-11-17/03-applied-date-0.png) 확인 |
| 적용 후 재진입 | 추적 → 상태일 설정 | 적용한11-16 18:00과 해제 옵션 미선택을 [복원](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/status-date-dialog-2026-11-17/04-reopened-applied-1.png) |
| 취소 | `2026-11-20 12:00`으로 변경하고 해제도 선택 → Cancel | 기존11-16 18:00 유지. [초안](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/status-date-dialog-2026-11-17/06-cancel-clear-draft-1.png) · [취소 후](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/status-date-dialog-2026-11-17/07-cancelled-0.png). 취소 전후 Ctrl+S 저장 파일은 바이트 동일 |
| 취소 후 재진입 | 상태일 설정 다시 열기 | 취소한 날짜와 체크 상태가 남지 않고11-16 18:00·미선택 [복원](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/status-date-dialog-2026-11-17/08-reopened-after-cancel-1.png) |
| 해제 | 해제 체크 → OK → Ctrl+S | [주 창](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/status-date-dialog-2026-11-17/10-cleared-0.png)의 상태일 미설정·현재 계획 모드. 저장 파일의 상태일null, 기준선1개·실적3건 보존 |
| 해제 후 재진입 | 상태일 설정 다시 열기 | 기존 기본 동작인 프로젝트 시작일11-09 18:00·해제 옵션 미선택으로 [열림](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/status-date-dialog-2026-11-17/11-reopened-after-clear-1.png). 취소하면 상태일 미설정 [유지](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/status-date-dialog-2026-11-17/12-final-cleared-state-0.png) |
| 최종 빌드 재확인 | 확대 글꼴 대응 후 앱 재실행 → 저장 사본 열기 → 상태일 진입 → 취소 | [날짜·설명 전체 표시](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/status-date-dialog-2026-11-17/13-final-build-reopened-1.png), [취소 후 미설정 유지](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/status-date-dialog-2026-11-17/14-final-build-cancelled-0.png) |

01~12는 초기560×280 수정 빌드의 입력·취소·해제·재진입 기록이다. 이후150% 글꼴의
offscreen 넘침을 해결하고 최종 빌드로13~14의 재진입·취소를 재확인했다.
최종 `tracking_ui` 자동 검사에서도 입력·취소·해제·재진입과 저장/복원을 검사한다.

`applied.ppm`과 `after-cancel.ppm`의 바이트 일치 및 `cleared.ppm`/`review.ppm`의 상태일null을
확인했다. 모든 사본에서 이전 기준선·실적·작업 입력이 그대로다. 상태일 변경에 따른 기존
평준화 채택 무효화는 원래 정책이며 이번 변경이 추가한 동작이 아니다.

PNG는 실제 native 창 캡처이고 TXT는 보조 접근성 텍스트다. 새 화면은 창 테두리 포함 약560×312다.
`manifest.json`에 수정 소스·실행 파일·결과 파일 해시와 확인 결과를 기록한다.
UX-W01 수용을 완료한 것이며 주간 갱신 전체 사용성·외부 제품 동등성 완료를 의미하지 않는다.
