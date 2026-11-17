# 기능범위와 추적표

2026-11-17 후속: GAP-11/12의 [SMAT-01~07](../shared-material-actuals.md) 공유 통합 실제 원장·잔여 재고 투영/자동 대기·근거 출력 지원을 완료로 반영한다. 프로젝트별 자재를 보존하며 여러 파일 일괄 날짜 채택·자재 제약 여유는 남는다.

2026-11-17 후속: GAP-11의 [MAT-01~08](../actual-materials.md) 실제 자재 원장과 단일 문서 잔여 재고 자동 대기를 완료로 반영한다. 공유 통합 잔여 원장 투영·자재 제약 여유·구매/비용 최적화는 별도 범위다.

2026-11-17 후속: [MAD-01~07](../material-arrival-leveling.md)을 GAP-11/12와 연결한다. `material_delay_contract/ui`와 기존 재고/자원 계획/공유/채택/원가/undo·복구/파서,독립 직렬 근무분 전수 후보·실제 보고서 판독으로 추적한다.

2026-11-17 후속: [MINV-01~07](../material-inventory.md)을 GAP-11·PRD 자재/자원 계획에 연결한다. `material_inventory_contract/ui`와 자원 계획/공유 통합/원가/승인·시나리오/통화·ID/undo·복구/파서 회귀,독립 정수/civil-minute/JSON/XLSX/PDF 대조로 추적한다.

2026-11-16 후속: [SPA-01~06 공유 풀 가용기간](../shared-resource-availability.md)을 MULTI-01~06·GAP-11/12에 연결한다. `shared_periods_contract/ui`와 공유/통합/작업공간/파서 회귀,독립3일 civil-minute·일별 적분·schema4/checksum·JSON/XLSX/PDF로 추적한다.

2026-11-16 후속: [SRA-01~06 공유 잔여 가용량](../shared-remaining-availability.md)을 MULTI-01~06·GAP-11/12와 연결한다. `shared_availability_contract/ui`, 작업공간/공유 부하/로컬 잔여/보고 회귀와 독립 civil-minute/JSON/XLSX/PDF 검사로 추적한다.

2026-11-16 후속: [RRA-01~06 잔여 가용량](../remaining-resource-availability.md)을 GAP-03/04/09/11·PRD-01/03/05/07/08에 연결한다. remaining_availability_contract/ui·Units/가용기간/보고/평준화/파일/파서와 독립 civil-minute/XML/XER/XLSX/PDF 대조로 추적한다.

2026-11-16 후속: [RAV-01~06 가용기간](../resource-availability.md)을 GAP-03/04/09/11과 PRD-01/03/05/07/08에 연결한다. resource_availability_contract/ui·기존 Units/자원/평준화·파서와 독립 civil-minute XML/보고/MPXJ 대조로 추적한다.

2026-11-15 후속: [XUF-01~06 Units·가용률](../xml-unit-fields.md)을 GAP-03/04/09/11·PRD-01/03/05/07/08에 연결한다. `xml_unit_fields_contract/ui`·기존 XML/요약·파서와 독립 Decimal/XML/XER/보고서/MPXJ 검증으로 추적한다.

2026-11-15 후속: [NBT-01~06 native 상세 시간분포](../native-baseline-timephased.md)를 GAP-03/04/09와 PRD-01/03/07/08에 연결한다. `xml_baseline_timephased_contract`·기존 XML/병합·schema77 파서 및 독립 XML/Decimal/MPXJ 상세 대조로 추적한다.

2026-11-15 후속: [XBM-01~06 기존 문서 기준선 병합](../xml-baseline-merge.md)을 GAP-03/09와 PRD-01/03/07/08에 연결한다. `xml_baseline_merge_contract`/`xml_baseline_merge_ui`·파서 schema76과 독립 XML/JSON/보고서 대조를 검사한다. 이전 XBE 증분의 기존 문서 병합 제외는 이 명시 연결/교체 지원 부분으로 대체한다.

