# XML Start·Finish·Date 날짜·시간 검증

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-date-time-fields-2026-11-10)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

2026-11-10 · XDATE-01~05 · Windows / Qt6.8.3 MinGW13.1 Release.

- 전체 **153/153 통과 · 251.22초**: [CTest](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-date-time-fields-2026-11-10/ctest.txt), [JUnit/검사별 출력](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-date-time-fields-2026-11-10/ctest.xml). 이번 전체 실행은 첫 실행에서 모두 통과했다.
- 날짜·시간·추가 형식·목록/GUID·기업/배정 UDF 집중 **9/9 통과 · 7.91초**: [기록](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-date-time-fields-2026-11-10/focused.txt).
- 파서 **630개** 결정적 시드, schema1~40와 구버전 envelope의 새 형식 거절·checksum/body 분리·저장소 일치를 통과했다. 전체 `parser_seed_replay` 출력을 보존한다.
- C++ 형식 **423파일**, 차이0: [결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-date-time-fields-2026-11-10/quality.json). [최종 Release 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-date-time-fields-2026-11-10/build.txt), [모델/형식/UI 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-date-time-fields-2026-11-10/core-build.txt), [집중 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-date-time-fields-2026-11-10/focused-build.txt)를 통과했다.
- Windows 기본 UI에서 배정 시작 일시의 원자 일괄 worker·stale 차단·정규화와 작업 종료 자정을 검사했다. [실행](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-date-time-fields-2026-11-10/native-ui.txt), [배정](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-date-time-fields-2026-11-10/assignment.png), [800×600](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-date-time-fields-2026-11-10/assignment-small.png), [작업 Finish](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-date-time-fields-2026-11-10/task-finish.png).
- 독립 ElementTree/Python datetime 산술로 **18개 정의·23개 선택 셀·2개 압축 GUID 목록**과 네 범위의 시각·공유 Date/Start 역할·참조를 XML 재내보내기와 대조했다. [결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-date-time-fields-2026-11-10/independent-reader.json).
- openpyxl/pypdf로 소수 초와 자정/미정의 문자열 보존 및 PDF **13페이지**를 확인했다. [결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-date-time-fields-2026-11-10/independent-reports.json), [XLSX](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-date-time-fields-2026-11-10/fields.xlsx), [PDF](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-date-time-fields-2026-11-10/fields.pdf).

## 지원·검사 범위

[지원 계약](../../xml-date-time-fields.md), [C++ 검사](../../../tests/XmlDateTimeTests.cpp), [독립 검사기](../../../scripts/verify-xml-date-times.py)를 따른다. Start(CFType6)/Finish(3)의 독립 역할과 기존 Date(1)의 시각, 작업/자원의 공식 로컬 슬롯 및 프로젝트/기업/배정 필드를 지원한다. 실제 작업 일정의 시작/종료·제약으로 자동 채택하지 않는다.

시간대 없는 civil time을 사용하며 초·최대 소수6자리, 자정과 미설정의 구분, 윤년/범위를 검사한다. Z/오프셋·24시·윤초·초과 정밀도를 거절한다. 일반 ValueList는 ISO 시각을 보존한다. GUID 목록 Type4/27은 1983-12-31 기준 날짜×65536+시각/6초의 압축 형식을 읽고 쓴다. GUID 목록의 기준일 이전·6초로 표현할 수 없는 값·소수 초를 반올림하지 않고 거절한다. 같은 날짜 사전을 Date/Start/Finish 필드가 공유할 수 있다.

현재/동결·시나리오·채택·전체 전후 사본의 시각이 필요할 때 schema40을 사용하며 기존 날짜 전용 파일의 최소 writer를 유지한다. 파일 저장/재열기·복구·구버전 거절, 원자 worker·취소/stale·읽기 전용·일반 undo/redo와 날짜 정렬/필터를 확인했다. 통화 전환은 일시·역할·참조를 바꾸지 않는다. XER는 Start/Finish와 시각 있는 Date를 문자열로 투영하며 source.ppm/진단에 원래 형식을 보존한다.

[원본 native](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-date-time-fields-2026-11-10/source.ppm), [JSON](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-date-time-fields-2026-11-10/native.json), [XML](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-date-time-fields-2026-11-10/export.xml), [재내보내기](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-date-time-fields-2026-11-10/reexport.xml), [XER](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-date-time-fields-2026-11-10/export.xer), [매핑·손실](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-date-time-fields-2026-11-10/mapping.json)을 보존한다.

## 독립 리더 한계·미실행

**MPXJ16.10.0 strict reader는 원본 XML의 소수 초를 거절한다.** 검사기는 원본의 이 실패도 확인한다. 원본을 수정하지 않고 시각 노드5개의 소수 초만 제거한 [별도 호환성 사본](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-date-time-fields-2026-11-10/reader-whole-seconds.xml)을 생성해, 무시 오류0과 실제 값4개(작업 Start/Finish·자원 Date·공유 GUID Start)를 확인했다. 이 결과를 원본 소수 초 파일의 MPXJ 호환성 통과로 확대하지 않는다. 원본의 최대 소수6자리와 기업 값·참조는 독립 XML/native 재수입 및 보고서 검사로 확인한다. MPXJ는 기업 16진 값도 소비하지 않는다.

실제 Microsoft Project/P6 열기·재계산·재저장과 macOS/스토어 실행은 미실행이다. 시간대 변환·마스크/자동 코드 생성·roll-down·외부 식 실행은 별도 범위다. 이전 증분의 기간 EVM UI 간헐 종료 기록은 기존 검증 폴더에 그대로 남긴다. 이번 전체 실행의 해당 검사도 통과했으며, 이전 문제의 원인 해결로 보고하지 않는다.

## 재현

```powershell
$taskWorkspace = (Get-Location).Path
$env:PATH = "$taskWorkspace/.deps/Qt/6.8.3/mingw_64/bin;$taskWorkspace/.deps/Qt/Tools/mingw1310_64/bin;$env:APPDATA/Python/Python314/Scripts;$env:PATH"
cmake --build build-governance --parallel 12
ctest --test-dir build-governance --output-on-failure --parallel 4 --output-junit "$taskWorkspace/build-governance/xml-date-time-ctest.xml"
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xml-date-times.py build-governance/xml-date-time-qa --reader
.deps/report-verification/Scripts/python.exe scripts/verify-xml-date-times.py build-governance/xml-date-time-qa --reports
Push-Location build-governance
./jobs_xml_date_time_tests.exe -platform windows --ui
Pop-Location
python scripts/check-cpp-quality.py format --tool .deps/cpp-quality/Lib/site-packages/clang_format/data/bin/clang-format.exe
```

[검증 파일·소스 SHA256](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-date-time-fields-2026-11-10/verification-summary.json). 이전 증분 검증 자료는 덮어쓰지 않았다. GAP 전체나 외부 제품 동등성의 완료 선언이 아니다.
