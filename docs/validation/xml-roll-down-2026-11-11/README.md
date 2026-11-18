# XML 배정 roll-down 검증

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-down-2026-11-11)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

2026-11-11 · RDOWN-01~05 · Windows / Qt6.8.3 MinGW13.1 Release.

- 전체 **157/157 통과 · 299.84초**: [CTest](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-down-2026-11-11/ctest.txt), [JUnit/검사별 출력](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-down-2026-11-11/ctest.xml). 첫 전체 실행에서 모두 통과했다.
- roll-down·마스크·날짜/추가 형식·목록/GUID·기업/배정 UDF·파서 집중 **15/15 · 10.00초**: [기록](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-down-2026-11-11/focused.txt).
- 전체 실행 후 화면 안내 문구와 미리보기의 사람이 읽을 수 있는 작업/자원 이름을 보완했다. 이 최종 UI 변경에 대한 roll-down/기존 객체 필드 추가 검사 **4/4 · 3.09초**, Windows 기본 UI를 다시 통과했다. [추가 검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-down-2026-11-11/ui-followup.txt), [빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-down-2026-11-11/ui-followup-build.txt). 데이터/계산 경로는 이 보완에서 바꾸지 않았다.
- 파서 **678개** 결정적 시드, schema1~42·구버전 envelope 새 데이터 거절·checksum/body 분리·저장소 일치를 통과했다.
- C++ 형식 **435파일**, 차이0: [결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-down-2026-11-11/quality.json). [모델/저장/UI](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-down-2026-11-11/core-build.txt), [집중](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-down-2026-11-11/focused-build.txt), [전체 Release](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-down-2026-11-11/build.txt) 빌드가 통과했다.
- Windows 800×600에서 설정/영향 미리보기·직접 값/출처·자동 전달 비활성/undo·원본 복원 worker·stale 차단을 확인했다. [실행](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-down-2026-11-11/native-ui.txt), [설정](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-down-2026-11-11/settings-small.png), [배정 값/복원](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-down-2026-11-11/assignment-small.png).

## 독립 대조

[독립 검사기](../../../scripts/verify-xml-roll-down.py)는 C++ 구현을 호출하지 않고 XML의 원본·SecondaryPID/AutoRollDown·명시 배정 값을 해석해 유효 값을 계산한다. **27개 정의·13개 연결·3개 배정·42칸 유효 값**을 native JSON 및 재내보낸 XML과 비교했다. 자동 값28개·직접 값6개·명시 미설정1개를 구별하며 자동 값이 XML의 배정 직접 값으로 덧써지지 않는지 확인했다. [결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-down-2026-11-11/independent-reader.json).

XER의 **34개 유효 배정 값**을 실제 UDFTYPE/UDFVALUE로 대조했다. 빈 텍스트·0·false·초/소수 초·경과/추정 기간과 금액을 확인했다. openpyxl은 배정 보고서42행과 실제 숫자 Cost 셀, pypdf는 **14페이지**를 확인했다. [결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-down-2026-11-11/independent-reports.json), [XLSX](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-down-2026-11-11/fields.xlsx), [PDF](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-down-2026-11-11/fields.pdf).

MPXJ16.10.0 strict reader는 원본을 무시 오류0으로 읽고 배정3개를 확인했다. 이 리더는 AutoRollDown을 실행하거나 해당 메타데이터를 모델에 노출하지 않으며 기업 16진 값도 생략한다. 따라서 플래그·직접 우선순위·실행 유효 값은 별도 XML/native 대조로 판정했다. 외부 앱의 roll-down 실행 동등성을 통과했다고 보고하지 않는다.

## 지원·검사 범위

[계약](../../xml-roll-down.md), [C++ 검사](../../../tests/XmlRollDownTests.cpp), [공유 fixture](../../../tests/XmlRollDownFixture.h)를 따른다. 작업·자원 원본의 Text/Number/Date/Flag/Cost/Duration/Start/Finish, 일반/공유 GUID/형식/다중/열린 목록을 배정에 전달한다. 일반 목록의 서로 다른 ValueID는 값으로, 공유 목록은 GUID로 대응한다. 명시 배정 값은 원본과 같아도 직접 값으로 유지한다.

원본 수정·새 배정, 0/빈 텍스트/false·명시 미설정과 원본 복원, 비활성·중복/누락/형식 불일치 거절·읽기 전용을 검사했다. worker/cancel/stale·일반 undo/redo, 배정률 편집의 명시 미설정 보존을 확인했다. XML 자체 왕복에서 자동 연결과 미설정이 유지됐다. 열린 목록의 관측 직접 값을 받는 목록으로 확장했다.

조건부 schema42와 기존 최소 writer, native 파일/복구·기준선/시나리오·전체 전후 사본을 확인했다. USD→EUR 1/2 전환에서 자동 금액12.34→6.17이 원본에서 한 번만 환산되고 직접0과 미설정은 유지된다. 작업 원장 ID 이관 후 배정의 자동 원본·직접/명시 미설정이 보존됐다.

## 자료·경계

[초기 native JSON](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-down-2026-11-11/native.json), [동일 상태 native 파일](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-down-2026-11-11/source.ppm), [XML](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-down-2026-11-11/export.xml), [재내보내기](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-down-2026-11-11/reexport.xml), [정적 XER](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-down-2026-11-11/export.xer), [매핑](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-down-2026-11-11/mapping.json), [통화 전환 후 전체 JSON](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-down-2026-11-11/converted.json)을 보존한다. 초기 JSON과 source.ppm의 schema42 payload를 독립 대조한다.

자동 XML 입력에는 명시한 지원 범위의 SecondaryPID가 필요하다. XML에 실제 값이 있는 배정은 같은 값이어도 직접 입력으로 간주하고, 값 없는 ExtendedAttribute는 명시 미설정으로 보존한다. 외부 앱이 파생 값을 명시 값으로 재저장한 뒤의 manual provenance는 추측하지 않는다. 실제 Microsoft Project/P6 열기·재계산·재저장 및 값 없는 요소 처리, macOS/스토어 실행은 미실행이다. 요약 roll-up·외부/native 계산식 원본 연결·외부 서버 등록은 별도 범위다.

## 재현

```powershell
$taskWorkspace = (Get-Location).Path
$env:PATH = "$taskWorkspace/.deps/Qt/6.8.3/mingw_64/bin;$taskWorkspace/.deps/Qt/Tools/mingw1310_64/bin;$env:APPDATA/Python/Python314/Scripts;$env:PATH"
cmake --build build-governance --parallel 12
ctest --test-dir build-governance --output-on-failure --parallel 4 --output-junit "$taskWorkspace/build-governance/xml-roll-down-ctest.xml"
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xml-roll-down.py build-governance/xml-roll-down-qa --reader
.deps/report-verification/Scripts/python.exe scripts/verify-xml-roll-down.py build-governance/xml-roll-down-qa --reports
Push-Location build-governance
./jobs_xml_roll_down_tests.exe -platform windows --ui
Pop-Location
python scripts/check-cpp-quality.py format --tool .deps/cpp-quality/Lib/site-packages/clang_format/data/bin/clang-format.exe
```

[검증 파일·소스 SHA256](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-roll-down-2026-11-11/verification-summary.json). 이전 증분의 검증 자료는 덮어쓰지 않았다. GAP 전체나 외부 제품 동등성의 완료 선언이 아니다.
