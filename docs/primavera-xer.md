# Primavera XER 교환 계약

2026-11-11 후속: [XML 외부 계산식](xml-external-formulas.md)은 결과를 XER에 정적으로 투영한다. 식/실행 설정/바인딩은 source.ppm에 남기고 날짜·시간 결과는 텍스트로 보존한다.

2026-11-11 후속: [XML 요약 roll-up](xml-roll-up.md)의 규칙·WBS/루트 명시 요약 값은 XER에서 실행하지 않는다. source.ppm에 전체 정의/값을 보존하고 손실 진단을 기록한다.

2026-11-10 [배정 UDF](enterprise-assignment-udf.md)의 TASKRSRC 스칼라·프로젝트/배정 소유 검증과 결합 ID 재배정을 추가했다. [전체 회귀·독립 판독](validation/enterprise-assignment-udf-2026-11-10/README.md)을 따른다.

2026-11-09: [자원 생산성·외부 고유 곡선 교환](resource-productivity-curves.md)으로 RSRCCURVDATA21값/TASKRSRC.curv_id와 자원별 총공수의 수입/내보내기를 확장했다. 원본 비중은 백분율 소수점6자리로 보존하며 변경된 구간의 집계/보간 차이를 진단한다. 결합 파일의 곡선 ID도 재매핑한다.

2026-11-08 [관측 원장 ID 이관](ledger-id-migration.md)은 현재 작업/자원의 명시 새 ID로 native 원장·기준선/마감·XER 재무 참조를 연결한다. raw 재연결 원본과 과거 일정 채택은 보존하고 이후 교환은 새 ID를 사용한다. 전후 원본은 schema33에 보존하며 교환 보고에 `ledgerIdMigrationCount`를 기록한다.

2026-11-08 [후속 XER 일정·WBS·달력 채택](xer-schedule-adoption.md)은 재무 원본 재연결과 별도의 명시 계획 채택 흐름이다. 선택한 작업의 일정·소속·달력과 원본 기본 달력/휴일·시작일을 채택하고 로컬 작업·자원/배정·원장·기준선은 보존한다. 원본과 연결표·전후 입력은 schema32에 보존하며 교환 보고에 `scheduleAdoptionCount`를 기록한다.

2026-11-08 [후속 원본 통화·자원 종류 전환](xer-source-conversion.md)은 새 원본·이전 집계별 명시 환율/공수비·전환 기준일/근거와 전후 미리보기·불변 schema31 이력을 지원한다. 전환 없는 통화/종류 불일치는 계속 거절한다. 최신 병합 집계를 현재 통화/종류로 재내보내고 교환 보고의 `sourceConversionCount`에 전환 revision 수를 기록한다. 최초/후속 원본과 현재 계획·관측 원장은 보존한다.

[사용자 기간·기간 마감](custom-financial-periods.md)은 native source.ppm에 보존하며 XER에 자동 매핑하지 않는다. native 원장을 생성하는 XER 편집/배분에도 활성 마감 날짜 잠금을 적용한다. 기존 원본/현재성 투영 지문은 사용자 기간 때문에 바꾸지 않는다.

2026-11-08 [외부 EV/PV native 채택](external-xer-evm-adoption.md)은 별도 원가 메뉴에서 기준선·개시 누계를 확인한 불변 이력으로 제출한다. 원본 TASKFIN 금액이나 AC·물리 진척을 바꾸지 않는다. XER의 native 채택 정책/철회는 표현하지 않으며 source.ppm에 schema29로 보존한다. 변환 보고의 `nativeEvmAdoptionCount`는 채택·철회 이력 건수다. 명시 채택 자체는 기존 실적·원가 재내보내기의 투영 지문을 바꾸지 않는다.

2026-11-08 [새 XER 버전·삭제 ID 재연결](xer-source-relink.md)은 현재 문서에 후속 파일/프로젝트를 선택하고 새 원본·이전 집계 ID를 현재 작업/자원에 명시 연결한다. 삭제/재생성 ID와 관측 없는 사전 항목 제외를 검증하고 기존 병합 화면에서 제출한다. 최초 원본·각 원본의 typed 재무/ID 사전·SHA256와 연결 이력은 schema28/source.ppm에 보존하며, 원본 XER 전체 바이트는 선택 파일을 별도로 보관한다. 현재 계획·원장·AC는 유지하며 일치하는 최신 집계를 재내보낸다. 보고서의 `sourceRelinkCount`로 등록 건수를 확인한다.

