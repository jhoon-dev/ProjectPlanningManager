# 번호별 기준선과 요약 기간 검증

2026-11-15 · Windows · Qt6.8.3 / MinGW13.1 / Release.

## 구현 범위

[XBS-01~05](../../xml-numbered-baselines.md): Baseline0~10 Start/Finish/Duration/Work/Cost를 native 승인 v1~v11에 고정 대응한다. 이름 없는 v1 참조를 보존하고 버전 기반 조회·누락/중복/오류 격리·동결 달력/곡선/원가·편집기/미리보기·XML 식 재작성·저장/복구를 연결했다. 번호별 바인딩만 있으면 최소 schema73이다.

[XSD-01~06](../../xml-summary-durations.md): 프로젝트와 WBS/필터 그룹에서 실제·잔여·기준선 기간을 해당 프로젝트 근무 달력의 날짜 범위로 계산한다. 병렬 기간을 더하지 않는다. 완료 잔여 제외/모두 완료0·명시 미시작0·빈 그룹/미보고 미설정·동결 달력·현재/과거 상태일·취소/stale를 검사했다. 새 프로젝트 바인딩과 작업 요약 설정의 writer는74이며 과거에 허용된 작업 요약 설정은 계속 읽고 다음 저장 때 승격한다. 작업별 기간만 있으면 기존 최소 writer다.

## 실행 결과

- 번호별 기준선 구현 후 기존 기준선/원가 및 새 번호별 계약·UI 6/6 통과, 13.49초. [로그](numbered-tests.log).
- 두 기능 결합 최종 회귀 29/29 통과, 35.65초. 기준선/원가/번호별·요약·실제/잔여·Early/Late·표준 실적 내보내기·XML 식/roll-up/down의 계약/UI, 파일 생명주기·tracking 저장·undo·복구를 포함한다. [로그](final-tests.log).
- 별도 Python 정수 마이크로초/Decimal 검산: 잔여 예측 1건 + 요일·휴일·분할 근무·소수 초 경계128건 일치.
- 기존 독립 검사기도 새 요약 의미와 구버전 증거를 함께 판독하도록 확장했다. 기준선 XML77값/XER32값·XLSX77칸/PDF20쪽, 잔여 XML64값/XER52값·XLSX64칸/PDF21쪽, 실제 XML50값/XER30값·XLSX50칸/PDF23쪽을 통과했다.
- 기준선 원가의 독립 유리수128사례/1408값, XML/XER 각32값·XLSX45행/PDF24쪽을 통과했다.
- 변경 C++21파일 clang-format 검사와 Python5파일 구문 검사를 통과했다. 최종 `PPM.exe` Release 앱 빌드를 완료했다.

최초 번호별 교환 검사에서는 XML의 형식별 사용자 필드 슬롯 수를 넘는 테스트 자료를 사용했다. native 번호 조회는11개 버전을 모두 검사하고, XML 동시 교환은 기존 슬롯 수 안의 대표 조합으로 수정했다. 요약 복구 검사는 실제 편집 후 checkpoint를 만드는 흐름으로 수정했다. 초기 필터에 포함됐지만 미빌드였던 표준 실적 검사 실행 파일도 빌드한 뒤 최종29개를 모두 통과했다.

## 재현

현재 체크아웃의 빌드는 `build-numbered-summary`를 사용했다. 기존 `build`는 다른 체크아웃 경로를 가리켜 재사용하지 않았다. 아래 명령은 Qt/MinGW/CMake를 PATH에 설정한 환경에서 실행한다.

```powershell
ctest --test-dir build-numbered-summary -R '^(xml_(summary_duration|baseline|remaining|quantity|actual_|early_late|formula_|roll_up|roll_down)|undo_contract$|recovery_|tracking_file$|file_lifecycle$)' --output-on-failure -j 3
python scripts/verify-xml-summary-durations.py build-numbered-summary
build/validation-python/Scripts/python.exe scripts/verify-xml-baseline-references.py build-numbered-summary/xml-baseline-qa --reports
build/validation-python/Scripts/python.exe scripts/verify-xml-remaining-references.py build-numbered-summary/xml-remaining-qa --reports
build/validation-python/Scripts/python.exe scripts/verify-xml-actual-quantities.py build-numbered-summary/xml-quantity-qa --reports
build/validation-python/Scripts/python.exe scripts/verify-xml-baseline-cost.py build-numbered-summary/xml-baseline-cost-qa
```

전체 CTest 실행·macOS/스토어 패키지 실행·실제 MS Project/P6 재계산/재저장은 이번 검증 범위가 아니다. 외부 XML 기준선 자체의 채택/내보내기와 외부 제품 의미 동등성은 미완료다. XML 식/정적 값은 보존하지만 재가져오기의 기준선 식은 native 승인본이 없으면 미설정이며 `source.ppm`이 전체 이력을 보존한다.
