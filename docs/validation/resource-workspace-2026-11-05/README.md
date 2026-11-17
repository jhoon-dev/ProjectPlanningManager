# 자재·비용·역할과 통합 일정 로컬 검증

2026-11-05 / Windows / Qt6.8.3 / MinGW13.1 / Release. [지원 범위와 저장·계산 계약](../../resource-plan-integrated-workspace.md). 이전 미커밋 구현을 보존한 체크아웃에서 수행했다.

## 최종 결과

- **전체 CTest108/108 통과**, 366.02초. [전체 결과](ctest.log), [검사별 로그](ctest-details.log). 검사 시작 이후 제품 소스와 테스트를 변경하지 않았다.
- **파서194 seeds**, schema1..20·작업공간2를 포함해 통과했다.
- **clang-format18.1.8: 233파일 통과**, 형식 변경0. [형식 기록](format.json). [최종 빌드](build.log)에 compiler 경고/오류가 없다.
- **독립 판독10종 통과**. [결과 목록](independent-readers.json)과 각 판독 로그를 보존한다. 새 기능 외에 기존 보고서/XML/진척 정책·평준화/결정 원장/객체 필드/기간 배정/인쇄·네트워크/활동 출력을 다시 읽었다.
- [실행 파일과 산출물 식별값](source-manifest.json), [요약 결과](results.json). 기존 미커밋 작업은 그대로 보존하며 commit/push는 수행하지 않았다.

## 검증 범위

새 계약 검사는 자재 가격의 정수 half-up, 수량 1.5 × 단가199 =299, 비용101을 더한 계획 예산400, 첫날350/둘째 날50의 보존을 확인한다. 단가를399로 바꾸면 현재 자재비599가 되고 기준선 BAC400은 그대로다. 시작 시점 미정 자재 원가가 다음 날의 알려진 비용50을 오염시키지 않는다. 큰 합산 수량 표시, 재고 미정/부족과 논리 일정·평준화의 구분, 원가 overflow의 미정도 검사한다.

역할을 구체 자원에 연결할 때 native 배정이 한 건만 생성되고, 정의 재저장이 기간 구간과 revision을 보존하는지 확인한다. 미연결 역할은 미완성, 종류 불일치·중복·잘못된 참조/숫자는 부분 적용하지 않는다. 작업 복제/삭제의 소모·비용·역할 참조 재발급과 정리, undo/redo, 읽기 전용, 변경된 revision, 기준선/시나리오·사본·복구 및 schema20/구 schema 거절을 확인한다. 새 평준화 우선순위의 독립 날짜 기대값·원본 불변·채택/undo/저장도 검사한다.

통합 계약은 두 프로젝트가 같은 작업 ID `A`를 사용하도록 구성했다. FS/SS/FF/SF와 signed lag의 독립 기대값, 프로젝트별 시작 기준, 빈 프로젝트가 통합 완료를 연장하지 않는지, 닫힌/누락 프로젝트와 삭제 작업, 중복 링크/전역 순환, 동일 상태일·달력 호환 검사, 계산 취소를 확인한다. 실제 완료 끝점 정책으로 역사 관계를 제외해도 원본 순환을 숨길 수 없으며 실제 날짜/완료일을 보존한다.

같은 공유 시공조의 두 작업을 월요일/화요일로 직렬 배치하여 최대 부하100%, 과부하0을 확인한다. 같은 작업의 여러 로컬 자원을 같은 pool에 연결할 때 배정 수요를 합산하며, 이전 native duration 프로필도 함께 검사한다. 공유 부하는 통합 날짜로 계산한다. 원본 메모리와 파일이 바뀌지 않고 결과의 revision/qualified 작업 ID를 보존한다. 작업공간 schema2의 상대 경로 이동, 링크와 통합 옵션 저장, XER 패키지의 native 링크 보존과 한쪽만 선택한 내보내기 거절을 확인한다.

## 독립 판독과 화면

[독립 native/XLSX/PDF 검사](verify-resource-workspace.json)는 checksum, schema20/작업공간2, 정확한 소모비599와 동결 예산400, qualified `P/A`와 `Q/A`, Q의 로컬 시작 월요일과 통합 시작 화요일, 공유 부하의 과부하0, 워크북의 정수/문자열/날짜와 PDF의 한글·실제 내용을 확인한다. openpyxl3.1.5/pypdf6.10.0은 검증 도구이며 앱 의존성이 아니다.

[자재 편집 화면](resource-plan-ui.png), [통합 일정 화면](integrated-workspace-ui.png)은 Qt offscreen으로 실행했다. 편집 저장/취소/undo/read-only/stale와 링크 적용·계산·변경 후 결과/내보내기 비활성화를 검사했다. 작은 화면의 편집·내보내기 버튼을 여러 행에 배치했고 표는 자체 스크롤을 제공한다.

[자재·원가·역할 PDF 명세](material-report.png), [통합 PDF](integrated-report.png)를 Poppler로 렌더링하여 한글·수량·단가·금액·날짜·원본 revision과 범위 설명이 읽히는지 확인했다. [원본 보고서 PDF](resource-plan.pdf), [원본 XLSX](resource-plan.xlsx), [통합 PDF](integrated-workspace.pdf), [통합 XLSX](integrated-workspace.xlsx), [typed JSON](integrated-workspace.json), [프로젝트](resource-plan.ppm), [작업공간](integrated.ppmworkspace)과 상대 참조 프로젝트를 보존했다.

## 재현과 한계

```powershell
cmake --build build-decisions --parallel 8
ctest --test-dir build-decisions --output-on-failure
python scripts/verify-resource-workspace.py build-decisions
python scripts/check-cpp-quality.py format --tool .deps/cpp-quality/Lib/site-packages/clang_format/data/bin/clang-format.exe
```

새 판독과 산출물 보존을 CI에 추가했다. 원격 CI, 다른 OS, 실제 P6/MSP 재계산·재저장, 실제 프린터는 이 로컬 검사의 실행 범위가 아니다. 자재 입고·초과근무·비용 최적화와 여러 원본 프로젝트 파일에 통합 날짜를 일괄 채택하는 기능은 후속이다. 프로젝트 간 링크는 native 작업공간에 보존하며 XER 외부 관계로 투영하지 않는다.

제품 소스 SHA256: `09b7a11687f4d56bb09dfb48e3752420de4731c6477b2d7f790c07bdbea0c4b4` (src/CMake 259개). [전체 검사 소스 목록](full-suite-source.json), [빌드 식별 헤더](BuildIdentity.h). `+dirty`는 기존 미커밋 작업과 이번 구현을 포함한다.