2026-11-07 [원본·현재 원장 병합](xer-ledger-merge.md)은 현재 AC와 원장을 유지하며 비겹침 명세를 원본 집계에 연결한다. 모호한 기간 교체·누계 구분은 명시 선택하고 일치하는 최신 병합 집계를 재내보낸다. 독립 외부 EV/PV 구간과 날짜 경비를 구분하며 같은 TASKFIN 기본 키는 한 행으로 합친다. 보고서의 `financialMergeCount`와 source.ppm의 전체 이력을 함께 확인한다.

2026-11-07 후속: [집계 편집·날짜별 배분](xer-aggregate-editing.md)으로 원본을 보존한 정정 집계와 native 날짜 원장을 제출한다. XER에는 문서와 일치하는 최신 정정 집계·수정 기간을 쓰고 날짜 배분/전체 이력은 source.ppm에 보존한다. 집계와 날짜 원가를 중복 출력하거나 native AC에 합산하지 않는다. 원본 및 외부 EV/PV 경계를 유지하며 실제 P6 실행 동등성을 주장하지 않는다.

2026-11-07 후속: [XER 보존 재무기간·누계](xer-financial-snapshots.md)는 여러 날짜의 기간과 정규/초과근무 누계를 schema25에 별도 보존한다. 현재 원장과 일치하면 기간/누계를 재내보내고, 변경 후에는 source.ppm에 원본 스냅샷을 유지한다. 외부 EV/PV는 native EVM과 별도다.

2026-11-06 후속: [실적·원가 상세 교환](xer-actual-cost-exchange.md)을 구현했다. 상태일·실제 날짜/잔여 기간·물리 진척, 누계 AC·유효 단가·직접 고정비와 일 단위 FINDATES/TRSRCFIN/TASKFIN을 매핑한다. 변환 보고의 `actualCosts`는 누계 공수·계획/잔여 금액·여러 날짜 집계·외부 EVM 등 원본 세부값도 보존한다. 아래 과거 실적/원가 미지원 표기는 당시 범위다. MPP·다일 집계의 native 편집·실제 P6 재계산/재저장 검증은 남아 있다.

2026-11-06 [작업·프로젝트·자원 UDF 매핑](external-udf-mapping.md)을 추가했다. UDFTYPE/UDFVALUE의 스칼라와 목록 이름·계산 결과, 프로젝트 선택/다중 결합의 ID 재배정을 지원한다. 아래 과거 사용자 필드 미지원 표기는 당시 범위이며 현재 제한은 새 계약을 따른다. 외부 식 실행·실적/원가 상세 교환과 실제 P6 왕복은 별도 범위다.

2026-11-05 현재: [일괄 작업공간·프로젝트별/통합 내보내기와 작업 코드 교환](xer-batch-exchange.md)을 지원한다. 아래 날짜별 관측은 당시 검증 범위다. 실적·원가 상세 교환과 MPP는 후속이다.

2026-11-03 당시 [다중 PROJECT 선택 가져오기](xer-project-selection.md)를 추가했다. 여러 프로젝트 중 하나를 명시 선택하고 필요한 자원·달력만 격리한다. 외부 incoming/outgoing 관계는 모두 차단하며 전체 원본은 보존한다. 다중 프로젝트 내보내기·일괄 작업공간 생성은 2026-11-05 확장으로 구현했고, MPP/실적/원가 상세 교환은 후속이다.

2026-11-03 기간별 실제공수·원가 원장은 이 교환 형식에 매핑하지 않으며 손실 진단을 추가한다. `source.ppm`에는 schema 11로 원본/정정·누계 링크를 보존한다. 원장이 없는 사본은 기존 schema 9/10이다. [기간 실적 계약](period-actuals.md)을 따른다.

