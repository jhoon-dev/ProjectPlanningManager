# 기간별 계획 공수 변환 검증

2026-11-05 Windows 64-bit, Qt6.8.3/MinGW13.1 Release/Ninja. [잔여 항목 재점검](../../planning/18-remaining-priorities-review.md)과 [PW-01~04 계약](../../planned-work-input.md)의 지원 범위를 검증한다. 이전부터 있던 미커밋 구현은 보존했다.

## 검사 구성

| 요구 | 실행 검사 |
|---|---|
| PW-01 입력/정확 변환 | 0·최소 공수·최대 배정률, 소수 시간 20,000개 및 상한 왕복, 앞쪽 정수 나머지, 무변경 혼합 곡선 보존, 선택 밖/기간 밖 보존, 다중 배정 |
| PW-02 달력/범위 | 프로젝트/자원 휴일·오전/상이한 작업 달력·점심·작업 중단, 자원 근무시간 없는 양수 거절, 가용률 초과를 자르지 않음, 월/월요일/연도9999 경계, 1,024행·구간 한도·정렬/겹침/범위·미지원 유형, Steps 허용 |
| 독립 수치 대조 | production의 TaskExecution/WorkCalendar/역산 공식을 쓰지 않는 civil-minute oracle, 고정 seed42의 달력/중단 24개 조합과 수기 기대값 |
| PW-03 초안/원자 적용 | 하위 창 변환/수락은 문서 불변, 부모 초안 반영 후 한 번의 문서 적용/undo/redo, 목표 수정 시 결과 무효화, 단위 변경 초기화, worker 취소/초기 취소 후 재조회·닫기·stale·권한 철회·읽기 전용 |
| PW-04 보존/출력 | schema17 native 저장/읽기·dirty 복구/재현, 기준선·실적 원장 보존, 날짜·Usage·유효 단가 원가 대조, XLSX 숫자·퍼센트/수식 없음·PDF 텍스트와 제어문자 검사 |

## 실행 결과와 증거

- 전체 **114/114 통과**, 556.82초. 새 planned_work_contract(18.31초)/planned_work_ui(1.51초), 기존 서버 인증·동기화·HTTP·주기 실행, 파서·저장/복구·평준화·출력 회귀를 포함한다. 별도 Windows 기본 UI도 통과했다. clang-format18.1.8 **258파일** 변경 없음, Python 구문 검사와 git diff --check도 통과했다.
- [빌드 로그](build.txt), [전체 CTest 로그](ctest.txt), [clang-format 검사](format.json).
- [독립 판독](independent-reader.json)은 native SHA256·목표 정수 공수, 계획 Usage 2/0/10 h·원가 120/0/1200(합계1320), 수치 퍼센트와 PDF를 확인한다. 상태일을 갖는 자원 보고서의 수요 표는 기존 계약대로 0 수요 날짜를 생략하고 잔여 곡선을 재생한 11/04·11/06의 2/10 h를 표시한다. 계획 Usage와 잔여 수요를 혼동하지 않는다.
- [Windows 기본 UI 실행](native-ui.txt), [기본 화면](planned-work-native.png), [800×600 화면](planned-work-small-native.png)을 시각 검토했다. 목표 2/0/10 h의 검산 합계 12 h와 문서 적용 전 초안 상태가 보인다.
- [자체 실행 runtime inventory](runtime.json): Qt6Network/Schannel과 MinGW·라이선스를 포함한 74파일. Qt/MinGW가 PATH에 없는 환경에서 동봉 PPM-Automation --help 실행을 확인했다. 새 로컬 실행 파일은 `build-governance/planned-work-runtime-7e46d552/payload/PPM.exe`다. 서명/스토어/외부 게시 범위는 아니다.
- [외부 제품 환경 확인](external-product-environment.json): Windows 설치 등록 두 경로와 PATH에서 MSP/P6를 찾지 못했다. 해당 제품을 실행하거나 설치하지 않았다. 이 제한적인 조회로 모든 portable 설치 위치가 없다고 단정하지 않는다.

## 재현

Qt/MinGW/CMake PATH 설정 후:

```powershell
cmake --build build-governance --parallel 4
ctest --test-dir build-governance --output-on-failure
# 기존 report-verification venv: openpyxl3.1.5, pypdf6.10.0
.deps/report-verification/Scripts/python.exe scripts/verify-planned-work.py build-governance
python scripts/check-cpp-quality.py format --tool .deps/cpp-quality/Lib/site-packages/clang_format/data/bin/clang-format.exe
# Windows 기본 플랫폼 확인 (build-governance 작업 디렉터리)
.\jobs_planned_work_ui_tests.exe -platform windows
```

검사 산출물은 `build-governance/planned-work-qa`에 native/목표 JSON·XLSX/PDF·화면을 생성한다. UI 검사는 임시 문서를 사용한다. 실제 사용자의 파일이나 원격 계정을 변경하지 않는다.

## 검증 경계

고정 기간 배정의 현재 논리 계획에서 목표 공수를 정확히 맞추는 **배정률 생성**이다. 작업 이동/달력 변경 뒤 총공수 고정, FixedWork/FixedUnits 곡선 역산, 날짜별 잔여 공수 직접 입력, 실제 MSP/P6 재계산/재저장, macOS·물리 프린터·실제 서버 배포를 완료로 표시하지 않는다. 기존 schema/엔진과 실적/동결 기록의 의미는 유지한다.
