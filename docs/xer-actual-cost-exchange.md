# XER 실적·원가 상세 교환

2026-11-07 후속: [집계 편집·날짜별 배분](xer-aggregate-editing.md)은 원본과 현재 문서가 일치할 때 별도 정정 집계·명시 날짜 원장을 제출한다. 아래 XAC의 AC 단일 누계·원장 정정·외부 EVM 분리 규칙을 그대로 사용한다. 정정이 있는 native 문서는 조건부 schema26이다.

2026-11-07 후속: [재무기간·누계 native 스냅샷](xer-financial-snapshots.md)을 추가했다. 아래 원본/변환 보고에만 보존하던 주·월 집계와 날짜 없는 누계는 schema25 스냅샷으로 조회·재내보내기할 수 있다. 현재 원장·native EVM과의 자동 합산/병합은 하지 않는다.

2026-11-06 · XAC-01~06 · GAP-09. 파일 → Primavera XER 가져오기/내보내기의 기존 미리보기·worker·원자 게시 흐름을 확장한다. 계획 투영과 관측 실적을 구분하고, 현재 지원 부분만 새 native 스냅샷에 연결한다. 기존 문서·정정 원장·기준선·사용자 파일은 수정하지 않는다.

## XAC-01 상태일과 작업 실적

`PROJECT.last_recalc_date`를 civil 상태일로 읽고 쓴다. `TASK.status_code`의 TK_NotStart/TK_Active/TK_Complete와 실제 시작·완료, `remain_drtn_hr_cnt`의 일관성을 검사한다. 시작/완료가 상태일 이후이거나 완료 순서·잔여 기간이 맞지 않으면 변환 전체를 거절한다. 진행 중인데 잔여 기간이 없는 과거 계획 파일은 실적을 만들어내지 않고 경로별 진단과 원본 날짜를 남긴다.

가져온 ProgressEntry는 새 원본 스냅샷의 보고자·변환 사유·상태일·UTC 변환 시각·문서/revision을 가진다. 실제 소요 기간은 실제 시작부터 완료 또는 상태일까지 **작업 달력의 근무분**으로 계산한 날짜 기반 투영이다. 관측 공수를 실제 소요 기간으로 바꾸지 않는다. native에서 별도 계측한 실제 소요 기간과 외부 달력 계산이 다를 수 있으므로 내보내기 미리보기에도 손실을 알린다. 실제 날짜가 프로젝트 시작 이전이면 같은 달력을 앞쪽에 고정해 계산한다.

`complete_pct_type=CP_Phys`인 `phys_complete_pct`만 0.01% 정밀도의 물리 진척으로 읽는다. 기간/공수 진척을 물리 진척으로 추정하지 않는다. 외부의 BCWP·BCWS와 승인 기준선·실적 정정 이력은 native 기준선이나 EVM으로 만들지 않는다. native EV/BAC/PV에는 기존 검증된 native 기준선이 필요하다.

## XAC-02 누계 원가와 단가

- TASKRSRC의 act_reg_cost + act_ot_cost와 PROJCOST.act_cost를 작업별 누계 AC로 합산한다. TASKFIN/TRSRCFIN은 그 안의 배분 내역이며 다시 더하지 않는다. 0은 보고된 0이고 빈 값은 미보고다.
- RSRCRATE.cost_per_qty의 자정 유효 단가를 날짜별 native 시간 단가로 가져오고 내보낸다. RSRC.cost_qty_type은 공란 또는 QT_Hour만 지원한다. 같은 가용량을 반복한 여러 단가 행은 가용량을 미정으로 바꾸지 않는다. 서로 다른 가용량의 날짜 곡선은 기존 미지원 진단을 유지한다. 일중 단가 변경·시간 이외 단가·외화 단가의 자동 환산은 거절한다. 추가 요율/초과근무 승수·배정별 override는 미지원 필드 진단과 원본에 남긴다.
- PROJCOST.target_cost는 작업 직접 고정비의 합계로 투영한다. 균등/시작/종료 발생은 같은 규칙끼리 보존하고, 같은 작업에 서로 다른 발생 규칙이나 미지 규칙이 있으면 진단 후 균등 고정비로 투영한다. 항목 이름·ID·원래 발생 규칙·잔여 원가는 변환 보고와 original.xer에 보존한다. TASKRSRC의 계획/잔여 비용·수량도 상세 보고에 보존하며 native 잔여 원가를 외부 숫자로 덮어쓰지 않는다.
- 실제원가는 관측 금액 그대로 기록한다. 단가×공수로 재평가하지 않는다. KRW/JPY는 정수, USD/EUR는 소수 2자리의 통화 최소단위를 엄격하게 검사하며 환산하지 않는다. 음수·지수·NaN·범위/최소단위 초과는 거절한다.

## XAC-03 날짜가 있는 실적만 일별 배분

FINDATES와 TRSRCFIN(act_qty/act_cost), TASKFIN.act_expense_cost를 연결한다. 한 civil 날짜 안의 재무기간만 기존 날짜·작업·자원별 PeriodActualEntry로 변환한다. 자원 종류·통화·같은 제출의 누계 CostEntry 연결을 기록한다. 날짜는 상태일 날짜 이하여야 하며, 기간 참조/소유/중복·시작/종료·합계 범위를 검사한다. 자원 공수는 시간×6000의 정수 %·분 단위이며 XER 소수 6자리 직렬화의 최대 반 단위 오차만 허용한다.

