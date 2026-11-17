# XML 월·요일 이름 검증 · 2026-11-14

[기능 계약](../../xml-date-name-functions.md) · XDN-01~06. MonthName/WeekdayName의 고정 en-US 이름·약어·주 시작일·짝수 반올림·Null/범위 오류와 조건부 schema67을 확인한다.

[Python 검사기](../../../scripts/verify-xml-date-names.py)는 월24건·요일112건·윤년 날짜/옵션5,856건을 calendar/datetime으로 독립 판독한다. [Windows 비교기](../../../scripts/verify-xml-date-names-vbscript.py)는 같은5,992건·직접 식26건·오류8건을 SetLocale1033의 VBScript로 실행한다. 실제 Project/P6 비교는 아니다.

일반 회귀 **220/220·697.84초**를 통과했다. 대용량 단독 **1/1·152.60초**를 통과했다. 테스트 221개를 확인했다. 표준 시간 제한 실패 없이 모두 통과했다. 관련 52개는 최종 전체 결과에서 추출했다. 파서 **918개**(schema1~67), C++ 형식 **550파일** 차이0이다. [최종 빌드](build.txt)·[전체 CTest](ctest.txt)·[JUnit](ctest.xml)·[대용량 단독](isolated.txt)·[단독 JUnit](isolated.xml)·[집중 부분집합](focused.xml)·[형식](quality.json)·[독립 검사](independent.json)·[Windows 비교](vbscript.json)·[결과/해시](verification-summary.json).

월 이름/약어24건·요일/주 시작일112건과 윤년366일×8개 주 시작일×2개 약어 옵션5,856건, 합계5,992건이 Python calendar/datetime 및 Windows VBScript(SetLocale1033)와 모두 일치했다. 직접 식27건 중 VBScript에서 실행한26건과 오류8건도 일치했다. XML20값(전파 메타데이터2칸 포함)/결과 없음4칸·식 정의32개와 XER20값, XLSX66행·PDF19쪽을 확인했다. [첫 빌드](initial-build.txt)·[첫 집중 결과](initial-focused.txt)·[입력](native.json)·[환산/동결 사본](archived.json)·[native 파일](source.ppm)·[XML](operators.xml)·[XER](operators.xer)·[매핑](mapping.json)·[직접 식](cases.json)·[전체 날짜/옵션](vectors.json)·[Windows 로그](vbscript.txt)·[XLSX](fields.xlsx)·[PDF](fields.pdf)·[PDF 화면](report-page.png)·[편집기](editor.png)·[800×650 화면](editor-small.png).

Windows 실행 패키지 **298파일**의 앱/CLI 해시가 최종 빌드와 같다. Qt/MinGW가 없는 PATH에서 CLI가 schema67 문서를 읽고 종료0을 반환했다. 앱은 Python/VBScript 없이 C++에서 계산한다. 실제 Project/P6 실행 비교는 수행하지 않았다. [패키지](runtime.json)·[배포](runtime-deploy.txt)·[파일 읽기](runtime-inspect.json).
