# 실제 P6 왕복 실행과 준비 자료

2026-11-15 외부 XML 기준선의 실제 MS Project/P6 왕복도 요청받아 실행 환경을 다시 조사했다. 두 제품을 찾지 못했고 사용자가 제품 실행 환경이 없음을 확인했다. 이번 기준선 왕복 역시 미실행이며 [실행 상태와 준비 XML](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-baseline-exchange-2026-11-15/product-execution-status.json)을 보존한다.

2026-11-03 사용자가 실제 P6 왕복을 요청했고, 이어 P6가 설치되어 있지 않다고 확인했다. 표준 설치/등록·실행 앱 조사에서도 P6를 찾지 못했다. **실제 P6 실행·왕복은 미실행**이다. 아래 준비 파일·native 재열기·MPXJ·비교기 자체검사는 이를 대신하지 않는다.

## 준비 자료

`ctest --test-dir build-forecast -R progress_policy_contract --output-on-failure`가 `build-forecast/progress-policy-qa/p6-kit/`에 다음 자료를 만든다. 사용자 업무 자료가 아닌 고정 수작업 사례다.

- `plan-input.xer`: 실적 없는 계획 A(16h)→B(8h)→C(8h), 독립 D/E(각 8h), 2026-11-02 시작, 월~금 09–12/13–18 달력. 원본 Activity Code는 변환 매핑의 J1…J5다.
- `expected-plan.json`: XER 바이트 해시와 native 계획의 ID/Activity Code·시작/완료·총/자유여유. 차이를 숨기지 않고 비교할 기준이다.
- `progress-native.ppm`, `expected-native-comparison.json`: 실제 A 시작 11-02 09:00, A 실제 8h/잔여 8h. B 시작 11-02 14:00, 실제 4h/잔여 4h. E 실제 11-02 09:00~18:00 완료. 상태일 11-03 09:00. PPM native A/B 비교 수치다.

현재 내보내기/가져오기의 지원 부분은 기간 기반 계획이다. 진행 실적 파일을 XER로 내보냈다는 이유로 실제/잔여 원장까지 옮겨졌다고 판단하지 않는다. native 전체 입력은 별도 보존한다.

## P6 설치 환경에서 실행할 절차

1. P6 제품·버전/빌드·언어·DB 종류와 실행자를 기록하고 업무 프로젝트와 구분된 테스트 위치에서 새 프로젝트로 가져온다. 기존 프로젝트 업데이트 옵션은 사용하지 않는다. 입력 파일 SHA-256과 가져오기 로그/진단·새 프로젝트 ID를 남긴다.
2. 작업/관계/기간과 프로젝트/작업 달력을 확인한다. 관계 지연 달력, hours/day, 상태일/data date, 제약·마감/Expected Finish, 자원 평준화, 임계/총여유 계산 방식과 Each Project 종점을 기록한다. 설정을 추정하여 동등하다고 표시하지 않는다.
3. F9로 재계산하고 Schedule Log와 날짜·여유 화면을 보존한다. 새 경로에 XER로 다시 내보내며 P6가 새로 부여한 내부 ID와 원본 Activity Code를 구분한다.
4. `python scripts/verify-p6-roundtrip.py --kit <준비폴더> --returned <P6가-내보낸.xer> --project-id <반환-프로젝트-ID>`로 비교한다. 인코딩은 명시하며 기본 UTF-8(BOM 허용), `--encoding cp1252`도 지원한다. 누락 날짜/여유는 미정으로 남기며 0이나 일치로 대체하지 않는다. 비교기는 P6를 실행하지 않으므로 파일 대조 성공만으로 실행 출처를 인증하지 않는다.
5. PPM 가져오기 미리보기→새 사본 열기→F9 재계산도 실행하고 원본/반환 파일·해시·변환 보고·P6 결과/PPM 수치와 차이 원인을 함께 보존한다. 입력/보존/각 제품 계산/실제 재저장 왕복을 별개로 판정한다.

## 진척 정책 추가 시험

계획 왕복과 별도 테스트 사본에 위 실제/잔여와 data date를 직접 입력한다. 같은 입력에서 Retained Logic과 Progress Override를 각각 재계산·별도 재내보내기하고 모든 설정을 기록한다. PPM native 비교의 B 잔여 재개 11-03 09:00 / 완료 14:00과 후행 C 완료 11-04 14:00을 대조하되 다른 결과를 버그/호환으로 자동 판정하지 않는다. P6 Actual Dates·Steps·LOE는 별도 미구현/미검증이다.

Oracle [Schedule Options](https://docs.oracle.com/cd/G48902_01/client_help/en_US/general_tab_-_schedule_options_dialog_box.htm)의 진척·lag·float 옵션을 기록한다. 비교기 `--self-test`는 합성 행의 일치/차이/미정·중복/비유한 수치 처리와 native 고정 사례를 검사할 뿐 **실제 P6 왕복 증거가 아니다**.

## 이번 요청의 산출물

[준비한 계획 XER](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/progress-policy-2026-11-03/p6-kit/plan-input.xer), [기대값/해시](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/progress-policy-2026-11-03/p6-kit/expected-plan.json), [native 실적 파일](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/progress-policy-2026-11-03/p6-kit/progress-native.ppm), [native A/B 결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/progress-policy-2026-11-03/p6-kit/expected-native-comparison.json)를 보존한다. [상태/검증 기록](validation/progress-policy-2026-11-03/README.md)에는 P6 실제 미실행과 MPXJ strict 읽기·비교기 자체시험 6개를 구분해 기록했다.
