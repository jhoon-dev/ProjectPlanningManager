# XML 값 목록과 GUID 참조

2026-11-11 후속: [roll-down 목록 대응](xml-roll-down.md)에서 공유 GUID 또는 정규 값/부모 경로로 원본 선택을 배정 선택에 대응한다. 범위별 ValueID 차이·단일/다중·열린 목록의 관측 직접 값을 보존한다.

2026-11-11 후속: [코드 마스크·자동 생성](xml-code-masks.md)을 지원한다. 텍스트 GUID 목록의 수준별 형식·길이·구분자/AllLevelsRequired와 새 항목 생성을 추가했다. 기존 ID/GUID·선택·공유 목록과 정적 XER 투영을 보존한다.

2026-11-10 후속: [날짜·시간 계약](xml-date-time-fields.md)에서 Date/Start/Finish 목록과 GUID 날짜 압축 형식·공유 역할을 확장했다. GUID 목록은 6초 단위로 보존하며 일반 ISO 값 목록과 정밀도 범위를 구분한다.

2026-11-10 후속: [추가 형식 계약](xml-additional-field-types.md)의 Flag17·Cost9·Duration6 목록/GUID 지원을 추가했다. 아래 처음 증분의 Flag/Cost/Duration 제외 표기는 당시 범위다. 기간 목록은 일반 정수1/10분 값으로 한정한다.

2026-11-10 · LOOKUP-01~05 · GAP-03/09. 기업/배정 스칼라 이후의 값 목록 지원 증분이다.

- 일반 ExtendedAttributes/ValueList의 ID와 값·설명, 기업 OutlineCodes의 Guid/ValueID/FieldGUID/부모·형식을 읽고 보존한다. Text/Number/Date와 네 객체 범위를 지원한다.
- ValueID/ValueGUID, 기업 16진 요소의 GUID, EnterpriseExtendedAttribute의 참조를 목록별로 해석한다. 중복·누락·다른 목록 참조·서로 충돌하는 값/ID/GUID는 거절한다. 기본값은 정의에 보존하며 미설정 객체에 자동 생성하지 않는다.
- 단일/다중 선택과 계층·leaf-only, 닫힌 목록을 지원한다. 열린 목록의 명시 직접 값은 원래 목록에 없는 관측 옵션으로 보존하고 다시 직접 값으로 출력한다. 외부 식을 실행하지 않는다.
- native는 Choice와 안정 옵션 ID로 선택을 보존한다. 외부 목록 정의는 원본 신원을 유지한다. 전용 마스크/코드 생성 창에서 마스크 변경과 항목 추가를 지원하며, 기존 항목의 신원·직접 삭제/재번호는 제한한다. 선택값은 기존 편집/worker·undo·보고서에서 수정한다. native 목록의 기존 정의 편집은 유지한다.
- 조건부 schema38에 목록 메타데이터를 저장하고 현재/기준선/시나리오·전환 사본·복구에 보존한다. XML은 목록과 참조를 재내보내며 XER는 표시 텍스트로 투영하고 원본/손실을 명시한다.

한도: 범위별64개 정의, 정의별1,000개 옵션, 전체100,000개 목록 항목/참조, 객체별 EnterpriseExtendedAttribute450개, 부모 깊이16, 기존16MiB XML·native 파일 제한. 형식/참조 검사·저장/복구·선택 편집과 독립 XML 판독 및 기존 회귀로 판정한다.

구조 근거: [ExtendedAttributes](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/xml-schema-for-the-extendedattributes-element), [OutlineCodes](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/xml-schema-for-the-outlinecodes-element), [EnterpriseExtendedAttribute](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/enterpriseextendedattribute-element). 실제 Microsoft Project/P6 재계산·재저장은 별도 검증이다.

지원 경계: GUID는 파일 안의 값 참조로만 해석하며 외부 서버의 목록을 조회하거나 등록하지 않는다. Flag/Cost/Duration·날짜 시각과 마스크/코드 생성은 후속 계약으로 확장했다. 배정 roll-down은 후속 RDOWN 계약으로 확장했다. 요약 roll-up·phonetic/정렬 규칙과 외부 식 실행은 변환하지 않고 원본 XML/진단에 남긴다. 열린 목록은 관측된 직접 값의 보존과 선택을 지원하며 새 직접 값 작성 UI는 제공하지 않는다. 일반 ExtendedAttribute는 표준 Value/ValueGUID로 쓰고 ValueID는 OutlineCode에 쓴다.

| 계약 | 검증 |
|---|---|
| LOOKUP-01 목록 정의 | 로컬 ValueList·GUID 테이블·형식·설명·부모/말단·기본값, 빈 사전/빈 값, 공유 사전 일치 |
| LOOKUP-02 참조 | Value/ValueID/ValueGUID/FieldValueGUID·기업 16진 GUID, 누락/다른 목록·충돌/중복·개수·순환 거절 |
| LOOKUP-03 native 보존 | schema38와 구버전 거절, 동결·전체 전환 사본·복구, 원자 편집·취소/stale·읽기 전용·undo/redo |
| LOOKUP-04 선택 편집 | 작업과 프로젝트/자원/배정의 단일·다중 선택, 말단 제한, 다중 객체 일괄 적용과 표시 이름 |
| LOOKUP-05 출력 | XML 목록/ID/GUID 재내보내기, XER 표시 문자열·원본/손실, XLSX/PDF와 독립 판독 |

기업 다중 선택은 반복 EnterpriseExtendedAttribute로 쓰며 GUID와 직접 값이 함께 있는 열린 목록도 같은 표현으로 쓴다. 로컬 Outline Code는 FieldID/ValueID/ValueGUID로 쓰고 직접 값이 섞이면 ExtendedAttribute의 값/값 GUID로 쓴다. SecondaryPID로 추론한 로컬 목록을 독립 배정 정의로 내보낼 때는 전역 값 ID 중복을 피하도록 보조 정의의 숫자 ID를 재배정한다. 원래 연결은 원본 XML/source.ppm에 보존한다.

선택 한도는 생략 시 단일 선택으로 읽는다. `MaxMultiValues=0`은 native 호환 정책상 정의별 1,000개 상한 안의 다중 선택으로 해석한다. XML 내보내기의 객체별 기업 요소450개 제한은 모든 기업 필드의 선택을 합해 검사하며 초과 시 전체 내보내기를 거절한다.

최종 [검증 기록](validation/xml-lookup-references-2026-11-10/README.md): 전체149/149, 파서598개, 형식419파일과 독립 XML/MPXJ/XLSX/PDF·Windows 기본 UI를 통과했다. 실제 Microsoft Project/P6 실행은 별도다.

기존 작업/자원 보기의 ‘같음’ 필터는 선택 집합 전체를 비교한다. 저장된 다중 선택 필터의 표시 이름과 내부 선택 집합을 함께 보존하며 단일 항목으로 바꾼 필터도 정규화한다. 임의 조합 선택을 위한 별도 필터 편집 UI는 추가하지 않으며 표시 이름 ‘포함’ 조건은 유지한다.
