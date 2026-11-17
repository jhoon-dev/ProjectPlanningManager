# XML 추가 수학 함수 검증 · 2026-11-13

[기능 계약](../../xml-math-functions.md) · XMH-01~06. Atn/Cos/Exp/Log/Sin/Tan의 라디안·자연로그·부동소수 근사/소수6자리 계약과 네 범위·요약/배정 전파·현재 값/원본 스냅샷·미리보기·교환/보고·schema58/전체 보관 사본을 확인한다. 전체 VBA 또는 실제 Microsoft Project 제품 실행 동등성을 검증하지 않았다.

계약 테스트는 정의역·범위·작은 Exp의0/1마이크로 경계, Null/오류·엄격한 숫자 문자열·Boolean·전체 인수 평가·인수 개수·취소를 확인한다. 현재/동결 사본·환산/ID 이력·복구/읽기 전용·이름 변경·undo/redo와 활성58/이전 태그 거절·비활성44를 검증한다. 저장된99/777 스냅샷으로 결과와 오류를 대체하지 않는다.

[Python 독립 검사기](../../../scripts/verify-xml-math.py)는 mpmath1.3.0의80자리 참조로 생성/경계 **6,189건**을 검사한다. 정확한 십진 입력의 반올림 결과와 일치한 수, 마지막 자리 차이와 입력 감도/부동소수 오차 허용 폭을 함께 기록한다. 허용 폭은 경험적 수치 검증이며 엄밀한 오차 증명이 아니다. 전체 ±9조 입력과 Tan 극점 근처, Exp 상한·underflow를 포함한다. 실행 앱은 Python/mpmath를 사용하지 않는다.

UI는 예시6개·삽입·비동기 미리보기·저장/undo·취소·표 값과800×650 배치를 확인한다. XML 정의/값 재계산·XER 정적 값·XLSX/PDF와 통화 전환 뒤 입력 재계산을 확인한다.

일반 회귀 **202/202·309.14초**를 통과했다. 대용량 단독 **1/1·146.42초**까지 합계 **203/203**, 실패/건너뜀0이다. 관련 집중34개는 전체 실행에서 추출한 같은 결과이며 중복 실행하지 않았다. 파서 **882개**(schema1~58), C++ 형식 **523파일** 차이0이다. [빌드](build.txt)·[202개 CTest](ctest.txt)·[JUnit](ctest.xml)·[대용량 단독](isolated.txt)·[단독 JUnit](isolated.xml)·[집중 부분집합](focused.xml)·[형식](quality.json)·[Python 독립 검증](independent.json)·[결과/해시](verification-summary.json).

Windows 실행 패키지 **298파일**의 앱/CLI 해시가 최종 빌드와 같다. Qt/MinGW가 없는 PATH에서 CLI가 schema58 파일을 읽고 종료0을 반환했다. 실행 앱은 Python/mpmath를 사용하지 않고 C++에서 계산한다. [패키지](runtime.json)·[배포](runtime-deploy.txt)·[파일 읽기](runtime-inspect.json).

[native 입력](native.json)·[환산/동결 사본](archived.json)·[native 파일](source.ppm)·[XML](numbers.xml)·[XER](numbers.xer)·[매핑/진단](mapping.json)·[직접 식](cases.json)·[80자리 참조 대상](vectors.json)·[XLSX](fields.xlsx)·[PDF](fields.pdf)·[PDF 화면](report-page.png)·[편집기](editor.png)·[800×650 결과](editor-small.png).

80자리 참조6,189건 중 **6177건**의 반올림 값이 같고 큰 Exp **12건**은 최대 **0.000001** 차이였다. 수학 라이브러리의 근사 차이를 보존했다. 초기 개발 검사에서 최종 Number 필드에 CStr를 넣은 테스트 타입 오류와 테스트 문서의 XML 숫자 슬롯 초과를 발견해 테스트 구성을 수정했다. [초기 결과](initial-focused.txt)·[슬롯 결과](refined-focused.txt)·[수정 결과](fixture-focused.txt). 최종 전체 결과는 같은 코드/테스트로 다시 검증했다.
