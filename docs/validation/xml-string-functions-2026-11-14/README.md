# XML 문자열 함수 검증 · 2026-11-14

[기능 계약](../../xml-string-functions.md) · XST-01~06. Replace/InStrRev/StrReverse의 BMP 위치·ASCII 비교·시작 이후 결과·횟수/비겹침·끝 경계·반전·Null 구분과 네 범위·요약/배정 전파·현재/원본 스냅샷·미리보기·교환/보고·schema59/전체 사본을 확인한다.

계약 검사는 시작/횟수/비교·빈 문자열/검색어·Null/오류·전체 인수 평가·인수 개수·취소를 확인한다. 잘못된 UTF-8과 BMP 밖 문자·서로게이트를 거절하고 결합문자 반전·반전 왕복, 입력/결과4096bytes 경계와 치환 증가 초과를 확인한다. 저장된 stale/777을 결과나 오류 대신 쓰지 않는다.

현재/동결 사본·환산/ID 이력·복구/읽기 전용·이름 변경·undo/redo와 활성59/이전 태그 거절·비활성44를 검증한다. 통화 전환 뒤12,34가6,17로 다시 계산되며 문자열 자체를 통화로 환산하지 않는다.

[Python 독립 검사기](../../../scripts/verify-xml-string-functions.py)는 Unicode 코드 포인트·접미 문자열·앞에서 비겹침 치환·제한한 접두부 rfind를 이용해 생성1024행의 결과3072건과 반전 왕복을 확인한다. [VBScript 대조기](../../../scripts/verify-xml-string-vbscript.py)는 Null 없는 BMP/ASCII·정수 제어 인수의 같은3072건을 Windows 함수로 비교한다. 문자열 데이터는 UTF-16 코드의16진 TSV로 읽으며 원본 식이나 문자열을 코드로 실행하지 않는다. 초기 고정 의미 탐침에서는 Replace의 시작 이후 반환/빈 검색/횟수0과 Null 오류, InStrRev 끝 경계를 확인한다. VBScript의 InStrRev Null 오류는 VBA 문서/native Null 전파와 다르며 비교 범위에서 제외한다. 실행 앱은 Python/VBScript를 사용하지 않는다.

UI는 예시4개·삽입·비동기 미리보기·저장/undo·취소·표 값과800×650 배치를 확인한다. XML 값/정의 재계산·XER 정적 값·XLSX/PDF를 별도로 판독한다. 전체 VBA/NLS 또는 실제 Microsoft Project 제품 실행 동등성 검증은 아니다.

일반 회귀 **204/204·299.23초**를 통과했다. 대용량 단독 **1/1·167.80초**까지 합계 **205/205**, 실패/건너뜀0이다. 관련36개는 최종 전체 결과에서 추출했다. 파서 **886개**(schema1~59), C++ 형식 **526파일** 차이0이다. [전체 빌드](build.txt)·[첫 집중](initial-focused.txt)·[전체 CTest](ctest.txt)·[JUnit](ctest.xml)·[대용량 단독](isolated.txt)·[단독 JUnit](isolated.xml)·[집중 부분집합](focused.xml)·[형식](quality.json)·[Python 독립 검증](independent.json)·[VBScript 비교](vbscript.json)·[대조 원시 출력](vbscript.txt)·[고정 의미 탐침](semantics.txt)·[결과/해시](verification-summary.json).

생성1024행의 문자열 결과3072건과 반전 왕복이 Python 검사에 통과했고, 같은3072건이 Windows VBScript와 일치했다. 직접 식32건, XML24값(전파 메타데이터2칸 포함)·식 정의34개, XER24값, XLSX74행·PDF20쪽을 확인했다. [입력](native.json)·[환산/동결 사본](archived.json)·[native 파일](source.ppm)·[XML](numbers.xml)·[XER](numbers.xer)·[매핑](mapping.json)·[직접 식](cases.json)·[생성 사례](vectors.json)·[VBScript 데이터](vbscript-vectors.tsv)·[XLSX](fields.xlsx)·[PDF](fields.pdf)·[PDF 화면](report-page.png)·[편집기](editor.png)·[800×650 결과](editor-small.png).

Windows 실행 패키지 **298파일**의 앱/CLI 해시가 최종 빌드와 같다. Qt/MinGW가 없는 PATH에서 CLI가 schema59 문서를 읽고 종료0을 반환했다. 실행 앱은 Python/VBScript 없이 C++에서 계산한다. [패키지](runtime.json)·[배포](runtime-deploy.txt)·[파일 읽기](runtime-inspect.json).