2026-11-15 후속: 사용자 요청 [XBE-01~07](../xml-baseline-exchange.md)을 GAP-03/09와 PRD-01/03/07/08에 연결한다. 외부 기준선의 명시 채택·표준 출력과 수식/파일/복구/통화/ID 검증을 범위로 하며 상용 제품의 실행 동등성·기존 문서 병합까지 완료로 확대하지 않는다.

2026-11-12 XMLR-01~05 → GAP-03/04/09: [잔여 기간·공수 참조](../xml-remaining-references.md), `xml_remaining_contract`/`xml_remaining_ui`, `scripts/verify-xml-remaining-references.py`에 연결한다. 현재 예측과 계획 내보내기 문맥 분리·schema49/복구·분 단위 독립 적분과 XML/XER·XLSX/PDF를 검사한다.

2026-11-12 XMLQ-01~05 → GAP-03/04/09: [XML 실제 기간·보고 공수 참조](../xml-actual-quantities.md), `xml_quantity_contract`/`xml_quantity_ui`, `scripts/verify-xml-actual-quantities.py`에 연결한다. 현재 상태일의 보고된 값·정정·범위별 합계·schema48/원장 보존·편집/출력을 검증한다.

2026-11-11 후속: [XMLF-01~05](../xml-external-formulas.md)의 지원식 실행·형식별 결과·바인딩·교환/보존·UI/출력을 연결한다. 계산된 일정/근무 달력·현재 시각 등 지원 외 함수와 실제 제품 동등성은 후속이다.

2026-11-11 후속: [RUP-01~05](../xml-roll-up.md)의 8개 요약 방식, Task/Resource 계층·원본/파생 분리·schema43·UI/출력과 독립 판독을 연결했다. 외부 식 실행·실제 제품 왕복은 잔여 범위다.

2026-11-11 RDOWN-01~05 → GAP-03/09: [XML 배정 roll-down](../xml-roll-down.md), `xml_roll_down_contract`/`xml_roll_down_ui`, `scripts/verify-xml-roll-down.py`로 AutoRollDown/SecondaryPID·직접 입력 우선·XML 왕복·전체 사본과 화면/보고서를 검증한다.

2026-11-11 후속: [XMASK-01~05](../xml-code-masks.md)의 Masks/AllLevelsRequired·공유 코드 생성·WBSMasks와 schema41/편집·복구를 추가했다. roll-down/외부 식 실행·제품 왕복은 잔여 범위다.

2026-11-10 XDATE-01~05 → GAP-03/09: [XML 날짜·시간 계약](../xml-date-time-fields.md), `xml_date_time_contract`/`xml_date_time_ui`, `scripts/verify-xml-date-times.py`에 연결한다.

2026-11-10 XTYPE-01~05 → GAP-03/09: [XML 추가 형식](../xml-additional-field-types.md), `xml_field_types_contract`/`xml_field_types_ui`, `scripts/verify-xml-field-types.py`에 연결한다.

2026-11-10 LOOKUP-01~05 → GAP-03/09, PRD-01/06/07/08: [값 목록·GUID 참조 계약](../xml-lookup-references.md), `xml_lookup_contract`/`xml_lookup_ui`, `scripts/verify-xml-lookups.py`에 연결한다.

2026-11-06 WCI-01~05는 GAP-04/11·PRD-03/05/06/07/08·J-03/05/06/08/09/14/16·단계 C/D에 연결한다. [공수·단위 곡선 역산](../work-contour-inverse.md)의 실제 공수/기간·비단조 후보·배치·동결·schema24/복구/출력을 [검증](../validation/work-contour-inverse-2026-11-06/README.md)한다. [잔여 재점검](19-work-contour-remaining-review.md)의 외부 제품/교환/운영 항목은 유지한다.

