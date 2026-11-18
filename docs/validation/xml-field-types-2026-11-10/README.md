# XML Flag·Cost·Duration 검증

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-field-types-2026-11-10)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

2026-11-10 · XTYPE-01~05 · Windows / Qt6.8.3 MinGW13.1 Release.

- 최종 전체 **151/151 재실행 통과 · 241.51초**: [CTest](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-field-types-2026-11-10/ctest.txt), [JUnit/검사별 출력](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-field-types-2026-11-10/ctest.xml).
- 추가 형식·기존 목록/GUID·기업/배정 UDF 집중 **7/7 통과 · 6.17초**: [기록](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-field-types-2026-11-10/focused.txt).
- 파서 **614개** 결정적 시드, schema1~39와 구버전 envelope 새 형식 거절·checksum/body 분리·저장소 일치를 통과했다. 전체 `parser_seed_replay` 출력을 보존한다.
- C++ 형식 **421파일**, 차이0: [결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-field-types-2026-11-10/quality.json). [최종 Release 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-field-types-2026-11-10/build.txt), [모델/형식/UI 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-field-types-2026-11-10/core-build.txt), [XML 매핑 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-field-types-2026-11-10/xml-build.txt)에 경고/오류가 없다.
- Windows 기본 UI의 Flag 선택·음수 Cost·경과/추정 Duration 일괄 worker·stale 차단과 작업 Flag를 검사했다. [실행](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-field-types-2026-11-10/native-ui.txt), [배정](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-field-types-2026-11-10/assignment.png), [800×600](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-field-types-2026-11-10/assignment-small.png), [작업 Flag](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-field-types-2026-11-10/task-flag.png).
- 독립 ElementTree/Decimal은 **18개 정의·18개 스칼라 셀·2개 typed GUID 목록**, CFType·배율·값/부호/0/미설정·DurationFormat·분 정밀도와 재내보내기를 대조했다. MPXJ16.10.0 strict reader는 무시 오류0으로 로컬 세 형식의 실제 값3개를 읽었다. [결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-field-types-2026-11-10/independent-reader.json).
- openpyxl/pypdf는 음수/0 Cost의 실제 숫자 셀·Flag/Duration 표시와 PDF **13페이지**의 문자열을 확인했다. [결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-field-types-2026-11-10/independent-reports.json), [XLSX](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-field-types-2026-11-10/fields.xlsx), [PDF](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-field-types-2026-11-10/fields.pdf).

## 최초 전체 실행에서 관측한 비정상 종료

첫 전체 실행은 **150/151 · 258.62초**로 기존 `native_period_evm_ui`가 stdout 없이 SEGFAULT로 종료했다. [첫 콘솔](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-field-types-2026-11-10/ctest-first-attempt.txt)과 [첫 JUnit](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-field-types-2026-11-10/ctest-first-attempt.xml)을 그대로 보존한다. 코드를 변경하지 않은 단독 반복 **3/3**은 3.89/3.57/3.47초에 통과했고, 이어 같은 parallel4의 전체 재실행도 **151/151**로 통과했다. [단독 반복 기록](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-field-types-2026-11-10/evm-repeat.txt). 원인은 재현/확정하지 못했으며 해결된 버그나 최초부터 전체 무실패로 보고하지 않는다. 기존 기간 EVM 제품/테스트 코드를 이 증분에서 바꾸지 않았다.

## 지원·검사 범위

[계약](../../xml-additional-field-types.md), [C++ 검사](../../../tests/XmlFieldTypesTests.cpp), [독립 검사기](../../../scripts/verify-xml-field-types.py)를 따른다. 작업/자원의 공식 로컬 슬롯과 네 범위 기업 필드, 기존 SecondaryPID 배정과 일반/기업 직접 값을 지원한다. Flag는 1/0·예/아니요, Cost는 통화 주단위100배의 XML 값과 native 최소 통화 단위, Duration은 ISO 시간/분/초와 분 정규화·경과/추정·프로젝트 기본 형식을 구분한다. 데이터는 일정/공수/원가에 자동 합산하지 않는다.

Flag/Cost/Duration의 목록 타입17/9/6과 GUID 참조를 검사한다. 원시 값 형식·단위·정밀도·범위 초과, 최소 통화 단위로 표현할 수 없는 Cost, 월/년/일 ISO 기간·비율/Null DurationFormat과 1/1,000,000분 초과 정밀도를 거절한다. 기간 목록은 일반 정수1/10분만 지원한다.

schema39와 구버전 거절, 파일 저장/재열기·동결/시나리오·전환 전체 사본·복구, 원자 worker·취소/stale·일반 undo/redo를 확인했다. Cost 정의가 있으면 단순 통화 라벨 변경을 차단한다. USD→EUR/JPY의 금액/목록 환산·반올림·ID 유지와 로컬 목록 값 충돌 시 원자 거절을 확인했다. 통화 전환은 기존 감사 이력 장벽을 유지한다.

[원본 native](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-field-types-2026-11-10/source.ppm), [JSON](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-field-types-2026-11-10/native.json), [XML](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-field-types-2026-11-10/export.xml), [재내보내기](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-field-types-2026-11-10/reexport.xml), [XER](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-field-types-2026-11-10/export.xer), [매핑·손실](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-field-types-2026-11-10/mapping.json)을 보존한다. XER의 새 형식은 문자열로 정적 투영하며 Cost의 큰 수치도 기존 일반 숫자 UDF 범위에 잘리지 않도록 한다.

## 판독 한계·미실행

MPXJ16.10.0은 기업 16진 값과 기간의 추정 비트를 소비하지 않는다. 로컬 Flag=false·Cost=-12.34·Duration=90.5 elapsed minutes를 실제 reader 값으로 확인했고 기업 값·추정 비트·단위와 lookup의 직접/참조 값은 별도 XML/native 재수입으로 대조했다. MPXJ의 전체 기업 값 왕복 통과로 확대하지 않는다.

실제 Microsoft Project/P6 열기·재계산·재저장, macOS/스토어 실행은 미실행이다. Start/Finish의 시각, 마스크/자동 코드 생성·roll-down/외부 식 실행과 새 형식을 native 숫자 계산식 입력/결과로 자동 변환하는 기능은 이 증분에 포함하지 않는다. 원본과 진단을 보존한다.

## 재현

```powershell
$taskWorkspace = (Get-Location).Path
$env:PATH = "$taskWorkspace/.deps/Qt/6.8.3/mingw_64/bin;$taskWorkspace/.deps/Qt/Tools/mingw1310_64/bin;$env:APPDATA/Python/Python314/Scripts;$env:PATH"
cmake --build build-governance --parallel 12
ctest --test-dir build-governance --output-on-failure --parallel 4 --output-junit "$taskWorkspace/build-governance/xml-field-types-ctest.xml"
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xml-field-types.py build-governance/xml-field-types-qa --reader
.deps/report-verification/Scripts/python.exe scripts/verify-xml-field-types.py build-governance/xml-field-types-qa --reports
Push-Location build-governance
./jobs_xml_field_types_tests.exe -platform windows --ui
Pop-Location
python scripts/check-cpp-quality.py format --tool .deps/cpp-quality/Lib/site-packages/clang_format/data/bin/clang-format.exe
```

[검증 파일·소스 SHA256](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-field-types-2026-11-10/verification-summary.json). 기존 증분의 검증 자료는 덮어쓰지 않았다. GAP 전체나 외부 제품 동등성의 완료 선언이 아니다.
