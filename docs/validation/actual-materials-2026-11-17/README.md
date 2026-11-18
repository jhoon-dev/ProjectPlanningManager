# 실제 자재 원장·잔여 일정 자동 대기 검증

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/actual-materials-2026-11-17)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

2026-11-17 Windows/Qt6.8.3 MinGW Release · `build-numbered-summary`. [MAT-01~08 계약/사용법](../../actual-materials.md).

- **관련 회귀59/59,132.35초**. 새 원장/잔여 자동 대기와 기존 계획 자재 재고/지연, 실적/원가 정정·날짜별 잔여 공수·공수 역산/여유·진척 정책/채택·로컬/공유 가용량·통합 일정·원가/Usage·XML/XER 교환·보고·시나리오/승인·통화/ID 이관·undo/복구·파서의 영향 범위다. 전체 suite 실행을 의미하지 않는다. 앞 집중7/7,17.41초도 보존한다.
- **파서1,100개**, native1~83/workspace1~4를 통과했다.83 원장/잔여 옵션 정확 왕복·82 거절, 음수/문자 수량·종류·필수 단위 누락·소수 revision·없는 이전 기록·다른 단위·비불리언 옵션·추가 키 오류를 포함한다. 공통 body harness의 여러 schema 재해석과82 envelope 거절을 분리했다.
- 개시2개 + 실제 입고1개 − 실제 소모3개 = 상태일 재고0개를 확인했다. 거래별 추가 전용 정정은 원래 행을 보존하고, 후일 입고1.5개 정정은 이전 상태일의0개와 새 상태일의0.5개를 구분한다.0 정정 취소, 같은 상태일의 늦은 정정에 따른 이전 확인 무효화, 오래된 정정·오류 batch·다른 확인 잔고의 원자 거절도 확인했다.
- 단위/개시 재고/기준·관측 작업·연결 입고의 보존을 검사했다. 실제 수량과 소모 연결은 작업 ID 이관 뒤에도 유지되고, 통화 전환은 물리 수량을 환산하지 않는다. 실제 기록이 있는 작업 삭제를 보호한다. 새 원장은 일반 undo로 삭제하지 않는다.
- 네 계산 프로필에서 실제 시작09:00을 보존하고 잔여 시작11:00/완료12:00, 후속 시작13:00을 확인했다. 이미 입고한1개를 계획 입고3개에서 차감해 미래 공급을2개로 사용한다. 전량 입고의 재사용·상태일이 지난 미입고 계획의 가상 공급·미확인/누락 잔여 수량은 거절한다. 완료 소모는10:00, 균등 소모는10:59에 잔여 착수한다.
- 고정 완료 상한을 완화하지 않고 공수 기반 곡선·날짜별 잔여 공수·완료 작업의 명시 잔여0·협력 취소를 확인했다. 진척 정책 세 가지와 잔여 자재 평준화의 조합도 통과했다. 실제 원장이 투영되지 않은 공유 통합 잔여 예측은 빈 결과와 명시 진단으로 거절한다.
- Document 채택/재현, 원장 정정에 따른 채택 해제, 재확인 후 재채택·당시 원장의 시나리오 동결/재계산, native83 파일 정확 왕복·복구를 확인했다. `source.ppm`에는 원장9행, 이전 상태의 시나리오에는5행이 남는다.
- UI 실제 입고/소모 제출·상태일 재고/잔여 수량 일괄 확인·잔여 옵션·비동기 평준화 비교 후 실제 채택·변경된 초안 무효화/명시 재불러오기·읽기 전용을 통과했다. `review.png`에서 재고0·명시 잔여1/1·작성 정보/확인 안내와 스크롤 바깥 닫기 버튼을 확인했다.
- 독립 Python은 native 입력과 거래별 정정에서 재고/미입고 잔량/명시 잔여 소모를 재구성하고 **근무분 후보4,746개**,24사례/48작업의 **실제 시작·잔여 시작·완료144개**를 모두 대조했다. 세 우선순위·FS/독립 작업·가용량100/200%·세 소모 방식·서로 다른 잔여 기간/수량/입고 시각을 조합했다. native83 checksum·현재 재고500milli·원장9행/동결5행, XLSX 원장/재고/새 정책 시트와 PDF16쪽을 판독했다. `independent.json`.
- PDF12쪽 원장/상태일 재고0.5개와13쪽 새 정책을 Poppler로 렌더링해 `stock-page.png`/`policy-page.png`에서 확인했다. C++ 형식·scoped whitespace·Python AST·새 문서 링크를 검사하고 `sha256.json`에 소스/산출물 해시를 보존한다.
- 초기 코어 컴파일에서 협력 취소 헤더 누락을 보완했다. 이후 집중·최종 기능 검사에는 실패가 없었다. 앱 `PPM.exe`와 `PPM-Automation.exe`를 갱신 빌드했으며 UI/검사 보완 뒤 앱과 새 검사 파일을 다시 빌드했다. 원시 로그와 소스/산출물 해시는 함께 보존한다.