2026-11-06 후속 DFL-01~04·UDF-01~05는 GAP-03/04/06/09/11·PRD-01/03/05/06/07/08·J-01/03/05/06/08/14/15/16·단계 B/C/D에 연결한다. [날짜 고정 자원 여유](../dated-resource-float.md)와 [외부 필드 매핑](../external-udf-mapping.md)의 후보/증인·다중 프로젝트 참조·독립 reader·원본 보존을 [검증](../validation/dated-float-udf-2026-11-06/README.md)한다. 외부 원본337행/39개 확인과제를 이 구현만으로 닫지 않는다.

2026-11-06 [RV-01~04](../resource-views.md)·[DRW-01~05](../dated-remaining-work.md)·[FF-01~05](../formula-fields.md)는 GAP-03/04/11, PRD-01/02/05/06/07/08, J-01/02/05/08/09/14/16과 단계 B/C/D에 연결한다. 저장된 자원 보기, 고정 기간 날짜별 잔여 목표, 숫자 계산식의 단위·의존성·읽기 전용 결과와 native21~23/보고/복구를 검증한다. [증거](../validation/professional-fields-2026-11-06/README.md). 아래 과거 증분의 후속 표기는 당시 스냅샷이며 외부 UDF 매핑과 외부 원본 확인과제는 계속 남는다.

2026-11-05 PW-01~04 [계획 공수 변환](../planned-work-input.md)은 GAP-04/11·PRD-06/07/08·J-05/09/14/16·단계 C/D에 연결한다. 고정 기간의 달력 교집합·정수 공수·초안/원자 적용·취소/권한·저장/출력 보존을 검증한다. 외부 원본 과제나 고정 공수 역산의 완료로 확대하지 않는다.

2026-11-03 기간 실적 입력/정정은 J-08/J-09/J-10/J-14/J-16, PRD-05/06/07/08에 연결한다. [원장·대사 계약](../period-actuals.md)의 관측 공수/원가·명시 누계 AC·불변 이력·schema 11·취소/복구를 구현한다. 원본 337행/39개 OPEN 확인과제를 이 구현만으로 닫지 않는다.
아래 JOBS ID는 전체 기능 후보의 독립 설계 묶음이며 외부 337행의 대체·요약 완료를 뜻하지 않는다. 초기 기획 이후 일부 native 기능을 구현했으며 완료 범위는 PRD와 기능별 계약·실행 증거로 판정한다. 기능군 전체 또는 상용 제품 동등성의 완료를 뜻하지 않는다.

2026-11-02 GAP-03은 J-01/J-02의 작업 사용자 필드·계층형 코드·그룹/저장된 보기, J-14의 분류 명세, J-16의 이력·복구·파일 안전성과 연결한다. [상세 계약](../task-classification.md)을 따르며 프로젝트/자원 필드·계산식·외부 UDF/코드 매핑은 후속 범위다. 원본 337행의 상태나 39개 확인과제를 이 구현만으로 닫지 않는다.

2026-11-03 GAP-04 조회 증분은 J-05/J-08/J-09/J-10/J-14와 PRD-05/06/07을 연결한다. [시간축 Usage 계약](../timephased-usage.md)의 계획·기준선·잔여 배정 공수/원가와 기간 미배분 실제원가를 다룬다. 실제 기간별 입력·자원 실적 추정·EAC/ETC 대체는 후속이며 원본 확인과제를 해결 처리하지 않는다.

