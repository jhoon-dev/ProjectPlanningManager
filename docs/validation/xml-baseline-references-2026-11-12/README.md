# XML 최초 기준선 참조 검증

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-baseline-references-2026-11-12)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

2026-11-12 · XMLB-01~05 · Windows / Qt6.8.3 MinGW13.1 Release.

[지원 계약](../../xml-baseline-references.md), [C++ 검사](../../../tests/XmlBaselineTests.cpp), [fixture](../../../tests/XmlBaselineFixture.h), [독립 검사기](../../../scripts/verify-xml-baseline-references.py).

## 결과

최종 **173개 검사를 확인**했다. 전체 실행은171/173·306.03초이며 서버 UI·HTTP 연동2개가 제한 환경에서 시간 초과되었다. 코드 변경 없이 샌드박스 밖에서 해당2개를 재실행해 **2/2·47.70초** 통과했다. 최초 실패 로그를 유지하며 두 실행을 단일 전체 통과 기록으로 합치지 않는다. 파서 시드 **788개(schema1~50)**와 C++ 형식 **467파일** 차이0을 통과했다. [빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-baseline-references-2026-11-12/build.txt), [전체 CTest](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-baseline-references-2026-11-12/ctest.txt), [JUnit](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-baseline-references-2026-11-12/ctest.xml), [서버 재검증](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-baseline-references-2026-11-12/server-recheck.txt), [서버 재검증 JUnit](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-baseline-references-2026-11-12/server-recheck.xml), [형식](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-baseline-references-2026-11-12/quality.json), [보고서 대조](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-baseline-references-2026-11-12/independent-reports.json), [리더 대조](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-baseline-references-2026-11-12/independent-reader.json).

독립 Python은 최초 기준선의 raw 입력·일정만 읽고 civil minute별 동결 작업/자원 달력·수동 중단·배정 곡선을 교차 적분한다. 고정 공수 마지막1자원분의 배분도 직접 계산한다. XML **77칸(미설정19칸)**·수식17개·XER 숫자32값·보고서77칸, XLSX26시트/PDF20페이지를 대조했다. MPXJ strict reader는 작업8행·배정4개·판독 오류0이다.

검증 값은 동결 작업 A의 기간8분·공수5.64분, 고정 공수 B의 기간6분·공수5.01분, 프로젝트10.65분과 자원3/7.65/0분이다. 동결 A의 시작은11월12일09:00·완료는11월13일09:01이다. 현재 프로젝트는11월16일 시작·다른 달력/기간/배정률을 사용하고 v2 기준선도 존재하지만 결과는 v1을 유지한다.

Windows800×800 미리보기 화면의 수식/참조 삽입·동결 출처(BASE-1)·소수 값·미설정 사유를 확인했다. 기본 표는 현재 일정 계산 없이도 동결 값을 표시한다.

실행 패키지291파일의 PPM/CLI 해시가 최종 빌드와 일치한다. Qt·MinGW 경로가 없는 PATH에서 CLI가 schema50 원본을 읽고 종료0을 반환했다. [실행 패키지 기록](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-baseline-references-2026-11-12/runtime.json), [배포 로그](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-baseline-references-2026-11-12/runtime-deploy.txt), [CLI 읽기](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-baseline-references-2026-11-12/runtime-inspect.json).

[화면](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-baseline-references-2026-11-12/baseline-small.png), [XML](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-baseline-references-2026-11-12/baseline.xml), [XER](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-baseline-references-2026-11-12/baseline.xer), [입력](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-baseline-references-2026-11-12/native.json), [원본](../../../tests/fixtures/native-history/xml-baseline-references-2026-11-12/source.ppm), [매핑/진단](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-baseline-references-2026-11-12/mapping.json), [XLSX](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-baseline-references-2026-11-12/fields.xlsx), [자원 XLSX](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-baseline-references-2026-11-12/resources.xlsx), [PDF](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-baseline-references-2026-11-12/fields.pdf). [verification-summary.json](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-baseline-references-2026-11-12/verification-summary.json)에 소스와 자료 SHA-256을 기록한다.

## 검사 범위

네 범위와 Task/Resource/Project 소유 참조, 소수 공수/형식 있는 Duration·이정표/미배정/미사용0·이후 추가 Null, WBS/필터·빈 그룹·알 수 없는 행의 엄격한 전파, 삭제 작업을 포함하는 동결 프로젝트 전체 합계를 확인한다. 현재 결과의 stale과 실적/상태일, 후속 기준선 저장·통화/ID 이관과 동결/시나리오/전체 전후 사본·schema50/구버전 거절·복구를 검사한다.

초기 집중 검사에서는 검증 fixture의 서로 겹치는 그룹 행과 관측 없는 ID의 이관 시도가 기존 계약에 의해 거절되었다. 그룹을 독립 조회로 분리하고 관측 원장을 추가한 뒤 정상 이관 경로를 검사하도록 수정했다. 독립 XML 검사에서는 모든 작업에 Start 요소가 있다는 가정을 제거하고 현재 Project.StartDate와 별도 동결 수식 값을 대조했다. 제품 코드의 검사를 우회하지 않았다.

## 재현과 잔여

Qt/MinGW/CMake를 PATH에 준비한다.

```powershell
cmake --build build-governance --parallel 10
ctest --test-dir build-governance --output-on-failure --parallel 4
.deps/report-verification/Scripts/python.exe scripts/verify-xml-baseline-references.py build-governance/xml-baseline-qa --reports
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xml-baseline-references.py build-governance/xml-baseline-qa --reader
```

최초 native v1 고정 의미이며 외부 번호별 기준선 슬롯/Cost·내장 요약 기간·Early/Late·제품 왕복은 후속이다. XML 기준선 자체와 실적 원장 자동 채택은 지원하지 않는다. 실제 MS Project/P6 실행·macOS/스토어 검증은 미실행이다. 이전 증분의 검증 자료는 변경하지 않는다.

이번 실행은 XMLB-01~05 구현·검증·문서·실행 패키지를 완료했다. 다음 실행 가능한 개발은 기존 순서의 Early/Late 참조이며, 이번 native 증분의 미해결 차단 사유는 없다. 실제 MS Project/P6 재계산·재저장은 제품 실행 환경과 실제 사례가 필요하며 별도 최우선 외부 검증 과제로 남긴다. 기존 미커밋 작업을 보존했고 자동화는 중복 생성하지 않았다.
