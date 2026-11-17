# 공유 풀 잔여 가용량 검증

2026-11-16 Windows/Qt6.8.3 MinGW Release · `build-numbered-summary`. [SRA-01~06 계약](../../shared-remaining-availability.md).

- 영향 범위 회귀 **31/31,57.36초**. 공유 조회/로컬 잔여·가용기간·Units/외부 식·공유 부하·작업공간 파일/UI·자원 작업공간·통합 일정·XML/XER batch·보고·평준화/재분배 여유·원가/Usage·undo/복구·파서를 포함한 부분집합이다. 전체 suite 실행으로 확대하지 않는다. 초기 집중5/5도 통과했다.
- 파서 **1,022개**,schema1~80을 통과했다. 새 파생 조회는 저장 필드를 추가하지 않으며 기존 native와 작업공간 reader 계약을 유지한다. `shared-availability-final-detail.log`.
- P의 같은 작업에75%+25%, Q에50%를 배정하고 같은 로컬 자원 ID/이름을 사용했다. 동시 수요150%·공유 공급100%·잔여−50%·배정 가능0%·부족50%, 종료 분 제외·점심/퇴근0공급·다른 프로젝트 시작일 정렬을 확인했다. 로컬 공급75000%/0%/10%를 공유 공급에 합하지 않는다.
- 미연결 자원 수·미배정 풀 수요0,미설정 공급·공유 상한10,000,000%·공유 휴일·날짜 예외를 확인했다. 연결된 Q가 닫히면 확인된 수요100%는 남지만 전체 수요·잔여·배정 가능은 미정이다. 다른 미배정 풀의 배정 가능200%는 유지한다. 불완전 일정/잘못된 종류 연결도0수요로 처리하지 않는다.
- P 상태일10:00,Q 상태일10:30의10:15 조회는 P의100%만 하한으로 표시하고 Q/전체/잔여는 미정이다. 완료 작업 제외·계획/예측 혼합·명시 계획 조회,제공된 오래된/부분/다른 모드 일정·중복 입력 거절·구성 불일치 출력 거절·인덱싱 전/후 취소·입력 불변을 확인했다.
- UI는 작업공간 진입,초기09:00·10:15 수요150%/잔여−50%,Q 기간 변경 직후 재계산100%/0%,대기 중 이전 세대 알림과 새 계산의 경합,잘못된 날짜의 빈 표/출력 비활성화를 확인했다. `review.png`를 눈으로 확인했다. 구현은 열기/닫기/입력/카탈로그/연결 세대 및 모드·조회 시각 변경을 무효화한다.
- 독립 Python oracle은 native fixture의 두 파일 배정/기간/명시 제약에서 수요를 다시 구성해 **1,440분 × 5지표 = 7,200값**을 대조했다. 전체/닫힌 파일/다른 예측 상태일의 JSON3개,전체/누락 XLSX 공유 풀6행과 기여/출처의 미정 revision,PDF총3쪽의 한글/음수/미정/이유를 독립 판독했다. `independent.json`.
- PDF 전체1쪽·누락2쪽을 Poppler로 렌더링해 표·제목·페이지 경계·미정/하한·입력 출처가 보이는지 확인했다. XLSX/PDF는 알 수 없는 값에 미정을 쓰며 JSON은null이다. 닫힌 파일의 revision과 계산 방식도 미정이다.
- 변경 C++10파일 clang-format,tracked whitespace,Python AST와 기능 문서의 로컬 링크를 확인했다. `sha256.json`은 보존 산출물과 소스의 해시다. `PPM.exe`도 갱신 빌드했다.

재실행:

```powershell
cmake --build build-numbered-summary --parallel 8 --target JOBS jobs_shared_availability_tests jobs_remaining_availability_tests jobs_shared_resource_tests jobs_workspace_file_tests jobs_workspace_ui_tests jobs_resource_workspace_ui_tests jobs_integrated_workspace_tests jobs_resource_availability_tests jobs_xml_unit_tests jobs_report_tests jobs_report_ui_tests jobs_project_xml_tests jobs_project_xml_ui_tests jobs_xml_formula_tests jobs_undo_tests jobs_recovery_tests jobs_recovery_ui_tests jobs_parser_seed_tests jobs_xer_batch_tests jobs_xer_batch_ui_tests jobs_leveling_float_tests jobs_repartition_float_tests jobs_cost_day_tests jobs_usage_tests jobs_resource_tests jobs_resource_ui_tests
ctest --test-dir build-numbered-summary --output-on-failure --parallel 2 -R '^(shared_availability_|remaining_availability_|shared_resource_intervals$|workspace_file_contract$|multi_project_workspace_ui$|resource_workspace_ui$|integrated_workspace_contract$|resource_availability_|xml_unit_fields_|reports_|project_xml_|xml_formula_|undo_contract$|recovery_|parser_seed_replay$|xer_batch_|leveling_float_contract$|repartition_float_contract$|cost_day_boundaries$|timephased_usage_contract$|resource_contract_file$|resource_ui$)'
build/validation-python/Scripts/python.exe scripts/verify-shared-availability.py build-numbered-summary/shared-availability-qa
```

시각은 독립 프로젝트의 현재/계획 일정에 대한 조회다. 교차 링크를 적용한 통합 일정의 일괄 채택·공유 재평준화·공유 부분 가용기간·실제 타임시트·외부 MS Project/P6 제품 실행은 이 증분의 범위가 아니다.
