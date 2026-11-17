# UX-W05 기준선·예측·자원 조정 결과 요약 검증

2026-11-17 · [사용법](../../outcome-summary.md) · [기획 우선순위](../../planning/21-weekly-update-ux-review.md).

## 구현 범위

주 창에 결과 비교 진입을 추가했다. 기준선·현재 계획·조정 전 논리 예측·현재 적용 일정의 완료일,
기준선 대비·조정 전 대비 편차와 채택 상태를 상단 표로 비교한다. 근무일(d)과 시간(h)을 함께 표시한다.
보고 범위·미보고 가정·배정 작업 검증 범위를 안내하고, 작업별 실적·관계·배정, 자원 부하,
채택·해제 이력으로 근거를 따라간다. 계산 출처·전체 진단은 펼치기로 유지했다.
자원 후보와 현재 적용 결과를 분리하고 요약 행에서 각각의 부하 탭으로 이동한다.
기준선 없음·미정은 —로 표시하고 다른 A/B 입력은 동일 입력의 정책 비교로 취급하지 않는다.
조회는 문서나 명시 저장 파일을 변경하지 않는다. 엔진·schema·기존 채택 검증 계약은 유지했다.

## 실제 Windows 화면

앞 단계의 합성 [submitted.ppm](../save-reforecast-flow-2026-11-17/submitted.ppm)을 복사해
native 앱에서 열고 클릭·키보드로 조작했다. 1562×932 어두운 테마의 전문가 점검이다.
상태일11-16 09:00, 작업5개·보고 원장6건·보고 작업3/5·자원 배정 작업2/5, 기준선1개다.
미보고 D/E를 미착수로 확정하지 않는다. 기준선 기본 달력은 하루8시간·월~금 근무다.

| 단계 | 실제 확인 | 증거 |
| --- | --- | --- |
| 진입 | 작업 표 위 결과 비교 버튼으로 기존 비교 기능에 진입 | [01](01-result-entry.png) |
| 완료일 비교 | 기준선/현재 계획11-17 18:00, 조정 전/현재 적용11-20 18:00, +3d(+24h), 채택 없음·과부하16h | [02](02-comparison-overview.png) |
| 작업 근거 | B 선택으로 실제16h·잔여16h, 예측11-17·기준선 대비+24h, 작업ID·선행 관계 확인 | [03](03-task-evidence.png) |
| 후보 비교 | A 후보11-24 18:00, 기준선 대비+5d(+40h), 조정 전 대비+2d(+16h), 미채택·과부하0h | [04](04-resource-overview.png) |
| 조정 전 근거 | A 조정 전 행 선택 → 원본 부하.11-18/19 기술자 수요200%·가용100%, 각8h 과부하, 연결 작업 조립/현장 설치 준비 | [05](05-before-load.png) |
| 후보 근거 | 검토 후보A 행 선택 → 평준화 후 부하. 조립11-18/19, 현장 준비11-20/23으로 분리, 과부하0h | [06](06-candidate-load.png) |
| 채택 | 사유 입력 후 명시적 채택. 현재 적용11-24·과부하0h와 후보의 현재 채택 설정 일치 표시 | [07](07-adopted-overview.png) |
| 현재 부하 | 현재 적용 일정 행 선택 → 현재 적용 부하, 채택한 날짜와 작업별 부하 일치 | [08](08-applied-load.png) |
| 결정 이력 | 기존2건 보존·새 채택1건. 사유·시각·원본revision10→11·잔여 예측 정책 확인 | [09](09-adoption-history.png) |
| 비교로 복귀 | 자동으로 새 요약 계산. 논리 예측11-20과 채택 일정11-24·각 편차를 나란히 유지 | [10](10-adopted-comparison.png) |
| 계산 출처 | 펼치기로 기준선 메타데이터·계산 엔진·revision·진단에 접근 | [11](11-calculation-provenance.png) |
| 저장 | Ctrl+S 후 파일 저장됨·재예측 완료. 주 화면 완료일11-24 18:00 일치 | [12](12-explicit-save.png) |

이미지를 판단 기준으로 사용했다. 같은 이름의txt는 접근성 원문이며 일부 자식 창은 도구가 텍스트를 반환하지 않아 빈 파일이다.
[before-explicit-save.ppm](before-explicit-save.ppm)은 채택 뒤 명시 저장 전 디스크 사본으로 원본과 바이트가 같다.
[review.ppm](review.ppm)은 명시 저장 후 프로젝트다. 작업·보고6건·기준선·자원 배정·상태일·기존 결정 이력을 보존하고
잔여 예측 채택 설정·결정1건·revision만 변경했다. [검사 스크립트](verify_saved.py)와 [결과](saved-file-check.txt)로 확인한다.

## 자동 검사와 한계

- [관련 회귀](regression-final.log)10/10 통과: outcome_summary_ui, tracking_ui, resource_ui,
  leveling_selection_ui, progress_leveling_ui, dialog_layout, workspace_presentation,
  tracking_contract, leveling_selection_contract, leveling_decision_contract.
- 새 [outcome_summary_ui](outcome-tests.txt)는 실제 검사3개+초기화/정리5건 통과.
  조회 무변경·채택/해제·오래된 요약·부하 탭 연결·기준선 없음·미정·휴일 편차·다른 A/B 입력을 확인한다.
- 최초 Windows native 실행의 임시 폴더 정리 경고는 테스트의 문서/폴더 수명 순서 때문이었다.
  문서를 먼저 닫도록 테스트 선언 순서만 수정하고 [다시 빌드](build-test-cleanup.log)했다.
  앱 소스는 이 수정으로 바꾸지 않았다. [최종 CTest](regression-final.log)와 [-platform windows](native-tests.txt)에서도5건 통과했다.
- 마지막 검토에서 불완전 입력의 A 조정 전 부하 합계를0h로 표시할 수 있는 경로도 미정으로 보완했다.
  해당 사례를 새 검사에 추가하고 앱과 관련 UI 검사를 [재빌드](build-missing-load-final.log)한 뒤 위 회귀·native 검사를 재실행했다.
  수동 캡처의 유효 입력 결과에는 영향을 주지 않는 표시 수정이다.
  [첫 재빌드](build-missing-load.log)는 남아 있던 빈 앱 창이 실행 파일을 점유해 링크에 실패했다.
  해당 창을 닫은 뒤 재빌드는 통과했다.
- [dialog_layout](layout-tests.txt)54건, [workspace_presentation](workspace-tests.txt)10건 통과.
  작은 창과 확대 글꼴 배치 회귀를 포함한다. OS DPI를 변경한 수동 검증은 아니다.
- 실제 PM·실제 업무 파일·macOS 수동 점검·MSP/P6 외부 왕복·전체 테스트 모음은 이번에 수행하지 않았다.
  후보는 기존 평준화 정책의 결과이며 전역 최적 일정이나 미배정 작업까지의 자원 검증을 뜻하지 않는다.

수동 점검 앱 빌드는 [build-feedback.log](build-feedback.log), 최종 빌드는 [build-missing-load-final.log](build-missing-load-final.log)다.
소스 기준점은 `2a77103`이다.
실행 파일·소스·증거 SHA-256은 [manifest.json](manifest.json)에 기록한다.
