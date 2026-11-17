# 자재 입고 자동 대기 검증

2026-11-17 Windows/Qt6.8.3 MinGW Release · `build-numbered-summary`. [MAD-01~07 계약](../../material-arrival-leveling.md).

- 영향 범위의 **47개를 모두 통과**했다. 관련 회귀는 46/47, 92.25초였고, 미저장 변경에서만 스냅샷을 만드는 기존 조건에 맞춰 새 복구 테스트 순서를 수정한 뒤 해당 계약 1/1, 6.61초를 통과했다. 제품 코드는 회귀 후 바뀌지 않았다. 전체253개 실행을 의미하지 않는다. 앱 `PPM.exe`/`PPM-Automation.exe`를 갱신 빌드했다.
- 파서 **1,076개**, native1~82/workspace1~4를 통과했다.82 정상, 비불리언 옵션·추가 키 오류,81 envelope의 새 필드 거절을 포함한다. 과거 결정만 남은82 파일을81로 해석하는 것도 거절한다.
- 독립 Python은 계획 입력에서 근무분 후보 **5,443개**를 열거해 24개 사례/48작업의 시작·완료 **96개**가 모두 일치했다. 자원 용량100/200%, FS 또는 독립 작업, 세 우선순위, 세 소모 방식·재고/입고를 조합했다. same-minute 공급/소모와 정수 누적 half-up을 독립 재구성했다. native82 체크섬·승인본/시나리오·XLSX 정책 시트와 PDF15쪽의 새 정책도 판독했다. `independent.json`.
- 기본 FS0·PDM·제약 PDM·공수의 네 계산 프로필에서 10:00 입고에 따른 A 작업 지연과 후속 B 작업 전파를 확인한다. 입력은 그대로이며 옵션을 끈 schema81 계획은 기존 후보 검산을 유지한다.
- 같은 입고를 쓰는 독립 작업의 순차 배치와 이전 작업의 미래 완료 소모 예약을 확인한다. 완료 소모는 입고 전 착수가 가능하고, 균등 60,000milli/60분은 09:59, 1milli/60분은 09:30에 착수해야 10:00 입고 전 재고가 음수가 되지 않는다.
- 점심 입고 → 13:00, 휴일 → 다음 근무일, 자원 가용 시작 → 11:00, 늦은 개시 재고 기준 이전 소비 금지, 0분 작업의 입고 대기를 확인한다. 고정 시작·재고 미정·전체 공급 부족·탐색 기간 초과는 후보를 적용하지 않는다. 실제 소모를 알 수 없는 잔여 예측은 거절한다.
- 공수 곡선 작업은 완료 소모 입고에 맞는 최초 합법 시작을 실행 모델의 분 단위 후보 열거와 비교한다. 별도 장기 균등 소모 사례는 20,000회 탐색 한도와 빈 후보 날짜를 검사한다. 한도 진단은 기존 계산 오류의 하위 원인에도 보존된다. 협력 취소를 전달한다.
- 공유 풀 통합에서 이름/ID가 같은 두 프로젝트의 자재를 서로 다른 재고로 유지하고 각자의 10:00/11:00 입고에 배치한다. 원본의 입력·파일은 수정하지 않는다.
- native82 정확 JSON/파일 왕복·81 새 필드 거절·기존81 writer, 승인본/시나리오·일정 채택·통화/ID 전후 사본을 확인한다. Document 채택·undo/redo·승인본 재계산·시나리오·복구를 검사한다. 옵션과 자재를 모두 지워 과거 결정 이력만 남아도82를 유지하고 정확히 다시 읽는다.
- UI는 명시 옵션 저장·undo/redo·읽기 전용, 비동기 비교 날짜/`MATERIAL_DELAY`, 사유 입력 후 실제 일정 채택/재현을 검사한다. `review.png`에서 날짜·진단·버튼을 확인한다.
- PDF13쪽의 자동 대기 정책·입고·개시 재고 표를 Poppler로 렌더링해 `policy-page.png`에서 확인했다. C++22파일 clang-format, scoped whitespace, Python AST와 새 문서 링크를 검사했다. 소스/산출물 해시는 `sha256.json`에 보존한다.
- 초기 집중 실행은 새 정책을 결정 이력 검증에서 인식하지 못해 채택 두 건이 실패했다. 역사 정책 검증의 입력을 보완했다. 이어 추가한 한도 검사는 최상위 진단만 확인해 실패했으며 하위 원인을 확인하도록 수정했다. 복구 테스트는 저장 직후 스냅샷을 요청하던 순서를 저장 전으로 바로잡았다. 초기 로그와 최종 회귀 로그를 함께 보존한다.

재실행은 관련 테스트 대상을 먼저 빌드한 뒤 다음을 사용한다.

```powershell
ctest --test-dir build-numbered-summary --output-on-failure --parallel 2 -R '^(material_delay_|material_inventory_|shared_periods_|shared_availability_|remaining_availability_|shared_resource_intervals$|workspace_file_contract$|multi_project_workspace_ui$|resource_workspace_ui$|integrated_workspace_contract$|resource_availability_|xml_unit_fields_|reports_|project_xml_|xml_formula_|undo_contract$|recovery_|parser_seed_replay$|xer_batch_|leveling_float_contract$|repartition_float_contract$|cost_day_boundaries$|timephased_usage_contract$|resource_contract_file$|resource_ui$|governance_|resource_plan_contract$|project_currency_conversion_contract$|ledger_identity_migration_contract$|leveling_selection_contract$|leveling_decision_contract$|milestone_baseline_regressions$|tracking_(contract|file)$)'
build/validation-python/Scripts/python.exe scripts/verify-material-delay.py docs/validation/material-arrival-leveling-2026-11-17
```

계획 입고의 결정적 직렬 평준화다. 전역 최적 완료일·구매/비용 최적화·실제 재고 원장·자재 제약 여유·여러 원본 파일의 통합 날짜 일괄 채택·외부 제품 실행은 이번 검증 범위가 아니다.
