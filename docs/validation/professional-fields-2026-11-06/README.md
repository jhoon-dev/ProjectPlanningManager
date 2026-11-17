# 자원 보기·날짜별 잔여 공수·계산식 검증

2026-11-06 Windows 64-bit, Qt6.8.3/MinGW13.1, Release/Ninja. 사용자 요청의 **자원 그룹·필터 → 날짜별 잔여 공수 → 계산식 필드**를 순서대로 구현하고 각각의 domain/UI 검사 후 통합 회귀를 실행했다. 기존 미커밋 구현은 보존했다.

## 검사와 수용 범위

| 계약 | 검증 내용 |
|---|---|
| [RV-01~04](../../resource-views.md) | 숫자/날짜/목록·AND 필터, 검색·정렬·3단계 그룹, 안정 ID·이름 변경, 미설정/빈 문자열/0, 고유 자원 행, 저장/갱신/삭제·undo/dirty, 참조 삭제 보호, 보기 저장 시 평준화·동결 불변, 1만 자원 projection |
| [DRW-01~05](../../dated-remaining-work.md) | 고정 기간·Steps, 명시 0/빈칸 상속, 정수 공수·최소 단위/상한, 달력/휴일/점심/중단/상태 시각, 근무시간 없는 양수·실행 밖 목표 거절, 과부하를 자르지 않음, 상태일 변경 후 한 배정씩 수정/해제 |
| 잔여 계산 연결 | 독립 civil-minute oracle, 논리 여유 후보의 목표 충족·479분 지연 증인, 평준화 가능/불가능·직접 엔진의 완료 작업 거절, 채택/자동 무효화·undo와 결정 원장, 시나리오 당시 입력 재현, Usage·유효 단가 원가 |
| [FF-01~05](../../formula-fields.md) | 숫자 6자리 고정소수 연산·부호/반올림/범위, 단위·의존성·스코프·안정 ID 이스케이프, lazy coalesce·미설정/오류 분리, 순환/문법/깊이/제어문자 거절, 데이터/정의 변경 시 캐시 정합, 512행 캐시 경계, 1만 자원 계산식 필터 |
| 편집/UI | 미리보기는 문서 불변, 준비 결과 적용/해제 한 undo, 입력 변경 시 인증 결과 폐기, worker 취소/닫기·revision/session·권한 철회·읽기 전용, 계산 결과 직접 입력 금지, 저장 보기/작업 필터와 계산식 연결 |
| 파일/출력 | schema21 자원 보기·22 날짜별 미래 입력·23 계산식과 세 기능 공존, 과거 버전/미래24 거절·dirty 복구·사본, 기준선·실적·진척·시나리오 보존, XML/XER 손실 명시/native 원본, XLSX 숫자 셀·PDF 텍스트 |

## 실행 증거

- 최종 전체 **120/120 통과**, **488.29초**. 새6개(domain/UI 각3), 기존 계획 공수·평준화·진척·Usage·교환·성능/취소·저장/복구·보고/원가·서버 인증/동기화/주기 실행 회귀를 포함한다. 서버 통합 내부23개 검사도 통과했다. 최종 빌드에 컴파일 경고·오류가 없었다.
- [최초 빌드](build.txt), [최종 변경분 빌드](build-followup.txt), [전체 CTest](ctest.txt), [형식 검사](format.json).
- schema1~23·정상/비정상·잘린 입력을 포함한 파서 **244 seeds**를 통과했다. clang-format18.1.8 검사 **284파일**은 변경 없음이며 Python 구문과 `git diff --check`도 통과했다.
- [독립 판독 JSON](independent-readers.json): openpyxl3.1.5/pypdf6.10.0, native 본문의 SHA256과 입력 보존, 자원 고유5행·미설정/빈 값/0, 날짜별 잔여 2/0/10 h 및 원가1440을 확인했다. 기존 계획32 h/3360, 실적6001 정수 단위·기간 진척·동결 시나리오를 보존한다. 보고된 실행 구간을 실제 civil-minute로 적분하며 production의 역산 공식을 재사용하지 않는다.
- 같은 판독기는 작업16/4 h·프로젝트4·자원20/4의 숫자 결과, 미설정과 실제 오류 표, 입력만 저장한 native 속성, 세 기능이 함께 있는 schema23과 최소 공수6001을 대조한다. 고정 seed20261106의 **400개 연산**은 Python Decimal precision80/ROUND_HALF_UP의 6자리 결과·예상 오류3건과 일치한다. 모든 XLSX에 실행 수식·매크로·외부 링크가 없으며 PDF3종의 텍스트/제어문자를 검사한다.
- Windows 기본 UI **3/3**: [자원 보기](native-resource-ui.txt)·[잔여 입력](native-remaining-ui.txt)·[계산식](native-formula-ui.txt). [자원 기본](resource-view.png)/[800×600](resource-view-small.png), [잔여 기본](remaining-editor.png)/[800×600](remaining-editor-small.png), [계산식 정의800×600](formula-editor.png), [자원 계산 결과와 오류](formula-resources.png)를 시각 검토했다. 목표 2/0/10 h·적용 전 검산 합계12 h, 참조 이름 선택/삽입·검사 결과16, 계산 오류 원인과 건수를 확인했다.
- [자체 실행 runtime](runtime.json): `build-governance/professional-fields-runtime-1711b133/payload/PPM.exe`, QtNetwork/Schannel·MinGW·라이선스 포함74파일. 원본 빌드와 PPM/PPM-Automation SHA256이 같다. Qt/MinGW 없는 PATH에서 동봉 CLI `--help` 종료0을 확인했다. 로컬 실행 패키지이며 서명/스토어 배포는 수행하지 않았다.

## 재현

Qt·MinGW·CMake/Ninja PATH 설정 후:

```powershell
cmake --build build-governance --parallel 4
ctest --test-dir build-governance --output-on-failure
.deps/report-verification/Scripts/python.exe scripts/verify-professional-fields.py build-governance
python scripts/check-cpp-quality.py format --tool .deps/cpp-quality/Lib/site-packages/clang_format/data/bin/clang-format.exe
# build-governance 작업 디렉터리에서 Windows 기본 UI
.\jobs_resource_view_ui_tests.exe -platform windows
.\jobs_dated_remaining_ui_tests.exe -platform windows
.\jobs_formula_field_ui_tests.exe -platform windows
```

검사는 임시 문서를 사용한다. `build-governance/professional-fields-qa`에 native/XLSX/PDF·400개 연산 JSON·화면을 만든다. 실제 사용자 파일·외부 계정/서버를 변경하지 않는다.

## 검증 경계

날짜별 입력은 고정 기간 작업의 미래 배정 수요다. 계획 예산·실적·기준선·기존 EVM EAC 공식을 바꾸지 않는다. 날짜 고정 입력의 **자원 순서 여유/임계는 미산정 사유를 표시**하며 논리 잔여 여유와 평준화 수요 검사는 지원한다. FixedWork/FixedUnits 역산·자원 생산성·외부 UDF/계산식 교환은 후속이다. 숫자 이외 계산식과 임의 스크립트를 지원하지 않는다. 실제 MS Project/P6 재계산/재저장·macOS·물리 프린터·실제 서버 배포/보안 감사·현장 수용은 이 Windows 로컬 증거로 완료 처리하지 않는다.
