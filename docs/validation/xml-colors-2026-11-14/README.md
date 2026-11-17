# XML RGB·QBColor 검증 · 2026-11-14

[기능 계약](../../xml-color-functions.md) · XCL-01~06. 색상 바이트 순서·성분 제한·짝수 반올림·16색 팔레트·Null/범위 오류와 조건부 schema70를 검사한다.

[Python 검사기](../../../scripts/verify-xml-colors.py)는 바이트 결합과 독립 팔레트 계산으로5,609건을 판독한다. [Windows 비교기](../../../scripts/verify-xml-colors-windows.py)는 RGB5,593건·직접 식17건·오류8건을 VBScript로, QBColor16색을 .NET Microsoft.VisualBasic으로 실행한다. 실제 Project/P6 비교는 아니다.

일반 회귀 **226/226·769.76초**를 통과했다. 대용량 단독 **1/1·167.64초**를 통과했다. 테스트 227개를 확인했다. 표준 시간 제한 실패 없이 모두 통과했다. 관련 58개는 최종 전체 결과에서 추출했다. 파서 **930개**(schema1~70), C++ 형식 **559파일** 차이0이다. [최종 빌드](build.txt)·[전체 CTest](ctest.txt)·[JUnit](ctest.xml)·[대용량 단독](isolated.txt)·[단독 JUnit](isolated.xml)·[집중 부분집합](focused.xml)·[형식](quality.json)·[독립 검사](independent.json)·[Windows 비교](windows.json)·[결과/해시](verification-summary.json).

독립 바이트 결합/팔레트 계산5,609건이 모두 일치했다. RGB5,593건·직접 식17건·오류8건은 Windows VBScript, QBColor16색은 Windows .NET Microsoft.VisualBasic과 일치했다. XML20값(전파 메타데이터2칸 포함)/결과 없음4칸·식 정의32개와 XER20값, XLSX66행·PDF19쪽을 판독했다. [첫 빌드](initial-build.txt)·[첫 집중 결과](initial-focused.txt)·[입력](native.json)·[환산/동결 사본](archived.json)·[native 파일](source.ppm)·[XML](operators.xml)·[XER](operators.xer)·[매핑](mapping.json)·[직접 식](cases.json)·[전체 색상](vectors.json)·[Windows RGB 로그](vbscript.txt)·[팔레트 로그](palette.txt)·[XLSX](fields.xlsx)·[PDF](fields.pdf)·[PDF 화면](report-page.png)·[편집기](editor.png)·[800×650 화면](editor-small.png).

Windows 실행 패키지 **298파일**의 앱/CLI 해시가 최종 빌드와 같다. Qt/MinGW가 없는 PATH에서 CLI가 schema70 문서를 읽고 종료0을 반환했다. 앱은 Python/VBScript 없이 C++에서 계산한다. 실제 Project/P6 실행 비교는 수행하지 않았다. [패키지](runtime.json)·[배포](runtime-deploy.txt)·[파일 읽기](runtime-inspect.json).
