# XML Partition 검증 · 2026-11-14

[기능 계약](../../xml-partition-function.md) · XPT-01~06. 정수 반올림·범위 전후·불완전 구간·공백·Null/오류와 조건부 schema71를 검사한다.

[독립 검사기](../../../scripts/verify-xml-partition.py)는 경계/혼합 입력5,332건을 나머지 연산으로 판독한다. [Windows 비교기](../../../scripts/verify-xml-partition-windows.py)는 .NET Microsoft.VisualBasic.Interaction.Partition과 공백까지 비교한다. 실제 Project/P6 비교는 아니다.

일반 회귀 **228/228·565.37초**를 통과했다. 대용량 CTest는 180초 제한으로 시간 초과했다. 코드/실행 파일 변경 없이 360초 한도 직접 재검증 **1/1·160.24초**를 확인했다. [확장 기록](extended.json). 테스트 229개를 확인했다. 표준 시간 제한 실패 1건은 보존한다. 관련 60개는 최종 전체 결과에서 추출했다. 파서 **934개**(schema1~71), C++ 형식 **562파일** 차이0이다. [최종 빌드](build.txt)·[전체 CTest](ctest.txt)·[JUnit](ctest.xml)·[대용량 단독](isolated.txt)·[단독 JUnit](isolated.xml)·[집중 부분집합](focused.xml)·[형식](quality.json)·[독립 검사](independent.json)·[Windows 비교](windows.json)·[결과/해시](verification-summary.json).

독립 나머지 연산/Windows Visual Basic 구간5,332건과 직접 인수21건·오류5건이 일치했다. 직접 식26건은 반올림·Null 연결·선행 오류를 포함한다. XML22값(전파 메타데이터2칸 포함)/결과 없음4칸·식 정의33개와 XER22값, XLSX68행·PDF19쪽을 판독했다. 최초 집중 검사는 기존 &의 단일 Null 연결 기대값1건이 틀려 실패했다. 기대값을 바로잡고 두 Null 연결/인수의 선행 오류를 추가한 재검사와 최종 전체 검사는 모두 통과했다. 최초 실패 기록은 보존한다. [첫 빌드](initial-build.txt)·[첫 집중 결과](initial-focused.txt)·[수정 빌드](refined-build.txt)·[수정 검사](refined-focused.txt)·[입력](native.json)·[환산/동결 사본](archived.json)·[native 파일](source.ppm)·[XML](operators.xml)·[XER](operators.xer)·[매핑](mapping.json)·[직접 식](cases.json)·[전체 구간](vectors.json)·[Windows 로그](windows.txt)·[XLSX](fields.xlsx)·[PDF](fields.pdf)·[PDF 화면](report-page.png)·[편집기](editor.png)·[800×650 화면](editor-small.png).

Windows 실행 패키지 **298파일**의 앱/CLI 해시가 최종 빌드와 같다. Qt/MinGW가 없는 PATH에서 CLI가 schema71 문서를 읽고 종료0을 반환했다. 앱은 Python/VBScript 없이 C++에서 계산한다. 실제 Project/P6 실행 비교는 수행하지 않았다. [패키지](runtime.json)·[배포](runtime-deploy.txt)·[파일 읽기](runtime-inspect.json).