| ID | 기능 후보와 범위 | PRD | 단계 | 검증 증거 |
|---|---|---|---|---|
| J-01 | 프로젝트·템플릿·WBS·작업·마일스톤·코드·사용자 정의 필드 | 01,02 | A,B | 계층 변경·복제·참조 보존 |
| J-02 | 표·간트·네트워크·필터·정렬·그룹·검색 | 02 | B,C | 동일 객체·필터 결과 일치 |
| J-03 | FS/SS/FF/SF·지연/선행·제약·달력·임계경로·여유 | 03 | A,B | 독립 기대값과 계산 계약 |
| J-04 | 다중 프로젝트·외부 링크·공유 달력·분할 작업 | 03 | C | 교차 프로젝트 변경 영향 |
| J-05 | 자원 사전·역할·단위·가용량·배정·과부하 | 06 | C | 기간별 수요/공급 대조 |
| J-06 | 자원 평준화·우선순위·시나리오 비교 | 06 | D | 원본 보존·결정 재현 |
| J-07 | 계획·다중 기준선·승인·변경통제·편차 | 04 | B,C | 기준선 불변성과 이력 |
| J-08 | 상태일·실적·타임시트·잔여량·예측 | 05 | B,C | 실적 보존·예측 재계산 |
| J-09 | 원가·예산·단가·비용계정·현금흐름 | 07 | C,D | 기간별 합계·통화 정책 |
| J-10 | EVM·성과·추정완료원가·보고서 | 07 | D | PV/EV/AC 공식·단위 |
| J-11 | 위험·이슈·확률 일정·시나리오 | 07 | D,E | 분포·시드·가정 보존 |
| J-12 | 포트폴리오·수요·투자·전략·용량 비교 | 07 | E | 지표 정의·집계 중복 방지 |
| J-13 | 협업·검토·워크플로·알림·문서·모바일 | 07 | C,E | 권한·승인·감사 시나리오 |
| J-14 | 대시보드·출력·인쇄·분석·보고 | 07 | B,E | 집계와 원자료 일치 |
| J-15 | 가져오기/내보내기·API·통합·자동화 | 07 | B~F | 형식별 매핑·왕복·손실 보고 |
| J-16 | 관리·보안·권한·감사·백업·복구 | 07 | A~F | 경계·복구·접근 기록 검증 |

## 공식 카탈로그 연결 절차
대상 Library ID: libfile_dba9d14b1df881918fc0eed0e2961da6. 전달된 집계는 Microsoft 159행, Oracle 178행, 합계 337행(중복 포함), 공식출처 164개, 확인과제 39개다. 공식 Library read 전체 본문으로 337행, 39과제와 출처 anchor164개를 확인했다. 원본 bytes 다운로드는 미완료다.
본문에서 추출한 09-source-traceability.json에 모든 행의 원래 ID·제품군·버전·공식 URL·확인수준을 그대로 보존하고 JOBS ID와 다대다로 연결한다. 묶거나 중복 제거하더라도 원본 행을 삭제하지 않는다.
추적 레코드: source_row_id, source_product, source_version, source_url, verified_at, jobs_requirement_id, semantic_difference, dependency, phase, status, acceptance_case_id, evidence_location, open_question_id.
39개 확인과제는 원래 ID로 별도 보존한다. 확인 전에는 추정 기능으로 구현 계약을 확정하지 않는다. 무매핑 행·출처 없는 확정 요구·증거 없는 완료 상태를 검증 오류로 취급한다.

## 동등성 판정
기능별 의미·제약·규모·교환·보안·운영 조건을 따로 판정한다. 경쟁 제품에서 파일을 열 수 있다는 사실만으로 계산 동등성을 주장하지 않는다. 지원하지 않는 차이는 공개된 손실 보고로 기록하며 후보에서 제거하지 않는다.

현재 10-detailed-requirements.md의 REQ-원본ID 337개는 기능군/PRD/단계에 초안 매핑됐다. 입력·출력·예외·수용조건은 제안이며 행별 상세 의미 승인 전이다. 11-open-source-questions.md의 39과제는 OPEN이다. 계산 관련 매핑은 전문 원고 C/T/G/D/CPM/CON/R/P/A/B/AUD/Q 및 F01~F12를 참조한다. Desktop DESK-01~09와 Superiority SUP-01~05는 경쟁 제품 원문 행과 별도 JOBS 제안 요구다.


