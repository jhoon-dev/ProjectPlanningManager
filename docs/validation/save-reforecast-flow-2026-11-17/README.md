# UX-W04 저장·재예측 흐름 검증

2026-11-17 · [사용법](../../save-reforecast-flow.md) · [기획 우선순위](../../planning/21-weekly-update-ux-review.md).

## 구현 범위

작업 표 위에 파일 저장 상태와 일정 계산 상태를 나란히 표시한다. 파일 저장·계산 버튼과 다음 행동 안내를 제공한다.
주간 갱신의 입력 마치고 재예측은 창을 닫고 기존 계산 명령을 실행한다. 파일을 자동 저장하지 않는다.
목록 닫기·초안 취소는 재계산을 시작하지 않는다. 계산 중·편집 적용 대기도 완료로 표시하지 않는다.
입력 불완전·충돌·취소를 구분하고 진단 확인·입력 수정·재시도를 안내한다.
엔진·영속화 schema·보고/기준선 계약과 기존 출력·채택의 최신성 검증은 변경하지 않았다.

## 실제 Windows 화면

기존 합성 [submitted.ppm](../weekly-update-flow-2026-11-17/submitted.ppm)의 사본을 native 앱에서 열고 클릭·키보드로 조작했다.
1562×932 어두운 테마의 전문가 점검이며 실제 PM·실제 업무 파일·OS DPI 변경·macOS 수동 검증은 아니다.
화면 이미지를 판단 기준으로 사용했다. 같은 이름의txt는 도구의 접근성 원문이며 자식 창 일부는 반환되지 않아 빈 파일이다.

| 단계 | 확인 | 증거 |
| --- | --- | --- |
| 저장 파일 열기 | 파일 저장됨과 일정 계산 필요를 별도로 표시 | [01](01-open-saved-needs-calculation.png) |
| 파일 저장 | 본문 파일 저장을 눌러도 계산 필요 유지 | [02](02-save-still-needs-calculation.png) |
| 본문 재예측 | 본문 재예측 버튼으로 현재 입력의 재예측 완료, 파일 저장됨 유지 | [03](03-saved-and-calculated.png) |
| 주간 입력 | 입력 마치고 재예측 버튼 확인, B 잔여1d→2d 정정·사유 제출 | [04](04-weekly-reforecast-entry.png), [05](05-correction-draft.png) |
| 취소·닫기 | 다음 D 입력을 취소하고 목록 닫기. 기존 제출1건 유지, 현재 일정은 오래됨, 파일 미저장. 재계산 자동 실행 없음 | [06](06-submitted-one-cancelled-next.png), [07](07-unsaved-stale-after-close.png) |
| 명시적 재예측 | 주간 갱신을 다시 열어 입력 마치고 재예측. 창이 닫히고 예측 완료11-20 18:00. 파일 미저장과 저장 안내 유지 | [08](08-reforecast-complete-unsaved.png) |
| 파일 저장 | 본문 저장 후 파일 저장됨·재예측 완료를 함께 유지 | [09](09-saved-and-reforecast-complete.png) |

[before-explicit-save.ppm](before-explicit-save.ppm)과 [after-reforecast-before-save.ppm](after-reforecast-before-save.ppm)은
명시 저장 전 디스크 사본이며 원래 입력과 바이트가 같다. 보고 제출과 재예측이 명시 저장을 대신하지 않는 것을 확인했다.
[submitted.ppm](submitted.ppm)은 명시 저장 후 사본이다. 기존 보고5건·기준선1개·작업·상태일을 보존하고,
B의 정정1건(실제960분/잔여960분)만 추가됐다. 취소한 D와 미입력 E는 보고되지 않았다.
[verify_saved.py](verify_saved.py)의 [실행 결과](saved-file-check.txt)로 확인한다.

정정 후 저장 파일의 추가 수동 재열기는 공유 화면 제어에서 `Computer Use helper already has an active request`,
접근성 캐시 소실과 요청한 경로와 다른 입력을 관찰해 중단했다. 파일 선택을 취소하고 창이 닫힌 것을 확인했다.
이 추가 수동 재열기를 통과로 계산하지 않는다. 재열기 상태·이력 보존은 아래 Windows native 자동 검사에서 확인했다.

## 자동 검사와 한계

- [관련 회귀 실행](regression.log): 기존9개 통과. async_editing_ui, undo_contract, tracking_ui, duration_units_ui,
  dialog_layout, tracking_file, weekly_update_ui, tracking_contract, workspace_presentation.
  새 workflow_ui는 빈 창을 만든 뒤 UI 명령을 거치지 않고 문서를 생성한 테스트 설정 때문에 계산 액션이 비활성인 상태로 실패했다.
  실제 생성·열기 경로와 같이 문서를 준비한 뒤 창을 만드는 검사로 수정했고 앱 코드는 이 실패로 변경하지 않았다.
- 수정 후 [workflow_ui 재실행](workflow-final.log) 통과. [상세](workflow-tests.txt)는5건(실제 검사3개+초기화/정리) 통과.
  처음 저장·기준선 저장·상태일/보고·저장/계산 독립·재열기·읽기 전용 사본 버튼·주간 닫기/명시적 완료를 검사한다.
  불완전 입력은 실제 계산으로 확인하고 충돌·취소 표시는 같은 문서에 해당 결과를 적용해 검사했다.
- [-platform windows](native-tests.txt)에서도5건 통과. 새 보고를 포함한 저장 파일을 재열면
  이력은 보존되고 저장됨·계산 필요로 표시됨을 확인한다.
- [workspace_presentation](workspace-tests.txt)10건, [dialog_layout](layout-tests.txt)54건,
  [weekly_update_ui](weekly-tests.txt)4건 통과. 작은 창의 작업 행·명령과 주간 입력 버튼 배치를 포함한다.
- 기간 입력 회귀 실행 파일도 최신 UI로 [다시 빌드](build-duration-test.log)한 뒤 [duration_units_ui](duration-final.log)를 재실행해 통과했다.
- 전체 테스트 모음·외부 MSP/P6 왕복·실제 사용자 무도움 성공 판정은 이번에 수행하지 않았다.

빌드는 [build.log](build.log), 테스트 설정 수정 빌드는 [build-workflow-test.log](build-workflow-test.log)다.
소스 기준점 `f48a34f` 이후 이 증분의 실행 파일·소스·증거 SHA-256을 [manifest.json](manifest.json)에 기록한다.
