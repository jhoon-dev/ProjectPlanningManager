# XML 필드 마스크·자동 코드 생성

2026-11-11 후속: [배정 roll-down](xml-roll-down.md)을 추가했다. 마스크/공유 코드 사전의 선택 값도 배정으로 전달하며 기존 코드 생성과 직접 배정 선택을 유지한다. 아래 이전 roll-down 제외 표기는 당시 범위다.

2026-11-11 · XMASK-01~05 · GAP-03/09.

작업·프로젝트·자원·배정의 GUID 텍스트 목록에 수준별 마스크를 적용하고 코드를 생성한다. 같은 GUID 목록을 여러 필드가 공유하면 마스크·항목 추가가 함께 반영된다. 기존 항목의 ID/GUID·계층·선택·기본값과 단일/다중·열린 목록 계약을 유지한다. 실제 일정·내부 WBS ID와 별도의 외부 코드다.

## 사용자 필드 마스크

- `OutlineCodes/OutlineCode/Masks/Mask`의 Level·Type·Length·Separator와 AllLevelsRequired를 읽고 쓴다. 수준을 정렬해 1부터 연속인지 검사한다. 최대16수준, 길이0~255(0=자유, 나머지=고정 길이), 구분자는 Unicode 한 문자 또는 고정 길이 수준 사이의 빈 구분자다. 전체 코드는255문자 이내다. 생략한 Level은 문서 순서, Type은 문자열, Length는0, Separator는점(.)으로 정규화한다.
- Type0은 ASCII 숫자, Type1/2는 영문 대문자/소문자, Type3은 Unicode 문자열이다. 제어 문자·잘못된 UTF-8·잘못된 길이/수준·부모·중복 형제 코드·구분자 충돌은 거절한다. 영문 이외 대소문자 판정은 자동 추측하지 않으며 문자열 마스크를 사용한다. 숫자/날짜 등 비텍스트 목록에는 코드 마스크를 적용하지 않는다.
- 값 사전에는 각 수준의 부분 값을 유지하고, 화면·보고서에는 마스크 구분자로 결합한 전체 코드를 표시한다. 중간 부모 항목도 사전에 보존한다. AllLevelsRequired이면 선택 값과 열린 목록 직접 값에 전체 수준을 요구한다. LeafOnly도 함께 적용한다.
- 각 범위의 사용자 필드 정의 창에서 GUID 텍스트 목록을 선택하고 **XML 코드 마스크…**를 연다. 기존 값과 맞지 않는 마스크 변경은 원자적으로 거절한다. **XML 목록 코드 생성…**에서 부모와 설명을 정하고 다음 순번 또는 직접 값을 미리 본 뒤 저장한다. 새 값이 객체에 자동 배정되지는 않으며 값 편집 창에서 선택한다.
- 숫자는1부터, 영문은A/a부터 시작한다. 고정 길이는 숫자0·문자A/a로 채운다. 같은 부모에 있는 현재 코드의 최대 순번 다음을 만들며 기존 코드의 재번호나 중간 빈 번호 채우기를 하지 않는다. 자유 길이 영문은Z→AA, 고정 길이 공간 소진은 거절한다. Type3은 직접 입력한다. 새 항목에 충돌 없는 숫자 ValueID와 새 GUID를 발급하고 공유 사전을 한 번의 편집으로 갱신한다. 삭제 이력 전체에 대한 영구 번호 예약은 제공하지 않는다.

## XML WBS 마스크·자동 생성

XML의 GenerateCodes는 사용자 필드 목록이 아닌 WBSMasks의 설정이다. **프로젝트 → XML WBS 마스크·자동 코드…**에서 접두사·마스크·중복 방지와 객체별 코드를 편집한다. VerifyUniqueCodes·GenerateCodes·Prefix·WBSMask 및 작업/요약 행의 WBS·WBSLevel을 읽고 쓴다. 접두사는50문자, 전체 코드는255문자 이내다. 마스크 없는 WBS 값도 보존하며 자동 생성은 기본 숫자/점 구분을 사용한다. MPP reader가 제공하는 WBS 코드도 이 XML 경로로 함께 보존하므로 해당 MPP 가져오기 파일도 schema41이 필요할 수 있다. 곡선 데이터만 있을 때의 최소 schema36 계약은 유지한다.

