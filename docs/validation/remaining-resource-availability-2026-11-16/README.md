# 현재 부하·잔여 가용량 검증

2026-11-16 Windows/Qt6.8.3 MinGW Release, `build-numbered-summary`. [기능 계약](../../remaining-resource-availability.md).

- 관련 회귀 **37/37**,79.23초. 새 조회/계산식·Units/가용기간·부하/평준화/재분배 여유·XML 기준선 교환/병합/상세 분포·잔여 참조/roll-up/down·보고/원가/Usage·통화/ID·일정 채택·undo/복구·파서를 포함한 영향 범위 부분집합이다. 집중6/6도 먼저 통과했다.
- 파서 **1,022개**,schema1~80. 새 활성 함수 정상 파일·잘못된 인수 수·과거79 이하 거절·절단과 기존 전체 seed. `remaining-availability-final-detail.log`에 보존했다.
- 공급100/50%,동시 수요125/75%,잔여−75/−25/25/100%,0이상 배정 가능/부족을 대조했다. 배정 종료 제외·점심/퇴근 시간·자원 지정 근무 달력과 휴일·가용 경계/날짜 예외·알려진 수요/미설정 공급·미배정 자원을 확인했다.
- 현재 계획/제공된 평준화 결과, 혼합 native/XML 식과 문맥 복원 캐시, 배정 자신의 수요 포함·작업 범위 Null, 일정 문맥 없음·오래된/부분 결과·잘못된 시각·취소를 확인했다. 예측은 완료 작업을 제외하고 상태일 이전 수요를 미설정으로 보존한다.
- schema80 exact JSON·79 거절·비활성 원문의 최소 writer44, 저장·자원 편집 undo/redo·복구 checkpoint, XML 재가져오기 후 수요/잔여 재계산을 확인했다.
- UI: 현재 기준09:00의 잔여−25%,10:30 재조회25%,기본 공급 변경 후125%,입력 변경 시 이전 표 무효화,잘못된 날짜의 빈 결과와 설명을 확인했다. `review.png`를 눈으로 확인하고 작은 화면에서 잘리는 헤더를 짧게 정리했다. 비동기 UI는 계산 중/취소·세션/revision 검사를 갖는다.
- 독립 Python civil-minute/Decimal oracle은 **1,440분 지점 × 공급/수요/잔여/배정 가능/부족 = 7,200값**을 대조했다. XML 자원 값 **12개**(미설정 포함),XER 정적 값 **9개**,XLSX 자원 명세 **3행**과 PDF **15쪽**의 함수/음수/미설정 표시를 확인했다. `independent.json`.
- WBS가 A 작업만 표시해도 다른 WBS의 B 작업 수요를 차감해125%를 유지한다. 보고서 기준 시각09:00의 잔여−25%와 사용자 필드가 명시한09:30의 잔여−75%를 구분했다.
- 변경 C++16파일 clang-format·tracked whitespace·Python AST와 보존 산출물/소스 해시를 확인했다. `sha256.json`에 기록한다.

재실행:

```powershell
ctest --test-dir build-numbered-summary --output-on-failure --parallel 2 -R '^(remaining_availability_|resource_availability_|xml_unit_fields_|xml_formula_|xml_remaining_|xml_baseline_(exchange_|timephased|merge_)|xml_roll_(up|down)_|resource_contract_file$|resource_ui$|project_xml_|reports_|project_currency_conversion_contract$|ledger_identity_migration_contract$|xer_schedule_adoption_contract$|undo_contract$|recovery_|parser_seed_replay$|leveling_float_contract$|repartition_float_contract$|cost_day_boundaries$|timephased_usage_contract$)'
build/validation-python/Scripts/python.exe scripts/verify-remaining-availability.py docs/validation/remaining-resource-availability-2026-11-16
```

새 함수는 PPM 확장이다. XML/XER 계획 출력은 Plan 평가 문맥이며 활성 예측은 native의 상태일/현재 일정 문맥을 따른다. 실제 MS Project/P6 실행이나 다른 프로젝트 파일의 공유 부하, 실제 출퇴근/타임시트 수요는 이 기록의 검증 범위에 포함하지 않는다.
