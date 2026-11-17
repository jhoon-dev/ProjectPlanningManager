# 기간별 계획 배정·자원 곡선 로컬 검증

2026-11-05 / Windows / Qt6.8.3 / MinGW13.1 / Release. [기능·계산·저장 계약](../../assignment-periods.md).

기존 미커밋 작업을 보존한 현재 체크아웃에서 구현·검증했다. 자원 메뉴의 새 편집 화면, 작업 근무분 구간과 정수 면적 곡선, 계획/잔여 공수·원가·Usage·순간 부하·평준화, schema17·동결 입력·undo/복구를 함께 검사했다.

```powershell
cmake -S . -B build-decisions -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=.deps/Qt/6.8.3/mingw_64
cmake --build build-decisions --parallel 8
ctest --test-dir build-decisions --output-on-failure --stop-on-failure
python scripts/verify-assignment-periods.py build-decisions
```

Qt/MinGW와 CMake/Ninja를 PATH에 추가한다. 독립 판독에는 optional openpyxl3.1.5/pypdf6.10.0 환경을 사용했다. 애플리케이션은 Python/Office를 요구하지 않는다.

## 결과

- **전체 CTest 99/99 통과**, 316.85초. [전체 로그](ctest.log), [테스트별 로그](ctest-details.log), [결과 JSON](results.json).
- **파서 148 seeds**, schema1..17 경로와 구 schema envelope 거절을 확인했다. 최초 실행의 새 seed 기대값이 checksum/본문 검증 경로를 혼동한 부분을 수정하고 최종 전체 검사를 다시 통과했다. [최초 로그](initial-ctest.log). 파서 계약·application 파일 버전 거절 검사를 약화하지 않았다.
- **clang-format18.1.8 177파일**, 변경 필요 0개. [형식 검사](format.json), [공백 검사](whitespace.log). 최종 빌드에 compiler 경고/오류가 없다. [빌드](build.log), [실행 구간 공유 개선 빌드](build-resource-cache.log).
- [독립 native/XLSX/PDF 판독 9종](independent-readers.json) 통과. 새 예제는 150%·100%·50%의 3일 배정에서 **12·8·4시간**, 날짜별 유효 단가에 따른 **720·960·480**, 합계 **2160**을 확인했다. 배정률은 수치/백분율 셀이고 수식은 없다. 기준선/시나리오 구간 동결·schema17 checksum과 PDF6페이지도 확인했다.
- [기간 편집 화면](period-editor.png), [800×600 화면](period-editor-small.png), [읽기 전용](period-readonly.png)을 직접 확인했다. 날짜·공수·곡선이 표시되고 작은 창은 본문 스크롤과 고정 적용/닫기를 제공한다. Qt 오프스크린 검사이며 실제 Windows 데스크톱 자동 조작은 아니다.

계약 검사는 홀수/짧은/최대 기간의 정확한 곡선 면적, 최대 배정률 거절, 기본률/0%/빈 구간, 휴일·중단·자원 달력 교집합, 작업 이동과 기간 단축/연장, 잔여 꼬리와 완료 수요 없음, 명시 실적 불변, 진짜 곡선 수요의 평준화와 재배치 여유 경로를 확인한다. 고정 공수/단위·역산·기간 미정/0 거절의 원자성, 1,024/100,000 구간 한도·잘못된 JSON, 구 writer/엔진, archive-only/과거 정책만 남은 schema17, 기준선 재현, 복제/삭제, dirty/undo/redo, 평준화 무효화 결정 원장, worker 취소/stale/session 재열기·창 닫기·읽기 전용, 일반 배정 창의 구간 보존도 포함한다.

## 제한된 규모 관측

같은 자원에 1%로 배정한 서로 독립인 60분 작업을 사용했다. 각 배정은 앞 30분 1%, 뒤 30분 0%의 두 구간이다. [원시 수치](scale.json).

| 배정 | 저장 구간 | 계획 계산 | 부하 집계 | 공수·최대 수요 검산 |
|---|---|---|---|---|
| 1,000 | 2,000 | 50ms | 25ms | 일치 |
| 10,000 | 20,000 | 327ms | 109ms | 일치 |

[공유 개선 전 관측](scale-before-cache.json)은 부하 집계 6,925/48,030ms였다. 실행 범위 안의 달력/실행 격자를 공유하고 해당 기간만 검색하도록 개선했다. 위 값은 각각 단일 로컬 관측이며 파일 준비/로드·1만 서로 다른 자원·실제 데스크톱 응답시간·반복 표본의 보장은 포함하지 않는다.

소스 SHA-256: `1924edfb43ad0b562509629a6a2b4e775f0fb24c7b3c4e91518f39f9b4fca5bd`. [220개 소스·최종 검사/reader/CI/출력 식별값](source-manifest.json), [빌드 식별 헤더](BuildIdentity.h). `+dirty`는 기존 미커밋 구현과 이번 확장을 포함한다.

검증 예제: [native](periods.ppm), [자원 XLSX](periods.xlsx), [Usage XLSX](usage.xlsx), [자원 PDF](periods.pdf). 실제 업무 데이터가 아니다. 고정 공수/단위 곡선 역산·기간별 직접 공수/잔여 공수 입력·MSP/P6 곡선 교환과 외부 제품 실행/다른 OS/배포 검증은 이 증분의 완료 범위에 포함하지 않는다.
