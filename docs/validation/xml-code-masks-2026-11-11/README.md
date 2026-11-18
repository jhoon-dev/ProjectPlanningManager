# XML 필드 마스크·자동 코드 생성 검증

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-code-masks-2026-11-11)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

2026-11-11 · XMASK-01~05 · Windows / Qt6.8.3 MinGW13.1 Release.

- 최종 전체 **155/155 통과 · 296.81초**: [CTest](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-code-masks-2026-11-11/ctest.txt), [JUnit/검사별 출력](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-code-masks-2026-11-11/ctest.xml).
- 마스크·WBS/MPP 연동·기존 XML 필드 집중 **14/14 통과 · 57.65초**: [기록](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-code-masks-2026-11-11/focused.txt).
- 파서 **650개** 결정적 시드, schema1~41·구버전 envelope의 새 데이터 거절·checksum/body 분리·저장소 일치를 통과했다.
- C++ 형식 **429파일**, 차이0: [결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-code-masks-2026-11-11/quality.json). [모델/저장/UI](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-code-masks-2026-11-11/core-build.txt), [집중](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-code-masks-2026-11-11/focused-build.txt), [계약 검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-code-masks-2026-11-11/test-build.txt), [전체 Release](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-code-masks-2026-11-11/build.txt), [화면 보완](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-code-masks-2026-11-11/refresh-build.txt), [최종 연동 보완](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-code-masks-2026-11-11/postfix-build.txt) 빌드를 보존한다.
- Windows 기본 화면에서 마스크 설정·전체 코드 미리보기/생성·공유 사전·stale 차단·WBS 미설정 생성·저장/거절과 Unicode 접두사를 검사했다. [실행](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-code-masks-2026-11-11/native-ui.txt), [800×600 마스크](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-code-masks-2026-11-11/mask-small.png), [코드 생성](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-code-masks-2026-11-11/generate.png), [800×600 WBS](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-code-masks-2026-11-11/wbs-small.png).
- 독립 ElementTree/Python/UUID는 **4개 정의·3개 마스크 목록·5개 목록 수준·3개 WBS 수준·4개 최초 선택·4개 최초 WBS 코드**를 대조했다. 생성 후 공유 사전5개 항목/전체7개 목록 값·WBS5개·명시 미설정을 XML/native/source.ppm 사이에서 확인했다. [결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-code-masks-2026-11-11/independent-reader.json).
- MPXJ16.10.0 strict reader는 원본에서 무시 오류0으로 **WBS 값4개·목록 마스크5수준**을 확인했다. 공유 GUID는 마지막 참조 필드에만 사전/마스크가 연결되는 리더 제한을 별도 기록한다. 전체 기업 값·공유 참조 의미나 WBS 생성 실행 동등성의 통과로 확대하지 않는다.
- openpyxl/pypdf는 `AA.01/공정`과 미정 표시를 확인했다. PDF **10페이지**: [결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-code-masks-2026-11-11/independent-reports.json), [XLSX](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-code-masks-2026-11-11/fields.xlsx), [PDF](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-code-masks-2026-11-11/fields.pdf).

## 첫 전체 실행과 MPP 연동 보완

첫 전체 실행은 **154/155 · 263.20초**였으며 `mpp_manual_curve_contract`의 기존 고정 schema36 기대값에서 실패했다. [첫 콘솔](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-code-masks-2026-11-11/ctest-first-attempt.txt), [첫 JUnit](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-code-masks-2026-11-11/ctest-first-attempt.xml)을 보존한다. 새 XML WBS 지원으로 MPP reader가 제공한 WBS 값도 native에 남아 schema41이 필요해졌다. 검사를 WBS 코드의 존재·schema41 보존과 WBS 메타데이터를 제거했을 때의 기존 최소 schema36으로 나눠 보강했다. 곡선 수치·공수·기간·원본 MPP 보존 검사는 유지했다. 수정 후 집중14/14 및 최종 전체155/155가 통과했다. 이를 첫 실행부터 전체 무실패로 보고하지 않는다.

## 지원·검사 범위

