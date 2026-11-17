# 기존 문서 XML 기준선 병합 검증

2026-11-15 · Windows · Qt6.8.3 / MinGW13.1 / Release · 조건부 schema76.

[XBM-01~06](../../xml-baseline-merge.md)을 구현했다. 파일 메뉴의 **외부 XML 기준선을 현재 문서에 병합…**에서 번호·작업/WBS/자원/프로젝트 연결 또는 명시 제외를 검토하고 현재 문서에 반영한다. 기존 외부/native 번호를 대체할 때 별도 교체 확인이 필요하다. 상태일은 필수가 아니다.

## 결과

- 최종 집중 회귀 **47/47 통과·53.34초**. 새 병합 계약/UI, 기존 기준선 교환/번호/원가·요약·실제/잔여·진척·식/전파·통화·ID·XER 일정 채택·파일/undo/복구와 prepared edit·평준화 선택 계약/UI를 포함한다. [로그](final-tests.log).
- 같은 실행의 **976 parser seeds·schema1~76** 통과. 새 병합·잘못된 전후 결과·중복 출처·확인 누락·미지원 필드·잘린 입력을 포함하고 schema75 envelope의 새 자료 거절도 직접 검사했다. [상세 로그](LastTest.log).
- 원본 바이트/후보 위변조, 누락/중복/다대일/없는 대상 연결, 다른 자원 종류·통화, 없는 배정, 기존 번호 교체 미확인과 읽기 전용을 거절했다. 취소와 stale 적용에 부분 변경이 없음을 확인했다.
- 작업·프로젝트·WBS·자원·배정의 명시 값/0/미설정과 시간분포를 병합했다. 제외 작업의 배정을 함께 제외하며 재병합은 기존 번호를 전체 교체한다. native 승인본은 그대로 두고 전후/연결표 이력을 추가했다.
- 다른 XML에서 온 번호0의 기존 동결 요일과 출처를 보존하면서 번호1의 새 값·달력·출처를 적용했다. 번호별 계산식의 달력 선택, schema76 저장/복구·통화 전환·ID 이관, 원본 통화/ID로 유지되는 병합 감사 기록을 확인했다.
- 실제 진척 보고와 채택 평준화가 있는 문서에 재병합한 뒤 보고·상태일·평준화 선택 및 계산된 작업 일정/프로젝트 완료가 같음을 확인했다. 미리보기 준비는 문서를 바꾸지 않으며 적용 후 dirty 상태와 일반 저장을 확인했다.
- Release 앱을 빌드했다. [전체 영향 범위 빌드](final-build.log), [마지막 추가 테스트 빌드](complete-build.log). 변경 C++27파일 형식 검사와 독립 Python 검사기 구문 검사를 통과했다.

## 독립 대조와 화면

[독립 검사기](../../../scripts/verify-xml-baseline-merge.py)는 native JSON의 병합 전후 집합과 연결표로 예상 행을 구성하고 XML의 현재 UID 매핑·날짜·고정점 기간/공수·원가·표시·추가 금액·시간분포를 별도로 대조했다. **기준선7행·명시 연결5개·번호0 보존/번호1 채택·시간분포4행·native 승인1건 보존**을 확인했다. XLSX25시트의 공수30분/원가12.34와 PDF15쪽도 확인했다. [결과](independent.json), [검증 자료](artifacts/), [화면](artifacts/review.png).

기존 독립 검사도 다시 통과했다. 외부 기준선 원본16행/채택6행/native 투영26행·통화1/2, 기준선 XML77/XER32·보고77칸/PDF20쪽, 잔여 XML64/XER52·보고64칸/PDF21쪽, 실제 XML50/XER30·보고50칸/PDF23쪽, 원가128사례/1408값·XML/XER32·보고45행/PDF24쪽, 요약 기간129사례다. 전체 수치는 위 JSON에 보존했다.

화면은 작은 창에서 바깥 세로 스크롤과 표의 가로 스크롤을 사용하고 하단 적용/취소 버튼을 유지한다. 번호와 대상의 기본 미선택, 현재/수신 값 조회, 작업자 스레드 적용과 검토 취소를 검사했다.

## 초기 실패와 범위

초기 테스트 자료의 승인 revision, 같은 파일을 동시에 쓰기 모드로 재열려던 잠금, 관측이 없는 ID를 원장 이관하려던 사례를 실제 계약에 맞췄다. 파서 seed는 envelope 태그가 오래돼도 내부 body를 최신 schema로 읽을 수 있다는 harness 특성을 반영해, 태그 거절은 envelope로 직접 검사하고 corpus의 부정 사례는 실제로 잘못된 body를 사용했다. 추가한 평준화 UI 검사 실행 파일도 빌드한 뒤 최종47개를 모두 통과했다.

실제 MS Project/P6 실행 환경은 없으며 제품 왕복은 미실행이다. 전체 CTest·macOS/스토어 실행과 native 상세 시간분포 투영은 이번 범위가 아니다. 병합 이력에는 지원 값·원본 SHA/검토 정보·연결표·전후 값을 저장한다. 원본 XML 전체 바이트는 추가 내장하지 않으므로 원본 파일을 별도 보관한다. 이번 감사 이력은 일반 undo 대신 명시 재병합으로 정정한다.

## 재현

현재 체크아웃의 `build-numbered-summary`를 사용한다. Qt/MinGW/CMake를 PATH에 설정한 환경에서 실행한다.

```powershell
ctest --test-dir build-numbered-summary -R '^(xml_(baseline|summary_duration|remaining|quantity|actual_|early_late|formula_|roll_up|roll_down|progress_|work_progress)|project_currency_conversion_contract$|ledger_identity_migration_contract$|xer_schedule_adoption_contract$|undo_contract$|recovery_|tracking_file$|file_lifecycle$|parser_|prepared_edit|leveling_selection)' --output-on-failure -j 3
build/validation-python/Scripts/python.exe scripts/verify-xml-baseline-merge.py docs/validation/xml-baseline-merge-2026-11-15/artifacts --reports
```
