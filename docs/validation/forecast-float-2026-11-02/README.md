# 실적 반영 예측의 여유·임계 검증

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/forecast-float-2026-11-02)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

2026-11-02. GAP-01 및 GAP-02의 native 정책 구체화 증분. [계산·표시 계약](../../forecast-float.md)을 따른다. 이 기록은 로컬 Windows 검증이며 MSP/P6 실행 동등성이나 전체 GAP 완료 기록이 아니다.

## 구현

- FS0·PDM·제약·공수 실행 모델의 기존 전진 날짜를 유지하고 잔여 후진 계산을 추가했다. 예측 정책은 `/remaining-float-v1`로 식별한다.
- 실제 시작·완료와 승인 기준선을 보존한다. 완료 후행의 과거 위반은 진단에 남기고 미래 잔여를 제한하지 않는다. SS/SF의 고정 실제 시작과 FS/FF의 예측 완료를 구분한다.
- 총/자유여유는 작업 달력 근무분이며 종점은 예측 프로젝트 완료일이다. 날짜 제약은 지키고 마감일은 기존 별도 경고를 유지한다. 자원 용량 평준화의 여유는 이번 범위가 아니다.
- 작업 표의 총여유·임계 상태/툴팁, 간트 막대·driving 관계, 기준선 비교 표, 보고서 KPI/예측 명세와 XLSX/PDF에 연결했다. 완료 실적과 미산정은 숫자 0으로 표시하지 않는다.
- native schema 9·최초 계획 엔진·기준선 형식은 변경하지 않았다. 여유 가용 여부는 저장하지 않는 세션 결과다.

## 검사 범위

| 검사 | 확인 내용 |
|---|---|
| `forecast_float_contract` | GAP-T01의 4개 프로필 0/0/8h 기대값, 완료/진행 실제 날짜, 상태일/잔여 변경, 평준화 여유 분리 |
| 독립 분 단위 oracle | 연속 달력의 108개 생성 네트워크에서 4종 관계·양/음의 지연·마일스톤·진행/완료 실적의 날짜와 총/자유여유를 전수 열거와 대조. 행 순서 반전 결과도 대조 |
| 제약·달력·공수 | 상한/고정 시작·충돌·마감 경고, 혼합 달력/휴일, 공수와 자원 교집합, 수동 중단, 혼합 달력의 음수 지연과 총/자유여유 차이 |
| 이력·파일·취소 | 기준선 재검산, 입력 불변, 저장/재열기, 복구 사본, 편집 undo, 오래된 계산 거절, 계산 취소와 잔여 후진 계산 중 협력 취소 |
| `forecast_float_ui` | 완료 `—` / 예측 임계 `0` / 예측 여유 `8`, 자유여유 툴팁, B→C driving 선, 비교 표, 임계 KPI, 계획 모드 복귀 |
| 독립 출력 reader | openpyxl로 숫자 시간·상태·미정·비수식 셀을 확인하고 pypdf로 한국어 레이블·제어문자 부재 확인 |
| 기존 회귀 | 일정·공수·기준선·원장·파일·복구·평준화·교환·보고서·편집·플랫폼 공통 UI의 기존 CTest |

**최종 결과: 65개 CTest 항목 통과.** 첫 전체 실행은 64/65였고, 실패한 `dialog_layout`은 새 예측 임계 KPI 추가로 카드가 12개에서 13개가 된 데 따른 기존 기대값 차이였다. 기대값을 갱신한 뒤 해당 항목을 재빌드·재검사해 통과했다. 최초 전체 실행 224.68초와 재검사 5.62초를 별도 보존하며 전체를 다시 실행했다고 주장하지 않는다.

- [검증 요약·소스 식별](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/forecast-float-2026-11-02/verification.json), [최초 전체 CTest 로그](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/forecast-float-2026-11-02/full-ctest.log), [레이아웃 재검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/forecast-float-2026-11-02/layout-retest.log)
- [clang-format 18.1.8 검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/forecast-float-2026-11-02/format.json), [독립 XLSX/PDF 검사](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/forecast-float-2026-11-02/independent-export.json)
- 기존 출력 회귀의 XLSX 5개/PDF 6개도 `verify-report-fixtures.py`로 독립 재검증했다. 새 예측 출력은 숫자 0/8h와 완료 미정·상태를 대조했고 PDF 8페이지에서 레이블·제어문자 부재를 확인했다. PDF 시각 렌더링 검증을 대신하지 않는다.
- [표·간트 캡처](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/forecast-float-2026-11-02/forecast.png): 완료 A는 미래 여유 `—`, B/C는 `예측 임계`, D는 `8h / 예측 여유`이며 B→C만 미래 임계 driving 선으로 표시된다.

