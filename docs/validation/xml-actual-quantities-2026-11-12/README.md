# XML 실제 기간·보고 공수 참조 검증

2026-11-12 · XMLQ-01~05 · Windows / Qt6.8.3 MinGW13.1 Release.

[지원 계약](../../xml-actual-quantities.md), [C++ 검사](../../../tests/XmlQuantityTests.cpp), [fixture](../../../tests/XmlQuantityFixture.h), [독립 검사기](../../../scripts/verify-xml-actual-quantities.py).

## 결과

최종 전체 **169/169 · 254.72초**, 실패/건너뜀0이다. 결정적 파서 시드 **762개(schema1~48)**와 C++ 형식 **461파일** 차이0을 통과했다. [빌드](build.txt), [최종 검사 빌드](test-build.txt), [전체 CTest](ctest.txt), [JUnit](ctest.xml), [형식](quality.json).

독립 Python은 native 원장을 직접 읽어 상태일·동일 상태일 정정·작업/자원/날짜별 최신 값을 선택하고 Decimal로 자원분을 합산한다. XML **50칸(미설정15칸)**·수식11개·XER 숫자30값·보고서50칸, XLSX24시트와 PDF23페이지를 대조했다. MPXJ strict reader는 오류0·작업7행·배정5개다. [보고서 대조](independent-reports.json), [리더 대조](independent-reader.json).

Windows800×700에서 한글·수식 입력·worker 미리보기·참조 삽입·상태일 출처, 실제 공수75.01·명시0·미설정 표시와 상태일 undo를 확인했다. [화면](quantity-small.png). 작업별 실제 기간은 시작/완료 차이360분과 다른 보고 기간300분을 사용한다. [XML](quantities.xml), [XER](quantities.xer), [입력](native.json), [매핑/진단](mapping.json), [원본 파일](source.ppm), [XLSX](fields.xlsx), [자원 XLSX](resources.xlsx), [PDF](fields.pdf).

실행 패키지291파일의 PPM/CLI가 빌드와 같은 SHA-256임을 확인했다. Qt·MinGW 경로 없는 PATH에서 CLI가 schema48 원본을 읽고 종료0을 반환했다. [배포/파일 해시](runtime.json), [배포 로그](runtime-deploy.txt), [CLI 파일 읽기](runtime-inspect.json).

[verification-summary.json](verification-summary.json)에 최종 JUnit/독립 판독 결과와 관련 소스·검증 자료 SHA-256을 기록한다.

첫 전체 실행은160개 통과·9개 실패였다. 기존 검사의 미래 버전48 거절 가정을 새 상한에 맞춰49로 수정했다. 이전 하한/잘못된 값 거절과 신규 schema47의 수량 바인딩 거절은 유지한다. [첫 실행](ctest-first.txt), [첫 JUnit](ctest-first.xml), [첫 상세 로그](ctest-first.log). 화면 검사에는 기존 실적 검사와 같은 한글 폰트를 명시했다.

## 검사 범위

진척 원장/기간 실적 원장의 상태일 선택·미래 제외·동일 상태일/과거 정정, 미보고와 명시0·최신 공수 비우기를 구별한다. 작업 실제 기간·프로젝트/자원/배정별 보고 공수와 Task/Resource/Project 참조, WBS/자원 그룹 합계·빈 실적 그룹·요약 기간 미지원, 정수1/100자원분을 확인한다.

불완전/충돌/stale/취소된 계획 결과와 실적 독립성, 입력 객체 격리·취소 전파·공수 범위 초과/음수 오류를 검사한다. schema48 저장/복구·동결/시나리오·통화 전환/ID 이관 사본, 날짜 전용 schema47 유지와 구버전 수량 바인딩 거절을 확인한다. XML 식 재작성과 실적 원장 없는 재가져오기의 Null, XLSX/PDF·XER 정적 결과를 구별한다.

## 재현과 경계

Qt/MinGW/CMake를 PATH에 준비한다.

```powershell
cmake --build build-governance --parallel 12
ctest --test-dir build-governance --output-on-failure --parallel 4
.deps/report-verification/Scripts/python.exe scripts/verify-xml-actual-quantities.py build-governance/xml-quantity-qa --reports
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xml-actual-quantities.py build-governance/xml-quantity-qa --reader
```

Actual Work는 **보고된 공수의 합계**이며 미보고 기간을 포함한 전체 실제 공수의 완전성을 보장하지 않는다. 프로젝트/자원·WBS 내장 실제 기간, Remaining/Baseline/Early/Late와 XML 실적 자동 채택은 후속이다. 실제 Microsoft Project/P6 열기·재계산·재저장, macOS/스토어 실행은 미검증이다. 기존 증분의 검증 자료는 변경하지 않는다.
