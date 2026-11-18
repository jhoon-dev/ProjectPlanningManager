# XML 외부 계산식 실행 검증

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-formulas-2026-11-11)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

2026-11-11 · XMLF-01~05 · Windows / Qt6.8.3 MinGW13.1 Release.

## 검사 결과

- 전체 **161/161 · 287.99초**: [CTest](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-formulas-2026-11-11/ctest.txt), [JUnit/검사별 출력](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-formulas-2026-11-11/ctest.xml). 첫 전체 실행에서 모두 통과했다.
- XML/native 식·roll-up/down·마스크·시각/추가 형식·목록/GUID·기업/배정 UDF·객체 필드·파서 집중 **21/21 · 17.49초**: [기록](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-formulas-2026-11-11/focused.txt), [JUnit](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-formulas-2026-11-11/focused.xml).
- 전체 실행 후 안정 ID의 대소문자 구분을 보강했다. 최종 집중 재검사 **21/21 · 15.69초**와 최종 전체 빌드를 별도 보존한다. 이 변경은 이름/네임스페이스의 ASCII 정규화와 안정 ID의 대소문자 보존을 분리한다.
- 파서 **714개** 결정적 시드, schema1~44·새 데이터의 구버전 envelope 직접 거절·checksum/body 분리·저장소 일치를 검사했다.
- C++ 형식 **448파일**, 차이0으로 통과했다. [전체 Release 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-formulas-2026-11-11/build.txt), [최종 전체 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-formulas-2026-11-11/final-build.txt), [최종 집중](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-formulas-2026-11-11/final-focused.txt), [형식 결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-formulas-2026-11-11/quality.json).

[지원 계약](../../xml-external-formulas.md), [C++ 검사](../../../tests/XmlFormulaTests.cpp), [fixture](../../../tests/XmlFormulaFixture.h), [독립 검사기](../../../scripts/verify-xml-formulas.py)를 따른다.

## 독립 대조

C++ 결과를 다시 호출하지 않고 Python Decimal로 **400개** 사칙/곱·나눗셈 반올림 사례를 대조한다. 원본 입력에서 네 범위의 수치·조건/문자열·금액·기간·일시를 계산해 원본/재내보낸 XML **126칸**, 식 정의 **40개**를 확인한다. XML의 AutoRollDown/SecondaryPID를 해석해 직접 값이 없는 수신 값도 검증한다.

원본 n=999와 요약 Cost=999.99가 native/source.ppm에 남고 XML에는 현재 결과가 나오는지 확인한다. 오류/미설정은 스냅샷으로 대체하지 않는다. XER의 **48개** UDFVALUE는 실제 정적 값과 대조한다. USD→EUR 입력 환산과 원본 요약 Cost의 half-away 환산을 확인한다.

openpyxl은 작업 필드42행·요약8행의 실제 숫자 Cost, 텍스트·미정·요약 식 출처와 식 활성/비활성 표시를 확인한다. pypdf는 **15페이지**의 내용과 출처를 확인한다. MPXJ16.10.0 strict reader는 오류0으로 요약 포함 작업4행·배정2개를 읽는다. 이 리더는 실행 동등성을 검증하지 않으므로 식/결과는 별도 독립 대조로 판정한다. [보고서 결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-formulas-2026-11-11/independent-reports.json), [리더 결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-formulas-2026-11-11/independent-reader.json).

## 실행·보존·화면 범위

네 범위·8개 결과 형식, stable ID/별칭 변경·표준/예약 이름 충돌·대소문자 ID, native/XML 혼합 참조·순환, 논리/비트·BMP 문자열·날짜 경계/소수 초·짝수 반올림·eager IIf/Switch·Null/오류를 검사한다. 비활성/미지원 식 보존, 빈 Formula, 지원식 재활성화와 XML 재교환을 구별한다.

CalculationType=1의 말단 결과 집계, CalculationType=2의 같은 그룹 요약 값 참조, XML 식 원본의 배정 전달과 새 수신 필드의 식/요약 메타데이터 제거를 확인한다. 활성 필드의 직접 입력/지우기 차단, 변경 전파·undo, worker·읽기 전용·stale·취소, 128 통합 프레임 제한과 정상 복구를 확인한다. 조건부 schema44·동결/시나리오·통화 전후 전체 이력·복구·원장 ID 이관을 검사한다.

Windows800×650의 설정/참조/미리보기·비활성 원본 표시·저장/undo·미지원/stale 차단과 배정 식·roll-down 미리보기를 확인한다. [실행](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-formulas-2026-11-11/native-ui.txt), [설정](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-formulas-2026-11-11/settings-small.png), [배정](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-formulas-2026-11-11/assignment-small.png).

[XML](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-formulas-2026-11-11/export.xml), [재내보내기](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-formulas-2026-11-11/reexport.xml), [원본 JSON](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-formulas-2026-11-11/native.json), [원본 파일](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-formulas-2026-11-11/source.ppm), [통화 전환 후 이력 포함 JSON](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-formulas-2026-11-11/converted.json), [매핑/진단](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-formulas-2026-11-11/mapping.json), [XER](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-formulas-2026-11-11/export.xer), [XLSX](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-formulas-2026-11-11/fields.xlsx), [PDF](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-formulas-2026-11-11/fields.pdf), [자원 XLSX](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-formulas-2026-11-11/resources.xlsx), [사칙 사례](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-formulas-2026-11-11/arithmetic.tsv)를 보존한다.

## 경계와 재현

전체 VBA/Project 문법, 계산된 일정 Start/Finish·근무 달력·현재 시각/난수, Choice·일반 Variant/로캘 강제 변환과 지원 외 함수는 구현하지 않았다. 실제 Microsoft Project/P6 열기·재계산·재저장, macOS/스토어 실행은 미실행이다. 식 숫자 상수는 환율로 변환하지 않는다. XML은 비활성 설정을 표현하지 않아 재가져오기는 지원식을 재활성화할 수 있다. 이러한 원본/실행 설정은 source.ppm에 보존한다.

Qt6.8.3 MinGW13.1 도구를 PATH에 준비한다.

```powershell
cmake --build build-governance --parallel 12
ctest --test-dir build-governance --output-on-failure --parallel 4
.deps/report-verification/Scripts/python.exe scripts/verify-xml-formulas.py build-governance/xml-formula-qa --reports
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xml-formulas.py build-governance/xml-formula-qa --reader
```

[verification-summary.json](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-formulas-2026-11-11/verification-summary.json)에 결과와 자료/관련 소스 SHA-256을 기록한다. 이전 증분의 검증 자료는 변경하지 않는다.
