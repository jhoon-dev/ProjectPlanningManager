# XML 숫자·백분율·통화 표시 검증 · 2026-11-14

[기능 계약](../../xml-numeric-display.md) · XNF-01~06. FormatNumber/Percent/Currency의 정확한 십진 표시·TriState·옵션/표시 반올림 구분·음수0·빈 문자열·고정 통화 프로필과 조건부 schema69를 검사한다.

[Python 검사기](../../../scripts/verify-xml-numeric-display.py)는 세 함수×8개 자릿수×27개 TriState×25개 수치16,200건을 Decimal로 독립 판독한다. [Windows 비교기](../../../scripts/verify-xml-numeric-display-vbscript.py)는 정확한 binary 값/옵션7,128건·직접 식28건·오류8건을 SetLocale1033으로 실행한다. digits7의 의도한 차이는 별도 기록한다. 실제 Project/P6 비교는 아니다.

일반 회귀 **224/224·658.42초**를 통과했다. 대용량 단독 **1/1·168.89초**를 통과했다. 테스트 225개를 확인했다. 표준 시간 제한 실패 없이 모두 통과했다. 관련 56개는 최종 전체 결과에서 추출했다. 파서 **926개**(schema1~69), C++ 형식 **556파일** 차이0이다. [최종 빌드](build.txt)·[전체 CTest](ctest.txt)·[JUnit](ctest.xml)·[대용량 단독](isolated.txt)·[단독 JUnit](isolated.xml)·[집중 부분집합](focused.xml)·[형식](quality.json)·[독립 검사](independent.json)·[Windows 비교](vbscript.json)·[결과/해시](verification-summary.json).

세 함수×8개 자릿수×27개 TriState×25개 수치16,200건이 Python Decimal과 모두 일치했다. binary float에서 정확한 값/옵션7,128건·직접 식28건·오류8건도 Windows VBScript(SetLocale1033)와 일치했다. digits7의 의도한 차이는 [Windows 기록](vbscript.json)에 별도로 남겼다. Text 값의 후행0·괄호·구분자를 정확히 확인하며 XML20값(전파 메타데이터2칸 포함)/결과 없음4칸·식 정의32개와 XER20값, XLSX66행·PDF19쪽을 판독했다. [첫 빌드](initial-build.txt)·[첫 집중 결과](initial-focused.txt)·[입력](native.json)·[환산/동결 사본](archived.json)·[native 파일](source.ppm)·[XML](operators.xml)·[XER](operators.xer)·[매핑](mapping.json)·[직접 식](cases.json)·[전체 수치/옵션](vectors.json)·[Windows 로그](vbscript.txt)·[XLSX](fields.xlsx)·[PDF](fields.pdf)·[PDF 화면](report-page.png)·[편집기](editor.png)·[800×650 화면](editor-small.png).

Windows 실행 패키지 **298파일**의 앱/CLI 해시가 최종 빌드와 같다. Qt/MinGW가 없는 PATH에서 CLI가 schema69 문서를 읽고 종료0을 반환했다. 앱은 Python/VBScript 없이 C++에서 계산한다. 실제 Project/P6 실행 비교는 수행하지 않았다. [패키지](runtime.json)·[배포](runtime-deploy.txt)·[파일 읽기](runtime-inspect.json).
