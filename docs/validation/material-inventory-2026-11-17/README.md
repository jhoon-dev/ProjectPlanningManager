# 시간축 계획 입고·잔여 자재 재고 검증

2026-11-17 Windows/Qt6.8.3 MinGW Release · `build-numbered-summary`. [MINV-01~07 계약](../../material-inventory.md).

- 관련 회귀 **44/44,66.22초**. 새 재고/입고·기존 자재 계획·공유 가용기간/잔여/부하·통합 평준화/작업공간·로컬 가용기간/계산식·XML/XER batch·보고/원가/Usage·자원 계획·운영/위험/자동화·통화/ID 이관·평준화 채택·마일스톤/승인·tracking·undo/복구·파서의 영향 범위 부분집합이다. 전체251개 suite 실행을 의미하지 않는다. JSON에 자재 이름/단위도 포함한 뒤 출력 UI를 별도로 재확인했다.
- 파서 **1,062개**,native1~81/workspace1~4를 통과했다. schema81 정상·소수/0수량·없는 자재·잘못된 시각/키·필수 키 누락·개시 이전 입고를 포함한다. 새 필드가 있는80 파일의 거절도 직접 확인했다. 공통 fuzz harness는 같은 본문을 모든 schema로 재해석하므로 이전 버전 envelope 거절을 별도 검사로 분리했다. 초기43/44 기록과 수정 후44/44 로그를 함께 보존한다.
- 개시2개·시작 소모3개·완료 소모3개·균등1.001개/61근무분과 시각별 입고를 사용했다.09:00 잔여1개,09:30 잔여1.508개,10:00 잔여0.015개,10:01 잔여−0.001개,10:02 입고로0개를 확인했다. 이후 회복에도 최초 부족10:01을 보존한다. 개시 재고 미정은 입고1개가 있어도 잔여가 미정이며 기준 이전 소모/입고/잔여도 미정이다.
- 균등 소모의 누적 half-up·마지막 총량·점심 구간의 동일 누계748milli·수동 중단·두 자원 실행 구간 합집합을 확인했다. 개시 시각을09:30으로 바꾸면09:29까지의 소모를 재차 차감하지 않고09:30 해당 분의16milli부터 반영한다. 이진 탐색은 균등 첫 소모09:01의 부족도 찾는다.
- 입고 참조·형식·개시 전 시각·수량0/상한 초과·ID 중복·시간대 오프셋을 거절한다. 오래된/부분/예측 일정·없는 자재·잘못된 조회 시각을 거절하고 인덱싱 후 취소를 전달한다. 조회는 입력을 바꾸지 않는다.
- 단일/공유 통합 평준화에서 총 입고량이 충분해도10:00 입고 전에09:00 시작 소모가 있으면 후보 날짜를 거절한다. 작업을10:00으로 직접 조정하면 유효하다. 입고 제약을 포함한 자원 여유는 미산정이며 가짜 여유를 표시하지 않는다. 입고 원가를 중복 추가하지 않아 기존 계획 원가700최소통화단위를 유지한다.
- native81 정확 JSON/파일 왕복·80 거절·미사용20 최소 writer,승인본/시나리오·일정 채택·통화/ID 전후 사본의 writer 감지를 확인했다. 실제 Document의 개시/입고 편집 undo/redo·복구·승인본 캡처·시나리오 저장과 그 파일의 정확 왕복도 통과했다. 프로젝트 원장과 작업 날짜는 조회로 수정하지 않는다.
- UI는 기존 입고 탭 불러오기·오류 시 원본 보존·수정 후 한 번에 저장/undo,10:01 음수/미정·세 형식 저장·입고 수정 시 표/출력 무효화·새 잔여0.999·잘못된 시각의 빈 표를 확인했다. 파일 선택 자동화의 초기 대기를 별도 타이머로 고친 뒤 실제 JSON/XLSX/PDF 저장이 통과했다. `review.png`의 표/버튼과 tooltip 출처를 확인했다.
- 독립 Python은 native81 checksum/입고/기간에서 **1,440분 × 5지표 = 7,200값**을 정수로 재구성했다. 최초 부족10:01·회복·UI JSON의 두 자재/단위/미정,시점 XLSX2행과 입고5행/개시 재고2행,시점1쪽+일반 보고13쪽 PDF의 음수/미정/새 표를 판독했다. `independent.json`.
- 시점 PDF와 일반 보고의 입고·개시 기준 표(11~12쪽)를 Poppler로 렌더링해 확인했다. 새 화면/보고의 마이너스 설명은 글꼴 누락 없이 표시되도록 정리했다. C++19파일 clang-format,scoped whitespace,Python AST·기능 문서 링크를 검사했으며 소스/산출물 해시는 `sha256.json`에 보존한다. `PPM.exe`/`PPM-Automation.exe`도 갱신 빌드했다.

재실행:

```powershell
cmake --build build-numbered-summary --parallel 8 --target JOBS jobs_material_inventory_tests jobs_shared_period_tests jobs_shared_availability_tests jobs_remaining_availability_tests jobs_shared_resource_tests jobs_workspace_file_tests jobs_workspace_ui_tests jobs_resource_workspace_ui_tests jobs_integrated_workspace_tests jobs_resource_availability_tests jobs_xml_unit_tests jobs_report_tests jobs_report_ui_tests jobs_project_xml_tests jobs_project_xml_ui_tests jobs_xml_formula_tests jobs_undo_tests jobs_recovery_tests jobs_recovery_ui_tests jobs_parser_seed_tests jobs_xer_batch_tests jobs_xer_batch_ui_tests jobs_leveling_float_tests jobs_repartition_float_tests jobs_cost_day_tests jobs_usage_tests jobs_resource_tests jobs_resource_ui_tests jobs_governance_risk_tests jobs_governance_ui_tests jobs_resource_plan_tests jobs_project_currency_tests jobs_ledger_identity_tests jobs_leveling_selection_tests jobs_milestone_baseline_tests jobs_tracking_tests jobs_tracking_file_tests
ctest --test-dir build-numbered-summary --output-on-failure --parallel 2 -R '^(material_inventory_|shared_periods_|shared_availability_|remaining_availability_|shared_resource_intervals$|workspace_file_contract$|multi_project_workspace_ui$|resource_workspace_ui$|integrated_workspace_contract$|resource_availability_|xml_unit_fields_|reports_|project_xml_|xml_formula_|undo_contract$|recovery_|parser_seed_replay$|xer_batch_|leveling_float_contract$|repartition_float_contract$|cost_day_boundaries$|timephased_usage_contract$|resource_contract_file$|resource_ui$|governance_|resource_plan_contract$|project_currency_conversion_contract$|ledger_identity_migration_contract$|leveling_selection_contract$|milestone_baseline_regressions$|tracking_(contract|file)$)'
build/validation-python/Scripts/python.exe scripts/verify-material-inventory.py docs/validation/material-inventory-2026-11-17
```

전체44개를 재실행할 때는 위 테스트 대상도 함께 빌드한다.

계획 입고는 실제 자재 입고/소모 원장이 아니다. 자동 자재 지연 탐색·구매/비용 최적화·실제 재고 원장·입고 제약을 포함한 여유·외부 제품 실행은 후속 범위다.
