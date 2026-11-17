# 공수 기반 작업 기간 병합 검증

2026-11-13 · [XWP-01~06](../../xml-work-driven-progress.md). 표본 상태일은 2026-11-16 11:00이며 이전/미래 보고를 포함한다.

전체 1차 **186/187·411.81초**에서 기존 10,000작업 교환 검사가180초 제한으로 시간 초과했다. 코드 변경 없는 단독 **재검증1/1·159.95초**를 통과해187개를 확인했다. 건너뜀0이며 [단독 로그](retry.txt)·[단독 JUnit](retry.xml)에 결과를 보존한다. 집중 **12/12**이며 로컬 서버 통합 검사를 포함한다. 파서 시드 **832개**, C++ 형식 **495파일** 차이0을 통과했다. [빌드](build.txt), [전체 CTest](ctest.txt), [JUnit](ctest.xml), [집중](focused.txt), [형식](quality.json), [독립 계산/보고](independent.json), [결과/해시](verification-summary.json).

Windows 실행 패키지 **291파일**의 PPM/CLI 해시가 최종 빌드와 같다. Qt/MinGW가 없는 PATH에서 CLI가 공수 곡선·기간 보고·잔여 공수를 보존한 schema24 파일을 읽고 종료0을 반환했다. [패키지](runtime.json), [배포](runtime-deploy.txt), [읽기](runtime-inspect.json).

[원본 XML](original.xml), [기존 문서](before.json), [병합](merged.json), [후속 정정](corrected.json), [기본 예측](forecast.json), [수량별 예측](cases.json), [날짜 목표 문서](dated.json), [날짜 목표 예측](dated-forecast.json), [native 파일](source.ppm), [감사 XLSX](audit.xlsx), [감사 PDF](audit.pdf), [PDF 감사 화면](audit-work-page.png), [800×680 연결 화면](work-merge-small.png), [800×680 수신/검토 화면](work-merge-values-small.png).

표본 A는 nominal 계획999분·공수9000u지만, 다른 자원 달력과 첫20분의 배정률 곡선으로 계산한 계획 기간은94분이다. B는 nominal333분·공수5001u이고 계획 기간87분이다. XML의 A 실제30분·잔여123분·잔여공수3501u를 그대로 보고로 기록하며 native 예측은 주말과 자원 달력/곡선을 반영해 11월17일09:00~09:37, 잔여37분이 된다. 마지막 분도 자원별68u/33u로 정확히 소비한다. 기존 실제 공수123u와 모든 계획 입력은 유지한다.

공수 고정·배정률 고정 공수·기간 기준·미착수 공수0·미선택을 섞고 완료의 미보고/명시0·누락/부정확/음수/한도/완료 모순·기간 대상의 잘못된 RemainingWork 무시를 검사한다. 처음 원장2건에서 한 revision의4건을 추가해6건이 되고, 후속 정정은7건으로 보존한다. 미래 보고·중복·ID 이관·마감·읽기 전용·취소·오래된 미리보기·원본 파일·기존 schema24 저장/복구와 작은 화면도 포함한다.

독립 검사는 원본 XML·출처 해시/UID·native 원장/전체 계획/스냅샷과 기존 실제 공수를 대조한다. 서로 다른 잔여 수량8개, 기본3501u와 날짜별 목표1000u를 유지하는 사례까지 총10개 예측의 자원별 분 구간을 직접 열거한다. 기간/공수 원장의 XLSX와 PDF 표제를 확인하고 PDF 감사 페이지를 렌더링해 새 열의 표시를 확인한다. 실제 MS Project/P6 실행은 미검증이다.
