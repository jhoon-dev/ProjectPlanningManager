# XML TimeSerial·TimeValue 검증 · 2026-11-13

[기능 계약](../../xml-time-functions.md) · XTM-01~06. TimeSerial/TimeValue의 명시 지원 부분, 네 범위·요약/배정 전파·현재 결과/원본 스냅샷·미리보기·교환/보고와 schema56/전체 보관 사본을 확인한다. 전체 VBA/NLS나 실제 Microsoft Project 제품 왕복을 뜻하지 않는다.

`xml_time_functions_contract`는 16비트 입력·짝수 반올림·분/초 넘침·24시간/음수 OLE·기준일1899-12-30, Date/24시간/영문 AM·PM/ISO 입력·날짜 제거·소수6자리, 잘못된 날짜/시각/시간대/입력 유형·Null/전체 인수 평가·취소를 확인한다. 음수-1시간은 기준일01시, -25시간은전날01시다. 이를 일반적인 시간 빼기로 바꾸지 않는다. 저장된1999/2000년 스냅샷은 현재 결과/오류를 대체하지 않는다.

새 활성56/이전 태그 거절·비활성 원문44와 호출 오인 방지, 기준선/시나리오만 남은 사본·환산/ID 이력·복구·읽기 전용·원본 필드 이름 변경·undo/redo를 검증한다. 새 필드는 없으며 기존 함수·레거시 날짜 식 읽기와 최소 writer를 유지한다.

[Python 독립 검사기](../../../scripts/verify-xml-time-functions.py)는 생성 **1,024건**, 시각 파싱/추출 **3,072건**, 직접 식 **34건**과 그중 숫자 반올림 **15건**을 확인한다. OLE 일수는 정수 분할, 반올림은 Decimal ROUND_HALF_EVEN, 날짜·소수 초는 datetime으로 검증한다. 네 범위 XML22값(전파 메타데이터로 해석한2칸 포함)·정의33개, XER 정적22값, XLSX 작업 필드74행·PDF19쪽을 대조한다. 원래 작업 날짜2024년/2023년은 보존하고 시각 함수 결과만1899년 기준일로 바뀐다.

별도의 [Windows VBScript 검사기](../../../scripts/verify-xml-time-functions-vbscript.py)는 설치된 cscript의 실제 TimeSerial/TimeValue와 **4,111건**을 비교한다. 정수 생성1024·반올림15·정수 초 시각3072건이다. VBScript가 지원하지 않는 native 마이크로초는 제거하고 시각 부분만 비교한다. ISO 날짜의 검증/제거와 소수 초 보존은 별도 Python 검사에 포함한다. 생성 스크립트에는 검증한 숫자 상수와 ASCII 시각 문자열만 넣으며 원본 계산식이나 Eval을 실행하지 않는다. native Null/전체 Variant 동등성을 주장하지 않는다.

`xml_time_functions_ui`는 예시6개, 날짜 기준일과 마이크로초가 보이는 비동기 미리보기·저장/undo·취소·표 읽기와800×650 결과 배치를 확인한다. 기존 XML 편집기 테스트는 오래된 편집·문서 변경·읽기 전용·오류 경로를 함께 검증한다.

일반 회귀 **198/198·300.17초**를 통과했다. 대용량 단독 **1/1·159.40초**까지 합계 **199/199**, 실패/건너뜀0이다. 관련 집중30개는 전체 실행에서 추출한 같은 결과이며 중복 실행하지 않았다. 파서 **874개**(schema1~56), C++ 형식 **517파일** 차이0이다. [빌드](build.txt)·[198개 CTest](ctest.txt)·[JUnit](ctest.xml)·[대용량 단독](isolated.txt)·[단독 JUnit](isolated.xml)·[집중 부분집합](focused.xml)·[형식](quality.json)·[Python 독립 검증](independent.json)·[Windows VBScript 비교](vbscript.json)·[결과/해시](verification-summary.json).

Windows 실행 패키지 **298파일**의 앱/CLI 해시가 최종 빌드와 같다. Qt/MinGW가 없는 PATH에서 CLI가 schema56 파일을 읽고 종료0을 반환했다. 실행 앱은 Python/VBScript를 사용하지 않고 C++에서 계산한다. [패키지](runtime.json)·[배포](runtime-deploy.txt)·[파일 읽기](runtime-inspect.json).

[native 입력](native.json)·[환산/동결 사본](archived.json)·[native 파일](source.ppm)·[XML](times.xml)·[XER](times.xer)·[매핑/진단](mapping.json)·[직접 식](cases.json)·[생성](serials.json)·[추출](values.json)·[XLSX](fields.xlsx)·[PDF](fields.pdf)·[PDF 화면](report-page.png)·[편집기](editor.png)·[800×650 결과](editor-small.png).
