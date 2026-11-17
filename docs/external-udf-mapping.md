# 외부 사용자 필드 매핑

2026-11-11 후속: [작업/자원→배정 roll-down](xml-roll-down.md)을 실행한다. XML은 원본/대상 PID·자동 플래그와 직접/명시 미설정을 보존하며 XER에는 현재 유효 배정 값을 정적으로 투영한다. 자동 연결은 source.ppm/진단에 남긴다.

2026-11-11: [GUID 텍스트 목록의 마스크·코드 생성](xml-code-masks.md)을 지원한다. 공유 사전의 새 항목과 표시 코드·ID/GUID를 네 범위에 반영하며 XER에는 정적 표시 값을 투영한다. WBS 자동 생성은 별도 XML WBS 설정이다.

2026-11-10 후속: [XDATE-01~05](xml-date-time-fields.md)는 CFType6/3과 Date 시각, 압축 GUID 날짜·공유 목록 역할을 지원한다. XER의 일시는 초/소수 초가 잘리지 않도록 문자열로 투영한다.

2026-11-10 추가: [XTYPE-01~05 Flag·Cost·Duration](xml-additional-field-types.md)으로 XML 형식을 확장했다. XER는 기존 형식으로 정적 문자열 투영하고 새 형식·금액 단위·기간 의미는 source.ppm/진단에 보존한다.

2026-11-10 후속: [XML 값 목록·GUID 참조](xml-lookup-references.md)를 추가했다. XML은 목록·참조를 보존하고 XER는 표시 이름으로 투영한다. 아래 이전 목록 제외 표기는 LOOKUP-01~05 지원 범위로 대체한다.

2026-11-10 확장: [기업·배정 UDF/XML 프로젝트 필드](enterprise-assignment-udf.md)의 XML 네 범위 Text/Number/Date·FieldID/GUID 보존, 배정 사전/편집·XER TASKRSRC 매핑과 schema37을 추가한다. 아래 기업/배정/프로젝트 필드 제외 표기는 이전 증분의 범위다. 값 목록/GUID 참조의 실행 의미와 외부 계산식 실행은 계속 제외한다.

2026-11-06. UDF-01~05, GAP-03/09, PRD-01/06/07/08, J-01/05/14/15/16에 연결한다. 기존 **파일 → Microsoft Project XML / Primavera XER 가져오기·내보내기**를 사용한다. 별도 외부 계정이나 실행 스크립트 없이 형식별 지원 필드를 자동 매핑하고, 적용 전 미리보기와 `conversion.json`에 범위·이름·외부 ID·native ID·형식을 표시한다.

## UDF-01 형식별 계약

| 형식 | 지원 범위 | 정의·값 |
|---|---|---|
| Project XML | 작업·자원 Text1~30 / Number1~20 / Date1~10 | 프로젝트 ExtendedAttributes 정의와 객체 ExtendedAttribute/FieldID/Value |
| Primavera XER | 작업 TASK·프로젝트 PROJECT·자원 RSRC | UDFTYPE / UDFVALUE. FT_TEXT, FT_FLOAT, FT_INTEGER, FT_START_DATE, FT_END_DATE 읽기; 텍스트/소수/시작 날짜로 쓰기 |

XML은 공식 로컬 FieldID의 범위·형식과 선택적 CFType/ElemType의 일치를 검사한다. 가져온 XML FieldID는 native 필드 ID에 포함하며 재내보내기 시 같은 형식/범위라면 예약해 재사용한다. 새 필드는 사전 순서로 남은 슬롯에 배정한다. 슬롯 부족은 변환 전체를 거절하며 다른 필드를 덮어쓰지 않는다. XML 프로젝트/기업·배정 필드와 Flag/Cost/Duration·외부 값 목록/GUID 참조는 이번 범위 밖이다. 직접 값이 없는 값 목록 참조는 원본과 진단에 남기고 값을 만들지 않는다.