JOBS 고유 PRD-08/FILE-01~10은 사용자 확정 파일 제품 요구와 설계 계약이며 원본337행과 별도로 추적한다. DESK-01~05·J-01/J-15/J-16 및 단계A/B/C/F와 연결한다. 파일 lifecycle 수용시험 FILE-T01~09는15문서를 따른다.

## 검토 보완: 기능행과 확인과제 다대다 연결
09 JSON 각 proposed_mapping에 open_question_id, question_links(blocking_scope·resolution_evidence_status·mapping_basis), question_mapping_status를 추가했다. 과제에는 역방향 linked_source_row_ids와 mapping_status·resolution_evidence_status를 둔다. 연결은 같은 제품·주제에 따른 제안이며 해결 근거가 아니다. 실제 의미 연결이 확인되지 않은 개별 필드/추가 관계는 후속 검토 대상이다. 어떤 연결도 없으면 UNMAPPED로 남기고 '과제 없음/차단 없음'으로 해석하지 않는다. 모든39과제는 OPEN, 해결증거는 NOT_PROVIDED다.
제품/버전/라이선스 공통 과제는 해당 제품 전행의 Parity 판정을 차단하지만 독립 native 설계 전체를 차단하지 않는다. 세부 계산·교환·운영 과제는 question_links의 차단 범위를 따른다.

2026-10-29 사용자 확정 MSIX-01~06은 J-16/PRD-08의 배포·파일 안전성과 연결하는 JOBS 고유 요구다. 기존 PACK-01~04를 유지하며 [MSIX 계약](../msix-store.md), [제출 절차](../store/submission-checklist.md), [실제 검증](../validation/msix-2026-10-29/README.md)으로 추적한다. Store 등록·WACK·최종 identity 제출/승인은 별도 미완료 항목으로 보존한다.
REQ-ORA-098은 J-03+J-11, PRD-03+PRD-07로 연결한다. B는 확정 비근무 예외·중복 처리의 결정론적 달력 기반일 뿐이다. 확률 날씨·분포·상관·seed·완료일 영향은 D-E에서 별도 검증한다. B 완료를 확률 기능 완료로 간주하지 않는다.

2026-11-03 GAP-06/J-03·06·07·14의 native 채택 증분은 [평준화 채택 계약](../leveling-adoption.md)과 [검증 기록](../validation/leveling-adoption-2026-11-03/README.md)에 연결한다. 자원 CPM·불변 채택 결정 원장·MSP/P6 왕복은 완료 판정에 포함하지 않는다.

2026-11-03 GAP-07 / J-02·J-14는 [일정표·간트 인쇄 계약](../schedule-printing.md)의 native 페이지 설정·미리보기·PDF/프린터 공통 배치로 연결한다. 실제 프린터·macOS 수용은 별개이며 [로컬 검증](../validation/schedule-printing-2026-11-03/README.md)에 실행 결과를 남긴다.

2026-11-03 GAP-08 / J-02·J-03은 [네트워크 계약](../network-diagram.md)의 native 관계/임계/driving 탐색·안정 ID 선택 연동·전체 계산과 제한 표시 분리로 연결한다. 구조 순환과 계산 실패를 구분하고 취소/revision/session을 검사한다. 관계 편집·네트워크 인쇄와 외부 제품 동등성은 후속이다.

GAP-08의 [로컬 77개 회귀·수치/화면·1천/1만 측정 증거](../validation/network-diagram-2026-11-03/README.md)를 연결한다.

2026-11-03 GAP-09 / J-04·J-15의 첫 증분은 [다중 XER 프로젝트 선택](../xer-project-selection.md)이다. 단일 독립 문서로 투영하고 다른 프로젝트와의 링크/병합은 지원하지 않는다. 원본 전체 보존·선택/제외 행·전체 파일 한도와 외부 관계 차단을 검증한다.

XER-SELECT-01~04의 [로컬 79개 회귀·9종 독립 검사·UI/원본/규모 증거](../validation/xer-project-selection-2026-11-03/README.md)를 연결한다.