제품 소스 SHA-256은 `71591ffd57333e3d6d90e5852284c2632f47f437196a202131d8fa415f9bedf9`다. `src`의 C++/헤더와 CMakeLists를 정렬한 manifest로 계산하며 `build-forecast/BuildIdentity.h`와 현재 파일이 일치함을 확인했다. Git 기준은 `6a1cd0a5115f3400d6cee4e5d5df795f351195ea + dirty`이며 새 커밋/CI 실행은 만들지 않았다.

## 전후 성능과 날짜 보존

기준 Git revision의 `src`를 `build-forecast-before`에 별도로 추출해 같은 벤치마크 소스로 빌드했다. 원본 작업 디렉터리의 제품 소스를 되돌리거나 수정하지 않았다. Windows 11, MinGW GCC 13.1, Release `-O3 -DNDEBUG`, 논리 CPU 16개 환경에서 전/후 프로세스를 순차 실행했다. 각 프로세스에서 계획 계산을 측정 구간 밖에 한 번 수행하고 예측 계산을 3회 반복했다. [환경·소스 hash·모든 원시 표본](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/forecast-float-2026-11-02/benchmark.json)을 보존한다.

| 작업 수 | 프로필 | 변경 전 중앙값(ms) | 변경 후 중앙값(ms) | 변경 후 관측 P95(ms) |
|---|---|---|---|---|
| 1,000 | FS0 | 30.61 | 48.67 | 53.23 |
| 1,000 | PDM·제약 | 62.74 | 97.61 | 98.95 |
| 1,000 | 공수·분할 | 371.31 | 377.98 | 446.14 |
| 10,000 | FS0 | 48.87 | 233.76 | 255.78 |
| 10,000 | PDM·제약 | 641.89 | 1,049.15 | 1,060.98 |
| 10,000 | 공수·분할 | 943.52 | 1,510.64 | 1,519.68 |

추가 후진 계산의 비용 때문에 계산 시간이 증가했다. 6개 시나리오 모두 최초 계획의 작업별 날짜·여유·임계·driving 값 hash와 예측 시작/완료/잔여 재개 날짜 hash가 전후 일치했다. 예측 여유·임계의 새 값은 날짜 보존 hash에 포함하지 않는다. hash는 동일 fixture 대조용 FNV-1a이며 보안 서명이 아니다. 3회 표본의 nearest-rank 관측 P95는 최댓값이고 모집단 지연 보장이 아니다. 메모리·화면 갱신 시간은 이 벤치마크에서 새로 측정하지 않았다.

## 재현

Qt 6.8.3 MinGW와 MinGW 13.1의 `bin`, CMake/Ninja를 PATH에 둔다. 기본 `build` 캐시는 저장소 이름 변경 전 경로를 가리켜 이번에는 독립 `build-forecast`를 사용했다.

```powershell
cmake -S . -B build-forecast -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=.deps/Qt/6.8.3/mingw_64
cmake --build build-forecast --parallel 4
ctest --test-dir build-forecast --output-on-failure
python scripts/verify-report-fixtures.py build-forecast
python scripts/verify-forecast-float-exports.py build-forecast
./build-forecast/jobs_forecast_float_benchmark.exe 1000 legacy 3
./build-forecast/jobs_forecast_float_benchmark.exe 10000 pdm 3
./build-forecast/jobs_forecast_float_benchmark.exe 10000 effort 3
```

독립 출력 reader에는 기존 선택 개발 의존성 openpyxl/pypdf가 필요하다. 벤치마크는 `legacy`, `pdm`(제약 포함), `effort`를 받는다. fixture는 100개 폭의 선행 체인과 60/75/90분 작업이며 공수 프로필은 자원 달력과 중단을 포함한다. 상태일은 2026-11-02 18:00, 실적 미보고 작업은 기존 정책대로 계획 잔여를 사용한다.

## 한계

- macOS·Linux 실행과 CI, sanitizer/퍼징 재실행, 실제 Microsoft Project/P6 재계산·재저장은 이 로컬 증분에서 검증하지 않았다.
- UI 증거는 Qt offscreen 화면이며 Windows 시스템의 맑은 고딕을 테스트에 명시 로드했다. 실제 데스크톱 상호작용·접근성 검증을 대신하지 않는다.
- 독립 oracle의 생성 사례는 연속 달력의 기간 작업이다. 근무 간격·공수·자원 교집합·분할은 별도 수기 기대값과 기존 회귀로 검증한다.
- 최초 계획이 불완전/충돌이면 기존 정책대로 예측도 확정하지 않는다. 고정 목표일 대비 음수 여유, Progress Override/Actual Dates, 예측 ALAP 재배치, 평준화 후 임계와 결과 채택은 후속 범위다.
- 성능 fixture는 실제 고객 일정이 아니며 소수 반복의 관측값이다. 10만 작업·긴 단일 체인·대규모 실적 원장·자원 교집합별 분산을 포괄하지 않는다. 제품 전체 성능 보장이 아니다.
