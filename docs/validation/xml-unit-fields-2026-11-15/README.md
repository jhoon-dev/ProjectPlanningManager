# Units·가용률 참조 실행 검증

2026-11-15 Windows/Qt6.8.3 MinGW Release, `build-numbered-summary`. [기능 계약](../../xml-unit-fields.md).

- 최종 회귀 **61/61**,93.01초: Units·XML 식·기준선/시간분포/병합·요약·실제/잔여·진척·달력·형 변환·계층·원가/Usage·기존 Project XML·통화/ID·일정 채택·undo·파일/복구·준비 편집/평준화.
- 파서 **1,000개**,schema1~78: 새 unit 바인딩/함수 정상 파일, key/scope 변조, 절단·체크섬·77 이하 거절과 기존 모든 seed. `xml-units-final-detail.log`의 parser_seed_replay 출력에 보존한다.
- 새 계약: 비율/소수6자리·별칭·명시 자원·기본 배정/기간 곡선 구별·범위/요약 Null·0/미설정·나눗셈 오류·혼합 식·동일 EvaluationScope 캐시 변경, 명시 Date/Text·잘못된 날짜/시차/숫자·Null 전파를 확인했다.
- XML: CapacityDay0/미설정/125%·다일 범위와 기본값 복귀·수식 재바인딩, 부분일/겹침/자원 AvailableFrom 경계의 부분 적용 방지 및 날짜 조회식 비활성 보존을 확인했다.
- schema78 exact JSON·77 거절·조건부 과거 writer, 비활성 바인딩과 비활성 원문 차이, 승인/시나리오/채택/통화·ID 사본 감지, 편집 undo/redo·복구 checkpoint를 확인했다.
- UI: 참조 삽입 항목·배정4행 미리보기·값1·저장, `review-references.png`/`review.png`를 눈으로 확인했다.
- 독립 Python Decimal/ElementTree: XML 계산값 **48개**,가용기간 **4개**,XER 배정 UDF **28개**,XLSX 배정 값 **28개**와 PDF **18쪽**의 식/반올림/오류 문구를 대조했다. `independent.json`.
- MPXJ16.10.0 strict reader: ignored errors0,배정률 **4건**·가용기간 **4건**(미설정 포함) 대조. 이 reader는 AvailabilityPeriods 밖 기본값을 PPM과 다르게 해석할 수 있어 해당 동등성은 검증하지 않는다.
- 변경 C++11파일 clang-format 검사·tracked whitespace·Python AST를 통과했다. `sha256.json`에 소스14개와 산출물 해시를 보존한다.

초기 회귀는60/61이었다. 과거 `project_xml_contract`의 “Baseline 없음” 기대가 이미 구현된 native 기준선 출력과 충돌했다. 기준선0 출력 및 실제 원장 미출력으로 기대를 수정한 뒤61개 전체를 다시 통과했다. 이번 변경이 기준선 출력을 제거하거나 실적을 자동 채택하지 않는다.

재실행:

```powershell
ctest --test-dir build-numbered-summary --output-on-failure --parallel 2 -R '^(xml_(unit_fields|baseline|summary_duration|remaining|quantity|actual_|early_late|formula_|roll_up|roll_down|progress_|work_progress|calendar_|conversion_|schedule_)|project_xml_|project_currency_conversion_contract$|ledger_identity_migration_contract$|xer_schedule_adoption_contract$|undo_contract$|recovery_|tracking_file$|file_lifecycle$|parser_|prepared_edit|leveling_selection|timephased_usage_contract$|cost_contract_file$|cost_day_boundaries$)'
build/validation-python/Scripts/python.exe scripts/verify-xml-unit-fields.py docs/validation/xml-unit-fields-2026-11-15 --reader
```

실제 MS Project/P6 실행 환경은 없으며 제품의 식 재계산·재저장 왕복은 미검증이다. MPXJ와 정적 수치 대조를 제품 수용 시험으로 취급하지 않는다. 가용기간 밖 native 기본값·부분일/무기한 자료의 제한은 기능 계약과 XML 교환 진단을 따른다.
