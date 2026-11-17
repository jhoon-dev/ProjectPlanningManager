# XML 사용자 필드의 배정 roll-down

2026-11-11 후속: [XML 계산식](xml-external-formulas.md)의 Task/Resource 결과도 배정에 전파한다. 새 배정 수신 필드에 원본 식/요약 규칙을 복제하지 않으며 활성 배정 식은 별도로 계산한다. native 식 원본은 기존 제한을 유지한다.

2026-11-11 · RDOWN-01~05 · GAP-03/09.

XML의 AutoRollDown/SecondaryPID를 작업·자원 사용자 필드에서 배정 필드로 이어지는 연결로 보존하고 실행한다. 같은 값을 각 배정에 복사해 표시하며 금액·기간을 배정률로 나누거나 실제 일정/공수/원가에 합산하지 않는다. WBS 하위 작업 전파나 요약 행의 roll-up 계산과는 별도 기능이다.

## 값과 우선순위

1. 배정의 직접 입력값을 먼저 사용한다. 원본과 같은 값으로 입력했더라도 직접 입력이며 원본 수정으로 덮어쓰지 않는다. 0·Flag=false·빈 텍스트도 유효한 직접 값이다.
2. XML에서 명시한 배정 미설정은 별도 상태로 보존하고 자동 값을 차단한다. Assignment.rollDownMissing에 해당 필드 키를 기록하며 직접 값과 동시에 존재할 수 없다.
3. 직접 값/명시 미설정이 없고 AutoRollDown이 켜져 있으면 연결된 작업 또는 자원의 원본 값을 사용한다. 원본도 미설정이면 배정 역시 미설정이다. 기본값을 임의로 만들지 않는다.

자동 값은 Assignment.attributes에 저장하지 않는다. 원본 수정과 새 배정에 즉시 반영하며 직접 입력과 파생 값을 구별한다. 배정 값 편집의 **원본 값 사용**은 직접 값/명시 미설정을 지우고 자동 전달로 복원한다. 일반 필드의 기존 값 제거 동작은 유지한다. 자동 전달을 끄거나 연결을 없애면 파생 값은 표시하지 않고 직접 값/명시 미설정은 유지한다.

## 형식·목록·연결 검사

Text·Number·Date·Flag·Cost·Duration·Start·Finish와 단일/다중 Choice를 지원한다. 날짜 시각·소수 초·기간의 경과/추정 표시와 금액 통화 정밀도를 유지한다. 원본과 배정의 형식·단위 및 목록의 값 형식/단일·다중 한도가 맞아야 한다. 외부 계산식은 실행하지 않으며 native 계산식 필드의 roll-down 연결도 이 증분에서는 지원하지 않는다.

GUID 목록은 같은 GUID 값으로 연결하고, 일반 목록처럼 범위별 ValueID가 다른 경우에는 정규 값과 부모 경로로 대응한다. 중복 후보·대응 없는 값은 거절한다. 열린 XML 목록의 관측 직접 값은 받는 목록에도 추가해 기존 직접 선택과 ID를 유지한다. 마스크·전체 수준 필수·LeafOnly와 선택 개수 검사를 함께 적용한다.

자동 XML 입력에는 지원 범위의 명시 SecondaryPID가 필요하며 누락된 로컬 배정 슬롯을 임의로 추정하지 않는다. 하나의 배정 필드에는 한 원본만 연결한다. 작업/자원 충돌·누락 대상·형식/단위 불일치·배정/프로젝트 원본·자동 전달의 빈 대상·잘못된 boolean/PID를 거절한다. 비활성 AutoRollDown=false만 있는 메타데이터는 대상 없이도 보존한다. SecondaryPID만 있으면 자동 전달은 꺼진 상태로 연결을 보존한다. 활성 연결의 유효 배정 조회는 전체100,000셀로 제한하며 기존 범위별64개 정의/1,000개 목록 옵션 한도를 유지한다.

## 화면·편집

작업/자원 사용자 필드 정의 창에서 필드를 선택하고 **배정 roll-down…**을 연다. 호환되는 배정 필드를 연결하거나 같은 형식의 새 배정 필드를 만든다. **영향 미리보기**는 자동 값·직접 값·명시 미설정 개수와 첫20개 배정의 유효 값/출처를 표시한다. 저장은 하나의 원자 편집이며 취소는 문서를 바꾸지 않는다.

