# 다중 프로젝트 XER의 선택 가져오기 (GAP-09 첫 증분)

2026-11-03 다음 단계 요청으로 구현했다. 여러 PROJECT가 들어 있는 원본에서 명시적으로 하나를 선택해 새 독립 PPM 사본으로 가져온다. 단일 프로젝트의 기존 자동 선택과 기간 기반 계획 투영은 유지한다. 일괄 작업공간 생성·프로젝트 병합/링크·다중 프로젝트 내보내기·MPP·실적/원가/기준선 교환은 후속이다.

## 선택·격리 계약

- 파일/명시 인코딩 선택 후 worker에서 전체 구조와 프로젝트 목록을 확인한다. 프로젝트 ID·코드·이름·시작일·작업 수·외부 선행/후행 수를 조회/검색하고 하나를 선택한다. 여러 프로젝트 중 첫 번째를 자동 선택하지 않는다. 목록은 변환 성공 판정이 아니며 선택 프로젝트의 기존 의미 검증은 계속 수행한다.
- PROJECT의 proj_id와 작업/WBS/관계/배정/달력/자원/단가의 고유 ID는 파일 전체에서 중복을 거절한다. TASK/PROJWBS/TASKRSRC/SCHEDOPTIONS의 proj_id는 포함된 PROJECT를 참조해야 한다. 선택 프로젝트의 작업/WBS/배정과 일정 옵션만 변환하며 다른 프로젝트의 미지원 작업 유형·달력 내용은 선택 프로젝트에 섞지 않는다.
- 선택 프로젝트와 연결된 TASKPRED는 incoming/outgoing 모두 외부 관계를 차단한다. 다른 프로젝트가 파일 안에 있어도 링크를 제거하거나 날짜 제약으로 대신하지 않는다. 내부 관계는 작업 소유 프로젝트도 검사한다. 외부 관계 수는 해당 PROJECT가 파일에 없는 참조도 포함한다.
- 다중 입력의 자원은 선택 배정이 참조하는 RSRC와 해당 RSRCRATE만, 달력은 선택 프로젝트 기본/작업/사용 자원 달력과 부모의 전이 폐쇄만 사용한다. 비어 있거나 0인 calendar.proj_id는 전역, 그 외는 선택 프로젝트 소유여야 한다. 선택 프로젝트가 다른 프로젝트 소유 달력을 참조하면 거절한다. 누락된 자원/달력/부모와 순환은 기존 검증으로 차단한다. 단일 PROJECT 입력의 기존 미배정 자원/달력 보존 동작은 유지한다.
- 고유 ID는 native 문서 안의 로컬 ID로 변환하고 각 가져오기는 새 documentId를 갖는다. 원본 ID/프로젝트 ID와 선택/제외 행 수를 conversion.json에 기록한다. 서로 다른 문서를 ID만으로 병합하지 않는다. 미지원 테이블/필드는 원본에만 남으며 미지원 데이터의 프로젝트별 완전 검증을 주장하지 않는다.

## 원본·세션·한도

목록과 변환은 한 번 읽은 동일 바이트를 사용한다. 기존 16 MiB·행/셀·테이블/엔터티 한도를 전체 파일에 적용하며 선택으로 우회하지 않는다. 원본 전체를 original.xer로 정확히 보존하고 SHA-256을 기록한다. project.ppm과 진단을 기존 임시 폴더/원자적 게시 경로로 재열기 검증한다. 취소·오류·선택/미리보기 동안 revision/session 변경 시 현재 문서와 기존 대상은 보존한다. 재선택/게시 실패 시 부분 문서를 열지 않는다. native schema와 원장·계산 정책은 변경하지 않는다.

conversion.json의 jobs-xer-plan-v1에 projectSelection을 추가한다. 선택 ID/원본 프로젝트 수/제외 프로젝트 수·각 지원 테이블의 원본/선택/제외 행 수·외부 관계 차단 정책을 기록한다. 원본에 다른 프로젝트 자료가 포함되므로 원본 파일 전체의 보관·전달 범위를 사용자가 알 수 있게 미리보기에 표시한다.

## 검증과 근거

서로 다른 프로젝트와 공유 전역/프로젝트 달력·자원, 비선택 미지원 작업/달력·중복/고아 ID·외부 선행과 후행·원본 해시·게시/재열기·취소/stale/UI 선택을 검사한다. 독립 TSV/MPXJ reader로 프로젝트 목록/선택 결과를 대조한다. 독립 reader 성공과 실제 P6 수용·재계산/왕복은 별도다.

필드 소유 관계는 Oracle [PROJECT](https://docs.oracle.com/en/industries/construction-engineering/primavera-p6-project/26/p6-xer-proj/project-projects-97885a.html), [XER project data map의 TASKPRED](https://docs.oracle.com/cd/E80668_01/English/Mapping_and_Schema/p6_pro_xer_import_export_data_map_project/p6_pro_xer_import_export_data_map_project.pdf), [CALENDAR](https://docs.oracle.com/cd/F88968_01/English/Mapping_and_Schema/xer_import_export_data_map_resource_only/97845.htm)을 참고한다. [MPXJ의 다중 프로젝트 선택](https://www.mpxj.org/howto-read-xer/#multiple-projects)은 별도 reader 대조 수단이다. 소유 격리/외부 관계 거절은 PPM의 이번 지원 부분 정책이며 P6의 전 기능 동작을 재현한다는 뜻이 아니다.

## 로컬 검증 결과

[Windows Release 전체 CTest 79개·92 parser seeds·TSV/MPXJ와 독립 검사 9종·화면/규모 관측](validation/xer-project-selection-2026-11-03/README.md)을 통과했다. 1천/1만은 전체 입력 작업 수이며 각각 500/5000개를 선택한 worker 측정이다. 실제 데스크톱/다른 OS·P6 수용/재계산·재저장 왕복은 미실행이다.

2026-11-05 확장: [다중 XER 일괄 작업공간·내보내기 및 코드 교환](xer-batch-exchange.md). 파일 메뉴에서 여러 원본/프로젝트를 선택하고 원자적 작업공간 폴더로 저장한다. 통합 내보내기는 모든 ID·참조를 다시 배정하며 ACTVTYPE/ACTVCODE/TASKACTV를 native 코드에 연결한다. 실적·원가·MPP와 외부 프로젝트 관계는 지원 범위를 넓혀 해석하지 않는다.
