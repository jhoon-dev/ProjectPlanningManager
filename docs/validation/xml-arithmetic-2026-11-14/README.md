# XML 산술 연산 검증 · 2026-11-14

[기능 계약](../../xml-arithmetic-operators.md) · XOP-01~06. 거듭제곱/정수 나눗셈의 우선순위·짝수 반올림·음수/0/범위·단위/Null·과거 문법 보존과 네 범위·요약/전파·미리보기/저장/교환/보고를 확인한다.

[독립 검사기](../../../scripts/verify-xml-arithmetic.py)는2048거듭제곱을80자리 수치로,2048정수 나눗셈을 정확한 유리수/정수로 재계산한다. XML/XER20값·결과 없음4칸·전파 메타데이터2칸과 XLSX 값/별도 오류 상태를 판독한다. [Windows 비교기](../../../scripts/verify-xml-arithmetic-vbscript.py)는 같은4096이항 수치 사례를 비교한다. VBScript의 단항 부호 우선순위 차이는 별도 기록하며 실제 Project/P6 실행 비교가 아니다.

직접 식38건과 오류/Null·32비트 피연산자/몫 한도·새 문법의 Mod 우선순위 및 기존 식의 해석 보존, 활성63/비활성44·이전 태그 거절·보관 사본/통화/ID 이력·취소/복구/읽기 전용·편집/undo/redo를 검사한다. UI는 예시 삽입·worker 미리보기·저장/취소와 작은 화면을 확인한다.

일반 회귀 **212/212·487.22초**를 통과했다. 대용량 CTest는180초 제한으로 시간 초과했다. 코드/실행 파일 변경 없이300초 한도 직접 재검증 **1/1·208.46초**를 확인했다. 기능213개를 확인했으나 표준 시간 제한 실패1건은 보존한다. [확장 기록](extended.json). 관련44개는 최종 전체 결과에서 추출했다. 파서 **902개**(schema1~63), C++ 형식 **538파일** 차이0이다. [전체 빌드](build.txt)·[첫 집중](initial-focused.txt)·[전체 CTest](ctest.txt)·[JUnit](ctest.xml)·[대용량 단독](isolated.txt)·[단독 JUnit](isolated.xml)·[집중 부분집합](focused.xml)·[형식](quality.json)·[독립 검사](independent.json)·[Windows 비교](vbscript.json)·[결과/해시](verification-summary.json).

80자리 거듭제곱2048건·정확한 정수 나눗셈2048건과 Windows VBScript4096건이 모두 정확히 일치했다. VBScript의 -2^2=4와 native VBA 문서 기준 -4 차이는 별도 기록한다. 직접 식38건, XML20값(전파 메타데이터2칸 포함)/결과 없음4칸·식 정의32개와 XER20값, XLSX66행·PDF19쪽을 확인했다. [입력](native.json)·[환산/동결 사본](archived.json)·[native 파일](source.ppm)·[XML](operators.xml)·[XER](operators.xer)·[매핑](mapping.json)·[직접 식](cases.json)·[수치 사례](vectors.json)·[VBScript 로그](vbscript.txt)·[XLSX](fields.xlsx)·[PDF](fields.pdf)·[PDF 화면](report-page.png)·[편집기](editor.png)·[800×650 화면](editor-small.png).

Windows 실행 패키지 **298파일**의 앱/CLI 해시가 최종 빌드와 같다. Qt/MinGW가 없는 PATH에서 CLI가 schema63 문서를 읽고 종료0을 반환했다. 앱은 Python/VBScript 없이 C++에서 계산한다. [패키지](runtime.json)·[배포](runtime-deploy.txt)·[파일 읽기](runtime-inspect.json).
