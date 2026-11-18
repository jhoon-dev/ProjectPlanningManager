# 연결 묶음 재분할 여유 확장 검증

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/scaled-repartition-float-2026-11-04)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

검증일: 2026-11-04, Windows Release / Qt6.8.3 / MinGW GCC13.1. 전체 CTest **89/89**, **271.77초**. 파서114 seeds(schema1~14), clang-format18.1.8 **146파일**, 독립 reader/비교기 자체시험 **17/17** 통과.

## 구현과 수용 범위

새 선택 엔진은 `jobs-adopted-leveling-1.4`다. `resource-repartition-float-v3`의 수학적 기준·현재 날짜·기간/공수·달력·진척 사실을 유지한다. 논리/자원 반환 부등식의 후보 지지를 반복 검사하고 단일 후보 작업을 고정한다. 이동 가능 작업만 명시적 스택으로 탐색한다. 기존 연결 묶음64개 제한을 묶음당 이동 가능128개 제한으로 바꿨다. 후보250,000·후보 구간2,000,000·판정2,000,000과 전체 작업10,000 등 [계약의 한도](../../leveling-float.md)는 계속 적용한다.

`LEVELING_REPARTITION_SEARCH_POLICY`는 연결/고정/이동 가능 작업 수를 설명한다. 1.4 증인은 해당 작업과 변경된 동료 날짜만 기록한다. 생략된 작업은 현재 날짜를 유지한다. 이동 작업의 총여유마다 전체 프로젝트 자원 부하를 검증하고, 고정 작업은 현재 유효 배치로0여유를 증명한다. 증인 날짜 총2,000,000개 한도도 적용한다. 모든 작업의 최후 날짜를 동시에 적용할 수 있는 공통 일정이라는 뜻은 아니다.

## 수치와 독립 재구성

아래 관측은 1분 작업·공유 자원100%의 준비된 사례다. `analysis_ms`는 논리 일정 계산+파생 여유 분석의 단회 관측이며 직렬 자원 전진 배치·UI·저장과 반복 표본 성능 보장은 포함하지 않는다.

| 연결 작업 | 고정/이동 가능 | 여유 기준 | 관측 ms |
| --- | --- | --- | ---: |
| 65 | 65 / 0 | 전체TF/FF0·임계 | 20.3076 |
| 128 | 0 / 128 | 전체TF2분, 마지막만FF2분 | 117.8047 |
| 65, 자원 인계만 연결 | 0 / 65 | 전체TF2분, 마지막만FF2분 | 103.6530 |
| 256 | 192 / 64 | 고정TF0, 나머지TF2분, 마지막만FF2분 | 132.3615 |
| 1,000 | 1,000 / 0 | 전체TF/FF0·임계 | 181.2515 |

독립 Python 검증은 6작업/2분 여유를 원시 전수 열거하고, 큰 단위 작업 체인의 순서·완료 상한으로 최대 여유를 유도했다. 모든 **1,514개** native 증인을 재구성해 자원 겹침·원본 하한·고정 날짜·최종 완료일·작업별 총여유를 대조했다. 메트릭의 독립 완료일 작업H는 표의 연결 작업 수에 포함하지 않는다. 1,000개가 모두 자유롭게 이동하는 대형 프로젝트의 성능 시험은 아니다.

추가 core 계약은 4종 관계/양음 지연의1.3/1.4 수치 일치, 세 작업50%의 실제 합산 수요, 기존 교차 분할에65개 고정 논리 동료를 붙인 준비된 배치(A660/B540·FF60), 이동 가능129개에서 원자적 미산정, 취소·반복 계산의 동일성, 기존1.3의64개 제한과 승인 기준선 JSON 재현, 현재 선택1.4의 계산 가능 전환을 검사한다. 기존 재분할 계약은 작업 유형·미정/0용량·자원 달력·FS0 좌표·진척/실제 시작·저장/Undo를 계속 검사한다.

## 출력·출처와 재현

`selected.ppm`, XLSX/PDF/인쇄PDF와 두 화면은 기존3작업 재분할 fixture를 새1.4에서 재생한 결과다. 기존 독립 분 단위 oracle의 A660, B540/FF60, C0도 일치한다. 새 탐색 진단과 증인의 생략 규칙은 보고서 진단에서 확인한다. 파일 schema12를 유지하며1.4를 위해 새 입력 schema를 추가하지 않았다.

`source-manifest.txt`는 src C++/헤더+CMake **204파일**, SHA256 `9e184382eb5d026f06a852ca3a9ef80d948bc5a1921a99044eaebe5d8866356c`다. 빌드 식별값과 일치한다. `source-identity.json`에 관련 테스트·검증 스크립트·CI·실행 파일 해시를 보존한다. `ctest.log`, `ctest-details.log`, `format-gate.json`, `metrics.json`, `verify-*`, `independent-summary.json`이 원시 근거다. 전체 회귀 시작 이후 제품/테스트 소스를 바꾸지 않았다.

```powershell
cmake --build build-forecast --parallel 4
ctest --test-dir build-forecast --output-on-failure
python scripts/verify-scaled-repartition-float.py build-forecast
.deps/report-verification/Scripts/python.exe scripts/verify-repartition-float.py build-forecast
```

Qt/MinGW DLL 경로와 CMake/CTest 실행 경로를 PATH에 설정한다. CI의 verifier/artifact 단계도 연결했고 워크플로 YAML을 로컬에서 판독했다. 원격 CI 실행, macOS, 신규 sanitizer/퍼징, 실제 프린터/패키징 검증은 수행하지 않았다. P6는 사용자 확인대로 미설치이며 실제 왕복은 미실행이다. MPXJ reader와 비교기6종 자체시험을 실제 P6 실행으로 보지 않는다. 과거 검증 폴더는 당시 증거로 그대로 보존했다.