GAP-10 / J-03·J-08의 첫 증분은 [native 진척 정책 A/B 비교](../progress-policy-comparison.md)이며 원본 미채택/읽기 전용이다. PROGRESS-POLICY-01~04로 수신 관계·실제 날짜·전체 계획 검증·JSON/세션을 추적한다. [GAP-05 실제 P6](../p6-roundtrip.md)는 미설치로 미실행이다.

PROGRESS-POLICY-01~04는 [81개 로컬 회귀·독립 검사·수기/화면·P6 미실행 기록](../validation/progress-policy-2026-11-03/README.md)에 연결한다.

PROGRESS-POLICY-05: A/B 정책 채택·교체·해제와 계산 반영. PROGRESS-POLICY-06: 담당자/사유/UTC/검토 revision, schema13·저장/재열기/복구·undo/redo. PROGRESS-POLICY-07: 원본 계획·실적·기준선 보존, 잔여 Usage/표/간트/네트워크/보고/인쇄와 교환 손실 표시. PROGRESS-POLICY-08: 비동기 취소·stale/session/SaveAs/읽기 전용·평준화 조합. 모두 GAP-10 / J-03·J-08 및 [채택 계약](../progress-policy-adoption.md)에 연결한다. 원본 39개 확인과제와 실제 P6 검증은 이 구현만으로 닫지 않는다.

PROGRESS-POLICY-05~08의 [최종 로컬 실행·출력·소스 해시·회귀 83개](../validation/progress-policy-adoption-2026-11-03/README.md)를 보존한다.

PROGRESS-LEVELING-01: 네 native 엔진에서 선택 정책의 관계로 자원 배치, 원본 관계/실제일 보존. 후속 LEVEL-FLOAT에서 자원 순서 여유·임계를 계산한다. PROGRESS-LEVELING-02: 조합 채택/해제·정책 변경의 원자적 이력·stale/cancel/읽기 전용. PROGRESS-LEVELING-03: schema14 조합/시나리오 정책·구버전 거절·재열기/SaveAs/복구·정책 동결 재생. PROGRESS-LEVELING-04: 표/간트/Usage/원가/보고/인쇄와 출처의 일치. GAP-06/10 및 J-03·J-08·J-10에 연결한다. [계약](../progress-policy-leveling.md).

PROGRESS-LEVELING-01~04는 [85개 회귀·schema14 파서·독립 출력·화면 및 소스 해시](../validation/progress-leveling-2026-11-03/README.md)에 연결한다.

LEVEL-FLOAT-01: 슬롯 흐름·가용량 감소·논리/제약·달력 후진 계산. LEVEL-FLOAT-02: 계획/잔여·A/B 조합·완료/실제일 보존과 미산정 경계. LEVEL-FLOAT-03: 자원 driving 관계·표/간트/네트워크/보고/인쇄. LEVEL-FLOAT-04: 기존1.0/1.1·신규1.2 승인 기준선 재현·저장/재열기·Undo/취소. GAP-01/02/06/10 및 J-03·J-08·J-10과 [계약](../leveling-float.md)에 연결한다.

LEVEL-FLOAT-01~04의 [최종 로컬 검증·독립 판독·화면·소스 해시](../validation/leveling-float-2026-11-03/README.md)를 보존한다.

LEVEL-FLOAT-05: 교차 분할 실행 창·자원 재사용·기간/공수 재적분·단조 시간 상한과 비단조 허용 시작 구분. LEVEL-FLOAT-06: 다중 자원/용량·진척 정책·실제일 보존·최후 예약/수요 검증. LEVEL-FLOAT-07: 간트/네트워크의 구간 연결과 보고서 인계 시각·독립 판독. LEVEL-FLOAT-08: 신규1.2·기존1.0/1.1 기준선과 native 저장/재열기·Undo/취소. [계약](../leveling-float.md), [검증](../validation/segment-leveling-float-2026-11-03/README.md).