가져오기만으로 누락 코드를 채우지 않는다. GenerateCodes가 켜져 있으면 이후 새 작업·WBS에 코드를 만든다. **미설정 코드 생성 미리보기**는 기존 코드가 없는 객체만 채우며 저장 전 취소할 수 있다. 문자열 수준이나 부모 코드가 없는 하위 항목, 정의된 마스크 수준을 넘는 객체는 미설정으로 남기고 직접 입력한다. 번호 공간 소진·충돌은 작업 추가 전체를 거절한다. 기존 코드는 이동·순서 변경 때 재번호하지 않는다. 현재 코드 목록에서 삭제한 최댓값의 영구 재사용 금지는 보장하지 않는다.

외부 WBS 코드는 안정적인 native 객체 ID에 연결해 저장한다. 작업 삭제 시 현재 코드만 제거하고 undo/redo는 같은 코드와 ID를 복원한다. 원장 ID 이관은 코드 소유 참조를 이관하며, 동결·전체 사본의 계약을 유지한다. 코드와 실제 작업 일정의 계산은 서로 독립적이다.

## 저장·경계

마스크/모든 수준 필수 또는 외부 WBS 코드 설정이 현재·기준선·시나리오·채택·전후 전체 사본에 있을 때 schema41을 사용한다. 기존 파일의 최소 writer는 유지하고 schema40 이하의 새 데이터는 거절한다. 저장/재열기·복구, 원자 편집·읽기 전용·취소/stale·일반 undo/redo와 통화 전환 보존을 검증한다.

XER는 필드의 결합 표시 값을 정적 문자열로 투영하며 XML 마스크·자동 생성 규칙과 별도 외부 WBS 코드는 source.ppm/진단에 보존한다. 외부 서버의 코드 사전 등록, 기존 코드 일괄 재번호, roll-down/외부 계산식 실행·phonetic/정렬 규칙, 실제 Microsoft Project/P6 열기·재계산·재저장은 별도 범위다.

| 계약 | 검증 |
|---|---|
| XMASK-01 | 네 범위·공유 GUID·Mask/AllLevelsRequired XML 재내보내기 |
| XMASK-02 | 숫자/대소문자/Unicode·고정/자유 길이·구분자·계층·잘못된 값 거절 |
| XMASK-03 | 자동/직접 항목 생성·중복/공간 소진·공유 사전과 ID/GUID·기존 선택 유지 |
| XMASK-04 | WBS 설정/코드·새 작업 자동 생성·명시 미설정 채우기·worker/취소/stale·undo/복구 |
| XMASK-05 | schema41/구버전 거절·동결/전후 사본·통화/ID 이관·Windows 화면·독립 XML/보고서 판독 |

구조 근거: Microsoft [OutlineCodes schema](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/xml-schema-for-the-outlinecodes-element?view=project-client-2016), [Length](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/length-element?view=project-client-2016), [WBSMasks schema](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/xml-schema-for-the-wbsmasks-element?view=project-client-2016), [GenerateCodes](https://learn.microsoft.com/en-us/office-project/xml-data-interchange/generatecodes-element?view=project-client-2016). GenerateCodes 페이지 예시의 비표준 boolean4와 중첩 Masks는 재현하지 않고 schema의 boolean 및 직접 WBSMask 구조를 사용한다.

독립 리더 경계: MPXJ16.10.0 strict reader로 원본의 WBS 값과 목록 마스크 구조를 대조한다. 같은 GUID 목록을 공유하면 마지막 참조 필드에만 사전/마스크를 연결하는 제한이 있어 공유 구조·기업 16진 선택 값·원시 마스크 형식·WBS 생성 플래그는 XML/native 별도 검사로 확인한다. 외부 제품에서 생성 규칙이 동일하게 실행됐다는 의미는 아니다.

[전체155/155·파서650개·C++ 형식429파일·독립 XML/MPXJ/XLSX/PDF·Windows UI 검증](validation/xml-code-masks-2026-11-11/README.md)을 통과했다. 첫 전체 실행의 MPP schema 기대값 실패와 수정 후 검증을 모두 보존한다.
