# XML 기존 문서 진척 병합 검증

2026-11-12 · [XPM-01~06 계약](../../xml-progress-merge.md).

## 범위

- 원본 UID→별도 native ID 수동 매핑, 기존 1건→선택 병합 5건→현장 정정/다른 출처 재검토 10건의 불변 연결을 검사한다. 미선택 유지·중복 UID/대상·없는 ID·불완전 값·변경된 원본/상태일·확인 누락을 거절한다.
- 한 batch/revision·기존 계획/상태일 유지, 재시도 무변경·후속 정정 보호·ID 이관 후 중복 대사, 마지막 행 오류의 전체 거절·worker 취소·stale/단일 사용과 재무기간 마감을 검사한다.
- native schema48 저장/재읽기·복구·전체 감사 보고서, UI 수동 연결/현재·수신 비교·미선택 기본/취소/검토 중 변경을 검사한다. 지원 상한51과 기존 사본 채택은 유지한다.
- 독립 Python 검사기는 원본 XML/SHA-256과 native 실제 날짜/기간·정정 체인·불변 prefix를 대조하고 XLSX 감사 원장·PDF 출처를 판독한다. 기존 계획과 상태일이 유지되는지도 비교한다.

## 결과

최종 전체 **179/179·271.78초**, 실패/건너뜀0이다. 로컬 HTTP/WebSocket 통합 검사를 포함한다. 파서 시드 **808개**와 C++ 형식 **482파일** 차이0을 통과했다. [빌드](build.txt), [CTest](ctest.txt), [JUnit](ctest.xml), [형식](quality.json), [독립 대조](independent-reports.json), [해시/결과](verification-summary.json).

전체 회귀 후 검토 안내 문구를 정리하고 새 사본 채택/기존 문서 병합 기능·UI [집중 4/4](focused.txt)를 최종 실행 파일로 다시 확인했다.

Windows 실행 패키지 **291파일**을 만들고 Qt/MinGW가 없는 PATH에서 CLI로 병합·정정된 schema48 native 원본을 읽어 종료0을 확인했다. 실행 파일/CLI 해시는 최종 빌드와 같다. [패키지](runtime.json), [배포](runtime-deploy.txt), [읽기](runtime-inspect.json).

[원본 XML](original.xml), [병합 전](before.json), [첫 병합](merged.json), [후속 정정](corrected.json), [native 파일](source.ppm), [감사 XLSX](audit.xlsx), [감사 PDF](audit.pdf), [현재/수신 비교 화면](merge-conflict-small.png), [미선택 검토 화면](merge-small.png).

`merge-small.png`는 800×650 크기 검토 화면이다. 원본 파일이 내장되는 기능이 아니며 원본 XML을 함께 보관해야 한다. 실제 MS Project/P6 실행·재계산·재저장과 배정/기간별 공수·원가 채택은 이번 검증에 포함하지 않는다.