LEVEL-FLOAT-09: 구간 소멸/재생성과 비교차 인계·교차 최종 반환 순서. LEVEL-FLOAT-10: 실제 합산 수요·달력/유효일 용량·비단조 후보·작업별 총여유 증인/고정 날짜 자유여유. LEVEL-FLOAT-11: 표/간트/네트워크/보고서·인쇄와 독립 분 단위 전수 판독. LEVEL-FLOAT-12: 한도·미정 용량·취소/원자성·저장/Undo·기존1.0~1.2/신규1.3 기준선. [계약](../leveling-float.md), [검증](../validation/repartition-float-2026-11-03/README.md).

LEVEL-FLOAT-13 (2026-11-04): 논리/자원 반환 부등식 후보 제거·단일 후보 고정·반복형 탐색. LEVEL-FLOAT-14: 묶음당 이동 가능128개·고정 작업 포함 큰 묶음·총 탐색/증인 한도와 원자적 미산정. LEVEL-FLOAT-15: 기존1.3의 64개 제한/승인 수치 재현, 새1.4 선택/저장·출력 출처·생략 날짜 증인 독립 재구성. [계약](../leveling-float.md).

WITNESS-01~04 (J-06/J-14, 2026-11-04): worker의 구조화된 불변 증인·A/B별 현재/증인/고정 실적 표·선택/동료 작업 필터·가상 대형 표·문서/조건 변경 시 오래된 창 차단·저장 비영속과 수치 보존. [계약](../leveling-witness.md).

LEVEL-FLOAT-16 (2026-11-04): 이동 가능128개 제한 제거·보수적인 후보 논리/수요 결합 분해·큰 단위 반복 탐색·공통 고정 부하. LEVEL-FLOAT-17: 129/512 결합·1,000개 이동 가능 독립 단위·132개 교차 분할/논리 묶음·잔여 실적·삼중 합산·전체 증인 독립 검산. LEVEL-FLOAT-18: 기존1.4 제한/기준선 재현·새1.5/native12·전체 근거 창·명시적 진단 미리보기/보고서·전체 예산/원자성/취소. [계약](../leveling-float.md).

PROP-01~04 (2026-11-04): 같은 GAP-01/02/06 범위에서 안전한 자원 상한의 생성 전 전파, 지지된 후보 제거 순회 생략, 조건부 이웃 후보 제외·정확한 복구와 제한된 끝점 캐시를 추가한다. 정확히 같은 단위 배치와 독립 단위의 native 전체 부하 인증을 공유한다. 기존1.0~1.5 재현·새1.6/native12·여유 정의/현재 날짜·원장·전체 예산을 보존한다. 동일 입력 교차 반복 관측과 비단조 용량 전수 검산을 [계약](../candidate-propagation.md)·[실행 근거](../validation/candidate-propagation-2026-11-04/README.md)로 연결한다.

NPROP-01~04 (2026-11-04): 같은 GAP-01/02/06 범위에서 실제 후보의 점별 최소 수요 합으로 후보를 축소하고, 분기에서 살아 있는 후보의 끝점 지지와 필수 부하를 고정점까지 연쇄 전파한다. 공백/수요 소멸은0, 제외는 가역 복원하며 현재 날짜·실적·기존1.6까지의 재현·새1.7/native12·전체 예산을 유지한다. 공수/다른 달력/분수 마지막1분·세 작업 합산의 독립 검산과 반복 관측을 [계약](../nonmonotone-propagation.md)·[실행 근거](../validation/nonmonotone-propagation-2026-11-04/README.md)로 연결한다.

COST-01~03 (2026-11-04): 같은 GAP-01/02/06의 추가 전파 적용 기준을 후보 수·구간 수·관계 방문 비용으로 추정하고 실제 실패 누적과 제외 효과로 조절한다. 지연 가역 마스크·정확한 탐색/native 인증·전체 예산과 기존1.7까지의 재현·새1.8/native12를 유지한다. [비용 계약](../propagation-cost.md)·[반복 관측과 독립 검산](../validation/propagation-cost-2026-11-04/README.md)을 따른다.