분류 데이터: native 사용자 필드·저장된 보기는 매핑하지 않는다. 계층형 작업 코드는 2026-11-05부터 ACTVTYPE/ACTVCODE/TASKACTV에 매핑한다. 내보내기 미리보기/변환 보고에서 손실을 명시하고 `source.ppm`에는 사전·값·보기와 이력을 모두 보존한다. 기본 분류 데이터는 schema10 이상을 사용하며 다른 기능의 새 필드가 있으면 필요한 더 높은 schema를 사용한다. 외부 파일만 전달하면 이 데이터는 전달되지 않는다. [분류 계약](task-classification.md)과 [검증 기록](validation/task-classification-2026-11-02/README.md)을 따른다.

2026-10-30 응답성 보완: XER 읽기/변환·내보내기 준비와 사본 게시/기준선 검증은 XML과 같은 worker·진행/취소 경로를 사용한다. 최종 rename 전 취소는 임시 폴더를 정리하고 현재 문서·원본·기존 목적지를 보존하며, 이미 게시한 완성 폴더는 늦은 취소로 삭제하지 않는다. 문서 revision/session 변경을 감지하고 가져오기 적용 전에도 출처를 확인한다. 세부 경계와 회귀는 [XML 교환 응답성 계약](project-xml.md)을 따른다. `jobs_exchange_recovery_tests --xer <1만 작업 XER 경로> -platform offscreen`으로 실제 파일/기준선의 GUI heartbeat와 검증 비용을 측정할 수 있다. 시간 표본은 환경별 관측이며 고정 지연 상한은 아니다.

로컬 Windows / Qt 6.8.3 / MinGW Release / offscreen 단일 표본: 기존 합성 1만 작업 XER(SHA-256 `8f6236ad4f6c90b6e35f47179ac451f05ced3cc913f72c31f8ae235abc19ee6c`)에 기준선 1개를 넣고 실제 내보내기 메뉴를 실행했다. 준비·미리보기 승인·게시까지 8,624ms, 5ms heartbeat 1,721회·최대 간격 10ms, GUI 계산 checkpoint 0개를 기록했다. 이전 동기 게시 관측 7,067ms와 전체 실행 시간을 직접 비교하지 않는다. 사본·기준선 검증은 worker에서 유지하며 사용자는 진행을 확인하고 취소할 수 있다. 같은 테스트의 합성 1만 작업 XML은 3,956ms·heartbeat 최대 7ms, XER 게시 취소는 원본/목적지 보존과 임시 폴더 정리를 통과했다. OS 캐시와 실행 순서를 통제한 성능 보장이 아니다.

2026-10-27 · J-15 / XER-01~04. 이 계약을 개발 전에 확정한다. .ppm schema 9와 기존 계산 의미는 유지한다.

