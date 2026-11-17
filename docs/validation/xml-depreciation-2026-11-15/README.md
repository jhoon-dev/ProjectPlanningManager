# XML SLN·SYD 검증 · 2026-11-15

[기능 계약](../../xml-depreciation-functions.md) · XDP-01~06. 정액/연수합계 감가상각·소수 life/period·정확한 중간 계산·최종 반올림·Null/범위/overflow와 조건부 schema72를 검사한다.

[독립 검사기](../../../scripts/verify-xml-depreciation.py)는9,032건을 Python Fraction으로 판독한다. [Windows 비교기](../../../scripts/verify-xml-depreciation-windows.py)는 제한된 공통 입력/결과 범위에서 .NET Financial과 비교하며 binary64 차이를 따로 기록한다. 실제 Project/P6 비교는 아니다.

일반 회귀 **230/230·703.60초**를 통과했다. 대용량 단독 **1/1·156.69초**를 통과했다. 테스트 231개를 확인했다. 표준 시간 제한 실패 없이 모두 통과했다. 관련 62개는 최종 전체 결과에서 추출했다. 파서 **938개**(schema1~72), C++ 형식 **565파일** 차이0이다. [최종 빌드](build.txt)·[전체 CTest](ctest.txt)·[JUnit](ctest.xml)·[대용량 단독](isolated.txt)·[단독 JUnit](isolated.xml)·[집중 부분집합](focused.xml)·[형식](quality.json)·[독립 검사](independent.json)·[Windows 비교](windows.json)·[결과/해시](verification-summary.json).

독립 유리수9,032건(overflow100건 포함)이 모두 일치했다. Windows 공통/제한 범위8,714건 중8,713건이 소수6자리에서 정확히 일치하며1건은 binary64 반올림으로0.000001 차이가 났다. [차이 기록](windows-differences.json). 공통 오류5건도 확인했다. 직접 식25건과 XML20값(전파 메타데이터2칸 포함)/결과 없음4칸·식 정의32개, XER20값·XLSX66행·PDF19쪽을 판독했다. [첫 빌드](initial-build.txt)·[첫 집중 결과](initial-focused.txt)·[입력](native.json)·[환산/동결 사본](archived.json)·[native 파일](source.ppm)·[XML](operators.xml)·[XER](operators.xer)·[매핑](mapping.json)·[직접 식](cases.json)·[전체 수치](vectors.json)·[Windows 로그](windows.txt)·[XLSX](fields.xlsx)·[PDF](fields.pdf)·[PDF 화면](report-page.png)·[편집기](editor.png)·[800×650 화면](editor-small.png).

Windows 실행 패키지 **298파일**의 앱/CLI 해시가 최종 빌드와 같다. Qt/MinGW가 없는 PATH에서 CLI가 schema72 문서를 읽고 종료0을 반환했다. 앱은 Python/VBScript 없이 C++에서 계산한다. 실제 Project/P6 실행 비교는 수행하지 않았다. [패키지](runtime.json)·[배포](runtime-deploy.txt)·[파일 읽기](runtime-inspect.json).
