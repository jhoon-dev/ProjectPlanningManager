# 공유 풀 부분·열린 가용기간 검증

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/shared-resource-availability-2026-11-16)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

2026-11-16 Windows/Qt6.8.3 MinGW Release · `build-numbered-summary`. [SPA-01~06 계약](../../shared-resource-availability.md).

- 관련 회귀 **36/36,75.75초**. 새 기간·공유 잔여 조회·공유 부하/통합 평준화·작업공간 파일/UI·로컬 가용기간/잔여·Units/계산식·XML/XER batch·보고·자원 계획·운영/위험/자동화·평준화/재분배 여유·원가/Usage·undo/복구·파서의 영향 범위 부분집합이다. 전체 suite 실행을 의미하지 않는다. 새 계약/UI 집중2/2도 통과했다.
- 파서 **1,042개**. 기존 native1~80/workspace1~3과 새 workspace4 정상 seed,역전·상한/정수·필수 키·알 수 없는 키·잘못된 시각·과거 schema의 새 필드·미래5 거절을 포함한다. `shared-periods-final-detail.log`.
- 09:15 전체 가용 시작,09:29까지50%,09:30~10:14 미정,10:15부터 열린200%,이틀 후10:29 전체 종료를 검사했다. 종료 다음 분0,단 한 분의 전체 경계,양방향 열린 기간,기간 공백의 기본값,날짜 예외 우선순위,날짜 미정과 근무 달력 휴무0을 구분했다. 공유10,000,000%와 로컬 평준화100,000% 상한도 구분한다.
- 기간 겹침·공통 끝점·중복 열린 구간·역전·음수/초과·잘못된 시각/시간대·10,001기간을 거절한다. 제공된 공유 풀과 두 로컬 프로젝트는 조회/통합 계산 후 변하지 않는다.
- 3일 일별 적분: 첫날 과부하30분·초과 공수1,500(%·분)·미정45분,둘째 날 날짜 예외75%로 과부하480분·초과 공수24,000(%·분),마지막 날 전체 종료 뒤 과부하150분·초과 공수11,250(%·분)을 대조했다. 여러 설정값은 capacity=null/variableCapacity=true 및 UI/XLSX/PDF 시간별로 표시한다. 수요가 있는 구간의 설정값 요약이며 실제 과부하에는 근무 달력을 적용한다.
- 공유 평준화 fixture는 기본0%,09:30 전체 가용 시작·10:00부터 열린200%·10:59 전체 종료에서 두 프로젝트가 함께10:00~11:00에 실행됨을 확인했다. 기간 값을100,001%로 올리면 기존 통합 평준화 상한으로 거절한다. 논리 통합 일정의 공유 부하와 JSON/XLSX/PDF에도 기간 공급이 반영된다.
- 작업공간4 정확 왕복·schema1/2/3 최소 writer 유지·통합/운영 필드와 새 기간의 공존·과거 schema 새 필드 거절·미래5·서명된 잘못된 자료·검증 실패 시 기존 파일 바이트 보존을 확인했다. 독립 reader도 workspace4와 두 native 파일의 checksum/상대 경로를 판독했다. native 상한80은 그대로다.
- UI: 경계/기간 편집·겹침 수정 후 적용·재열기·취소·실패 후 새 풀 한 개만 추가·09:45 공급 미정/수요125%·10:30 공급200%/잔여75%·기간 변경 시 표/출력 무효화·재조회 공급300%/잔여175%를 확인했다. 800×600 편집 화면의 스크롤과 고정 확인/취소 버튼을 `editor.png`로 확인했다. 초기 UI 테스트는 새 풀 이름을 누락해 대기 제한에 걸렸으며 fixture를 보완한 후 집중/최종 회귀가 통과했다.
- 독립 Python oracle은 저장한 native 배정/기간/명시 제약과 workspace4 경계/기간/날짜/달력에서 **4,320분 × 5지표 = 21,600값**을 재구성했다. 일별3행 적분·통합 JSON/시간별 표시·시점 JSON2개·XLSX 시점2행/통합3행·PDF총4쪽의125%/75%·미정·시간별을 판독했다. `independent.json`.
- Poppler 렌더와 눈으로 본 화면/PDF의 기간 입력·날짜·공급/수요·시간별/미정·페이지 경계를 확인했다. 변경 C++13파일 clang-format·scoped whitespace·Python AST·기능 문서 로컬 링크를 검사했다. `PPM.exe`와 `PPM-Automation.exe`를 갱신했고,보존 소스/산출물 SHA256은 `sha256.json`에 기록한다.

재실행:

```powershell
cmake --build build-numbered-summary --parallel 8 --target JOBS jobs_shared_period_tests jobs_shared_availability_tests jobs_remaining_availability_tests jobs_shared_resource_tests jobs_workspace_file_tests jobs_workspace_ui_tests jobs_resource_workspace_ui_tests jobs_integrated_workspace_tests jobs_resource_availability_tests jobs_xml_unit_tests jobs_report_tests jobs_report_ui_tests jobs_project_xml_tests jobs_project_xml_ui_tests jobs_xml_formula_tests jobs_undo_tests jobs_recovery_tests jobs_recovery_ui_tests jobs_parser_seed_tests jobs_xer_batch_tests jobs_xer_batch_ui_tests jobs_leveling_float_tests jobs_repartition_float_tests jobs_cost_day_tests jobs_usage_tests jobs_resource_tests jobs_resource_ui_tests jobs_governance_risk_tests jobs_governance_ui_tests jobs_resource_plan_tests
ctest --test-dir build-numbered-summary --output-on-failure --parallel 2 -R '^(shared_periods_|shared_availability_|remaining_availability_|shared_resource_intervals$|workspace_file_contract$|multi_project_workspace_ui$|resource_workspace_ui$|integrated_workspace_contract$|resource_availability_|xml_unit_fields_|reports_|project_xml_|xml_formula_|undo_contract$|recovery_|parser_seed_replay$|xer_batch_|leveling_float_contract$|repartition_float_contract$|cost_day_boundaries$|timephased_usage_contract$|resource_contract_file$|resource_ui$|governance_|resource_plan_contract$)'
build/validation-python/Scripts/python.exe scripts/verify-shared-periods.py docs/validation/shared-resource-availability-2026-11-16
```

가용기간은 공유 풀의 공급 모델이다. 로컬 프로젝트의 가용량·배정·원장을 덮어쓰지 않으며 실제 타임시트/초과근무·입고·비용 최적화·여러 파일 날짜 일괄 채택과 외부 제품 실행은 별도 범위다.
