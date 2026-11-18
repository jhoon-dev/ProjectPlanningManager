# 개발 구성과 구현 기록

README에서 옮긴 상세 기능·파일 형식·배포·증분 기록입니다. 날짜별 기록은 당시 구현 범위를 설명하며,
현재의 개발 우선순위는 [로드맵](planning/06-roadmap.md)과 [첫 사용자 과업](planning/20-first-user-weekly-update.md)을 따릅니다.

개발자 **jhoon-dev** · 브랜드 **Toolmoa** · App ID **`app.toolmoa.ppm`** · 표시 이름 **Project Planning Manager**. 앱의 **설정 → Project Planning Manager 정보…**에서 개발자와 브랜드를 확인할 수 있다. 제품군 공통 기준은 `app.toolmoa.{product-id}`이며, 스토어가 별도로 발급한 패키지 ID·Publisher·제품 번호와 구분한다.

## 기존 구현 개요와 당시 상태
프로젝트별 독립 PPM 파일로 새로 만들기·열기·저장·다른 이름 저장과 Windows/macOS 이동 열기를 제공하며, 기본 작업은 계정·서버 없이 수행한다.
사용자 확정 목표는 Microsoft Project와 Oracle Primavera를 대체할 수준의 전문 PM 도구다. 양 제품의 전체 기능을 후보로 유지한다. 개발 단계는 제외 목록이 아니라 개발 순서다.
PPM은 빈 폴더에서 기획을 시작했다. 2026-10-26 요청으로 C++20/Qt Widgets 앱과 파일 관리, 기본 CPM, WBS·표·간트를 구현했다. 기준선 버전 저장, 상태일별 실적 보고 이력, 잔여 예측 및 기준선 대비 변경 비교, 인력·장비 등록/배정·일별 과부하 및 별도 평준화 시나리오 A/B 비교를 추가했다. 유효일 단가·작업 비용·누적 실제원가와 기준선 EVM 분석, 프로젝트 대시보드·일정/자원/원가 보고서와 XLSX/PDF 내보내기도 구현했다. 호환성 및 제품 동등성은 아직 검증되지 않았다.
별도 체크포인트 자동 저장, 비정상 종료 후 세대 선택/사본 복구, 일반 입력의 실행 취소·다시 실행을 추가했다. 승인·제출 이력은 보존하며 복구본이 명시 저장을 대신하지 않는다.
파일명의 대소문자를 보존하고 같은 파일 판정을 별도로 수행한다. 실행 취소는 변경분을 보존하며 일반 이름 변경의 복사 비용을 줄였다. [편집 성능 계약·측정](editing-performance.md)에 1천/1만 작업의 셀 편집·undo/redo 측정과 범위를 기록한다.
상태일 이후 잔여 예측에서도 총/자유여유·임계와 driving 관계를 계산한다. 실제 날짜·기준선을 보존하고 표/간트·기준선 비교·보고서/XLSX/PDF에 같은 의미로 표시한다. [예측 여유·임계 계약](forecast-float.md)에 완료 실적 제외·작업 달력·날짜 제약·마감과 평준화의 구분을 기록한다.
프로젝트 메뉴에서 작업용 텍스트·숫자·날짜·목록 필드와 계층형 작업 코드 사전을 관리한다. 보기 메뉴에서 최대 3단계 그룹·분류 필터/정렬·열 배치를 설정하고 이름 있는 보기로 보존한다. WBS 아래 분류 노드를 선택하면 같은 작업이 표·간트에 표시되며 계산 그래프와 원본 선행 번호는 유지한다. [분류·보기 사용법과 파일 계약](task-classification.md)을 따른다. 진척 정책과 평준화 조합 또는 정책을 동결한 시나리오가 있으면 schema14, 단독 진척 정책 선택이 있으면 schema 13, 현재 또는 동결 입력에 평준화 채택 설정이 있으면 schema 12, 없으면 기간 실적 원장 11 / 분류·보기 10 / 일반 9로 저장한다.
작업 표는 QAbstractTableModel/QTableView를 사용하며 공수 미리보기·역산, 실적 입력과 자원 배정의 계산은 비동기로 수행한다. 툴바 또는 대화상자의 계산 취소를 사용하고, 취소한 대화상자의 초안을 수정해 다시 적용할 수 있다. [모델 전환·비동기 편집 계약과 검증](model-async-editing.md)을 따른다.
대규모 편집에서는 worker 요청의 입력 소유권을 공유하고 dirty와 모델/WBS를 변경 작업 기준으로 갱신한다. [준비·반영 단계 측정과 남은 비용](responsive-editing.md)을 기록하며 복합 변경·최초 계산 실효화의 전체 갱신은 보존한다.

FS·SS·FF·SF 선행관계와 지연/선행 시간, 작업별 근무 달력을 지원한다. 표에서 `1SS+2h,3FF-30m`처럼 입력하고 프로젝트 메뉴에서 달력을 등록·배정한다. [계산·입력·파일 계약](pdm-calendars.md)을 따른다.
선행관계·작업 제약·작업 유형은 내부에서 enum class를 사용한다. 기존 `.ppm` schema1~9의 문자열 표현과 XML/XER 코드 매핑은 유지하며, 이전 writer 파일 13개로 호환성을 검사한다. [타입·변환표·호환 회귀 계약](plan-enums.md)을 따른다.
오류는 안정된 코드·매개값·관련 대상·원인 트리로 전달하고 표시 계층에서 문구와 수정 안내를 만든다. [구조화 진단 계약](structured-diagnostics.md)에 한국어/영문 진단 기반과 관련 작업 선택 범위를 기록한다.
파일 메뉴의 **다중 프로젝트·공유 자원…**에서 여러 문서를 독립 창으로 열고 공유 인력·장비를 명시적으로 연결한다. 계획/현재 예측의 실제 중첩 구간으로 통합 부하를 계산하고, 별도 `.ppmworkspace` 파일에 목록·용량·달력·연결을 저장한다. [작업공간 사용법과 계산 범위](multi-project-workspace.md)를 따른다.
해당 구현의 [검증 기록](validation/workspace-2026-10-28/README.md)에 플랫폼별 48개 회귀·67개 제품 파일 정적 분석·파서 2만 회 퍼징·기존 파일/일정 hash·새 설치 파일 검증과 한계를 보존한다.

