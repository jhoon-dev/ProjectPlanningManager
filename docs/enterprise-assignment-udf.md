# 기업·배정 UDF와 XML 프로젝트 필드

2026-11-11 후속: [XML 배정 roll-down](xml-roll-down.md)에서 SecondaryPID 관계와 AutoRollDown을 보존·실행한다. 자동 값은 배정 직접 값과 구분하며 기존 독립 배정 UDF·원본 값/미설정 계약과 schema42 전체 사본 보존에 연결한다.

2026-11-10 후속: [값 목록·GUID 참조](xml-lookup-references.md)로 목록/다중 값·계층·기본값과 schema38 보존을 확장했다. 아래 EUDF 증분의 목록 제외 표기는 당시 범위다.

2026-11-10. EUDF-01~05 · GAP-03/09 · PRD-01/06/07/08. 기존 파일 → Project XML 가져오기/내보내기와 프로젝트의 사용자 필드 메뉴를 확장한다. [전체 회귀·독립 판독·Windows 검증](validation/enterprise-assignment-udf-2026-11-10/README.md).

- EUDF-01: XML 기업 작업/자원/프로젝트/배정 필드의 CFType 7/5/1(텍스트/숫자/날짜)을 읽고 쓴다. FieldID의 공식 범위와 ElemType 20/21/23을 검증한다. 프로젝트 필드는 UID0 요약 작업에 기록하며 입력의 다른 작업에 반복된 값은 일치해야 한다. 배정의 직접 ExtendedAttribute 값과 f404000~f4040c8 로컬 정의도 같은 스칼라 검사로 읽는다.
- EUDF-02: FieldID·Guid·FieldName을 native 정의에 보존한다. 새 프로젝트/배정 필드는 해당 기업 범위의 빈 ID와 문서/필드별 안정 GUID로 투영한다. SecondaryPID의 배정 값은 독립 배정 정의로 보존·재내보내고 원래 연결은 원본과 진단에 남긴다. 기업 ID/GUID는 외부 서버 등록이나 갱신 권한을 뜻하지 않는다. GUID 참조·값 목록·다중 값과 외부 계산식 실행은 지원하지 않고 원본/손실 진단을 보존한다. 계산식의 명시 스칼라는 수동 값으로 읽는다.
- EUDF-03: 배정 사전/값을 독립 범위로 제공한다. 정의·일괄 값 편집·제거, 원자 worker 적용·취소/stale/읽기 전용, undo/redo와 숫자 XLSX/PDF 보고를 연결한다. 배정 식은 지원하지 않는다. 값은 일정/공수/원가를 변경하지 않는다.
- EUDF-04: 조건부 schema37에 정의의 XML 신원과 배정 사전/값을 저장한다. 기존 최소 버전 writer를 유지한다. 현재/기준선/시나리오·일정 채택 사본·통화/ID 이관 전후 사본, 사본/복구에 보존한다. XER의 TASKRSRC UDF를 추가하며, 배정 ID와 프로젝트 소유권·다중 프로젝트 결합 ID를 검증한다. XML 기업 신원은 source.ppm/진단에 보존하고 XER에는 정적 값으로 투영한다.
- EUDF-05: XML 내보내기 별칭은 50자까지다. 범위별 64개 정의·객체별 64개 값과 XML 100,000개 값/조회 셀 한도를 유지한다. 중복/미정의 ID·GUID·범위/형식 불일치·충돌 프로젝트 값·비자정 날짜·수치 정밀도 초과를 거절한다. 원본 XML/native 동반 사본과 교환 폴더 원자 게시를 유지한다.

공식 구조: [Custom Field Data in XML](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/custom-field-data-in-xml), [ElemType](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/elemtype-element), [배정 로컬 필드](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/f404000-f4040c8-elements). 실제 Microsoft Project/P6 실행·재저장 검증은 별도다.
