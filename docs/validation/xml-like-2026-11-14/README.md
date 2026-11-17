# XML Like 검증 · 2026-11-14

[기능 계약](../../xml-like-operator.md) · XLK-01~06. BMP 정확 비교·와일드카드/목록/범위/부정·빈 목록/리터럴 이스케이프·패턴 전체 검증·Null/오류·유한 작업량·우선순위/과거 문법 보존을 확인한다.

[Python 검사기](../../../scripts/verify-xml-like.py)는 유효 패턴을 정규식으로 변환해 전수 조합4836건과 생성4096건, 합계8932건을 대조한다. [Windows 비교기](../../../scripts/verify-xml-like-dotnet.py)는 같은8932건을 .NET VisualBasic Binary Like로 실행한다. 비ASCII 숫자와 빈 입력의 잘못된 패턴 처리 차이는 별도로 남기며 실제 Project/P6 비교가 아니다.

직접 식30건·잘못된 패턴/UTF-8/보조 평면·문자열 전용/Null·입력4,096 bytes와2,000,000상태 제한·취소·토큰 경계, 활성65/비활성44·이전 태그 거절·전체 사본/환산/ID 이력·복구/읽기 전용·편집/undo/redo를 검사한다. XML/XER20값·결과 없음4칸·전파2칸, XLSX 값과 별도 오류 상태·PDF를 판독한다. UI는 예시 삽입·worker 미리보기·저장/취소와 작은 화면을 확인한다.

일반 회귀 **216/216·805.89초**를 통과했다. 대용량 단독 **1/1·161.81초**를 통과했다. 테스트 217개를 확인했다. 표준 시간 제한 실패 없이 모두 통과했다. 관련 48개는 최종 전체 결과에서 추출했다. 파서 **910개**(schema1~65), C++ 형식 **544파일** 차이0이다. [최종 빌드](build.txt)·[전체 CTest](ctest.txt)·[JUnit](ctest.xml)·[대용량 단독](isolated.txt)·[단독 JUnit](isolated.xml)·[집중 부분집합](focused.xml)·[형식](quality.json)·[독립 검사](independent.json)·[Windows 비교](dotnet.json)·[결과/해시](verification-summary.json).

유효 BMP 패턴 8,932건은 독립 정규식 및 Windows .NET Binary Like 결과와 모두 일치했다. 비ASCII 숫자와 빈 입력의 잘못된 패턴 차이는 [비교 기록](dotnet.json)에 명시한다. 직접 식30건, XML20값(전파 메타데이터2칸 포함)/결과 없음4칸·식 정의32개와 XER20값, XLSX66행·PDF19쪽을 확인했다. [첫 빌드](initial-build.txt)·[첫 집중 결과](initial-focused.txt)·[입력](native.json)·[환산/동결 사본](archived.json)·[native 파일](source.ppm)·[XML](operators.xml)·[XER](operators.xer)·[매핑](mapping.json)·[직접 식](cases.json)·[패턴 사례](vectors.json)·[Windows 로그](dotnet.txt)·[XLSX](fields.xlsx)·[PDF](fields.pdf)·[PDF 화면](report-page.png)·[편집기](editor.png)·[800×650 화면](editor-small.png).

Windows 실행 패키지 **298파일**의 앱/CLI 해시가 최종 빌드와 같다. Qt/MinGW가 없는 PATH에서 CLI가 schema65 문서를 읽고 종료0을 반환했다. 앱은 Python/.NET 없이 C++에서 계산한다. 실제 Project/P6 실행 비교는 수행하지 않았다. [패키지](runtime.json)·[배포](runtime-deploy.txt)·[파일 읽기](runtime-inspect.json).