프로젝트 메뉴에서 8종 작업 제약·마감일과 일정 충돌을 관리한다. 간트 막대 이동/오른쪽 끝 resize, 자동 재계산과 실행 취소를 지원한다. [제약·마감·간트 편집 사용법](constraints-gantt.md)을 따른다.
공수 기반 일정, 고정 기간·고정 공수·고정 단위, 자원별 달력 교집합과 수동 분할 중단을 지원한다. 프로젝트 메뉴의 공수·유형·분할과 자원 등록의 달력에서 설정한다. [공수·자원 달력·분할 계약](effort-calendars-splits.md)을 따른다.
PRD PERF-01~04에 따라 일정 계산의 단계/진행 표시·취소와 화면 주변 행만 그리는 간트를 추가했다. [통합 검증·성능·계산 취소](performance-integration.md)에 1천/1만 작업 측정·검증 범위와 재현 방법을 기록한다. 앞으로도 PRD에 없는 요구사항은 먼저 반영한 뒤 구현한다.
1만 작업 XER의 실제 가져오기·계산·간트·스크롤/선택 계측과 앱/설치기 아이콘, 컨트롤 높이·배율 검사는 [최신 검증 기록](validation/large-xer-ui-2026-10-30/README.md)을 참고한다. 첫 실행의 긴 지연을 포함한 원시 표본과 한계를 함께 보존한다.
편집 메뉴에서 여러 작업의 삽입·삭제·복제와 원자적 TSV 붙여넣기를 지원한다. 표 위에서 이름/ID 검색·필터·정렬을 조합하며 선행 번호는 항상 프로젝트 원본 행 번호다. [작업 편집 사용법·보호 규칙](task-editing.md)을 따른다.
PM Canvas는 별도 개인용 프로토타입이다. 그 코드·아키텍처·결정을 PPM에 가져오지 않는다.

## 문서 읽는 순서
중심 문서는 제품 PRD다. 기능 추적표·계산 규칙·데이터모델·화면·로드맵은 PRD의 요구사항과 완료조건을 구체화하는 보조 문서다. 서로 충돌하면 임의로 구현하지 않고 PRD와 결정기록에 차이를 반영한다.

1. [제품기획과 PRD](planning/01-product-prd.md)
2. [기능범위와 추적표](planning/02-scope-traceability.md)
3. [개념 데이터모델](planning/03-domain-model.md)
4. [핵심 계산 규칙 초안](planning/04-calculation-contract.md)
5. [화면과 사용자 흐름](planning/05-experience.md)
6. [단계별 로드맵](planning/06-roadmap.md)
7. [결정기록과 질문](planning/07-decisions.md)
8. [자료와 검증 기록](planning/08-evidence-validation.md)
9. [원본 337행과 추적 데이터](planning/09-source-traceability.json)
10. [세부 요구사항 337개 초안](planning/10-detailed-requirements.md)
11. [원본 확인과제 39개](planning/11-open-source-questions.md)
12. [기능군별 상세 계약](planning/12-capability-contracts.md)
13. [데스크톱 기술 대안과 결정 게이트](planning/13-desktop-options.md)
14. [C++ + Qt 기본 아키텍처와 계약](planning/14-cpp-qt-architecture.md)
15. [프로젝트 파일과 전달·복구 계약](planning/15-project-file-contract.md)
16. [제품 기능 보완과 검증 우선순위](planning/16-product-gap-priorities.md)

## 문서 운영
사용자 확정 사항은 CONFIRMED, 설계 제안은 PROPOSED, 미확인 근거는 UNVERIFIED로 구분한다. 이 문서 묶음의 상세 설계는 전부 PROPOSED이며 구현 승인이 아니다.
기능 상태는 후보→요구사항 확정→설계→구현→검증→호환성 검증으로 관리한다. 문서 존재만으로 기능 완료를 표시하지 않는다.
플랫폼은 Windows/macOS 설치형 데스크톱으로 사용자 확정됐다. 최종 목표는 기존 상용 PM 도구를 능가하는 도구이며, Parity와 Superiority를 별도로 검증한다. 사용자 결정 위임에 따라 C++ + Qt, UI 독립 계산 코어, 로컬 저장·자동 복구를 기본 방향으로 선택했다. Qt 세부 모듈/버전·DB·라이선스·사업모델은 미결정이다.

## 개발 시작

Windows 배포는 **Microsoft Store의 MSIX만** 사용한다. EXE/NSIS 설치기와 GitHub Release 설치 파일 배포는 폐기했다. [MSIX·Store 배포 절차](msix-store.md)를 따르며, 개발용 unsigned MSIX는 `build/msix-development`의 CI 검증 전용이다. 최종 업로드 전 패키지는 `build/msix-store-upload`에 만들고, 사용자는 Microsoft Store가 서명·제공하는 패키지를 설치한다.

Microsoft Store용 MSIX는 [제출 준비 절차](store/submission-checklist.md)와 [패키지 계약](msix-store.md)을 따른다. 앱 이름 예약 전에는 내부 CI용 unsigned 개발 패키지만 만들며, 실제 Partner Center ID를 넣은 Store 모드 생성기와 한국어 소개·아이콘·개인정보 처리방침을 준비한다. MSIX판 설정·복구는 패키지 LocalState에 저장하고 일반 제거 시 함께 제거한다. [검증 기록과 실제 화면](validation/msix-2026-10-29/README.md)에 패키지 identity의 설치·업데이트·복구·제거 및 파일 해시를 보관한다. Store 등록·WACK·제출/승인은 아직 완료되지 않았다.

초기 개발 기준은 C++20, CMake 3.24+, Qt 6.8.3 Core/Widgets/Concurrent다. 서버·계정 없이 WBS·기간·FS 선행·휴일을 입력하고 F9로 일정 계산한다. 추적 메뉴에서 기준선을 저장하고 상태일·실적을 보고한 뒤 계획 대비 비교를 연다. 자원 메뉴에서 가용량·배정을 입력하고 부하와 평준화 시나리오를 비교한다. `.ppm` 단일 파일에 계획·자원·기준선·실적·시나리오 입력을 저장한다. 원가 메뉴에서 유효일 단가·고정비·누적 실제원가·물리 진척을 입력하고 기준선 EVM을 분석한다. 보고서 메뉴에서 대시보드/XLSX/PDF, 자동 저장·복구 메뉴에서 체크포인트/사본 복구, 편집 메뉴에서 실행 취소/다시 실행을 사용한다. 고급 평준화·회계/환율 연계는 후속 구현이다.