재실행:

```powershell
cmake --build build-numbered-summary --parallel 8 --target JOBS jobs_material_actual_tests jobs_material_delay_tests jobs_material_inventory_tests jobs_shared_period_tests jobs_shared_availability_tests jobs_remaining_availability_tests jobs_shared_resource_tests jobs_workspace_file_tests jobs_workspace_ui_tests jobs_resource_workspace_ui_tests jobs_integrated_workspace_tests jobs_resource_availability_tests jobs_xml_unit_tests jobs_report_tests jobs_report_ui_tests jobs_project_xml_tests jobs_project_xml_ui_tests jobs_xml_formula_tests jobs_undo_tests jobs_recovery_tests jobs_recovery_ui_tests jobs_parser_seed_tests jobs_xer_batch_tests jobs_xer_batch_ui_tests jobs_leveling_float_tests jobs_repartition_float_tests jobs_cost_day_tests jobs_usage_tests jobs_resource_tests jobs_resource_ui_tests jobs_governance_risk_tests jobs_governance_ui_tests jobs_resource_plan_tests jobs_project_currency_tests jobs_ledger_identity_tests jobs_leveling_selection_tests jobs_leveling_decision_tests jobs_milestone_baseline_tests jobs_tracking_tests jobs_tracking_file_tests jobs_period_actual_tests jobs_period_actual_ui_tests jobs_dated_effort_tests jobs_dated_remaining_tests jobs_dated_remaining_ui_tests jobs_forecast_float_tests jobs_progress_leveling_tests jobs_progress_leveling_ui_tests jobs_progress_policy_adoption_tests jobs_progress_policy_adoption_ui_tests
ctest --test-dir build-numbered-summary --output-on-failure --parallel 2 -R '^(material_actual_|material_delay_|material_inventory_|shared_periods_|shared_availability_|remaining_availability_|shared_resource_intervals$|workspace_file_contract$|multi_project_workspace_ui$|resource_workspace_ui$|integrated_workspace_contract$|resource_availability_|xml_unit_fields_|reports_|project_xml_|xml_formula_|undo_contract$|recovery_|parser_seed_replay$|xer_batch_|leveling_float_contract$|repartition_float_contract$|cost_day_boundaries$|timephased_usage_contract$|resource_contract_file$|resource_ui$|governance_|resource_plan_contract$|project_currency_conversion_contract$|ledger_identity_migration_contract$|leveling_selection_contract$|leveling_decision_contract$|milestone_baseline_regressions$|tracking_(contract|file)$|period_actual|dated_effort_contract$|dated_remaining_|forecast_float_contract$|progress_leveling_|progress_policy_adoption_)'
build/validation-python/Scripts/python.exe scripts/verify-material-actuals.py docs/validation/actual-materials-2026-11-17
```

단일 문서 잔여 일정의 직렬 배치 범위다. 실제 원가 자동 생성·승인/타임시트·구매/원가 최적화·자재 제약 여유/임계·공유 통합 잔여 원장 투영·여러 원본 파일 날짜 일괄 채택과 외부 제품 실행은 이번 완료 범위가 아니다.
