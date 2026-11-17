# XML 일별 실적 구간 합산 검증

2026-11-12 · [XAF-01~05](../../xml-daily-actual-fragments.md).

원본의 명시 구간12개를 날짜/배정별5개 행으로 검토한다. 그중 공수/원가 종류별4개 그룹이 여러 구간을 합산한다. 공수25501u·배분4775센트·누계14000센트는 기존 관측 보존을 포함한 첫 채택의 검산 값이다. native 일별 원장7건·누계4건에서 후속 정정과 새 원본 채택 후 일별13건·누계8건의 불변 연결을 확인한다.

맞닿는 경계/역순/빈 시간/다른 종류의 같은 범위, 명시0과 미보고, 정확한 합계/한도, 같은 종류의 중복/겹침/포함/길이0·잘못된 시각/단위/수치의 전체 일자 거절을 검사한다. 기존 일별·작업 진척 채택, 재수입/정정·누계 부족·취소/변경, schema11 저장·복구·감사 보고와 800×650 화면을 포함한다.

전체 **183/183·272.33초**, 집중 **8/8**, 실패/건너뜀0이다. 로컬 서버 통합 검사를 포함한다. 파서 시드 **820개**, C++ 형식 **489파일** 차이0을 통과했다. [빌드](build.txt), [전체 CTest](ctest.txt), [JUnit](ctest.xml), [집중](focused.txt), [형식](quality.json), [독립 판독](independent-reports.json), [결과/해시](verification-summary.json).

Windows 실행 패키지 **291파일**의 PPM/CLI 해시가 최종 빌드와 같다. Qt/MinGW가 없는 PATH에서 CLI가 합산·정정된 기존 schema11 native 파일을 읽고 종료0을 반환했다. [패키지](runtime.json), [배포](runtime-deploy.txt), [읽기](runtime-inspect.json).

[원본 XML](original.xml), [수정된 원본](updated.xml), [기존 native](before.json), [첫 합산](merged.json), [후속 정정](corrected.json), [native 파일](source.ppm), [감사 XLSX](audit.xlsx), [감사 PDF](audit.pdf), [800×650 연결 화면](fragments-small.png), [800×650 합계 화면](fragments-values-small.png). 실제 MS Project/P6 실행·다일 자동 배분과 표준 실적 XML 쓰기는 이번 검증 범위가 아니다.