배정 값 편집 화면은 유효 값을 표시한다. 툴팁과 선택 설명에서 직접 입력/작업 roll-down/자원 roll-down/명시 미설정 출처를 확인한다. 직접 입력과 원본 값 복원은 기존 worker·취소/stale·읽기 전용·undo/redo 경로를 사용한다. 배정률 편집은 직접 값과 명시 미설정을 보존한다. XLSX/PDF에는 유효 값을 출력한다.

## XML·native·XER 계약

ExtendedAttribute 정의에 SecondaryPID와 AutoRollDown을 읽고 쓴다. 독립 배정 정의가 없으면 기존 SecondaryPID 규칙으로 정의를 만들고 연결한다. 내보낼 때 실제 배정 필드의 외부 PID를 참조한다. XML에 자동 값을 명시 값으로 덧쓰지 않아 자체 재가져오기 후에도 연결이 계속 동작한다.

XML에서 값이 명시된 배정 행은 원본과 값이 같아도 직접 입력으로 취급한다. 명시 미설정은 값 없는 ExtendedAttribute로 재내보낸다. 빈 텍스트 Value는 직접 빈 문자열로 구분한다. 날짜의 NA/빈 값 및 비텍스트의 빈 값도 배정 명시 미설정으로 읽는다. 외부 제품이 자동 값을 명시 값으로 재저장했을 때의 manual provenance는 XML만으로 추측하지 않는다. 실제 외부 앱의 값 없는 요소 처리도 별도 검증이다.

현재·기준선·시나리오·채택 및 통화/ID 이관의 전후 전체 사본에 연결 또는 명시 미설정이 있을 때만 schema42를 쓴다. 구버전 envelope는 새 데이터를 거절하고 기존 파일의 최소 writer는 유지한다. native 저장/복구는 원본 연결·직접 값·명시 미설정을 모두 보존한다. 통화 전환은 원본 금액과 직접 금액을 각각 한 번만 환산하고 자동 값은 새 원본에서 다시 얻는다. 원장 ID 이관은 작업/배정의 소유 참조를 따라 연결을 유지한다.

XER는 현재 유효 배정 값을 정적으로 투영한다. 자동 연결·직접 값/명시 미설정 구분은 source.ppm과 진단에 남긴다. [요약 roll-up](xml-roll-up.md)은 후속 계약을 따른다. 외부 식 실행, 외부 서버 등록·갱신, 실제 Microsoft Project/P6 열기·재계산·재저장은 별도 범위다.

| 계약 | 검증 |
|---|---|
| RDOWN-01 | AutoRollDown/SecondaryPID·작업/자원 원본·배정 정의 합성·잘못된 연결 거절 |
| RDOWN-02 | 원본 수정/새 배정·직접/동일 값/빈 텍스트/0/false/명시 미설정·비활성/복원 |
| RDOWN-03 | 모든 스칼라·일시/기간 표시·일반/형식/GUID/다중·열린 목록 대응·원자 실패 |
| RDOWN-04 | schema42·기존 최소 writer·동결/전체 사본·통화/ID 이관·저장/복구·worker/undo |
| RDOWN-05 | 설정/영향 미리보기·값/출처·복원 화면·XML 재교환·XER 유효 값·XLSX/PDF·독립 판독 |

근거: Microsoft [ExtendedAttribute](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/extendedattribute-element?view=project-client-2016), [SecondaryPID](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/secondarypid-element?view=project-client-2016), [기업 필드의 assignment roll-down](https://support.microsoft.com/en-gb/project/add-or-edit-an-enterprise-custom-field). 작업/자원 값을 같은 값으로 전달하고 직접 배정 입력을 우선하는 규칙을 따른다. MPXJ16.10.0은 AutoRollDown을 실행하거나 해당 메타데이터를 모델에 노출하지 않고 기업 16진 값도 생략하므로, 실행 의미·플래그·유효 값은 XML/native 독립 대조로 확인한다.

[전체157/157·파서678개·C++ 형식435파일·독립 XML/XER/MPXJ/XLSX/PDF·Windows UI 검증](validation/xml-roll-down-2026-11-11/README.md)을 통과했다. 전체 실행 후의 UI 안내/이름 표시 보완은 별도4/4와 Windows 화면으로 재검증했다.
