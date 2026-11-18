# XML 값 목록·GUID 참조 검증

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-lookup-references-2026-11-10)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

2026-11-10 · LOOKUP-01~05 · Windows / Qt6.8.3 MinGW13.1 Release.

- 전체 **149/149 통과 · 239.04초**: [CTest](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-lookup-references-2026-11-10/ctest.txt), [JUnit/검사별 출력](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-lookup-references-2026-11-10/ctest.xml).
- 집중 **9/9 통과 · 8.10초**: 목록 계약/UI, 기존 UDF·기업/배정·작업 분류·자원 보기 회귀. [기록](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-lookup-references-2026-11-10/focused.txt).
- 파서 **598개** 결정적 시드와 schema1~38, 구버전 envelope 새 필드 거절·checksum/body 분리·저장소 일치를 통과했다. 전체 실행의 `parser_seed_replay` 출력을 보존한다.
- C++ 형식 **419파일**, 차이0: [결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-lookup-references-2026-11-10/quality.json). 최종 Release [UI/필터 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-lookup-references-2026-11-10/build.txt)와 [모델/XML 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-lookup-references-2026-11-10/core-build.txt)에 경고/오류가 없다.
- Windows 기본 UI에서 작업/배정의 다중 선택, 배정 일괄 worker 적용, 문서 변경 후 stale 차단을 통과했다. [실행](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-lookup-references-2026-11-10/native-ui.txt), [배정](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-lookup-references-2026-11-10/assignment.png), [800×600](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-lookup-references-2026-11-10/assignment-small.png), [작업](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-lookup-references-2026-11-10/task.png). 말단 전용 목록의 상위 항목은 비활성화한다.
- 독립 ElementTree/Decimal은 XML **7개 정의·공유 GUID 테이블4개·선택 셀9개**, 목록 ID/GUID·기본값·부모·단일/다중·직접 값과 재내보내기를 대조했다. MPXJ16.10.0 strict reader는 무시 오류0으로 일반 목록과 Outline Code의 실제 값2개를 읽었다. [결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-lookup-references-2026-11-10/independent-reader.json).
- 독립 openpyxl/pypdf는 다중 선택의 계층 표시 이름, 열린 목록 직접 값·미설정, 내부 선택 코드의 비노출을 확인했다. PDF **12페이지**를 판독했다. [결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-lookup-references-2026-11-10/independent-reports.json), [XLSX](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-lookup-references-2026-11-10/fields.xlsx), [PDF](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-lookup-references-2026-11-10/fields.pdf).

## 검사 범위

[지원 계약](../../xml-lookup-references.md), [테스트](../../../tests/XmlLookupTests.cpp), [독립 검사기](../../../scripts/verify-xml-lookups.py)를 따른다. 일반 ValueList와 OutlineCodes를 Choice로 연결하며 값 형식은 Text/Number/Date로 검증한다. 네 객체 범위, 공유 사전·부모/말단·기본값·선택 한도, 빈 사전/빈 텍스트/숫자0/미설정, 열린 목록의 등록·직접 값 혼합 선택을 검사한다. 기업 요소450개 정상 경계와 초과도 검사한다.

다른 목록/미정의 GUID·ID, 직접 값과 참조 충돌, 중복·단일 선택 초과·부모 순환, 잘못된 날짜 시각/숫자 정밀도와 native 옵션 참조를 거절한다. native schema38·구버전 거절, 동결/시나리오·통화 전환 전체 사본·복구, 원자 편집·취소/stale·읽기 전용·undo/redo와 외부 사전 변경 차단을 확인했다. 작업/자원의 저장된 다중 선택 필터 복원·재저장과 단일 항목 필터 정규화도 확인했다. XML 목록/참조를 재내보내고 XER는 표시 텍스트로 투영한다. [원본 native](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-lookup-references-2026-11-10/source.ppm), [정의/선택 JSON](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-lookup-references-2026-11-10/native.json), [XML](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-lookup-references-2026-11-10/export.xml), [재내보내기](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-lookup-references-2026-11-10/reexport.xml), [XER](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-lookup-references-2026-11-10/export.xer), [매핑/손실](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-lookup-references-2026-11-10/mapping.json)을 보존한다.

## 제한과 미실행

MPXJ16.10.0은 기업 16진/EnterpriseExtendedAttribute 값을 소비하지 않는다. 따라서 기업 GUID 선택은 별도 XML 파서와 native 재수입으로 대조했고 MPXJ 기업 값 왕복 성공으로 확대하지 않는다. 실제 Microsoft Project/P6 열기·재계산·재저장, macOS/스토어 실행은 미실행이다.

Flag/Cost/Duration 등 추가 형식, 마스크/자동 코드 생성·roll-down/외부 식 실행은 지원 범위에 포함하지 않는다. 열린 목록의 새 직접 값 작성 UI는 별도이며 관측된 직접 값의 보존·선택을 지원한다. 외부 서버의 목록 등록/조회도 하지 않는다. 원본 XML과 손실 진단을 보존한다.

## 재현

```powershell
$taskWorkspace = (Get-Location).Path
$env:PATH = "$taskWorkspace/.deps/Qt/6.8.3/mingw_64/bin;$taskWorkspace/.deps/Qt/Tools/mingw1310_64/bin;$env:APPDATA/Python/Python314/Scripts;$env:PATH"
cmake --build build-governance --parallel 12
ctest --test-dir build-governance --output-on-failure --parallel 4 --output-junit "$taskWorkspace/build-governance/xml-lookup-ctest.xml"
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xml-lookups.py build-governance/xml-lookup-qa --reader
.deps/report-verification/Scripts/python.exe scripts/verify-xml-lookups.py build-governance/xml-lookup-qa --reports
Push-Location build-governance
./jobs_xml_lookup_tests.exe -platform windows --ui
Pop-Location
python scripts/check-cpp-quality.py format --tool .deps/cpp-quality/Lib/site-packages/clang_format/data/bin/clang-format.exe
```

[검증 파일·소스 SHA256](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-lookup-references-2026-11-10/verification-summary.json). 이전 기업/배정 스칼라 검증 기록은 덮어쓰지 않았다. 이 기록은 LOOKUP 지원 범위의 판정이며 GAP 전체 완료 선언이 아니다.