주/월 등 여러 날짜의 기간 합계는 날짜를 추정하거나 균등 배분하지 않는다. 구간·값은 original.xer 및 conversion.json.actualCosts.financialPeriods에 남긴다. TASKRSRC의 정규/초과근무 누적 공수도 assignmentTotals에 남기며, 알려진 날짜별 공수와 차액을 임의 날짜에 기록하지 않는다. 자원 누적 공수·원가를 초과한 기간 명세나 누적 직접비를 초과한 TASKFIN 경비는 전체 변환 오류다.

TASKFIN의 자원 비용은 TRSRCFIN의 요약이므로 원장을 하나 더 만들지 않는다. 불일치는 보고에 표시한다. 비용계정/자재·역할·원가의 날짜별 분류, 초과근무 별도 native 구분, 여러 날짜 집계의 native 보존/편집은 후속 범위다. 동반 원본과 보고를 함께 전달해야 이 정보가 유지된다.

## XAC-04 내보내기와 원가 대사

현재 상태일에 유효한 최신 정정만 투영한다. 배정별 관측 공수·원가 합계를 TASKRSRC에 쓰고, 날짜별 내역은 일 단위 FINDATES/TRSRCFIN에 쓴다. TASKFIN에는 자원 요약과 직접비만 쓴다. native 누계 AC에서 자원 명세를 뺀 나머지는 PROJCOST의 직접/미배분 비용으로 내보낸다. 따라서 **자원 명세 + 직접/미배분 비용 = 누계 AC**이며 날짜별 배분은 그 안에 포함된다.

실적이 있는 작업의 배정별 잔여 공수는 완료의 0만 확정하고 나머지는 공란으로 쓴다. 작업 잔여 기간은 TASK에 별도로 쓰며 이를 관측 자원 공수로 바꾸지 않는다.

비용 배분은 있으나 명시 누계 AC가 없는 native 문서는 먼저 누계를 제출해야 내보낼 수 있다. 공수만 보고한 문서에서는 AC를 생성하지 않는다. 현재 배정이 해제된 과거 자원 명세는 계획 배정을 새로 만들지 않고 source.ppm과 작업 미배분 원가에 보존하며 손실을 표시한다. 정정 원장의 모든 버전·미래 보고·native 기준선/시나리오·원래 비용 발생 규칙은 source.ppm에 정확히 보존한다.

## XAC-05 선택·일괄·원자성·한도

프로젝트 선택 전에 모든 PROJCOST/TASKFIN/TRSRCFIN의 작업·프로젝트·배정·재무기간 참조와 중복을 검증한다. 선택하지 않은 프로젝트의 잘못된 참조도 선택으로 우회하지 못한다. 선택 후에는 해당 명세와 사용한 FINDATES만 남긴다. 다중 내보내기는 비용 항목·재무기간·배정과 모든 참조를 프로젝트별로 재배정한다.

파일 16 MiB·전체 셀 1,000,000개 등 기존 파서 한도를 유지한다. 추가 테이블은 PROJCOST/TASKFIN/TRSRCFIN 각각 100,000행, FINDATES 10,000행, native 기간 원장 50,000건 한도를 따른다. 취소 checkpoint·새 폴더 원자 게시·기존 목적지 보호와 원본 바이트/source.ppm·변환 보고를 유지한다. 새 native 저장 필드는 추가하지 않는다. 날짜별 실적은 기존 조건부 schema11 이상·복구 계약을 그대로 쓴다.

## XAC-06 검증과 외부 경계

수작업 입력 tests/fixtures/primavera-actual-cost.xer, xer_actual_cost_contract 및 기존 XER UI/일괄/파서·전체 회귀를 사용한다. 누계 AC 122.80 USD = 자원 110.46 + 경비 12.34, 날짜 배분 105.80 및 미배분 17.00을 독립 기대값으로 대조한다. 날짜별 공수는 3.25h이고 원본 누계 3.75h의 차액은 날짜를 만들지 않는다. 저장/복구·왕복·두 프로젝트 결합/선택·잘못된 참조·음수/0/미설정/정밀도·취소와 미리보기를 확인한다.

scripts/verify-xer-actual-costs.py는 표준 TSV/Decimal·native SHA256와 고정 MPXJ16.10.0을 사용한다. 독립 reader 성공은 실제 Oracle P6 설치본의 수용·재계산·재저장·동등성을 뜻하지 않는다. 실제 P6 검증과 MPP reader는 후속 범위다. [실행 증거](validation/xer-actual-cost-2026-11-06/README.md).

필드 근거: Oracle [TASK](https://docs.oracle.com/cd/F88968_01/English/Mapping_and_Schema/xer_import_export_data_map_project/97906.htm), [PROJCOST](https://docs.oracle.com/cd/F25600_01/English/Mapping_and_Schema/xer_import_export_data_map_project/97884.htm), [FINDATES](https://docs.oracle.com/cd/F88966_01/English/Mapping_and_Schema/xer_import_export_data_map_project/97876.htm), [프로젝트 XER 공식 매핑표의 TASKRSRC/TASKFIN/TRSRCFIN](https://docs.oracle.com/cd/E17784_01/Technical_Documentation/Data_Mapping_Docs/XERprojects.pdf). 상태일 해석은 기존 검증용 MPXJ의 PROJECT.last_recalc_date → StatusDate 매핑도 대조했다.