근거: Oracle [TASK](https://docs.oracle.com/cd/G48897_01/English/Mapping_and_Schema/xer_import_export_data_map_project/97906.htm), [CALENDAR](https://docs.oracle.com/cd/F88966_01/English/Mapping_and_Schema/xer_import_export_data_map_project/97869.htm), [TASKPRED](https://docs.oracle.com/en/industries/construction-engineering/primavera-p6-project/26/p6-xer-proj/taskpred-activityrelationships-97914a.html), [PROJECT](https://docs.oracle.com/en/industries/construction-engineering/primavera-p6-project/26/p6-xer-proj/project-projects-97885a.html)의 공식 필드 매핑을 따른다. 세부 직렬화와 독립 reader는 [MPXJ](https://www.mpxj.org/howto-read-xer/)와 구현 소스 `71d78a9b64f5de3bbeb7b667a480ab0888d011e8`의 StructuredText/Primavera reader/writer를 참고한다. Oracle이 공개한 필드 표를 모든 버전의 완전 문법 명세로 간주하지 않는다. 연구용 원본/외부 코드를 앱에 포함하지 않는다.

## 지원 범위

- 변환 단위는 PROJECT 하나다. 원본에 여러 PROJECT가 있으면 [선택 계약](xer-project-selection.md)으로 명시 선택하며 한 개면 기존처럼 자동 선택한다. PROJWBS의 proj_node_flag=Y 루트 하나는 프로젝트 이름/최상위에 매핑하며 하위 WBS와 leaf TASK를 ID로 연결한다. TASK task_code/PROJECT proj_short_name은 native 이름과 구분하여 변환 보고서에 기록한다. 새 수입은 새 documentId와 `xer-task-<task_id>` 등 구분된 ID를 사용한다.
- TASK의 target_drtn_hr_cnt를 기간 기반 고정 기간으로 투영한다. 공백은 미정, 0은 마일스톤이다. TT_Task/TT_Rsrc/TT_Mile/TT_FinMile을 지원하며 자원 종속·시작/완료 마일스톤 차이와 duration_type 손실을 진단한다. TT_LOE/TT_WBS·미지 유형은 차단한다. target/early/actual 날짜를 고정 실적이나 하드 제약으로 추론하지 않는다.
- PR_FS/PR_SS/PR_FF/PR_SF와 lag_hr_cnt를 지원한다. PPM은 후행 작업 달력 기준이다. SCHEDOPTIONS의 rcal_Successor 외 정책/누락을 진단하고 원본 설정을 보고한다. 선택 프로젝트의 여러 SCHEDOPTIONS/잘못된 프로젝트 참조는 차단한다. 다른 프로젝트의 옵션은 선택 범위에서 제외한다. 외부 프로젝트 관계는 임의 제거하지 않는다.
- primary constraint는 공백 ASAP, CS_ALAP, CS_MSOA/MSOB/MEOA/MEOB를 SNET/SNLT/FNET/FNLT, CS_MANDSTART/MANDFIN을 MSO/MFO로 매핑한다. CS_MSO/MEO도 고정 날짜로 투영하되 P6의 Start/Finish On과 native 하드 고정 의미 차이를 진단한다. 보조 제약은 차단한다. native 소프트 마감은 XER expect_end_date로 바꾸지 않고 손실 진단한다.
- CALENDAR clndr_data의 `(0||CalendarData()(…))`에서 DaysOfWeek의 일=1~토=7을 native 월=1~일=7로 매핑한다. 각 근무 요일의 일중 구간이 같아야 한다. 종료 00:00은 당일 끝 24:00으로 해석해 24시간 달력도 보존한다. 그 밖의 자정을 넘는 구간은 차단한다. 전일 휴일은 Exceptions d의 1899-12-30 기준 일수로 매핑한다. 부모 달력에서는 휴일만 상속하며 주간 시간은 각각 명시한다. 상속 순환/부모 누락, 근무 예외·서로 다른 요일 시간·누락된 주간 패턴·알 수 없는 일정 구조는 차단한다. UI 표시용 VIEW 정보는 진단 후 원본에만 보존한다. 프로젝트 달력은 수입 작업/자원에 명시 배정하고 새 native 작업은 기존 JOBS 기본 달력을 쓰는 차이를 표시한다.
- RT_Labor/RT_Equip 자원과 TASKRSRC target_qty_per_hr를 1% 정밀도로 매핑한다. material 자원은 차단하며 곡선 배정은 위21지점 계약을 따른다. RSRCRATE는 한 자원에 시작일이 프로젝트 시작 이전/같은 단일 행일 때 max_qty_per_hr만 기본 가용량으로 매핑한다. 없거나 다중/미래 가용량이면 미정과 진단으로 남긴다. 단가/원가·원장·기준선·역할/사용자 필드·분할/공수 규칙은 XER 계산으로 이식하지 않는다.

## 직렬화·한도

- UTF-8(선택 시 strict decode/BOM 허용)과 명시 선택 Windows-1252만 지원한다. 인코딩을 자동 추측하지 않는다. CP949/기타 파일은 원본을 보관하고 사본을 지원 인코딩으로 변환해야 한다. 내보내기는 한국어가 유지되는 UTF-8 BOM/CRLF이며 ERMHDR 버전 20.12의 지원 부분을 기록한다. P6 설치본이 이 파일을 수용한다는 뜻이 아니다.
- 탭 구분이며 마지막 빈 셀도 유지한다. %F는 테이블마다 한 번, 열 이름 중복/행 폭 불일치/%E 누락·후행 데이터/알 수 없는 레코드 마커는 차단한다. 필드 안의 탭/줄바꿈/NUL·제어문자는 수출 때 임의 escape/절삭하지 않고 거절한다. 날짜는 timezone 없는 YYYY-MM-DD HH:mm이다. 프로젝트 시작은 native 날짜의 09:00이므로 원본 시각 차이는 진단한다.
- 숫자는 locale 없는 최대 소수 6자리 십진수다. 시간×60을 정수 분으로 만들 때 6자리 시간 표현의 최대 반올림 오차(0.000030분) 이내만 허용하고 0이 아닌 보정은 진단한다. 수출은 소수 시간 6자리로 기록한다. 그 밖의 분수 분은 차단한다. 배정/가용량은 정확한 1% 단위여야 한다. 지원 통화는 기존 native KRW/JPY/USD/EUR이며 환산하지 않는다.
- 파일 16 MiB·행 300,000개·테이블 128개·열 256개·셀 1 Mi자·전체 셀 1,000,000개, 주요 엔터티 각10,000개, 관계/자원 배정/코드 배정 각100,000개다. clndr_data는 깊이 32·총 300,000 노드, 달력 1개 휴일 10,000일·상속 후 총 휴일 100,000일로 제한한다. Calendar parser는 전체 입력 한도와 깊이/노드 한도로 메모리 증폭을 제한한다. 원본 데이터는 코드/쿼리로 실행하지 않는다.

## 저장·화면·판정

파일 → Primavera XER 가져오기에서 파일/인코딩을 고르고 변환 미리보기의 건수/진단과 새 폴더 경로를 확인한다. 승인 후 project.ppm/original.xer/conversion.json을 같은 부모의 임시 폴더에서 모두 기록·재열기 검증하고 새 폴더로 rename한다. 내보내기는 전체 프로젝트를 project.xer/source.ppm/conversion.json으로 같은 방식으로 게시한다. schema 9/10 입력/승인·실적/원가는 source.ppm에 정확한 현재 스냅샷으로 보존하며 기존 디스크 파일 바이트 사본이라는 뜻은 아니다. .ppm만 따로 저장/이동하면 동반 파일은 자동 복제되지 않는다. 기존 대상은 덮어쓰지 않으며 취소/오류/오래된 미리보기는 현재 문서를 교체하지 않는다.

conversion.json contract는 `jobs-xer-plan-v1`이다. 방향·원본 헤더/버전/선택 인코딩·SHA-256·source/target ID/Activity Code·native profile/09:00 시작/후행 달력 지연 정책·경로별 손실을 기록한다. import/originalPreservation/jobsRecomputation/externalRoundTrip는 독립 판정이며 외부 계산/왕복은 unverified다. 독립 MPXJ 읽힘도 P6 재계산/재저장 동등성으로 확대하지 않는다.

## 검증

수작업 골든 fixture의 주간 달력/상속 휴일, FS/SS/FF/SF/음수 지연, 제약/자원/배정과 독립 날짜 기대값을 확인한다. 지원 부분 JOBS→XER→JOBS의 ID 정규화/일정 의미, 1분/미정/0/정밀도·한국어/Windows-1252, 헤더/원본 바이트/해시, 승인·실적·원가 사본과 실패/기존 폴더 보호를 검사한다. UI는 취소·미저장 경고·스냅샷/session 경계·읽기 전용과 native 화면을 검사한다. Python 표준 TSV reader 및 고정 MPXJ 16.10.0/JPype1 1.6.0(Java 21)로 수출물을 독립 재열기한다. 이들은 검증 의존성이며 앱 실행에는 Python/Java가 필요 없다. 기존 XML·계산·성능·보고서 회귀를 함께 실행한다.

## 사용과 현재 검증 산출물

1. 파일 → Primavera XER 가져오기에서 `.xer` 파일과 원본 인코딩을 선택하고, PROJECT가 여러 개면 원본 ID/코드/이름을 검색해 하나를 선택한다. 변환 건수/손실 진단과 아직 없는 새 폴더 경로를 확인한다.
2. `변환 확인 · 새 사본 열기` 후 미저장 문서를 저장/버림/취소한다. 새 폴더의 `project.ppm`가 열리며 F9로 JOBS 일정을 계산한다. `original.xer`/`conversion.json`을 원본 증거로 함께 보관한다.
3. 내보내기는 파일 → Primavera XER 내보내기에서 같은 미리보기를 거친다. `project.xer`/`source.ppm`/`conversion.json`이 생성된다. 원본 활동 ID(task_code)는 native 필드가 아니므로 내보낸 활동 ID는 J1…로 생성한다. 분류용 작업 코드(ACTVTYPE/ACTVCODE)와 구분한다. 프로젝트 코드는 문서 ID 해시에서 생성한 PPM-…이며 ID/코드 매핑과 기존 P6 프로젝트 업데이트 동일성이 보장되지 않는다는 진단을 기록한다. 기존 P6 DB를 자동 갱신하지 않는다.

`primavera_xer_contract`/`primavera_xer_ui`를 포함해 CTest 32개다. 골든 입력 `tests/fixtures/primavera-plan.xer`는 실제 P6 저장본으로 위장하지 않은 수작업 fixture다. 독립 검사 대상은 C++가 기록한 `primavera-export.xer`/`primavera-24h.xer`이며 Python TSV reader와 MPXJ strict reader가 원본 출력 파일을 직접 읽는다. UTF-8 BOM을 인식하는 MPXJ UniversalProjectReader proxy를 사용한다. 24시간 달력은 시간 구간의 실제 밀리초 합도 확인한다. 이것은 Oracle P6 설치본의 수용/재저장/계산 동등성 시험이 아니다.

네이티브 화면은 `primavera-xer-import-native.png`/`primavera-xer-export-native.png`이고 CI의 `primavera-xer-Windows`/`primavera-xer-macOS` artifact에 출력/진단과 함께 보관한다. 오류/한도·표현할 수 없는 값은 부분 투영하지 않으며, 지원하지 않는 정보는 경로별 진단과 원본/전체 native 사본에 보존한다.

평준화 채택 정책/실행 날짜는 기간 기반 XER 투영에 보존되지 않는 손실로 진단한다. `source.ppm`에 조건부 schema12 native 설정을 보존한다. 실제 P6 왕복은 별도 미실행이다. [채택 계약](leveling-adoption.md).

다중 PROJECT 선택의 [로컬 검증](validation/xer-project-selection-2026-11-03/README.md)은 전체 79개 CTest·독립 TSV/MPXJ/기존 출력 9종과 1천/1만 입력 worker 관측을 포함한다. 원본 실적 기간과 native target 기간 기반 투영을 동일하게 판정하지 않는다.

[실제 P6 왕복 준비와 절차](p6-roundtrip.md)를 추가한다. 사용자가 미설치를 확인해 실제 실행은 미실행이다. 계획 XER와 진척 정책의 실제/잔여 수동 입력 시험을 구분하며 비교기/MPXJ 성공을 실제 P6 증거로 대체하지 않는다.

schema13의 진척 정책 선택과 근거는 XER에 매핑하지 않고 교환 손실 진단을 표시한다. source.ppm에는 전체 선택을 보존한다. XER은 원본 기간 기반 논리 계획이며 P6 진척 재계산 왕복은 미검증이다.

schema14 조합과 시나리오의 동결 진척 정책은 native source.ppm에 보존한다. XER은 계속 원본 기간 기반 논리 계획이며 실제 P6 재계산 왕복은 미검증이다.

2026-11-05 확장: [다중 XER 일괄 작업공간·내보내기 및 코드 교환](xer-batch-exchange.md). 파일 메뉴에서 여러 원본/프로젝트를 선택하고 원자적 작업공간 폴더로 저장한다. 통합 내보내기는 모든 ID·참조를 다시 배정하며 ACTVTYPE/ACTVCODE/TASKACTV를 native 코드에 연결한다. 실적·원가·MPP와 외부 프로젝트 관계는 지원 범위를 넓혀 해석하지 않는다.