2026-11-05 LEVEL-DECISION-01~04는 GAP-06, PRD-03/06/08 및 J-03/06/07/14에 연결한다. [결정 계약](../leveling-decisions.md)과 [회귀 증거](../validation/leveling-decisions-2026-11-05/README.md)를 따른다. 설정 복원과 별개로 불변 이력을 추가하며 외부 제품/신원 인증 검증을 포함하지 않는다.

2026-11-05 OBJECT-FIELD-01~04는 GAP-03, PRD-01/06/08 및 J-01/05/14에 연결한다. [계약](../object-custom-fields.md), [로컬 회귀/독립 판독 증거](../validation/object-fields-2026-11-05/README.md). 작업/프로젝트/자원 범위의 ID 충돌을 격리하고 승인 기준선과 저장 시나리오의 사전·값을 동결한다.

2026-11-05 GAP-04/11의 고정 기간 배정률 구간·집중 곡선은 J-05/J-08/J-09/J-10과 PRD-05/06/07에 연결한다. 작업 근무분 구간에서 Usage/원가/순간 부하/평준화를 공통 계산하고 schema17 동결·복구/과거 정책 재현을 검증한다. [기간별 배정 계약](../assignment-periods.md). 고정 공수/단위 역산·기간별 잔여 공수 직접 입력은 후속이다.

2026-11-05 GAP-07/08의 인쇄 설정·WBS 요약/관계선·네트워크 편집/배치/출력은 [확장 계약](../print-network-extension.md)과 [회귀/독립 출력 기록](../validation/print-network-2026-11-05/README.md)으로 연결한다. 표시 정보는 계산 입력 밖에서 저장하며 외부 장치/다른 OS 검증을 완료로 확대하지 않는다.

## 2026-11-05 GAP-13/14 로컬 증분

사용자 후속 개발 요청으로 로컬 EPS/OBS·구성원·프로젝트 역할/문서 편집 통제·통화별 포트폴리오와 추가 전용 협업/운영 감사를 구현한다. 위험은 계획 CPM·삼각 기간 분포/발생 확률·공통 순위 그룹/seed·분위수/임계 빈도/마감 비율/표본 안정성, 자동화는 안정 ID/revision·검증 미리보기·단일 batch/undo·원자 파일 저장 CLI를 제공한다. [계약](../organization-risk-automation.md)의 역할 경계·schema3 호환·취소/stale/원자성·독립 기대값을 수용 기준으로 사용한다. 로컬 프로필을 서버 신원 인증/동시 협업으로 확대하지 않으며 일반 상관/확률 자원·HTTP/스케줄러·운영 배포는 후속이다.

## 2026-11-05 서버 후속: 인증 → 실시간 동기화 → HTTP → 주기 실행

사용자 후속 개발 요청으로 GAP-13/14의 서버 인증·동기화·HTTP API·주기 실행을 선택 서버로 구현했다. AUTH-SRV: scrypt/불투명 세션/만료·철회/현재 서버 권한; SYNC-SRV: 순번 feed/WebSocket·long-poll 재연결/깨끗한 화면 갱신·초안 보존/버전 충돌; API-SRV: native 검증·미리보기/현재 권한 재검사·원자 DB commit·멱등 영수증; AUTO-SRV: 영속 간격/소유자 권한·lease/중단 재개·중복 차단/취소·충돌 정지/실행 원장을 수용 기준으로 사용한다. [계약·사용법](../../server/README.md)과 [검증](../validation/server-collaboration-2026-11-05/README.md)을 따른다. 기존 단일 사용자 파일 흐름은 서버를 요구하지 않는다. 서버의 현재 원격 편집 범위는 native batch의 작업 이름/기간이며, 임의 필드 병합·SSO/MFA·cron·HA·실제 외부 배포는 별도 범위다.