[빌드·실행·파일 형식·검증 범위](development.md)를 참고한다. GitHub 저장소는 [jhoon-dev/JOBS](https://github.com/jhoon-dev/ProjectPlanningManager)다.
[기준선·실적·비교 사용 예제](baseline-progress.md)에서 첫 추적 흐름을 재현할 수 있다.

[자원·배정·부하·평준화 사용법과 계산 계약](resources-leveling.md)에서 F05와 우선순위 대안을 재현할 수 있다.

[단가·작업 비용·실제원가·EVM 사용법](costs-evm.md)에서 F10·EAC 가정과 집계 범위를 확인할 수 있다.

[대시보드·보고서·엑셀/PDF 내보내기](reports-export.md)에서 필터·집계·출처와 출력 방법을 확인할 수 있다. 보고서 메뉴에서 열며 Excel·Python 설치 없이 출력한다.

[자동 저장·복구·실행 취소 사용법](recovery-undo.md)에서 저장 간격·복구본 선택·사본 저장과 편집 이력의 범위를 확인할 수 있다.

[Project XML 가져오기·내보내기](project-xml.md)는 파일 메뉴에서 실행한다. 작업/WBS·관계·달력·자원/배정의 기간 기반 계획을 변환하고 새 교환 폴더에 원본/현재 PPM 사본과 손실 진단을 함께 보관한다. [MPP 읽기](mpp-import.md)는 별도 메뉴에서 지원하며, 실제 Microsoft Project 재계산/재저장 호환은 별도 검증 범위다.

[Primavera XER 가져오기·내보내기](primavera-xer.md)도 파일 메뉴에서 실행한다. 단일 프로젝트의 기간 기반 작업/WBS·네 관계/지연·지원 제약·달력/휴일·자원/정률 배정을 변환한다. UTF-8/명시 선택 Windows-1252를 지원하며 새 교환 폴더에 원본 또는 현재 PPM 사본과 진단을 보존한다. 실제 P6 계산/재저장 동등성·다중 프로젝트·실적/원가/공수의 완전 교환은 후속 범위다.

[구조·대량 편집 응답성](bulk-editing.md): 작업 추가·삽입·삭제·복제·표 붙여넣기와 해당 undo/redo를 비동기로 준비하고 도구 모음에서 취소할 수 있다. 기본 보기의 연속 구조 변경은 행 단위로 반영한다.

[필터·정렬·WBS 대량 편집](projected-task-views.md)에서도 표시 자료를 비동기로 준비하고 생존 작업 ID를 유지하며 갱신한다. 많이 흩어진 변경은 준비된 자료로 한 번 reset하고, 준비 중 보기/입력이 바뀌면 안전한 경합 처리를 사용한다.

[외부 파일 파서 검증](parser-safety.md): `.ppm`·Project XML·Primavera XER seed 재생과 선택적 Clang/libFuzzer·ASan/UBSan 검사를 제공한다. 기본 앱 실행에는 퍼징 도구가 필요 없다.

[C++ 품질 검사](cpp-quality.md): 고정 clang-format·점진적 포맷 적용과 실제 Qt 컴파일 설정을 사용하는 clang-tidy CI를 제공한다. 편집 권한 가드는 공통화하며 상태일·간트·저장/복구·읽기 전용 사본의 기존 정책을 유지한다.

설정 → 업데이트에서 Microsoft Store의 새 버전을 확인하고 스토어를 열어 업데이트한다. 새 버전은 비차단 배너로 알린다. 개발 실행본과 개발 MSIX는 업데이트 미지원으로 표시하며, Mac App Store판은 App Store로 연결한다. 앱 자체의 EXE 다운로드·설치기 실행·GitHub Release 조회는 제공하지 않는다. [업데이트 동작·배포 운영](application-updates.md)을 따른다.

[편집·간트·입력 화면 개선과 검증](validation/editing-polish-2026-10-30/README.md): 작업 선택의 날짜 자동 이동, 선택 보기(Ctrl+Shift+J)·전체 일정 맞춤(Ctrl+Shift+F), 작은 화면 배치와 주요 편집기 공통 높이·인라인 오류 안내를 제공한다. 1만 작업의 변경 전후 계측과 공개 P6 XER 9개 비교 행렬을 보존하며, 전체 P6 재계산 호환성은 별도로 구분한다.

평준화 후 총/자유여유·임계경로는 자원 슬롯 순서를 고정해 다시 계산한다. 자원 연결은 간트/네트워크에서 원본 관계와 구분하고 계산 불가 사유는 진단에 표시한다. [계산 기준·호환 계약](leveling-float.md).

교차 분할 작업도 실행 창별 자원 인계 순서로 여유·임계를 계산한다. 구간별 작업량은 기존 실행 모델로 다시 배분하고, 간트·네트워크·보고서는 실제 인계 시각을 표시한다. [기준·한도](leveling-float.md).

현재 엔진1.8은 실행 구간 소멸·재생성과 이동 가능 작업128개를 넘는 묶음을 지원한다. 후보 결합을 보존한 독립 탐색 단위에 생성 전 자원 상한·분기 후보 제외/복원·중복 인증 절감을 적용한다. 전체 작업·후보·판정 예산을 넘으면 사유와 함께 미산정으로 표시한다. 총여유는 가능한 작업별 재배치, 자유여유는 다른 날짜 고정 기준이며 기존1.0~1.7 기준선을 보존한다. [계산·한도](leveling-float.md), [효율화·측정](candidate-propagation.md), [비단조 수요 전파](nonmonotone-propagation.md), [추가 비용 판단](propagation-cost.md).

평준화 비교 창의 **A/B 총여유 근거**에서 현재 날짜와 검증된 재배치 날짜를 나란히 확인한다. 함께 이동한 작업·고정 실적·총여유와 자유여유의 차이를 보여 준다. [사용법과 범위](leveling-witness.md).

자원 → **평준화 결정 이력**에서 채택·해제·자동 무효화와 실행 취소/다시 실행의 변경 전후 설정·사유·출처를 확인한다. 결정은 설정을 되돌려도 남으며 저장·사본·복구로 보존한다. [결정 원장](leveling-decisions.md), [미개발 점검과 개발 순서](planning/17-remaining-development.md).

프로젝트/자원 메뉴의 **사용자 필드 정의… / 사용자 필드 값…**에서 텍스트·숫자·날짜·목록을 관리한다. 자원 값은 여러 자원에 한 번에 적용하고 미설정·빈 텍스트·0을 구분한다. [범위·저장·보고 계약](object-custom-fields.md).

자원 → **기간별 계획 배정·자원 곡선…**에서 일·주·월 구간별 배정률과 앞쪽/뒤쪽/중앙 집중 곡선을 편집합니다. 계획/잔여 공수·Usage·부하·원가·평준화가 같은 곡선을 사용합니다. [계약과 지원 범위](assignment-periods.md).

인쇄 창에서 페이지 설정을 문서에 저장하고 표시 범위의 WBS 요약·관계선을 출력한다. 네트워크 창에서는 원본 관계 편집·카드 배치 저장/초기화와 벡터 미리보기/PDF/인쇄를 지원한다. [설정·편집·출력 계약](print-network-extension.md).

2026-11-05: [자재·비용·역할과 통합 일정](resource-plan-integrated-workspace.md)의 schema20/작업공간 schema2 native 확장과 지원 범위를 추가했다.

조직·포트폴리오·협업·위험 분석과 JSON 자동화는 작업공간의 전용 창에서 사용합니다. 로컬 역할 프로필·seed 기반 계획 CPM 분석·PPM-Automation CLI의 [사용법과 지원 범위](organization-risk-automation.md)를 확인하세요.

서버 계정 인증·실시간 협업·HTTP API·주기 실행은 선택 서버와 작업공간의 **서버 인증·실시간 협업** 화면에서 사용합니다. [서버 실행·API·복구 안내](../server/README.md)를 따릅니다. 로컬 파일 사용에는 서버를 요구하지 않습니다.

후속 XER 원본의 통화·인력/장비 단위는 [명시적 전환](xer-source-conversion.md)과 전후 미리보기·기존 재무 병합 검토로 연결한다. 원본·현재 계획·관측 원장을 보존하고 환산비/근거를 schema31 감사 이력에 저장한다.

새 XER 버전의 [일정·WBS·달력 채택](xer-schedule-adoption.md)은 명시 작업 연결·신규 추가, 범위별 전후 검토와 schema32 이력을 제공한다. 현재 로컬 작업·자원/배정·실적/원가·기준선·기간 마감은 보존한다.

## 기능 추가 기록

2026-11-17 후속: [자재 제약 총여유·자유여유·임계](material-float.md)를 지원한다. 계획/잔여·공유 통합에서 입고/재고/소모를 후보와 최대 여유 배치에 검산하고, 통합 표·JSON/XLSX/PDF와 승인 기준선 재현/조건부 schema84로 연결한다.

2026-11-17 후속: [공유 통합 예측의 실제 자재 원장](shared-material-actuals.md)을 지원한다. 프로젝트별 확인 재고·잔여 소모·미입고 잔량을 공유 가용량/교차 관계와 함께 배치하고 통합 잔여 시작·재고 근거·JSON/XLSX/PDF로 연결한다.

2026-11-17 후속: [실제 자재 원장·잔여 자동 대기](actual-materials.md)를 지원한다. 실제 입고/소모 정정·상태일 재고/잔여 소모 확인·입고 잔량 예약·실제 시작 보존·단일 문서 잔여 채택과 조건부 schema83을 연결한다.

2026-11-17 후속: [자재 입고 자동 대기](material-arrival-leveling.md)를 지원한다. 옵션을 켜면 기존 평준화가 자원·선후관계와 시각별 재고를 함께 예약해 작업을 지연한다. 비교/채택·공유 통합·결정 이력·조건부 schema82로 연결한다.

2026-11-17 후속: [시간축 계획 입고·잔여 자재 재고](material-inventory.md)를 지원한다. 개시 재고/시각·입고 편집,계획 소모·최초 부족 조회와 출력,평준화의 시각별 재고 검사·조건부 schema81을 연결한다.

2026-11-16 후속: [공유 풀의 부분·열린 가용기간](shared-resource-availability.md)을 지원한다. 경계/기간별 공급을 잔여량·기간별 부하·공유 평준화와 출력에 적용하며, 새 값이 있는 작업공간은 조건부 schema4로 보존한다.

2026-11-16 후속: [공유 풀 잔여 가용량](shared-remaining-availability.md)을 지원한다. 명시 연결된 여러 프로젝트의 동시 수요를 합산하고 공유 공급에서 차감한다. 누락/파일별 예측 상태일은 미정으로 유지하며 프로젝트별 기여·JSON/XLSX/PDF를 제공한다. 저장 schema는 그대로다.

2026-11-16 후속: [현재 부하를 차감한 잔여 가용량](remaining-resource-availability.md)을 지원한다. 시점 조회·동시간대 전체 배정 수요·음수 잔여/추가 배정 가능·상태일 예측·외부 계산식과 조건부 schema80을 연결한다.

2026-11-16 후속: [부분일·무기한 자원 가용기간](resource-availability.md)을 지원한다. 가용 시작/종료·기간 예외와 분 단위 과부하/평준화·시점 계산식·완전한 XML 기간표·조건부 schema79를 연결한다.

2026-11-15 후속: [외부 계산식 Units·가용률 참조](xml-unit-fields.md)를 지원한다. 배정/자원의 1=100% 수치, 명시 날짜의 가용률 조회·XML 날짜 예외 교환과 조건부 schema78을 연결한다. [검증 기록](validation/xml-unit-fields-2026-11-15/README.md).

2026-11-15 후속: [native 기준선 상세 공수·원가 XML 출력](native-baseline-timephased.md)을 지원한다. 동결 실행 공수 구간·일별 반올림 원가·직접비/자재/경비와 다섯 범위의 표준 시간분포를 연결하고 명시 재채택/병합에는 조건부 schema77을 사용한다. [검증 기록](validation/native-baseline-timephased-2026-11-15/README.md).

2026-11-15 후속: [기존 문서에 외부 XML 기준선 병합](xml-baseline-merge.md)을 지원한다. 번호별 전체 교체·명시 객체 연결/제외·현재/수신 비교·번호별 출처/동결 달력·전후 이력과 조건부 schema76을 연결한다. [검증 기록](validation/xml-baseline-merge-2026-11-15/README.md).

2026-11-15 후속: [외부 XML 기준선 채택·표준 내보내기](xml-baseline-exchange.md)를 지원한다. Baseline0~10의 번호별 검토·명시 값/시간분포 보존·native 승인본 표준 투영과 조건부 schema75·복구/통화/ID 이관·원본 대조를 연결한다. [검증 기록](validation/xml-baseline-exchange-2026-11-15/README.md).

2026-11-15 후속: [번호별 기준선](xml-numbered-baselines.md)의 Baseline0~10(native 승인 v1~v11)과 [요약 기간](xml-summary-durations.md)의 프로젝트/WBS/그룹 실제·잔여·동결 근무 기간을 지원한다. 조건부 schema73/74와 과거 요약 설정의 읽기·저장 시 승격을 연결한다. [검증 기록](validation/numbered-baselines-summary-2026-11-15/README.md).

2026-11-15 후속: [XML SLN·SYD](xml-depreciation-functions.md)의 감가상각·정확한 유리수 계산·소수6자리 최종 반올림과 조건부 schema72를 지원한다. [검증](validation/xml-depreciation-2026-11-15/README.md).

2026-11-14 후속: [XML Partition](xml-partition-function.md)의 수치 구간 분류·짝수 반올림·공백 보존과 조건부 schema71를 지원한다. [검증](validation/xml-partition-2026-11-14/README.md).

2026-11-14 후속: [XML RGB·QBColor](xml-color-functions.md)의 색상 코드·짝수 반올림·16색 팔레트와 조건부 schema70를 지원한다. [검증](validation/xml-colors-2026-11-14/README.md).

2026-11-14 후속: [XML 숫자 표시3개](xml-numeric-display.md)의 FormatNumber/Percent/Currency·정확한 십진 표시·TriState와 조건부 schema69를 지원한다. [검증](validation/xml-numeric-display-2026-11-14/README.md).

2026-11-14 후속: [XML FormatDateTime](xml-format-datetime.md)의 고정 en-US 날짜·시각 표시5종·기준일/자정·옵션 반올림과 조건부 schema68을 지원한다. [검증](validation/xml-date-display-2026-11-14/README.md).

2026-11-14 후속: [XML MonthName·WeekdayName](xml-date-name-functions.md)의 월·요일 이름/약어·주 시작일·반올림과 조건부 schema67을 지원한다. [검증](validation/xml-date-names-2026-11-14/README.md).

2026-11-14 후속: [XML AscW·ChrW](xml-unicode-functions.md)의 Unicode 문자 코드 변환·짝수 반올림·부호 있는16비트 코드와 조건부 schema66을 지원한다. [검증](validation/xml-unicode-2026-11-14/README.md).

2026-11-14: [XML Like 패턴 비교](xml-like-operator.md)의 BMP 정확 비교·와일드카드/문자 목록·범위·부정과 조건부 schema65를 지원한다. 패턴 전체 검증·유한 작업량과 기존 식 호환성을 연결한다.

2026-11-14: [XML 논리 연산](xml-logical-operators.md)의 Eqv·Imp, Boolean/32비트·형식별 Null·우선순위와 조건부 schema64를 지원한다. 기존 식의 문법은 유지한다.

2026-11-14: [XML 산술 연산](xml-arithmetic-operators.md)의 `^`·`\`, 거듭제곱/정수 나눗셈 우선순위·짝수 반올림과 조건부 schema63을 지원한다. 새 연산이 없는 과거 식은 기존 해석을 유지한다.

2026-11-14: [XML Baseline Cost](xml-baseline-cost.md)는 최초 native 기준선의 날짜별 동결 원가를 작업·프로젝트·자원·배정과 그룹/전파에서 조회한다. 활성/비활성 바인딩과 전체 사본은 조건부 schema62다.

2026-11-14: [XML Project 변환](xml-project-conversion-functions.md)의 ProjDateValue/ProjDateConv/ProjDurConv·ISO/NA·고정 날짜/기간 표시와 조건부 schema61을 지원한다.

2026-11-14: [XML 형 변환 함수](xml-conversion-functions.md)의 CDec/CSng/CVar·native 고정점·정확한 binary32 변환·형식/기간 보존과 조건부 schema60을 지원한다.

2026-11-14: [XML 문자열 함수](xml-string-functions.md)의 Replace/InStrRev/StrReverse·BMP 위치/ASCII 비교·시작 이후 치환·끝 경계 역검색과 조건부 schema59를 지원한다.

2026-11-13: [XML 추가 수학 함수](xml-math-functions.md)의 Atn/Cos/Exp/Log/Sin/Tan·라디안/자연로그·근사 계산/소수6자리·조건부 schema58을 지원한다.

**XML 계산식 Str·Val·Hex·Oct**를 지원합니다. 숫자 접두부/지수·고정 소수6자리·형식 접미사 지원 부분과32비트 진수·선행 부호 공백을 연결합니다. 활성 식/전체 사본은 조건부 schema57을 사용합니다. [지원 범위](xml-number-text.md).

**XML 계산식 TimeSerial·TimeValue**를 지원합니다. 16비트 인수·짝수 반올림·넘침/음수 OLE 규칙과24시간/영문 AM·PM/ISO 입력·소수6자리를 연결합니다. 결과는1899-12-30 기준 Date이며 새 활성 식은 조건부 schema56입니다. [지원 범위](xml-time-functions.md).

**XML 계산식 날짜 함수 확장**을 지원합니다. DatePart·Weekday와 DateAdd/DateDiff의 연·분기·월·주 간격, 월말/윤년·주 시작일·마이크로초를 연결합니다. 새 기능 저장은 조건부 schema55이며 기존3인수 날짜 식은 계속 읽습니다. [지원 범위](xml-date-functions.md).

**XML 계산식 Format·StrConv**를 지원합니다. 숫자·날짜·문자열 표시와 고정 Unicode16.0 대소문자·전각/반각·가나 변환을 실행합니다. Format은 en-US, PPMFormat은 명시 LCID1033/1041/1042이며 활성 식은 조건부 schema54를 사용합니다. [지원 범위](xml-format-strconv.md).

**XML 계산식 시간대 변환**을 지원합니다. PPM 확장 함수로 명시 오프셋·고정 IANA2026f의597개 시간대를 변환하고, 누락 시각 거절·중복 시각 선택·마이크로초와 오프셋 포함 문자열을 보존합니다. 기존 일정/실적을 자동 보정하지 않으며 활성 식은 조건부 schema53을 사용합니다. [지원 범위](xml-formula-timezones.md).

**XML 계산식 추가 함수13개**를 지원합니다. Choose·InStr·StrComp·문자열 정리/반복·문자 코드·CByte/CCur·DateValue·Sqr를 네 범위의 계산·미리보기·XML/XER·보고서에 연결합니다. 활성 새 함수는 조건부 schema52를 사용하며 기존 함수와 과거 비활성 식은 이전 저장 계약을 유지합니다. [지원 범위](xml-additional-functions.md).

**XML 다일 실적 비례 배분**을 지원합니다. 구간 전체 총량을 확인한 뒤 경과분 또는 작업·자원 공통 근무분으로 일별 공수/원가를 배분합니다. 정수 총량·기존 실적·누계 대사·추정 기원과 후속 정정을 보존하고 검토 기록/감사/표준 실적 XML을 연결합니다. 기존 실적 채택 화면의 `다일 구간 비례 배분…`에서 사용할 수 있습니다. [지원 범위](xml-multiday-proportional-actuals.md).

**공수 기반 작업의 XML 기간 진척 병합**을 지원합니다. 명시 잔여 공수를 검토해 실제/잔여 기간 보고와 함께 기록하고, 예측 기간은 현재 달력·곡선·배정률로 다시 계산합니다. 원래 계획·실제 공수/원가·정정 이력은 유지합니다. [지원 범위](xml-work-driven-progress.md).

**표준 실적 XML 내보내기**를 지원합니다. 현재 상태일의 작업 진척·누계 원가와 배정별 일별 실제 공수/원가를 표준 요소로 출력합니다. 0/미보고·누계/배분 대사·전체 native 이력 보존과 미리보기/새 폴더 게시를 연결합니다. 파일 메뉴의 `표준 실적 포함 Project XML 내보내기…`에서 사용할 수 있습니다. [지원 범위](xml-standard-actual-export.md).

**XML 일별 실적의 여러 구간 합산**을 지원합니다. 같은 배정·날짜에서 겹치지 않는 공수/원가 구간을 정확히 합산하고 검토 화면에 구간 수를 표시합니다. 겹침·중복·오류 일자·합계 한도를 검사하며 기존 정정/누계/마감·저장/복구를 유지합니다. [지원 범위](xml-daily-actual-fragments.md).

**XML 배정·기간별 실제 공수·원가 채택**을 지원합니다. 일별 실제 값을 native 배정에 직접 연결하고 현재 관측·작업 누계 AC와 대사해 선택 반영합니다. 정확한 단위·0/미보고, 기존 값 보존·정정/중복·마감/취소 검사와 기존 Usage·저장/복구·감사 보고를 연결합니다. [지원 범위](xml-period-actual-adoption.md).

**XML 작업 진척의 기존 문서 병합**을 지원합니다. UID→native 작업 ID를 직접 연결하고 현재/수신 보고를 비교해 선택한 행만 정정 보고로 추가합니다. 동일 출처 중복·후속 정정·마감 기간·취소/변경 검사를 거쳐 한 번에 반영합니다. [지원 범위](xml-progress-merge.md).

**XML 작업 진척의 새 사본 원장 채택**을 지원합니다. XML 가져오기에서 작업 UID 연결·실제 날짜·명시 실제/잔여 기간을 선택 검토하고 새 사본에만 기록합니다. 불완전 값 비추정·원본 재대조·기존 원장/후속 정정 보존과 보고서·기존 저장/복구를 연결합니다. [지원 범위](xml-progress-adoption.md) · [검증 기록](validation/xml-progress-adoption-2026-11-12/README.md).

**XML 계산식의 Early/Late Start·Finish 참조**를 지원합니다. 현재 계획 배치와 검증된 후진 날짜, 예측의 미완료 잔여 구간·완료 제외·미산정 Late의 사유를 schema51·미리보기/보고서·계획 기준 교환에 연결합니다. [지원 범위](xml-early-late-references.md) · [검증 기록](validation/xml-early-late-references-2026-11-12/README.md).

**XML 계산식의 Baseline Start·Finish·Duration·Work 참조**를 지원합니다. 최초 native 기준선(v1)의 동결 일정·달력·곡선·배정 공수를 현재 계획과 독립적으로 조회하고, 새 객체의 미설정과 schema50·미리보기/보고서·교환에 연결합니다. [지원 범위](xml-baseline-references.md) · [검증 기록](validation/xml-baseline-references-2026-11-12/README.md).

**XML 계산식의 Remaining Duration·Remaining Work 참조**를 지원합니다. 현재 상태일 예측의 작업별 잔여 기간과 작업·자원·배정·프로젝트 잔여 공수를 달력/곡선·선택 정책과 함께 계산하고 schema49·미리보기/보고서·교환에 연결합니다. [지원 범위](xml-remaining-references.md) · [검증 기록](validation/xml-remaining-references-2026-11-12/README.md).

**XML 계산식의 Actual Duration·Actual Work 참조**를 지원합니다. 작업의 보고 실제 기간과 작업·자원·배정·프로젝트의 날짜별 보고 공수 합계, 상태일/정정·0/미보고 구분과 schema48·미리보기/출력을 연결합니다. [지원 범위](xml-actual-quantities.md) · [검증 기록](validation/xml-actual-quantities-2026-11-12/README.md).

**XML 계산식의 Actual Start·Actual Finish 참조**를 지원합니다. 상태일 기준 최신 실적/정정, 미설정·전체 완료 요약, 배정의 소유 작업 참조와 schema47·보고서/교환을 연결합니다. [지원 계약](xml-actual-references.md) · [검증 기록](validation/xml-actual-references-2026-11-11/README.md).

**XML 계산식의 근무 달력 함수**를 지원합니다. ProjDateAdd/Sub/Diff로 휴게·주말·휴일을 반영하고, 객체별/지정 달력과 XML 프로젝트 달력·미리보기·보고서·schema46을 연결합니다. [지원 계약](xml-calendar-functions.md) · [검증 기록](validation/xml-calendar-functions-2026-11-11/README.md).

**XML 계산식의 계산된 Start·Finish 참조**를 지원합니다. 계획·상태일 예측과 WBS/그룹 요약 날짜, 백그라운드 미리보기·보고서·조건부 schema45를 연결합니다. [지원 계약](xml-schedule-references.md) · [검증 기록](validation/xml-schedule-references-2026-11-11/README.md).

**XML 필드의 외부 계산식 실행**을 지원합니다. 지원 연산·함수와 안정 ID 참조로 네 범위의 값을 재계산하고, 요약 식·배정 전파·편집/미리보기·보고서·schema44 보존에 연결합니다. [지원 범위와 제한](xml-external-formulas.md) · [전체161/161·최종집중21/21 검증](validation/xml-formulas-2026-11-11/README.md).

**XML 필드의 요약 roll-up**을 지원합니다. WBS·프로젝트 요약과 작업/자원 그룹의 합계·평균·개수·최대/최소·Flag OR/AND를 계산하고, 원본 명시 요약 값과 규칙을 schema43에 보존합니다. [지원 범위](xml-roll-up.md) · [159/159 검증](validation/xml-roll-up-2026-11-11/README.md).

**XML 필드의 배정 roll-down**을 지원합니다. 작업·자원의 사용자 필드 값을 배정에 자동 전달하고 직접 입력·명시 미설정을 우선합니다. 설정/영향 미리보기와 원본 값 복원·보고서·schema42 저장/복구에 연결합니다. [지원 범위](xml-roll-down.md) · [157/157 검증](validation/xml-roll-down-2026-11-11/README.md).

**XML 필드 마스크·자동 코드 생성**을 지원합니다. 네 범위의 GUID 코드 목록에 수준별 형식·길이·구분자를 적용하고 코드를 생성하며, XML WBS 마스크와 새 작업의 자동 코드도 편집·저장합니다. [지원 범위](xml-code-masks.md) · [155/155 검증](validation/xml-code-masks-2026-11-11/README.md).

**XML Start·Finish와 Date 시각 필드**를 지원합니다. 네 범위의 일시·자정/미설정·소수 초와 목록/GUID를 보존하고 편집·보고서·schema40 저장/복구에 연결합니다. [지원 범위](xml-date-time-fields.md) · [153/153 검증](validation/xml-date-time-fields-2026-11-10/README.md).

**XML Flag·Cost·Duration 필드**를 지원합니다. 네 객체 범위의 참/거짓·금액·기간과 목록/GUID 참조를 읽고 쓰며 편집·보고서와 통화 전환에 연결합니다. [지원 범위](xml-additional-field-types.md) · [151/151 재검증](validation/xml-field-types-2026-11-10/README.md).

**XML 값 목록·GUID 참조**를 지원합니다. 네 범위의 단일/다중 선택·계층·기본값을 보존하고 작업·프로젝트·자원·배정 값 편집과 저장/복구·보고서에 연결합니다. [지원 범위](xml-lookup-references.md) · [149/149 검증](validation/xml-lookup-references-2026-11-10/README.md).

**Project XML 기업·프로젝트·배정 사용자 필드**의 텍스트·숫자·날짜와 외부 ID/GUID를 교환합니다. **프로젝트 → 배정 사용자 필드 정의… / 배정 사용자 필드 값…**에서 배정별 값을 편집하고 보고서에 출력합니다. XER의 배정 UDF도 읽고 씁니다. [지원 범위와 저장 계약](enterprise-assignment-udf.md) · [전체147/147 검증](validation/enterprise-assignment-udf-2026-11-10/README.md).

**파일 → Microsoft Project MPP 읽기…**에서 MPP의 수동 정규 계획 곡선·무공수 구간을 읽을 수 있습니다. 미리보기에 채택/제외와 분 단위 투영 차이를 표시합니다. [기능 계약](mpp-manual-curves.md) · [전체 회귀 검증](validation/mpp-manual-curves-2026-11-09/README.md).

**자원 → 자원 생산성·외부 고유 곡선…**에서 물량/시간당 생산성으로 계획 공수를 검산하고 MS 기본·사용자21지점 곡선을 적용합니다. XML 시간대별 계획 공수와 XER 원본 곡선의 읽기/쓰기를 지원합니다. [기능·저장 계약](resource-productivity-curves.md) · [전체 회귀 검증](validation/resource-productivity-curves-2026-11-09/README.md).

**자원 → 날짜별 잔여 공수…**에서 FixedWork/FixedUnits의 날짜 고정 잔여 목표를 입력합니다. 공수 기준은 잔여 총공수와 날짜 목표를 함께 유지하며 완료를 역산하고, 기간 기준은 잔여 기간을 유지합니다. 미래 날짜 추가·검산 미리보기·undo와 평준화/Usage·저장/복구를 지원합니다. [계산·저장 계약](dated-effort-targets.md) · [전체 회귀 검증](validation/dated-effort-2026-11-08/README.md).

**원가 → 프로젝트 전체 통화 전환…**에서 환율·기준일·근거를 지정하고 현재 단가/원장·기준선·시나리오·기간 마감·외부 재무 금액을 검토해 함께 환산합니다. 미정값과 작성 정보를 유지하며 전환 전후 원본과 이력을 보존합니다. [환산·저장 계약](project-currency-conversion.md) · [전체 회귀 검증](validation/project-currency-2026-11-08/README.md).

**파일 → Microsoft Project MPP 읽기…**에서 MPP8/9/12/14의 지원 계획을 미리 보고 새 사본으로 엽니다. 원본 MPP·진단을 동반 보관하며 실적/원가·기준선은 원본에 남깁니다. Java/Python 설치 없이 동작하는 별도 reader를 동반합니다. [읽기 계약](mpp-import.md) · [검증 기록](validation/mpp-import-2026-11-08/README.md).

**파일 → 관측 원장 ID 이관…**에서 작업·자원의 새 ID를 명시하고 원장·기준선·시나리오·마감/XER 참조를 이관합니다. 기록 수치와 정정/작성 정보 및 이관 전후 원본을 보존합니다. [이관 계약](ledger-id-migration.md) · [검증 기록](validation/ledger-id-migration-2026-11-08/README.md).

**원가 → 사용자 정의 기간·기간 마감…**에서 기준선별 사용자 기간과 작업별 기간 PV/EV를 지정하고 검토·마감·재개방할 수 있습니다. 마감 당시 revision의 원가/EVM 근거를 고정하고 이전 날짜 실적 정정을 잠급니다. [사용법과 계산·저장 계약](custom-financial-periods.md) · [전체 회귀134/134 검증](validation/custom-financial-periods-2026-11-08/README.md).

**원가 → 외부 XER EV/PV native 채택…**에서 기준선·작업·재무기간과 개시 누계를 확인해 EV/PV를 각각 채택하거나 철회합니다. 현재/채택 EVM을 미리 보고 제출하며 AC·물리 진척·최초 원본을 보존합니다. [채택 계약](external-xer-evm-adoption.md) · [전체 회귀132/132 검증](validation/external-xer-evm-2026-11-08/README.md).

**원가 → native 기간 EVM…**에서 승인 기준선과 보고 원장의 일·주·월별 PV/EV/AC 증감, CV/SV·CPI/SPI와 날짜 배분 대사를 조회하고 XLSX/PDF로 출력합니다. 개시 미보고는 미정으로 유지하고, 마감 EAC/ETC는 누계 기준으로 계산합니다. [기간 EVM 계약](native-period-evm.md) · [전체 회귀130/130 검증](validation/native-period-evm-2026-11-08/README.md).

**파일 → 새 XER 버전·삭제 ID 재연결…**에서 새 원본의 작업/자원 ID와 삭제 후 다시 만든 현재 ID를 연결합니다. 연결표와 재무 병합을 검토해 후속 원본을 등록하며 기존 계획·원장·누계 AC와 최초 원본은 보존합니다. [재연결 계약](xer-source-relink.md) · [전체 회귀 128/128 검증](validation/xer-relink-2026-11-08/README.md).

XER 원본 집계와 현재 날짜 원장을 **파일 → XER 원본·현재 원장 병합…**에서 대조·병합합니다. 현재 원장과 누계 AC를 유지하며 미배분 잔량의 날짜 명세를 연결하고, 겹친 기간·정규/초과근무 구분은 검토 후 제출합니다. 원본과 병합 이력을 보존하고 기존 집계 편집·배분을 이어서 사용할 수 있습니다. [병합 계약](xer-ledger-merge.md) · [편집·배분 계약](xer-aggregate-editing.md).

XER 가져오기/내보내기는 상태일·실제 날짜·물리 진척, 누계 실제원가·유효 단가와 일 단위 재무기간 실적을 지원합니다. 미리보기에서 범위와 누계/배분 원가를 확인하며 원장 이력은 동반 원본에 보존합니다. [상세 교환 계약](xer-actual-cost-exchange.md).

고정 공수·고정 단위 작업에도 상대 자원 곡선을 적용하고 기간·공수를 역산합니다. 곡선 창에서 계산 기간·공수를 미리 보고 문서에 적용할 수 있습니다. [유형별 유지값·사용법](work-contour-inverse.md) · [잔여 항목 재점검](planning/19-work-contour-remaining-review.md) · [검증](validation/work-contour-inverse-2026-11-06/README.md).

날짜 고정 잔여 목표가 있는 평준화에도 총여유·자유여유·임계와 작업별 증인을 제공합니다. XML/XER 가져오기·내보내기에서는 사용자 필드의 형식별 매핑을 미리 보고 변환할 수 있습니다. [날짜 고정 자원 여유](dated-resource-float.md) · [외부 UDF 매핑](external-udf-mapping.md) · [검증](validation/dated-float-udf-2026-11-06/README.md).

**자원 → 자원 그룹·필터 · 저장된 보기…**에서 자원 필드로 그룹·필터·정렬하고 보기를 저장하거나 XLSX/PDF로 출력한다. **자원 → 날짜별 잔여 공수…**에서 고정 기간 배정의 날짜별 목표를 검산 후 적용한다. 작업·프로젝트·자원 숫자 필드 정의에는 단위와 참조를 검사하는 계산식을 지정할 수 있다. [자원 보기](resource-views.md) · [잔여 공수 입력](dated-remaining-work.md) · [계산식](formula-fields.md) · [검증 기록](validation/professional-fields-2026-11-06/README.md).

**추적 → 진척 정책 비교…** 에서 기존 잔여 논리 유지와 진행 중 수신 관계 무시의 날짜·여유·임계 및 비활성 관계를 비교하고 JSON으로 저장한다. 담당자·사유를 남겨 A/B 정책을 현재 예측에 채택하거나 해제할 수 있다. 선택은 저장·재열기와 실행 취소를 지원한다. [진척 정책+평준화 사용법](progress-policy-leveling.md) · [채택·저장 계약](progress-policy-adoption.md) · [비교 계약](progress-policy-comparison.md) · [실제 P6 왕복 절차와 미실행 사유](p6-roundtrip.md).

여러 프로젝트가 들어 있는 XER는 **파일 → Primavera XER 가져오기**에서 프로젝트를 검색·선택해 독립 사본으로 변환한다. 선택 프로젝트와 연결된 외부 선행·후행은 차단하고, 원본 전체와 선택/제외 기록을 보존한다. [선택 가져오기 계약](xer-project-selection.md).

보기 메뉴의 **네트워크 다이어그램…** 에서 선택 작업의 선행·후행과 driving 관계를 탐색한다. FS/SS/FF/SF·지연, 임계·실제 날짜·구조상 순환을 표시하고 전체 직접 관계 목록과 표·간트 이동을 제공한다. [사용법과 계산·표시 계약](network-diagram.md).

파일 메뉴의 **일정표·간트 페이지 설정 / 인쇄… (Ctrl+P)** 에서 표시/선택 작업의 표와 간트를 미리 보고 동일 배치 PDF 또는 프린터로 출력한다. A4/A3·방향·여백·표 글자·기간 나눔, 반복 열·긴 행 이어짐·기준선/상태일·출처를 제공한다. [사용법과 출력 계약](schedule-printing.md).

평준화 결과를 계획/잔여 일정으로 채택·해제하고 표·간트·Usage·원가·보고서에 연결하는 흐름은 [채택 계약](leveling-adoption.md)을 따른다. 일정 입력 변경 시 해제하며 채택 후 여유·임계는 [자원 슬롯 순서 고정 기준](leveling-float.md)으로 계산한다.

기간별 실제공수·실제원가를 날짜/자원별로 제출·정정하고 누적 실제원가와 배분/미배분을 대사한다. [입력 방법과 원장 계약](period-actuals.md)을 따른다.

작업·자원 시간축 Usage에서 계획·승인 기준선·잔여 공수/원가를 일·주·월로 조회하고 XLSX/PDF로 내보낸다. 실제원가는 기간 미배분 작업 누계로 표시한다. [사용법과 계산 범위](timephased-usage.md)를 따른다.
