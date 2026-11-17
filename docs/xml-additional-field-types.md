# XML Flag·Cost·Duration 사용자 필드

2026-11-10 후속: [Start·Finish·Date 시각](xml-date-time-fields.md)의 지원 부분을 추가했다. 아래 XTYPE 증분의 Start/Finish 제외 표기는 당시 범위다. 시간대 변환·외부 식 실행과 실제 제품 왕복은 계속 별도다.

2026-11-10 · XTYPE-01~05 · GAP-03/09.

- Flag(CFType4), Cost(0), Duration(2)을 native의 독립 형식으로 지원한다. 작업/자원의 공식 로컬 슬롯과 네 범위의 기업 필드, SecondaryPID 배정 필드를 읽고 쓴다. false·금액/기간0과 미설정을 구분한다.
- Flag는 1/0을 예/아니요로 편집·표시한다. Cost의 XML 값은 통화 주단위의100배이며 native는 프로젝트 통화의 최소 단위에 정확히 맞는 값을 보존한다. 음수도 허용한다. 범위는 최소 단위 ±9조이며 정밀도 초과·오버플로를 반올림 없이 거절한다.
- Duration의 직접 값은 PT 시간/분/초 ISO 기간을 읽고 분 단위 소수6자리로 정규화한다. native는 `90 m`, 경과 시간 `90 em`, 추정 `90 m?`/`90 em?`로 보존한다. 시간/일/주/월의 표시 단위는 분으로 정규화하고 경과/추정 의미를 유지한다. XML은 PT 시간/분/초와 DurationFormat3/4/35/36으로 쓴다. 년/월/일 구성 ISO 기간, 비율/Null DurationFormat은 시간 해석이 모호해 거절한다. 일정의 작업 기간/공수로 자동 채택하지 않는다.
- 값 목록/GUID 참조의 새 형식은 Flag17/Cost9/Duration6을 지원한다. 기간 목록은 일반 정수1/10분만 지원하며 경과/추정 목록 값과 더 세밀한 값은 거절한다. Cost 정의가 있으면 단순 통화 라벨 변경을 차단한다. Cost 목록·명시 직접 금액은 프로젝트 통화 전환의 미리보기/환산·현재/기준선/시나리오에 포함하고 원래 ID/GUID를 유지한다. 반올림으로 일반 목록 값이 중복되면 원자적으로 거절한다.
- schema39에 새 형식을 저장하며 이전 파일의 최소 writer를 유지한다. 저장/복구·동결/전체 전후 사본·undo/원자 worker 편집을 지원한다. 새 형식은 수동 값이며 외부 식/roll-down은 실행하지 않는다. native 숫자 계산식의 입력/결과로 자동 변환하지 않는다. Cost는 보고서의 숫자 금액 셀, Flag와 Duration은 표시 문자열로 출력한다. XER는 금액·참/거짓·기간의 표시 문자열로 정적 투영하며 원래 형식은 source.ppm에 보존한다.

근거: [CFType](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/cftype-element?view=project-client-2016), [PjField](https://learn.microsoft.com/en-us/office/vba/api/project.pjfield), [DurationFormat](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/durationformat-element?view=project-client-2016), MPXJ16.10.0의 MSPDI DatatypeConverter. 실제 MS Project/P6 열기·재계산·재저장은 별도 검증이다.

| 계약 | 검증 |
|---|---|
| XTYPE-01 XML 매핑 | CFType0/2/4, 작업/자원 로컬 슬롯과 네 범위 기업 필드, SecondaryPID·일반 직접 값/기업 값 |
| XTYPE-02 형식·목록 | false/0/미설정, 부호·통화 정밀도·배율, ISO 시간/분/초·경과/추정·기본 형식, 값 목록/GUID 타입9/6/17 |
| XTYPE-03 보존·전환 | schema39·구버전 거절, 현재/동결·전체 전후 사본·복구, Cost 단위 변경 차단·환산/반올림과 목록 충돌 거절 |
| XTYPE-04 편집 | Flag 선택·음수 금액·기간 입력, 일괄 worker·원자성·취소/stale·일반 undo/redo, 비교/정렬·금액 보고서 |
| XTYPE-05 출력 | XML 재내보내기·XER 문자열 투영/원본·손실, 독립 ElementTree/Decimal·MPXJ·XLSX/PDF·Windows UI |

통화 전환은 기존 감사 이력 장벽을 유지한다. 일반 필드 값의 편집은 undo/redo를 지원하며, 통화 전환을 일반 실행 취소에 넣지 않는다. XER의 일반 숫자 UDF 범위보다 큰 Cost도 안전하게 보존하기 위해 새 형식은 XER 문자열로 투영한다.

최종 [검증 기록](validation/xml-field-types-2026-11-10/README.md): 전체151/151 재실행·파서614개·형식421파일과 독립 XML/MPXJ/XLSX/PDF·Windows 기본 UI를 통과했다. 최초 전체 실행의 기존 기간 EVM UI 비정상 종료1회와 단독3회·전체 재검사를 함께 보존한다.
