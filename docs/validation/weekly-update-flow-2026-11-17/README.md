# UX-W03 주간 갱신·연속 실적 입력 검증

2026-11-17 · Windows 로컬 전문가 점검. [사용법](../../weekly-update.md) · [기획 우선순위](../../planning/21-weekly-update-ux-review.md).

## 결과와 범위

주 창 툴바와 추적 메뉴에서 주간 갱신으로 진입한다. 상태일 기준으로 이번 주 확인 대상·기존 보고·미보고를
조회하고, 기존 작업별 실적 입력창을 저장 후 다음 대상으로 이어 연다. 미보고를 미착수/0%로 추정하지 않는다.
기본 대상은 완료가 보고되지 않은 작업과 이번 주 보고 작업이다. 향후 계획도 포함될 수 있고,
지난주 완료는 전체 작업에서 확인한다. 요약 건수는 필터와 무관한 전체 프로젝트 기준이다.

첫 입력부터 취소·저장·재열기·F9 재예측까지 실제 앱 화면에서 확인했다. 파일에서 기존 보고3건과 기준선1개가
보존되고 보고2건만 추가된 것을 독립 판독했다. 취소·미입력 작업의 보고는 생성되지 않았다.
저장과 계산의 다음 행동 안내 전체(UX-W04), 자원 후보 비교·출력 개선(UX-W05~07)은 이 증분의 완료 판정에 포함하지 않는다.

## 실제 화면 절차

합성 입력은 [기존 weekly-prepared.ppm](../weekly-update-ux-2026-11-17/weekly-prepared.ppm)을 사본으로 열었다.
앱 조작은 Computer Use의 Windows native 클릭·키보드로 수행했다. 파일의 사후 판독만 Python으로 했다.
주 창1562×932, 어두운 테마. OS DPI 변경·다른 해상도의 수동 조작·macOS·실제 PM 업무 파일은 미검증이다.
접근성 텍스트의 일부 갱신이 늦어 화면 이미지를 판단 기준으로 사용했다.

| 단계 | 실제 확인 | 증거 |
| --- | --- | --- |
| 진입 | 툴바 주간 갱신, 상태일11-13, 전체5/대상5/현재2/미보고3 | [툴바](01-toolbar-entry.png), [현황](02-weekly-overview.png) |
| 새 주 | 창 안에서 상태일11-16 09:00 적용. 대상4, 이전 주 보고2(숨은 완료A 포함), 미보고3. B는 이전 주 보고만 | [상태일 변경](03-new-week.png) |
| 기존 보고 | B의 이전 보고2건 확인, 실제2d/잔여1d·보고자·사유 제출 | [입력](04-existing-report-edit.png) |
| 연속 입력 | 저장 후 목록/메뉴를 거치지 않고 C 입력창 열림. 실제0d/잔여2d, 날짜 빈칸으로 미착수를 명시 보고 | [자동 진입](05-next-task-opened.png), [명시 보고](06-explicit-not-started.png) |
| 초안 취소 | 다음 D 입력창의 잔여를3d로 바꾼 뒤 목록으로(초안 취소). 현재 보고2, 미보고2, 제출2건 안내. D/E 자동 보고 없음 | [다음 작업](07-third-task-opened.png), [초안](08-unsaved-draft.png), [취소 결과](09-cancel-returned.png) |
| 저장·재열기 | 창 닫기→Ctrl+S→앱 종료. 보정 빌드로 같은 파일 재열기. 현재2/미보고2와 B 2d/1d, C 0d/2d 유지 | [저장](11-file-saved.png), [재열기](12-reopened-final.png) |
| 필터 | 현재 상태일 미보고는 D/E 2행, 이번 주 기존 보고는 B/C 2행. 모든 선택 문구 표시 | [선택지](13-filter-choices-final.png), [미보고](14-pending-filter-final.png), [기존 보고](15-existing-filter-final.png) |
| 이력·취소 | B 재진입 시 이전2건+신규1건 이력과 잔여1d 확인. 제출 없이 취소 후 저장해도 바이트 동일 | [이력](16-history-reopened.png), [취소](17-reentry-cancel.png), [저장 상태](18-after-cancel-saved.png) |
| 재예측 | F9 후 실적 반영 예측 완료·완료일11-19 18:00·표/간트 표시 | [계산](19-forecast-calculated.png) |

01~11은 동일 기능의 첫 수동 빌드다. 이때 필터 이름 잘림을 발견해 고정 선택지의 글꼴 폭으로 보정했다.
12~19는 최종 보정 빌드다. [10](10-pending-filter.png)은 수정 전 필터 표시 증거이며 완료 화면으로 사용하지 않는다.
같은 이름의txt에 도구가 반환한 접근성 원문을 보존했다. 일부 자식 입력창·팝업은 원문을 반환하지 않아 빈txt이며,
해당 검증은 이미지에 근거한다. 13 이미지는 열린 필터 팝업 부분 캡처다.

[submitted.ppm](submitted.ppm)은2건 제출 후 명시 저장 사본이다. [after-cancel.ppm](after-cancel.ppm)은
재열기·이력 조회·초안 취소 후 저장 사본이며, 현재 [review.ppm](review.ppm)과 함께 SHA-256이 동일하다.
[verify_saved.py](verify_saved.py)로 기존 작업·기준선·보고 원장 접두부, 신규B/C값, D/E 미보고,
같은 파일 형식과 취소 전후 바이트를 확인한다. 상태일 변경에 따른 기존 평준화 채택 무효화 항목1건은
기존 도메인 계약대로 추가됐으며 이전 채택 이력은 보존됐다. [실행 결과](saved-file-check.txt).

## 자동 검사

- [관련 회귀](regression-final.log):10/10 통과. weekly_update_ui, tracking_ui, duration_units_ui,
  dialog_layout, async_editing_ui, effort_ui, task_editing_ui, tracking_contract, tracking_file, undo_contract.
- 필터 폭 보정 후 다시 빌드하고 영향을 받는 [weekly_update_ui·dialog_layout](layout-fix-regression.log):2/2 통과.
- [주간 입력 상세](weekly-tests.txt):QtTest4건(실제 검사2개+초기화/정리) 통과.
  현재/이번 주 앞선/이전 주/미보고 분류, 같은 시점 마지막 정정, 미래 보고 제외, 필터·검색,
  상태일 없음·읽기 전용·세션 교체, 조회 무변경, 주 창에서 숨은 작업의 안정ID 연결,
  잘못된 기간은 같은 창 유지,2건 연속 제출·다음 초안 취소·저장/재열기를 검사했다.
- [배치 상세](layout-tests.txt):54건 통과. 주간 창을 포함해640×480/1024×650/1440×900,
  밝은/어두운 테마의 버튼·스크롤 배치를 검사한다. 실제 OS 화면 조작과는 구분한다.
- 최종 빌드의 [-platform windows 실행](native-tests-final.txt):주간 입력 QtTest4건 통과.
- 전체 테스트 모음·대규모 프로젝트 UI 성능·외부 MSP/P6 왕복은 이번에 실행하지 않았다.

빌드 기준은 기존 `dc50154` 이후 이 증분이다. 실행 파일과 변경 소스 및 증거 파일 SHA-256을
[manifest.json](manifest.json)에 기록했다. 최종 빌드는 [build-layout-fix.log](build-layout-fix.log),
그 전 빌드·회귀 로그는 진행 중 증거로 보존한다. 엔진·영속화 schema 변경은 없다.
