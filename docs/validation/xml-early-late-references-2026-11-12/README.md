# XML Early/Late 참조 검증

2026-11-12 · XMLE-01~05 · Windows / Qt6.8.3 MinGW13.1 Release.

[지원 계약](../../xml-early-late-references.md), [C++ 검사](../../../tests/XmlEarlyLateTests.cpp), [fixture](../../../tests/XmlEarlyLateFixture.h), [독립 검사기](../../../scripts/verify-xml-early-late.py).

## 결과

최종 전체 **175/175·259.48초**, 실패/건너뜀0이다. 로컬 HTTP/WebSocket 통합 검사를 포함해 제한 밖에서 실행했다. 파서 시드 **800개(schema1~51)**와 C++ 형식 **471파일** 차이0을 통과했다. [빌드](build.txt), [전체 CTest](ctest.txt), [JUnit](ctest.xml), [형식](quality.json), [독립 보고서 대조](independent-reports.json), [독립 리더 대조](independent-reader.json).

독립 Python은 raw 입력·실적을 읽고 서로 다른 근무 달력의 분/이정표 후보를 열거한다. 전진 최소 날짜와 후진 최대 실행 가능 날짜를 따로 계산하며 native 좌표나 수식 결과를 oracle로 재사용하지 않는다. XML 계획60칸·XER 계획48값·수식18개와 보고서 예측60칸(미설정9칸), XLSX21시트·PDF16페이지를 대조했다. MPXJ strict reader는 작업7행·배정4개·판독 오류0이다.

계획 C는11월12일09:02~09:04, Late는09:04~09:06이다. 예측 B는 실제 시작09:06과 달리 잔여 Early Start09:07을 사용하고 Late Start는11월13일09:00, 완료는09:04다. 같은 입력의 XML/XER는 계획 기준을 유지한다. 완료 A의 미래 날짜는 Null이며 프로젝트/WBS는 미완료 대상만 요약한다.

Windows800×800 미리보기에서 완료 미설정 사유와 후진 날짜·출처·참조 삽입을 확인했다. 표의 상태일 변경 후 오래된 날짜 제거, undo·재계산 후 복원도 검사한다.

실행 패키지291파일의 PPM/CLI 해시가 최종 빌드와 일치한다. Qt·MinGW 경로가 없는 PATH에서 CLI가 schema51 원본을 읽고 종료0을 반환했다. [실행 기록](runtime.json), [배포 로그](runtime-deploy.txt), [CLI 읽기](runtime-inspect.json).

[화면](timing-small.png), [XML](timing.xml), [XER](timing.xer), [입력](native.json), [원본](source.ppm), [매핑/진단](mapping.json), [XLSX](fields.xlsx), [PDF](fields.pdf). [verification-summary.json](verification-summary.json)에 소스와 자료 SHA-256을 기록한다.

## 범위와 보정

기본 CPM 근무분 시계·휴일/주말의 이정표, PDM 혼합 달력, ALAP 배치, 선택 평준화 자원 순서 후진·미산정 가용 플래그, 공수/곡선·날짜 고정 잔여 목표를 검사한다. stale/충돌/취소·다른 상태일/입력·누락/중복 행, 잘못된 Late 좌표와 유효한 Early의 격리, 완료 제외/빈 그룹/모두 완료·범위별 참조를 확인한다. schema51/구버전 거절·동결/시나리오·통화/ID 전후 사본·복구를 확인한다.

초기 빌드의 날짜 정규화 선언 헤더 누락을 보정했다. 초기 fixture는 PDM에서 허용하지 않는 자원 자체 달력을 지정해 거절되었으며 작업별 달력만 사용하도록 수정했다. 독립 XER 검사기는 기존 Start/Finish UDF가 시각 보존 텍스트로 투영되는 계약에 맞췄다. 제품의 범위/형식 검증을 완화하지 않았다.

## 재현과 남은 작업

Qt/MinGW/CMake를 PATH에 준비한다. 로컬 HTTP/WebSocket 서버 검사는 제한 밖 실행 권한을 필요로 할 수 있다.

```powershell
cmake --build build-governance --parallel 10
ctest --test-dir build-governance --output-on-failure --parallel 4
.deps/report-verification/Scripts/python.exe scripts/verify-xml-early-late.py build-governance/xml-early-late-qa --reports
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xml-early-late.py build-governance/xml-early-late-qa --reader
```

추가 Project 함수·시간대 변환과 XML 실적 원장 채택은 기존 잔여다. 실제 MS Project/P6 재계산·재저장은 제품 환경과 실제 사례가 필요하며 미완료로 유지한다. macOS·스토어·실제 서버 배포도 이 Windows 로컬 결과로 검증 완료 처리하지 않는다. 이전 증분 자료와 미커밋 작업을 보존하며 자동화는 중복 생성하지 않는다.

이번 실행은 XMLE-01~05 구현·검증·문서·실행 패키지를 완료했으며 미해결 native 차단 사유는 없다. 다음 기존 잔여 후보는 XML 실적의 명시적 원장 채택과 추가 Project 함수·시간대 변환이다. 실제 제품 재계산·재저장은 외부 실행 환경과 사례가 필요한 미완료 검증으로 유지한다.
