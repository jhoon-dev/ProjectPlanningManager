# XML 논리 연산 검증 · 2026-11-14

[기능 계약](../../xml-logical-operators.md) · XLG-01~06. Eqv/Imp의 Boolean/32비트 비트 연산·형식별 Null·우선순위·과거 문법 보존과 네 범위·요약/전파·미리보기/저장/교환/보고를 확인한다.

[독립 검사기](../../../scripts/verify-xml-logical.py)는4096개 결과를32비트 위치별로 재계산하고, Boolean/Null/대표 정수10종의 조합200개를 별도로 판정한다. [Windows 비교기](../../../scripts/verify-xml-logical-vbscript.py)는 같은4296건을 VBScript 직접 연산으로 실행한다. 과거 Or/Xor 우선순위의 차이는 보존 규칙으로 명시하며 실제 Project/P6 비교가 아니다.

직접 식33건·잘못된 구문/숫자 인접 토큰·분수/32비트 범위·Null/오류·단위·원본99/777 비대체, 활성64/비활성44·이전 태그 거절·전체 사본/환산/ID 이력·복구/읽기 전용·취소·편집/undo/redo를 검사한다. XML/XER20값·결과 없음4칸·전파 메타데이터2칸, XLSX 값과 별도 오류 상태도 확인한다. UI는 예시 삽입·worker 미리보기·저장/취소와 작은 화면을 확인한다.

일반 CTest **214개 중211개 통과·시간 초과3건·637.02초**다. 동일 코드/바이너리를 순차 확장 검증해 **3/3 통과**했다. [일반 확장 기록](general-extended.json). 일반 순차 로그: [resource_productivity_curve_contract](general-extended-resource_productivity_curve_contract.txt) · [work_contour_contract](general-extended-work_contour_contract.txt) · [server_http_auth_sync_scheduler](general-extended-server_http_auth_sync_scheduler.txt). 대용량 CTest는180초 제한으로 시간 초과했다. 코드/실행 파일 변경 없이300초 한도 직접 재검증 **1/1·224.57초**를 확인했다. 기능215개를 확인했으나 표준 시간 제한 실패 합계4건은 보존한다. [확장 기록](extended.json). 관련46개는 최종 전체 결과에서 추출했다. 파서 **906개**(schema1~64), C++ 형식 **541파일** 차이0이다. [최종 빌드](build.txt)·[전체 CTest](ctest.txt)·[JUnit](ctest.xml)·[대용량 단독](isolated.txt)·[단독 JUnit](isolated.xml)·[집중 부분집합](focused.xml)·[형식](quality.json)·[독립 검사](independent.json)·[Windows 비교](vbscript.json)·[결과/해시](verification-summary.json).

독립 비트 연산4096건·형식별 진리표200건과 Windows VBScript4296건이 모두 정확히 일치했다. 직접 식33건, XML20값(전파 메타데이터2칸 포함)/결과 없음4칸·식 정의32개와 XER20값, XLSX66행·PDF19쪽을 확인했다. 첫 테스트의 Flag 기대값을 기존1/0 규약에 맞췄으며 [첫 결과](initial-focused.txt)와 [수정 결과](refined-focused.txt)를 보존한다. 숫자 인접 토큰의 실행/버전 판별 경계를 보완하며 [중간 빌드](build-interrupted.txt)를 중단했고 최종 소스에서 전체 빌드/회귀를 다시 수행했다. [입력](native.json)·[환산/동결 사본](archived.json)·[native 파일](source.ppm)·[XML](operators.xml)·[XER](operators.xer)·[매핑](mapping.json)·[직접 식](cases.json)·[비트 사례](vectors.json)·[진리표](truth.json)·[VBScript 로그](vbscript.txt)·[XLSX](fields.xlsx)·[PDF](fields.pdf)·[PDF 화면](report-page.png)·[편집기](editor.png)·[800×650 화면](editor-small.png).

Windows 실행 패키지 **298파일**의 앱/CLI 해시가 최종 빌드와 같다. Qt/MinGW가 없는 PATH에서 CLI가 schema64 문서를 읽고 종료0을 반환했다. 앱은 Python/VBScript 없이 C++에서 계산한다. [패키지](runtime.json)·[배포](runtime-deploy.txt)·[파일 읽기](runtime-inspect.json).