XER는 새 UDF ID를 생성한다. 새 독립 사본의 변환이며 P6의 기존 객체 갱신 동일성을 보장하지 않는다. 프로젝트 선택 전에 모든 UDFVALUE의 정의·프로젝트·지원 객체 참조와 중복을 검사한다. 다중 XER 결합 시 UDF ID뿐 아니라 범위에 따라 fk_id를 작업/자원/프로젝트 ID로 재배정한다. 선택 프로젝트의 값과 사용 자원만 가져온다. 다중 파일에서 값이 없는 전역 정의는 소유 프로젝트를 결정할 수 없어 제외 행 수/원본에 남긴다. 단일 프로젝트의 빈 정의는 유지한다.

## UDF-02 값과 손실

- 숫자는 기존 범위 ±999999999.999999와 소수6자리의 정확한 십진 문자열이다. 0·음수·미설정을 구분한다. NaN/무한대·지수·범위/정밀도 초과·형식 불일치는 거절한다.
- 날짜는 시간대 없는 civil date만 매핑한다. 외부 자정 시각을 날짜로 읽고 자정으로 쓴다. 자정 이외의 시각은 임의로 버리지 않고 거절한다.
- 텍스트는 native 길이·문자 검증을 따른다. XML은 표준 이스케이프를 사용한다. XER의 탭/줄바꿈 등 레코드 제어문자는 거절한다. 빈 텍스트는 값 행으로 기록하지만 MPXJ 등 외부 reader가 미설정으로 해석할 수 있어 진단하고 source.ppm에 원래 구분을 보존한다.
- 목록은 옵션 표시 이름의 텍스트로 투영한다. 옵션 ID·사전은 native 원본에 보존한다. 계산식은 현재 숫자 결과만 투영하며 외부 식을 만들거나 실행하지 않는다. 미설정/계산 오류는 외부 값을 생략하고 원인을 진단한다. 단위·계산식·의존성·목록 사전의 손실을 미리보기에 표시한다.
- 가져온 스칼라는 수동 필드다. 외부 계산식/기업 필드·코드 목록의 실행 의미나 임의 단위 변환을 추정하지 않는다. 미지원 정의/메타데이터는 원본과 손실 진단에 남긴다. 중복 이름/ID·중복 객체 값·정의 없는 참조는 거절한다.

## UDF-03~05 보존·한도·검증

조회·변환은 소유 스냅샷으로 수행하고 문서에 직접 쓰지 않는다. 기존 새 교환 폴더 원자 게시·원본 바이트/source.ppm·취소/stale/session·읽기 전용 내보내기·사본 열기 정책을 유지한다. 새 native 저장 필드는 추가하지 않으며 가져온 일반 필드와 값은 기존 저장/복구/undo 규칙을 따른다. 기존 기준선·실적·동결 시나리오는 바꾸지 않는다.

기존 파일16 MiB·행/노드·사전64개 한도에 더해 UDF 조회 셀100,000개(미설정 포함)를 제한한다. XER 값100,000행·사전10,000행을 프로젝트 선택 전에 검사한다. 실패하면 부분 교환 폴더를 게시하지 않는다.

수용 검사: 세 범위/세 스칼라·목록/계산식 정적 투영, 미설정/0/빈 값·6자리·잘못된 값/중복/범위, XML 슬롯 보존/초과·형식 불일치, 다중 프로젝트 ID/소유 격리·제외 자료의 잘못된 참조, native 재열기·동반 원본, 실제 XML/XER 화면의 매핑 미리보기/출력, 독립 ElementTree/TSV·MPXJ reader를 확인한다. [실행 증거](validation/dated-float-udf-2026-11-06/README.md). 독립 reader 검사를 실제 Microsoft Project/P6 실행·재계산·재저장과 동일시하지 않는다.

매핑 구조의 근거: Microsoft [ExtendedAttribute](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/extendedattribute-element?view=project-client-2016), [CFType](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/cftype-element?view=project-client-2016), [ElemType](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/elemtype-element?view=project-client-2016), [PjCustomField 상수](https://github.com/MicrosoftDocs/VBA-Docs/blob/main/api/Project.PjCustomField.md), Oracle [XER Import/Export Data Map의 UDFTYPE/UDFVALUE](https://docs.oracle.com/cd/F88966_01/English/Mapping_and_Schema/xer_import_export_data_map_project/xer_import_export_data_map_project.pdf).
