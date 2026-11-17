# XML Unicode 문자 코드 검증 · 2026-11-14

[기능 계약](../../xml-unicode-functions.md) · XUC-01~06. AscW의 부호 있는16비트 코드·ChrW의 음수 별칭/짝수 반올림·native/XML-safe BMP·전체 입력 검증·Null/오류·기존 Asc/Chr 보존과 조건부 schema66을 확인한다.

[Python 검사기](../../../scripts/verify-xml-unicode.py)는 지원 문자63,456개를 Unicode codec/부호 코드와 대조하고 XML/XER·XLSX/PDF를 판독한다. [Windows 비교기](../../../scripts/verify-xml-unicode-vbscript.py)는 같은63,456개 및 직접 식24개를 VBScript로 실행한다. 실제 Project/P6 실행 비교가 아니다.

일반 회귀 **218/218·728.70초**를 통과했다. 대용량 CTest는 180초 제한으로 시간 초과했다. 코드/실행 파일 변경 없이 300초 한도 직접 재검증 **1/1·273.18초**를 확인했다. [확장 기록](extended.json). 테스트 219개를 확인했다. 표준 시간 제한 실패 1건은 보존한다. 관련 50개는 최종 전체 결과에서 추출했다. 파서 **914개**(schema1~66), C++ 형식 **547파일** 차이0이다. [최종 빌드](build.txt)·[전체 CTest](ctest.txt)·[JUnit](ctest.xml)·[대용량 단독](isolated.txt)·[단독 JUnit](isolated.xml)·[집중 부분집합](focused.xml)·[형식](quality.json)·[독립 검사](independent.json)·[Windows 비교](vbscript.json)·[결과/해시](verification-summary.json).

지원 BMP 문자 63,456개는 Python Unicode codec과 부호 코드 판독 및 Windows VBScript AscW/ChrW 결과와 모두 일치했다. 직접 식25건 중 VBScript가 제공하는 구문24건도 일치했고 Null/빈 입력 오류도 확인했다. XML20값(전파 메타데이터2칸 포함)/결과 없음4칸·식 정의32개와 XER20값, XLSX66행·PDF19쪽을 판독했다. 첫 테스트 자료의 이름이 기존 Asc/Chr 필드와 중복되어 [첫 결과](initial-focused.txt)는 실패했으며 이름을 구분한 [수정 결과](refined-focused.txt)와 최종 회귀는 통과했다. [첫 빌드](initial-build.txt)·[수정 빌드](refined-build.txt)·[입력](native.json)·[환산/동결 사본](archived.json)·[native 파일](source.ppm)·[XML](operators.xml)·[XER](operators.xer)·[매핑](mapping.json)·[직접 식](cases.json)·[전체 문자](vectors.json)·[Windows 로그](vbscript.txt)·[XLSX](fields.xlsx)·[PDF](fields.pdf)·[PDF 화면](report-page.png)·[편집기](editor.png)·[800×650 화면](editor-small.png).

Windows 실행 패키지 **298파일**의 앱/CLI 해시가 최종 빌드와 같다. Qt/MinGW가 없는 PATH에서 CLI가 schema66 문서를 읽고 종료0을 반환했다. 앱은 Python/VBScript 없이 C++에서 계산한다. 실제 Project/P6 실행 비교는 수행하지 않았다. [패키지](runtime.json)·[배포](runtime-deploy.txt)·[파일 읽기](runtime-inspect.json).
