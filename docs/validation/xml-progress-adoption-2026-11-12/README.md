# XML 작업 진척 새 사본 채택 검증

2026-11-12 · XPA-01~06 · Windows / Qt6.8.3 MinGW13.1 Release.

[지원 계약](../../xml-progress-adoption.md), [C++ 검사](../../../tests/XmlProgressTests.cpp), [fixture](../../../tests/XmlProgressFixture.h), [독립 검사기](../../../scripts/verify-xml-progress-adoption.py).

## 결과

최종 전체 **177/177·259.24초**, 실패/건너뜀0이다. 로컬 HTTP/WebSocket 통합 검사를 포함해 제한 밖에서 실행했다. 파서 시드 **808개(지원 schema1~51)**와 C++ 형식 **481파일** 차이0을 통과했다. [빌드](build.txt), [전체 CTest](ctest.txt), [JUnit](ctest.xml), [형식](quality.json), [독립 보고서 대조](independent-reports.json), [독립 리더 대조](independent-reader.json).

독립 Python은 original.xml의 UID·실제 날짜·실제/잔여 기간을 직접 읽어 conversion.json의 선택/매핑과 native 진척4건·48항목을 대조했다. native schema48 원본과 실제 XML 사용자 필드36칸(미설정27칸)·보고서36칸, 최초 원장4행과 정정 후5행의 이전 보고 링크·선택 상태를 검사했다. XLSX23시트·PDF22페이지와 MPXJ strict reader 작업10행·배정9개·오류0을 확인했다.

선택한 실제 기간은120/60/0/0분, 잔여는0/180/60/0분이다. 실제 공수9h·실제 원가999·진척률 원본이 있어도 실제 공수/원가 원장을 만들지 않는다. 미보고의 Actual Duration/Work는 Null, 미착수의 명시 보고 실제 기간0은0이다. 미지원 초 정밀도·분 미만 기간·누락 실제 기간·미래 실제 시작 후보는 묵시 채택하지 않는다.

Windows800×650 검토 창에서 기본 미채택/선택 해제, UID 연결·명시 메타데이터·불완전 행 비활성, worker 원본 재검증·새 폴더 게시·새 사본 열기를 검사했다. 이전 native 실적 파일이 그대로이고 검토 취소·오래된 검토의 거절이 사용자 수정을 보존하는지 확인한다.

실행 패키지291파일의 PPM/CLI 해시가 최종 빌드와 일치한다. Qt·MinGW 경로가 없는 PATH에서 CLI가 채택 진척을 가진 기존 schema48 원본을 읽고 종료0을 반환했다. [실행 기록](runtime.json), [배포 로그](runtime-deploy.txt), [CLI 읽기](runtime-inspect.json).

[검토 화면](progress-small.png), [XML 원본](original.xml), [사용자 필드 XML](fields.xml), [입력](native.json), [native 원본](source.ppm), [변환/채택 기록](conversion.json), [내보내기 매핑](export-mapping.json), [XLSX](fields.xlsx), [정정 후 XLSX](corrected.xlsx), [PDF](fields.pdf). [verification-summary.json](verification-summary.json)에 소스와 자료 SHA-256을 기록한다.

## 검사 범위와 보정

정상 완료·진행·미착수·0기간 이정표, 없는 상태일의 명시 지정·명시 상태일 변경, NA/0/미보고와 누락/분 정밀도·진척률 비추정, 중복 UID/선택·알 수 없는 ID·원본/계획/후보 변경·기존/이미 채택한 원장의 거절을 검사한다. 같은 검토본의 준비 결과가 중복 없이 재현되고 원본/원장/매핑이 일치해야 게시되는지 확인한다. 게시 실패는 새 목적지를 남기지 않는다.

새 구조 없이 기존 schema9/48의 저장/재열기·복구를 재사용하며 native 정정의 추가 전용 연결·원본 불변, 기준선/시나리오·통화/ID 이관을 검사한다. 새 원장 보고서는 선택 상태를 한 번 색인해 대량 이력의 반복 탐색을 피한다.

초기 빌드의 취소 체크 선언 헤더 누락을 보정했다. 독립 검사기는 기존 native JSON에서 비어 있는 기간 실적 배열을 생략할 수 있는 계약을 반영했다. 제품의 파서·원장 검증은 완화하지 않았다.

## 재현과 잔여

Qt/MinGW/CMake를 PATH에 준비한다. 로컬 HTTP/WebSocket 검사는 필요 시 제한 밖에서 실행한다.

```powershell
cmake --build build-governance --parallel 10
ctest --test-dir build-governance --output-on-failure --parallel 4
.deps/report-verification/Scripts/python.exe scripts/verify-xml-progress-adoption.py build-governance/xml-progress-qa --reports
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xml-progress-adoption.py build-governance/xml-progress-qa --reader
```

기존 문서의 ID 매핑·재가져오기 대사/병합, 배정·기간 실제 공수/원가 채택과 실적 XML 쓰기는 후속이다. 추가 Project 함수·시간대 변환과 실제 MS Project/P6 재계산·재저장도 남아 있다. 실제 제품 실행 환경·사례, macOS/스토어/현장 수용은 이번 Windows 로컬 검사로 대체하지 않는다. 자동화·세션을 새로 만들지 않았고 기존 미커밋 작업과 과거 검증 자료를 보존했다.

이번 실행은 XPA-01~06의 새 사본 작업 진척 채택·검증·문서·실행 패키지를 완료했다. 이 지원 부분의 미해결 차단 사유는 없다. 기존 문서 원장 병합과 기간 실제 공수/원가·추가 Project 함수/시간대 변환은 다음 잔여이며 실제 제품 검증은 환경·사례 확보가 필요하다.