[계약](../../xml-code-masks.md), [C++ 검사](../../../tests/XmlCodeMaskTests.cpp), [공유 fixture](../../../tests/XmlCodeFixture.h), [독립 검사기](../../../scripts/verify-xml-code-masks.py)를 따른다. 네 범위의 GUID 텍스트 목록과 ExtendedAttribute 정의 없이 존재하는 로컬 OutlineCode의 Masks/AllLevelsRequired를 검사했다. 숫자·영문 대/소문자·Unicode 문자열, 고정/자유 길이·빈/문자 구분자·부모/전체 수준과 잘못된 형식·중복을 확인했다.

자동 순번의 자리올림·고정 공간 소진과 직접 문자열 입력, 충돌 없는 ValueID/GUID·공유 사전의 원자 갱신과 기존 선택을 확인했다. Type3 문자열 수준은 직접 입력하며 외부 서버에 사전을 등록하지 않는다. 기존 코드를 재번호하지 않으며 삭제한 최댓값의 영구 재사용 금지는 보장하지 않는다.

WBSMasks의 GenerateCodes/VerifyUniqueCodes/Prefix와 WBS/WBSLevel을 보존한다. 가져오기 시 누락 코드는 채우지 않으며 새 작업은 플래그에 따라 생성한다. 기본 숫자 WBS·32수준 경계, 명시 미설정 생성과 중복/부적합 접두사 거절을 검사했다. 작업 삽입/삭제·worker/cancel/stale·일반 undo/redo와 읽기 전용, schema41 저장/복구·기준선/시나리오·통화 전환 전체 사본·원장 ID 이관을 확인했다.

## 자료 구분·판독 경계

[초기 JSON](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-code-masks-2026-11-11/native.json), [초기 XML](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-code-masks-2026-11-11/export.xml), [가져오기 후 재내보내기](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-code-masks-2026-11-11/reexport.xml), [초기 정적 XER](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-code-masks-2026-11-11/export.xer), [초기 매핑](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-code-masks-2026-11-11/mapping.json), [초기 보고서](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-code-masks-2026-11-11/fields.xlsx)를 보존한다. [편집/생성 후 XML](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-code-masks-2026-11-11/generated.xml), [편집/생성 후 JSON](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-code-masks-2026-11-11/generated.json), [해당 native 파일](../../../tests/fixtures/native-history/xml-code-masks-2026-11-11/source.ppm)은 코드 생성·undo/worker·명시 미설정·기준선/시나리오와 USD→EUR 전환 후 상태다. source.ppm의 schema41 payload와 generated.json을 독립 대조한다.

MPXJ의 공유 테이블 한계 때문에 원본의 마스크 구조는 마지막 참조에 연결된 필드에서 확인하고 공유 연결 전체·기업 16진 선택 값·원시 마스크 Type·WBS 생성 플래그는 XML/native 독립 검사로 대조한다. WBS 자동 생성 동작은 native 검사로 확인했다. 실제 Microsoft Project/P6 열기·재계산·재저장, macOS/스토어 실행은 미실행이다. roll-down·외부 식 실행·phonetic/정렬 규칙은 별도 범위다.

## 재현

```powershell
$taskWorkspace = (Get-Location).Path
$env:PATH = "$taskWorkspace/.deps/Qt/6.8.3/mingw_64/bin;$taskWorkspace/.deps/Qt/Tools/mingw1310_64/bin;$env:APPDATA/Python/Python314/Scripts;$env:PATH"
cmake --build build-governance --parallel 12
ctest --test-dir build-governance --output-on-failure --parallel 4 --output-junit "$taskWorkspace/build-governance/xml-code-masks-ctest.xml"
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xml-code-masks.py build-governance/xml-code-masks-qa --reader
.deps/report-verification/Scripts/python.exe scripts/verify-xml-code-masks.py build-governance/xml-code-masks-qa --reports
Push-Location build-governance
./jobs_xml_code_mask_tests.exe -platform windows --ui
Pop-Location
python scripts/check-cpp-quality.py format --tool .deps/cpp-quality/Lib/site-packages/clang_format/data/bin/clang-format.exe
```

[검증 파일·소스 SHA256](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-code-masks-2026-11-11/verification-summary.json). 이전 증분의 검증 자료는 덮어쓰지 않았다. GAP 전체나 외부 제품 동등성의 완료 선언이 아니다.
