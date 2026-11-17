# FixedWork/FixedUnits 곡선 역산 검증

2026-11-06 Windows 64-bit, Qt6.8.3/MinGW13.1 Release/Ninja. [WCI-01~05](../../work-contour-inverse.md)와 [잔여 항목 재점검](../../planning/19-work-contour-remaining-review.md)의 native 지원 범위를 구현·검증했다. 기존 미커밋 작업은 보존했다.

## 수용 검사

| 영역 | 확인 내용 |
|---|---|
| 독립 실행 oracle | 80종의 명시 달력·휴일·작업 중단·0/혼합률·고정 공수/단위·작은 정수 공수. C++ 직접 civil-minute ledger와 Python의 독립 근무분 순회가 실제 완료 시각/목표 공수를 대조한다. production TaskExecution/WorkCalendar/역산을 oracle로 재사용하지 않는다. |
| 유형별 역산 | FixedWork 공수 보존·요청 기간·정수 나머지·1단위·0공수/0기간·불가능한 자원 끝점, FixedUnits 기간/공수/곡선 편집과 기존 공수 확정, 변경 밖/기본률·잠재 구간 보존. 다중 자원의 정확 배분·마지막 분의 안정 ID 배분 확인. |
| 비단조 후보 | 0분 시작 완료09:07, 1분 시작 완료09:05인 자원 달력/곡선 사례. 완료 하한과 SS 시작 하한의 교집합을 재검산해03분 시작/06분 완료를 찾고 논리/자원 총·자유여유/증인을 확인한다. 완료 실적은 과거 날짜라도 미래 수요를 만들지 않는다. |
| 편집과 동결 | 역행 선후행의 행/명령 순서4조합·batch worker 한 commit/undo, 미리보기 문서 불변·stale·초기 취소·readonly·실패 원자성, 관측 진척/잔여 공수·기준선 보존, 평준화 채택 엔진·입력 변경 무효화/결정 원장·동결 시나리오 재현 |
| 파일/출력 | 조건부 schema24·구 버전 조합 거절·미래25 거절·dirty 복구/재현·일반 사본, Usage·공수·유효 단가 원가·부하, 숫자 XLSX·PDF 텍스트, 기존 schema/고정 기간/균등 경로 |
| UI | 같은 순수 곡선 편집 함수의 미리보기/문서 적용 일치, FixedWork/FU의 다른 계산 기간·고정 공수8h, cancel/undo/stale/readonly·닫기, 기본/800×600과 스크롤 가능한 내용·고정 하단 버튼 |

## 실행 증거

- 최종 전체 회귀 **123/123 통과**, **523.44초**. 서버 인증/동기화/HTTP/주기 실행23개 검사, 기존 일정·여유·교환·저장/복구·UI를 포함한다. 새 곡선 계약/UI와 집중5/5·독립80사례·파서266 seeds도 통과했다. [검증 요약](verification-summary.json).
- [집중 빌드](build-focused.txt), [전체 빌드](build.txt), [엔진/채택 최종 빌드](build-final.txt), [완료 실적 가드 빌드](build-completion-guard.txt), [최종 집중5/5](focused-final.txt), [전체 회귀](ctest.txt), [형식 검사](format.json).
- [파서266 seeds](parser-check.txt): schema1~24의 정상/비정상/잘린 입력을 재생한다. 최초 집중 검사에서 새 seed 자원의 값만 복사하고 사전을 생략한 fixture가 CLASSIFICATION_INVALID로 거절되었다. fixture의 불필요한 속성을 제거하고, schema24에서 유효해진 FixedUnits raw-body 기대를 갱신했다. 구 버전 native envelope의 거절은 별도로 확인한다.
- [독립 판독](independent-readers.json): native SHA256·schema24·Work48,000(8h)/기간960분(16h), 기본률100%와 기간 밖1500~1700분·7% 보존, 실제 실행 구간의 분 적분을 확인한다. 일별 공수4/4h·원가240/480(합계720), 숫자 셀·실행 수식/매크로/외부 링크 없음, PDF10페이지의 한글 텍스트/제어문자도 검사한다. Python은 C++이 내보낸80개 입력을 별도 달력 순회로 다시 계산했다.
- 저장 후 clean 문서는 복구 체크포인트를 만들 수 없는 기존 계약에 맞춰, 출력 fixture를 저장한 뒤 별도 dirty 변경으로 복구를 검증했다. 같은 파일을 두 편집 문서로 열지 않고 임시 사본을 사용했다. [출력 fixture 글꼴 빌드](build-export-fixture.txt)·[최종 계약 재실행](export-fixture.txt)은 Windows 한글 글꼴을 지정한 headless PDF 판독을 포함한다. 제품 실행 모델의 변경은 아니다.
- Windows 기본 [UI 실행](native-ui.txt): [FixedWork 기본](fixed-work.png)/[800×600](fixed-work-small.png), [FixedUnits 기본](fixed-units.png)/[800×600](fixed-units-small.png)을 시각 검토했다. 계산 기간400/540분과8h, 그래프·구간 끝 잘림/보존, 미리보기/적용·하단 버튼을 확인했다.
- [Runtime inventory](runtime.json): `build-governance/work-contour-runtime-525eb11e/payload/PPM.exe`, QtNetwork/Schannel·MinGW·라이선스 포함74파일. 빌드와 PPM/PPM-Automation 해시 일치·[깨끗한 PATH의 help](runtime-help.txt), 동일 PATH에서 [schema24 파일 inspect](runtime-inspect.json) 종료0을 확인했다. 서명/스토어 배포는 수행하지 않았다.

## 재현

Qt/MinGW/CMake/Ninja PATH 설정 후:

```powershell
cmake --build build-governance --parallel 4
ctest --test-dir build-governance --output-on-failure
.deps/report-verification/Scripts/python.exe scripts/verify-work-contour-inverse.py build-governance
python scripts/check-cpp-quality.py format --tool .deps/cpp-quality/Lib/site-packages/clang_format/data/bin/clang-format.exe
# build-governance 작업 디렉터리
.\jobs_work_contour_ui_tests.exe -platform windows
```

`build-governance/work-contour-qa`에80개 oracle 입력/완료 JSON, schema24 native·XLSX/PDF·화면을 생성한다. 임시 문서를 사용하고 사용자 프로젝트나 외부 계정/서버를 수정하지 않는다.

## 검증 경계

상대 곡선과 현재 논리 날짜에서의 정확 기간/공수 역산이다. 달력·배정·선후행/평준화 변경 후 FixedWork의 기간은 달라질 수 있다. 공수 기준 잔여는 명시 보고된 잔여량을 현재 실행의0분부터 적분하며 실제공수/과거 위치를 추정하지 않는다. 기존 고정 기간 목표 공수 하위 창과 균등 역산 반올림 경로는 유지한다. 날짜 고정 잔여 목표의 FixedWork/Units·자원 생산성·지원 활동 직접 자원/원가·외부 고유 곡선·실제 MSP/P6 재계산/재저장·macOS/물리 프린터·스토어/서버 배포·현장 수용은 별도 범위다.
