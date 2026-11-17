# XML 배정·기간별 실제 공수·원가 검증

2026-11-12 · [XAP-01~06](../../xml-period-actual-adoption.md).

초기 native 2개 일별 보고와 누계1개를 보존하고 XML의 명시5개 날짜/배정 행을 채택한다. 일별 원장7건·누계4건에서 후속 정정과 다른 원본 채택을 거쳐 일별13건·누계8건으로 이어지는 append-only 연결을 검사한다. 공수25501u·배분4775센트·누계14000센트를 구별한다. 0.6초 정밀도, 0/미보고와 native 값 보존, 노동/장비 매핑, XML 작업 누계/날짜별 원가 대사와 물리 진척 보존을 확인한다.

다일/단위/중복/통화/미지원 정밀도/미래 범위·부족한 누계와 마지막 행 오류, 중복 재수입/정정/ID 이관/통화 전환, 미래 보고·취소/변경·읽기 전용·재무기간 마감, schema11 저장/복구·Usage/감사 보고와 800×650 검토 화면을 검사한다.

전체 **181/181·254.38초**, 집중 **6/6**, 실패/건너뜀0이다. 로컬 서버 통합 검사를 포함한다. 파서 시드 **814개**, C++ 형식 **487파일** 차이0을 통과했다. [빌드](build.txt), [전체 CTest](ctest.txt), [JUnit](ctest.xml), [집중](focused.txt), [형식](quality.json), [독립 판독](independent-reports.json), [결과/해시](verification-summary.json).

Windows 실행 패키지 **291파일**의 PPM/CLI 해시가 최종 빌드와 같다. Qt/MinGW가 없는 PATH에서 CLI가 채택·정정된 기존 schema11 native 파일을 읽고 종료0을 반환했다. [패키지](runtime.json), [배포](runtime-deploy.txt), [읽기](runtime-inspect.json).

[원본 XML](original.xml), [기존 native](before.json), [첫 채택](merged.json), [후속 정정](corrected.json), [native 파일](source.ppm), [감사 XLSX](audit.xlsx), [감사 PDF](audit.pdf), [800×650 화면](period-small.png). 실제 외부 제품 실행과 표준 실적 XML 쓰기는 이번 검증 범위에 포함하지 않는다.
