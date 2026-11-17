# Steps·LOE·WBS Summary·Actual Dates 로컬 검증

2026-11-05 / Windows / Qt6.8.3 / MinGW13.1 / Release. [기능·저장·계산 계약](../../activity-extensions.md).

가중 Steps 정의와 동결 보고, 선택한 물리 진척 원천/EVM, LOE·WBS Summary 지원 구간, C Actual Dates 끝점 정책을 구현했다. 기존 미커밋 구현을 보존한 체크아웃이다. 실제 작업/CPM/자원·예산에 지원 활동을 추가 집계하지 않는다.

## 결과

- **전체 CTest 105/105 통과**, 405.95초. [전체 로그](ctest.log), [테스트별 로그](ctest-details.log). 단계의 제품 소스 식별값은 [전체 검사 소스 목록](full-suite-source.json)에 보존한다.
- 전체 검사 후 **두 UI 파일**에서 지원 유형별 입력 활성화·A/B/C 비교 라벨·기존 Steps 그룹명 보존을 보완했다. 최종 생산 파일과 해당 테스트를 다시 빌드하고 **영향 범위 5/5**, 17.97초로 확인했다. [최종 검사](final-checks.log), [최종 테스트별 로그](final-check-details.log), [최종 빌드](final-build.log). 계산·저장·원장·보고서 구현은 전체 검사 이후 변경하지 않았다.
- **파서 184 seeds 통과**. schema1..19, 새 정의/보고의 정상 입력·가중치0·없는 작업·진척 범위, 기존 네 파서와 잘린 입력을 포함한다. 로컬 유한 seed 재생이며 ASan/libFuzzer 실행을 뜻하지 않는다. 미래 schema20 거절도 기존 파일 수명주기 검사에 반영했다.
- **clang-format18.1.8 208파일**, 변경 필요0개. [형식](format.json), [공백](whitespace.log). [주 빌드](build.log)와 최종 빌드에 compiler 경고·오류가 없다.
- [최종 독립 판독 9종](independent-readers.json) 통과. 새 판독 외에 기존 보고서/XML/진척 정책·평준화/결정 원장/객체 필드/기간 배정/인쇄·네트워크 출력을 재확인했다. 각 스크립트 로그를 같은 폴더에 보존한다.

새 [native/XLSX/PDF 판독](verify-activity-extensions.log)은 schema19 checksum과 정의·보고·기준선 분리, 동결 가중치1/7, 물리 진척25%/87.5%, 수동 물리10%와 Steps의 분리, EV87,500을 검증한다. 원본 작업3개·지원 활동2개·추가 배정0개, 지원 근무 기간1,200분, A/B/C 독립 완료일과 실제 시작 고정·원본 관계 보존을 확인했다. openpyxl3.1.5/pypdf6.10.0을 사용했다. 앱 실행 의존성은 아니다.

## 확인한 경계

Steps 정의 저장·dirty·undo/redo, 작업 복제 시 정의 보존, 보고 이후 정정 추가·이력 비우기, 정의 가중치 변경/undo가 과거 보고를 수정하지 않는지 확인했다. 상태일 이전 조회에 미래 보고가 나타나지 않고, 같은 상태일 정정은 마지막 보고가 이긴다. 수동 물리 원천과 Steps 원천을 전환해 EV10,000/87,500을 구분했다. 기간50%·Steps25%·보고분 공수50% 및 보고 없음의 미정을 별도로 확인했다.

보고자/사유·범위·형식·중복·참조 오류는 문서를 부분 변경하지 않는다. Steps 보고만 있는 작업도 정의 삭제 이후 작업 삭제를 차단한다. 지원 활동의 undo·중첩 WBS 범위/제외 작업·역전 LOE 끝점·오래된 계산 결과, 실제 작업 수·CPM/자원 입력 불변을 검사했다. schema19 저장/재열기·사본·복구·기준선, 잘못된 JSON과 schema18의 새 정보 거절, XML/XER 손실 진단을 확인했다.

Actual Dates는 실제 시작한 B의 수신 FS를 이력으로 분류하고 FF를 잔여 계산에 유지한다. B의 완료 기대값은 A=2026-11-04 14:00, B=2026-11-03 14:00, C=2026-11-03 18:00이다. B의 실제 시작2026-11-02 14:00과 완료 E의 실제 날짜를 보존한다. 원본 순환을 관계 마스크로 숨기지 못하는지와 채택/저장·화면 결과를 검사했다. [A/B/C 비교 JSON](policy-comparison.json)에는 C 이력 관계도 포함된다.

## 화면·출력

[Steps 화면](steps.png), [LOE/WBS Summary 화면](support.png), [C Actual Dates 화면](actual-dates.png)을 직접 확인했다. WBS Summary를 선택하면 사용하지 않는 LOE 기준 작업 입력을 비활성화한다. 읽기 전용·다른 revision의 편집 차단·그룹 이름 보존·메타데이터 입력과 채택을 확인했다. Qt offscreen 검사이며 실제 데스크톱/P6 제품 조작은 아니다.

[PDF 11쪽](report-11.png), [PDF 12쪽](report-12.png)을 Poppler로 렌더링해 표·한글·가중치/진척·날짜/기간 배치를 확인했다. [13쪽 PDF](activities.pdf), [XLSX](activities.xlsx), [native 사본](activities.ppm), [채택 설정](actual-dates.ppm), [독립 기대값](expected.json)을 보존한다. 실제 물리 프린터 검사는 수행하지 않았다.

## 재현

Qt/MinGW와 CMake/Ninja를 PATH에 추가한 뒤 실행한다.

```powershell
cmake -S . -B build-decisions -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=.deps/Qt/6.8.3/mingw_64
cmake --build build-decisions --parallel 8
ctest --test-dir build-decisions --output-on-failure
python scripts/verify-activity-extensions.py build-decisions
```

새 스크립트를 CI의 기존 openpyxl/pypdf 판독 단계와 산출물 보존에 추가했다. CI 원격 실행을 로컬 검증으로 대신 주장하지 않는다. [빌드 식별 헤더](BuildIdentity.h), [최종 소스/테스트/실행 파일·산출물 식별값](source-manifest.json), [결과 JSON](results.json)을 기록한다.

전체 검사 소스: `524f4199dfa9f1c140fed365b3e19e78958dc580d5febf3c5cc5d52bfad81235`. 최종 소스: `eb22778f6f12f1d5da9154f7091b50754a47cb69eaddd80a6f638c5496dd149b` (src/CMake 245개). 두 단계의 차이는 ActivityDialog/ProgressPolicyDialog다. `+dirty`는 이전 미커밋 구현과 이번 변경을 포함한다.

LOE/WBS Summary는 지원 구간 모델이다. 직접 자원/원가를 가진 실행 활동·지원 카드 간트·P6 TT_LOE/TT_WBS 이관은 후속이다. C 정책은 native 끝점 처리 계약이며 P6의 전체 Actual Dates/SS 지연 의미와 동등하다고 검증하지 않았다. 실제 P6 재계산·재저장 왕복, 다른 OS 실행은 미실행 범위로 유지한다.
