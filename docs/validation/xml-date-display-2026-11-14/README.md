# XML 날짜·시각 표시 검증 · 2026-11-14

[기능 계약](../../xml-format-datetime.md) · XDT-01~06. FormatDateTime의 고정 en-US 형식0~4·기준일/자정·옵션 반올림·ISO/Null/오류·소수 초 표시 정밀도와 조건부 schema68을 확인한다.

[Python 검사기](../../../scripts/verify-xml-date-display.py)는 정수 초 입력7,370건을 calendar/datetime으로 판독한다. [Windows 비교기](../../../scripts/verify-xml-date-display-vbscript.py)는 같은7,370건·직접 식19건·오류4건을 SetLocale1033으로 실행하며 ISO는 DateSerial/DateAdd로 명시 구성한다. 소수 초와1600년의 의도한 차이를 별도로 기록한다. 실제 Project/P6 비교는 아니다.

일반 회귀 **222/222·664.49초**를 통과했다. 대용량 단독 **1/1·174.28초**를 통과했다. 테스트 223개를 확인했다. 표준 시간 제한 실패 없이 모두 통과했다. 관련 54개는 최종 전체 결과에서 추출했다. 파서 **922개**(schema1~68), C++ 형식 **553파일** 차이0이다. [최종 빌드](build.txt)·[전체 CTest](ctest.txt)·[JUnit](ctest.xml)·[대용량 단독](isolated.txt)·[단독 JUnit](isolated.xml)·[집중 부분집합](focused.xml)·[형식](quality.json)·[독립 검사](independent.json)·[Windows 비교](vbscript.json)·[결과/해시](verification-summary.json).

윤년366일×4개 시각×5개 형식7,320건과기준일/경계50건, 합계7,370건의 정수 초 입력이 Python calendar/datetime 및 Windows VBScript(SetLocale1033)와 모두 일치했다. 직접 식25건 중 비교 가능한19건과 오류4건도 일치했으며 소수 초·1600년의 의도한 차이는 [Windows 기록](vbscript.json)에 별도로 남겼다. XML20값(전파 메타데이터2칸 포함)/결과 없음4칸·식 정의32개와 XER20값, XLSX66행·PDF19쪽을 판독했다. 첫 UI 테스트의 초기 표시 기대값이 잘못되어 [첫 결과](initial-focused.txt)는 UI1건이 실패했으며 기대값을 바로잡은 [수정 결과](refined-focused.txt)와 최종 회귀는 통과했다. [첫 빌드](initial-build.txt)·[수정 빌드](refined-build.txt)·[입력](native.json)·[환산/동결 사본](archived.json)·[native 파일](source.ppm)·[XML](operators.xml)·[XER](operators.xer)·[매핑](mapping.json)·[직접 식](cases.json)·[전체 날짜/시각/형식](vectors.json)·[Windows 로그](vbscript.txt)·[XLSX](fields.xlsx)·[PDF](fields.pdf)·[PDF 화면](report-page.png)·[편집기](editor.png)·[800×650 화면](editor-small.png).

Windows 실행 패키지 **298파일**의 앱/CLI 해시가 최종 빌드와 같다. Qt/MinGW가 없는 PATH에서 CLI가 schema68 문서를 읽고 종료0을 반환했다. 앱은 Python/VBScript 없이 C++에서 계산한다. 실제 Project/P6 실행 비교는 수행하지 않았다. [패키지](runtime.json)·[배포](runtime-deploy.txt)·[파일 읽기](runtime-inspect.json).
