# 기업·배정 UDF와 XML 프로젝트 필드 검증

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/enterprise-assignment-udf-2026-11-10)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

2026-11-10 · Windows x64 / Qt6.8.3 / MinGW13.1 Release. [EUDF-01~05 계약](../../enterprise-assignment-udf.md)을 따른다.

## 결과

- 전체 **147/147 통과 · 277.58초**: [CTest](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/enterprise-assignment-udf-2026-11-10/ctest.txt) · [JUnit/검사별 출력](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/enterprise-assignment-udf-2026-11-10/ctest.xml).
- 최종 집중 **5/5 통과 · 9.69초**: 기업/배정 계약·화면, 기존 UDF·프로젝트/자원 필드, 파서. [실행 기록](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/enterprise-assignment-udf-2026-11-10/focused.txt).
- 파서 **582개** 결정적 시드, schema1~37의 checksum/body 구분과 구버전 envelope의 새 필드 거절을 통과했다.
- C++ 형식 **418파일**, 차이0: [품질 결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/enterprise-assignment-udf-2026-11-10/quality.json). 원래 Release 최적화 설정으로 [최종 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/enterprise-assignment-udf-2026-11-10/build.txt)를 통과했다.
- Windows 실제 플랫폼에서 배정 일괄 입력·소유 구분·stale 차단을 통과했다. [실행](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/enterprise-assignment-udf-2026-11-10/native-ui.txt) · [기본 화면](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/enterprise-assignment-udf-2026-11-10/assignment.png) · [800×600 화면](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/enterprise-assignment-udf-2026-11-10/assignment-small.png).
- XML **7개 정의/9개 값**, 네 범위·공식 ID/요소 이름·날짜·수치/0/빈 값·재내보내기 GUID를 독립 ElementTree/Decimal로 확인했다. XER 단일/결합2프로젝트의 배정 외래키와 값도 독립 TSV로 대조했다. [독립 결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/enterprise-assignment-udf-2026-11-10/independent-reader.json).
- XLSX 배정 **6행**의 실제 숫자0·소수6자리·빈 텍스트/미정을 구분하고 PDF **12페이지**에서 한국어와 필드 수치를 판독했다. [결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/enterprise-assignment-udf-2026-11-10/independent-reports.json) · [XLSX](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/enterprise-assignment-udf-2026-11-10/fields.xlsx) · [PDF](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/enterprise-assignment-udf-2026-11-10/fields.pdf).

## 기능과 예외 범위

기업 작업/자원, 프로젝트 요약 작업, 배정의 스칼라를 읽고 쓴다. 외부 FieldID·Guid·FieldName을 native 정의에 보존하고 프로젝트 값의 작업별 충돌을 차단한다. SecondaryPID의 배정 별칭을 독립 정의로 보존·재내보내며 직접 ExtendedAttribute 배정 값도 읽는다. 네 범위의 XML 필드 참조/형식, 중복 값·GUID, 날짜 시각/숫자 정밀도와 미지원 lookup의 스칼라 오인을 검사했다.

배정 정의/값·원자 worker 편집, 취소·같은 문서 재열기의 stale·읽기 전용, undo/redo·dirty 복원과 기존 배정률 편집의 값 보존을 검사했다. schema37의 현재/기준선/시나리오·보관 사본, 통화 전환의 전체 전후 사본과 복구를 실제 저장/재열기로 대조했다. 과거 schema의 최소 writer와 기존 교환 동반 source.ppm/원본 보존을 유지한다.

XER TASKRSRC UDF의 프로젝트/배정 외래키를 프로젝트 선택 전에 검사한다. 결합 내보내기의 UDF ID·배정 fk_id 재배정과 선택 프로젝트 격리를 확인했다. 기존 세 범위의 스칼라·계산식/목록 정적 투영 회귀도 통과했다.

## 독립 reader의 제한과 미실행 범위

고정된 MPXJ16.10.0 strict reader는 XER의 두 배정 숫자를 읽었고 무시 오류는0이었다. XML에서는 기업 정의를 읽지만 이번 검사에서 공식 `f408...` 배정 요소의 값을 읽지 않았다. 다른 범위의 기업 값도 이 reader로 왕복 통과했다고 주장하지 않는다. 배포 대응 소스의 `MSPDIReader.readAssignmentExtendedAttributes` 등은 ExtendedAttribute 값을 처리하며 이 기업 요소를 읽는 경로가 없다. 따라서 XML 값 보존은 별도 ElementTree/Decimal·native 재수입으로 판정했고, MPXJ의 기업 값 왕복 통과로 보고하지 않는다. [검사기](../../../scripts/verify-enterprise-udf.py)의 결과에 제한을 명시했다.

실제 Microsoft Project/P6 열기·재계산·재저장, macOS/스토어 실행은 미실행이다. 값 목록/GUID 값 참조·다중 값, Flag/Cost/Duration 등 추가 형식과 외부 계산식 실행은 이번 스칼라 범위에 포함하지 않는다. 원본 XML과 손실 진단을 보존한다.

## 빌드에서 보정한 부분

확장된 편집 명령 variant의 암시 변환/수명 관리가 두 비교 화면에서 GCC 컴파일 메모리 사용을 크게 늘렸다. 타입을 제한한 명령 생성과 공통 구현의 copy/move/destructor로 정리하고, 비교 화면의 채택 명령 형식을 명시했다. 최종 코드는 원래 Release 최적화를 유지하며 임시 최적화/GC 옵션은 남기지 않았다. 정책 채택·원자 편집·전체 회귀가 함께 통과했다.

offscreen 검사의 한국어 폰트가 없으면 PDF가 텍스트를 생성하지 않는 것을 확인해 기존 Windows 검사와 같이 Malgun Gothic을 등록했다. 숫자/빈 값의 XLSX 표현과 PDF 텍스트의 공백 분할은 실제 reader 결과를 반영했다. 글자가 없는 PDF를 성공 증거로 사용하지 않았다.

## 재현

```powershell
$taskWorkspace = (Get-Location).Path
$env:PATH = "$taskWorkspace/.deps/Qt/6.8.3/mingw_64/bin;$taskWorkspace/.deps/Qt/Tools/mingw1310_64/bin;$env:APPDATA/Python/Python314/Scripts;$env:PATH"
cmake --build build-governance --parallel 12
ctest --test-dir build-governance --output-on-failure --parallel 4 --output-junit "$taskWorkspace/build-governance/enterprise-udf-ctest.xml" --output-log "$taskWorkspace/build-governance/enterprise-udf-ctest.txt"
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-enterprise-udf.py build-governance/enterprise-udf-qa --reader
.deps/report-verification/Scripts/python.exe scripts/verify-enterprise-udf.py build-governance/enterprise-udf-qa --reports
Push-Location build-governance
./jobs_enterprise_udf_tests.exe -platform windows --ui
Pop-Location
python scripts/check-cpp-quality.py format --tool .deps/cpp-quality/Lib/site-packages/clang_format/data/bin/clang-format.exe
```

[검증 파일/소스 SHA256](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/enterprise-assignment-udf-2026-11-10/verification-summary.json). 위 결과는 이 지원 범위의 증거이며 전체 GAP-03/09나 외부 제품 동등성의 완료 선언이 아니다.
