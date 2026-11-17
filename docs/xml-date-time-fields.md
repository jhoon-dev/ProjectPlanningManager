# XML Start·Finish와 날짜·시간 사용자 필드

2026-11-10 · XDATE-01~05 · GAP-03/09.

- Start(CFType6)와 Finish(3)를 native의 독립 시작/종료 일시 형식으로 지원한다. 작업/자원의 공식 Start1~10/Finish1~10 슬롯과 네 객체 범위 기업·배정/SecondaryPID를 읽고 쓴다. 실제 작업 일정의 시작/종료·제약으로 자동 채택하지 않는다.
- 기존 Date(CFType1)는 날짜와 시각을 허용한다. 날짜/자정 값은 기존 YYYY-MM-DD 표기를 유지하고 자정 외 시각은 YYYY-MM-DDTHH:MM:SS[.소수6자리]로 보존한다. Start/Finish는 자정도 전체 일시로 저장한다. 분 단위 입력·공백 구분·소수 초 뒤0은 정규화한다. 시간대 없는 일정의 civil time이며 Z/오프셋·24시·윤초·잘못된 날짜와 6자리 초과 소수 초는 거절한다. 호스트 시간대로 변환하거나 오프셋을 임의로 버리지 않는다.
- 날짜 스칼라의 NA/빈 값은 미설정으로 읽고 진단한다. 목록 사전의 NA는 유효한 시각이 아니므로 거절하며 기본값을 미설정 객체에 채우지 않는다. 같은 프로젝트 필드의 값과 명시 미설정이 충돌하면 거절한다.
- 일반 ValueList는 ISO 시각과 모든 지원 정밀도를 보존한다. GUID 목록의 Type4(Date/Start)/27(Finish)는 1983-12-31 기준 날짜×65536+시각/6초의 압축 값을 읽고 쓴다. 목록 값의 허용 범위는 해당 기준일~9999-12-31·6초 단위이며 더 세밀한 값은 반올림하지 않고 거절한다. 이전 ISO 표기의 GUID 날짜 목록도 호환 입력으로 읽되 같은 제한을 적용한다. 같은 GUID 날짜 사전을 Date·Start·Finish 필드가 공유할 수 있으며 필드 역할에 맞춰 시각과 테이블 타입을 정규화하고 진단한다. ID/GUID·계층·기본값·단일/다중·열린 목록의 명시 직접 값을 기존 계약과 함께 보존한다.
- 시작/종료 형식 또는 Date의 시각 값이 현재/동결·채택·전체 전후 사본에 있을 때만 schema40을 쓴다. 기존 날짜 전용 파일의 최소 writer를 유지하며 저장/복구·원자 worker·취소/stale·읽기 전용·일반 undo/redo에 연결한다. 일시는 문자열로 보고하고 시간 순으로 필터/정렬한다. 통화 전환은 날짜·역할·선택 ID를 바꾸지 않는다. XER는 시작/종료 및 시각이 있는 Date를 문자열로 투영하여 초·소수 초를 보존하며 원래 형식은 source.ppm/진단에 남긴다.

| 계약 | 검사 |
|---|---|
| XDATE-01 | 공식 CFType/PID와 네 범위·프로젝트 요약·배정/보조 필드 |
| XDATE-02 | Date 시각·Start/Finish 역할·자정/미설정·NA·소수 초·윤년·범위·시간대 경계 |
| XDATE-03 | ISO 값 목록·압축 날짜 GUID 목록·공유 역할·ID/GUID·정밀도/참조/충돌 거절 |
| XDATE-04 | schema40/구버전 거절·동결/전체 사본·복구·원자 편집/undo·역할/일시 보존 |
| XDATE-05 | 편집 화면·시각 필터/정렬·XML 재내보내기·XER 문자열·XLSX/PDF·독립 판독 |

구조 근거: Microsoft [CFType](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/cftype-element?view=project-client-2016), [PjField](https://learn.microsoft.com/en-us/office/vba/api/project.pjfield), [OutlineCodes schema](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/xml-schema-for-the-outlinecodes-element?view=project-client-2016). 압축 날짜는 MPXJ16.10.0의 DatatypeConverter/MicrosoftProjectConstants 구현과 별도 날짜 산술로 대조한다. 실제 Microsoft Project/P6 열기·재계산·재저장과 시간대 변환·외부 식/roll-down은 별도 범위다.

검증: [전체153/153·630개 파서 seed·423파일 형식 검사와 독립 XML/날짜 산술·Windows UI·XLSX/PDF](validation/xml-date-time-fields-2026-11-10/README.md)를 통과했다. MPXJ16.10.0은 소수 초가 있는 원본을 거절하므로 해당 한계를 확인하고, 소수 초를 제거한 별도 사본의 정초 값4개만 strict reader로 대조했다. 원본의 소수 초와 기업 값은 XML/native 재수입 및 보고서 검사로 검증하며 실제 제품 호환성 전체 통과로 확대하지 않는다.
