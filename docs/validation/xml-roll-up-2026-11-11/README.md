# XML 요약 roll-up 검증

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-up-2026-11-11)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

2026-11-11 · RUP-01~05 · Windows / Qt6.8.3 MinGW13.1 Release.

## 검사 결과

- 전체 **159/159 통과 · 293.52초**: [CTest](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-up-2026-11-11/ctest.txt), [JUnit/검사별 출력](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-up-2026-11-11/ctest.xml). 첫 전체 실행에서 모두 통과했다.
- 최종 Windows800×600에서 WBS/자원 미리보기·원본 보존·계층 평균·저장/undo·stale 차단을 확인했다. [실행](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-up-2026-11-11/native-ui.txt), [설정](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-up-2026-11-11/settings-small.png), [자원](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-up-2026-11-11/resource-small.png).

- XML roll-up/roll-down·마스크·날짜/추가 형식·목록/GUID·기업/배정 UDF·객체 필드·파서 집중 **17/17 · 9.24초**: [기록](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-up-2026-11-11/focused.txt), [JUnit](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-up-2026-11-11/focused.xml).
- 파서 **698개** 결정적 시드, schema1~43·checksum/body 분리·실제 파일 디코더의 구버전 거절·저장소 일치를 통과했다.
- C++ 형식 **441파일**, 차이0: [결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-up-2026-11-11/quality.json). [Release 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-up-2026-11-11/build.txt), [최종 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-up-2026-11-11/final-build.txt), [파서 최종 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-up-2026-11-11/parser-build.txt).
- 첫 집중 실행은16/17이었다. 다중 스키마 body 탐색도 수행하는 fuzz harness에 schema42 envelope/schema43 body를 거절 시드로 잘못 넣었다. 이 경우 body 자체는43으로 읽을 수 있어 시드가 부적절했다. 기존 roll-down 검사와 같이 `decodeProjectFile`의 직접 거절 검사로 옮겼고 production 파서는 변경하지 않았다. [첫 기록](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-up-2026-11-11/focused-first.txt), [첫 JUnit](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-up-2026-11-11/focused-first.xml)을 보존한다.

[계약](../../xml-roll-up.md), [C++ 검사](../../../tests/XmlRollUpTests.cpp), [공유 fixture](../../../tests/XmlRollUpFixture.h).

## 독립 대조

[독립 검사기](../../../scripts/verify-xml-roll-up.py)는 C++ 구현을 호출하지 않고 ElementTree와 Decimal로 XML의 실제 계층과 말단 값을 읽어 요약을 다시 계산한다. 원본/재내보내기 90칸을 대조하고, native 말단 값·schema43 payload와 source.ppm의 일치, 원본 명시 합계999와 내보낸 자동 합계30의 분리를 확인한다.

[판독 결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-up-2026-11-11/independent-reports.json)와 [리더 결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-up-2026-11-11/independent-reader.json)를 보존한다. openpyxl은 WBS/루트 요약48행과 필터가 적용된 자원 요약2행, 실제 숫자 Cost와 개수 단위를 검사한다. pypdf는17페이지의 요약 표·자동 출처를 확인한다. MPXJ16.10.0 strict reader는 무시 오류0으로 요약 포함 작업7행·자원2개를 읽는다. 이 리더는 사용자 필드 요약을 실행하지 않으므로 계산 규칙/결과는 별도 독립 계산으로 판정한다.

[XML](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-up-2026-11-11/export.xml), [재내보내기](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-up-2026-11-11/reexport.xml), [원본 native JSON](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-up-2026-11-11/native.json), [동일 상태 native 파일](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-up-2026-11-11/source.ppm), [통화 전환 JSON](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-up-2026-11-11/converted.json), [매핑/손실 진단](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-up-2026-11-11/mapping.json), [정적 XER](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-up-2026-11-11/export.xer), [XLSX](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-up-2026-11-11/fields.xlsx), [PDF](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-up-2026-11-11/fields.pdf), [필터 자원 XLSX](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-up-2026-11-11/resources.xlsx)를 보존한다.

## 범위와 경계

합계·평균·직접 하위 평균·최대/최소·전체/직접/말단 개수, 빈 그룹, Flag의3값 OR/AND, 누락과0, Cost 반올림·Duration 경과/추정·혼합 오류·날짜 극값/소수 초를 확인한다. 큰 양/음 합계가 signed64 중간 범위를 넘어도 상쇄를 보존하고 최종 필드 범위 초과는 미설정/사유로 표시한다.

원본/파생 분리, UID0 Task/Project 분리, WBS 스냅샷, 활성/외부 식 메타데이터, XML 재교환을 검사한다. 말단 변경·구조 변경·undo/redo·worker·취소·stale/읽기 전용, 명시 값의 편집/형식 변경 보호, 조건부 schema43·동결/시나리오·복구·통화/ID 이관을 확인한다. UI 설정/미리보기는 Windows800×600에서 검사하고 화면을 보존한다.

평균 가중치·누락 정책은 native 계약이다. Cost/Duration 개수, Text/Choice 활성 roll-up, Project/Assignment 요약 실행, 외부 식 실행은 지원하지 않는다. XER에는 규칙/WBS 요약 실행을 투영하지 않고 source.ppm과 손실 진단을 남긴다. 실제 Microsoft Project/P6 열기·재계산·재저장, macOS/스토어 실행은 미실행이다.

## 재현

Qt6.8.3 MinGW13.1 도구가 PATH에 있는 환경에서 다음을 실행한다.

```powershell
cmake --build build-governance --parallel 12
ctest --test-dir build-governance --output-on-failure --parallel 4
.deps/report-verification/Scripts/python.exe scripts/verify-xml-roll-up.py build-governance/xml-roll-up-qa --reports
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xml-roll-up.py build-governance/xml-roll-up-qa --reader
```

[verification-summary.json](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-up-2026-11-11/verification-summary.json)은 검사 결과와 자료/관련 소스의 SHA-256을 보존한다. 과거 증분의 검증 자료는 변경하지 않았다.
