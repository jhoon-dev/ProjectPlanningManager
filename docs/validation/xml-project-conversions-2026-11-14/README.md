# XML Project 변환 검증 · 2026-11-14

[기능 계약](../../xml-project-conversion-functions.md) · XPC-01~06. ProjDateValue/ProjDateConv/ProjDurConv의 ISO/NA·고정 날짜/기간 형식·반올림/범위·네 범위·요약/전파·원본 보존·미리보기/교환/보고·schema61/전체 사본을 확인한다.

[독립 검사기](../../../scripts/verify-xml-project-conversions.py)는 Python datetime의 Gregorian 요일/주차, 직접 영문 이름·21개 표시와기본255를 사용해128일시/2816표시를 대조한다. 기간512분 값×20단위의10240표시는 Decimal ROUND_HALF_UP으로 계산하고, 표시 문자열의 재파싱은 정확한 십진 분 값과 대조한다. 표시 반올림이 원래 분을 항상 보존한다고 가정하지 않는다. 실행 앱은 Python/OS 로캘을 사용하지 않는다.

직접 식은 시각/마이크로초 보존·윤일·NA/Null·영문/주차·입력/출력 경과·추정·음수·최소 척도·월 프로필·범위·잘못된 제어 인수·전체 인수 평가·취소를 확인한다. 과거 함수의 단위 문법과 의미를 변경하지 않는다. stale/777 저장값을 현재 결과나 오류로 쓰지 않는다.

현재/기준선/시나리오·환산/ID 이력·복구/읽기 전용·이름 변경·undo/redo, 활성61/이전 태그 거절·비활성44를 확인한다. 통화 전환 후에도 날짜와 일반 분 입력은 유지된다. UI는예시4개·삽입·비동기 미리보기·저장/undo·취소·표 값과800×650 배치를 확인한다. XML 재계산·XER·XLSX/PDF를 별도로 판독한다. 실제 Microsoft Project/P6 실행 검증은 아니다.

일반 회귀 **208/208·335.90초**를 통과했다. 대용량 단독 **1/1·152.38초**까지 합계 **209/209**, 실패/건너뜀0이다. 관련40개는 최종 전체 결과에서 추출했다. 파서 **894개**(schema1~61), C++ 형식 **532파일** 차이0이다. [전체 빌드](build.txt)·[첫 집중](initial-focused.txt)·[전체 CTest](ctest.txt)·[JUnit](ctest.xml)·[대용량 단독](isolated.txt)·[단독 JUnit](isolated.xml)·[집중 부분집합](focused.xml)·[형식](quality.json)·[Python 독립 검증](independent.json)·[결과/해시](verification-summary.json).

128일시·2816날짜 표시와10240기간 표시/재파싱이 독립 검사에 통과했다. 직접 식20건, XML26값(전파 메타데이터2칸 포함)·식 정의35개, XER26값, XLSX76행·PDF20쪽을 확인했다. 초기 계약의 이름 변경 테스트에 잘못된 필드 ID가 있어 비정상 종료했다. 테스트 참조/존재 검사를 수정했으며 제품 계산 코드는 변경하지 않았다. [초기 결과](initial-focused.txt)·[디버거](debugger.txt)·[수정 집중 결과](refined-focused.txt)를 보존한다. [입력](native.json)·[환산/동결 사본](archived.json)·[native 파일](source.ppm)·[XML](numbers.xml)·[XER](numbers.xer)·[매핑](mapping.json)·[직접 식](cases.json)·[날짜 사례](dates.json)·[기간 사례](durations.json)·[XLSX](fields.xlsx)·[PDF](fields.pdf)·[PDF 화면](report-page.png)·[편집기](editor.png)·[800×650 결과](editor-small.png).

Windows 실행 패키지 **298파일**의 앱/CLI 해시가 최종 빌드와 같다. Qt/MinGW가 없는 PATH에서 CLI가 schema61 문서를 읽고 종료0을 반환했다. 실행 앱은 Python 없이 C++에서 계산한다. [패키지](runtime.json)·[배포](runtime-deploy.txt)·[파일 읽기](runtime-inspect.json).
