# 외부 XML 기준선 채택·내보내기 검증

2026-11-15 · Windows · Qt6.8.3 / MinGW13.1 / Release · 조건부 native schema75.

후속 실제 제품 왕복 요청으로 설치 경로·등록 정보·실행 프로세스와 Computer Use 앱40개를 조사했으나 MS Project/P6를 찾지 못했다. 사용자가 **제품 실행 환경 없음**을 확인했다. 따라서 두 제품의 가져오기·재계산·재저장·재내보내기는 **미실행**이다. [실행 상태](product-execution-status.json). 아래 독립 판독 결과는 실제 제품 실행 증거가 아니다.

[XBE-01~07](../../xml-baseline-exchange.md)의 새 사본 채택과 표준 Baseline 출력을 구현했다. Baseline0~10의 작업·프로젝트 UID0·WBS·자원·배정 명시 값, 0/미설정, 표시 속성과 Baseline 안의 작업/배정 시간분포를 보존한다. 외부 채택 번호의 계산식 우선순위, 나머지 native 승인본 투영, 저장/복구·통화/ID 이관과 원본 재대조를 포함한다. 기존 문서 병합과 native 상세 시간분포 투영은 포함하지 않는다.

## 실행 결과

- 집중 회귀 **42/42 통과·53.36초**. 새 계약/native 투영/UI와 기존 기준선/원가/번호별·요약·실제/잔여·진척 채택·XML 식/전파·통화·ID·XER 일정 채택·파일/undo/복구를 포함한다. [최종 로그](final-tests.log), [빌드 로그](final-build.log).
- 같은 실행의 parser replay **960개·schema1~75** 통과. [상세 로그](LastTest.log).
- native 승인본의 번호·동결 값·삭제 객체 생략 진단과 1000작업/1001기준선 행의 출력·재채택을 통과했다.
- 독립 ElementTree/Decimal 검산: 원본16행/11번호, 채택6행, 시간분포4행, native 투영26행, USD→EUR 1/2 전환을 통과했다. XLSX25시트와 PDF15쪽을 확인했다. [수치 결과](independent.json), [검증 자료](artifacts/), [UI 화면](artifacts/review.png).
- 기존 독립 검사도 통과했다. 기준선 XML77/XER32값·XLSX77칸/PDF20쪽, 잔여 XML64/XER52값·XLSX64칸/PDF21쪽, 실제 XML50/XER30값·XLSX50칸/PDF23쪽, 기준선 원가128사례/1408값·XML/XER32값·XLSX45행/PDF24쪽, 요약 기간129사례다.
- 변경 C++27파일 clang-format과 Python 검사기 구문 검사를 통과했다. 최종 Release 앱을 빌드했다.

## 독립 판독기와 원가 전송 단위

MPXJ16.10.0으로 독립 판독하는 과정에서 원가 전송 단위 오류를 발견해 수정했다. XML Cost는 주요 통화 단위의1/100을 나타내며 USD12.34는 XML1234, JPY1234는 XML123400이다. 기존 XML Cost 변환과 일치시키고 USD/EUR/JPY/KRW의0·최소 단위·일반 값·상한, 정밀도 거절과 통화 전환을 회귀 검사했다. 독립 판독기에서도 채택 원가12.34, 변환 원가6.17과 공수30분, native 슬롯1 공수2.07분을 확인했다.

MPXJ16.10.0은 소수 초가 있는 ISO 날짜를 읽지 못한다. 따라서 외부 원본 출력과 변환 출력은 각각 날짜3필드의 소수 초를 제거한 **판독기 전용 사본**으로 공수/원가를 검사했다. 실제 출력의 마이크로초는 그대로 유지하며 ElementTree/Decimal 검사에서 원본과 정확히 비교한다. native 투영 XML은 원래 출력 그대로 읽었고 세 경우 모두 판독 오류0건이다. 이 결과를 외부 원본 XML의 완전한 MPXJ 호환성으로 해석하지 않는다.

초기 검사에서 Qt `slots` 매크로 충돌, 중복 Number의 번호 연결 누락과 이전 요약 기간의 오래된 부정 seed도 수정했다. 최종 로그는 수정 후 결과다.

## 재현

현재 체크아웃용 `build-numbered-summary`를 사용했다. Qt/MinGW/CMake를 PATH에 설정한 환경에서 실행한다. `--reader`는 Java와 MPXJ가 설치된 Python 환경이 필요하다. 저장한 검증 자료를 다시 읽으려면 첫 검사 명령의 마지막 경로를 이 문서의 `artifacts` 폴더로 바꾼다.

```powershell
ctest --test-dir build-numbered-summary -R '^(xml_(baseline|summary_duration|remaining|quantity|actual_|early_late|formula_|roll_up|roll_down|progress_|work_progress)|project_currency_conversion_contract$|ledger_identity_migration_contract$|xer_schedule_adoption_contract$|undo_contract$|recovery_|tracking_file$|file_lifecycle$|parser_)' --output-on-failure -j 3
build/validation-python/Scripts/python.exe scripts/verify-xml-baseline-exchange.py build-numbered-summary/xml-baseline-exchange-qa --reports --reader
build/validation-python/Scripts/python.exe scripts/verify-xml-baseline-references.py build-numbered-summary/xml-baseline-qa --reports
build/validation-python/Scripts/python.exe scripts/verify-xml-remaining-references.py build-numbered-summary/xml-remaining-qa --reports
build/validation-python/Scripts/python.exe scripts/verify-xml-actual-quantities.py build-numbered-summary/xml-quantity-qa --reports
build/validation-python/Scripts/python.exe scripts/verify-xml-baseline-cost.py build-numbered-summary/xml-baseline-cost-qa
python scripts/verify-xml-summary-durations.py build-numbered-summary
```

전체 CTest·macOS/스토어 패키지·실제 MS Project/P6 재계산·재저장은 이번 검증 범위가 아니다. 원본 전체 이력은 교환 폴더의 `source.ppm`에 보존한다.
