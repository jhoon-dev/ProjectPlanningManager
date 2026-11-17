# XER 실적·원가 상세 교환 검증

2026-11-06 Windows x64 · Qt6.8.3 / MinGW13.1 · Release/Ninja. 기존 미커밋 개발 내용을 보존하고 [XAC-01~06](../../xer-actual-cost-exchange.md)의 지원 부분을 구현했다. 실제 실행 결과와 소스/실행 파일 해시는 [검증 요약](verification-summary.json)에 기록한다.

최종 전체 회귀 **124/124 통과 ·158.52초**, XER/파서 집중 **8/8 ·14.52초**, 파서 **272 seeds**, 형식 **298파일**을 통과했다. 새 실적·원가 및 기존 단일/선택/일괄 XER·UDF/XML·공개7개 표본의 독립 reader와 Windows 기본 UI도 통과했다.

## 수용 검사

| 영역 | 검증 내용 |
|---|---|
| 상태일·실적 | Data Date와 실제 시작/완료·상태·잔여 기간의 일관성, 작업 달력 기반 실제 소요 기간과 관측 공수 구분, 물리 진척, 미시작 잔여 추정·미보고/0·미래 보고 제외 |
| 실제원가 | 자원 정규/초과근무 원가+경비 누계122.80 USD, 날짜 배분105.80·미배분17.00. TASKFIN 자원 요약을 중복 합산하지 않음, 단가로 AC 재평가하지 않음, 최신 정정·미정 누계 차단 |
| 단가·고정비 | 날짜별 시간 단가·미정 전환·같은 가용량 보존, 균등/시작 발생, KRW/JPY/USD/EUR 최소단위, 외화/일중/시간 외 요율·음수/지수/정밀도 초과 차단 |
| 날짜별 공수·비용 | FINDATES/TRSRCFIN와 TASKFIN 경비, 한 civil 날짜만 원장화, 주/월 집계 원본/보고 보존, 3.25h 날짜 명세와3.75h 누계의 차액 미배분, 정확1 %·분 왕복, 미래·초과 배분·중복/깨진 참조 차단 |
| 원자성·영속성 | 원본 바이트와 source.ppm 정확 보존, native 저장/재열기·dirty 복구, 기존 폴더 보호·취소, 배정 해제 이력으로 계획 배정을 생성하지 않음 |
| 다중 교환 | 두 프로젝트의 재무기간·비용 항목·작업·배정 ID 재배정과 선택 격리, 제외 프로젝트의 잘못된 참조도 선택 전 차단 |
| UI | 실제 가져오기·내보내기·취소·stale·읽기 전용, 상태일/건수/누계122.80 USD 미리보기, Windows 기본 한글·스크롤·하단 승인/취소 버튼 |

## 실행 증거

- [전체 Release 빌드](build.txt), [XER·파서 집중8개](focused.txt), [전체 CTest](ctest.txt) 및 [기계 판독 JUnit](ctest.xml), [형식 검사](format.json).
- [새 실적·원가 독립 판독](independent-actual-costs.json): Python 표준 TSV/Decimal·native SHA256와 MPXJ16.10.0/JPype1.6.0 strict reader. 상태일/실제 날짜·물리37.25%·AC122.80과 자원100.46/직접·미배분22.34, 날짜 공수3.25h와 원본 누계3.75h를 대조한다.
- [기존 단일 XER](independent-classic.txt), [프로젝트 선택](independent-selection.json), [일괄 작업공간](independent-batch.json), [UDF/XML](independent-udf.json), [공개 코퍼스 판독](independent-public.json)의 독립 회귀를 실행한다.
- Windows 기본 [UI 실행 로그](native-ui.txt)와 [가져오기 화면](import.png)/[내보내기 화면](export.png)을 확인했다. 800×580 미리보기에서 한글·상태일·실적2/원가2/날짜 원장4건과 통화 표기, 스크롤·경로/승인/취소가 보인다. offscreen 플랫폼의 글꼴 대체 화면 대신 Windows 플랫폼 화면을 증거로 사용한다.
- [가져오기 상세 보고](import-report.json), [내보내기 상세 보고](export-report.json), [독립 판독 XER](export.xer), [두 프로젝트 XER](combined.xer)를 보존한다. 입력은 저장소의 수작업 `tests/fixtures/primavera-actual-cost.xer`다.

첫 전체 회귀는123/124 통과했다. 공개 generated 표본 `percent-complete-8.4.xer`가 상태 시각2015-02-24 00:00보다 미래인 실제 시작08:00을 포함해 새 `FILE_PRIMAVERAXER_080`으로 거절됐다. 원본 바이트와 해시를 바꾸지 않고 native 관측 시각 계약에 맞춘 기대 판정·사유를 manifest에 기록했다. [첫 실행](first-regression.txt)과 [해당 표본 재검사](corpus-focused.txt)를 남긴다. P6가 이 파일을 거절한다는 주장으로 확대하지 않는다. 기존 계획 날짜 검사는 상태일 이후 예측과 구분하도록 calculatePlan을 명시했다.

## 재현

Qt/MinGW/CMake/Ninja PATH를 설정한 뒤:

```powershell
cmake --build build-governance --parallel 4
ctest --test-dir build-governance -R 'xer_|primavera_xer|parser_seed_replay' --output-on-failure --parallel 2
ctest --test-dir build-governance --output-on-failure --parallel 4
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xer-actual-costs.py build-governance
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-primavera-xer.py build-governance
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xer-project-selection.py build-governance
```

native UI는 build-governance 작업 디렉터리에서 `jobs_primavera_xer_ui_tests.exe -platform windows`로 실행한다. 실제 사용자 프로젝트가 아닌 임시 교환 폴더를 사용한다.

## 검증 경계

기존 native 원장에 표현할 수 있는 현재 상태와 일 단위 명세의 교환이다. 여러 날짜 집계·날짜 없는 누계 공수·초과근무 별도 분류·외부 EVM/기준선·정정 이력·배정별 override는 동반 원본/보고/source.ppm의 보존 경계를 따른다. 실제 Oracle P6 설치본의 수용·재계산·재저장, MPP reader와 macOS 실행은 이번 Windows 로컬 검사로 완료 처리하지 않는다. 기존 설치 앱은 개발 회귀와 별개이며 이 검증은 스토어 서명/제출 증거가 아니다.
