# 공유 통합 실제 자재 원장·잔여 재고 검증

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/shared-material-actuals-2026-11-17)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

2026-11-17 Windows/Qt6.8.3 MinGW Release · `build-numbered-summary`. [SMAT-01~07 사용법/계약](../../shared-material-actuals.md).

- **관련 회귀61/61,115.11초**. 새 공유 실제 자재·기존 단일 원장/계획 자재·공유/로컬 가용량·통합 일정/작업공간·날짜별 잔여 공수/곡선·진척 정책/채택·실적/원가·보고/교환·통화/ID 이관·undo/복구·파서의 영향 범위다. 전체257개 suite 실행을 의미하지 않는다. 앞 집중6/6,9.80초와 원시 로그도 보존한다. 앱 `PPM.exe`/`PPM-Automation.exe`를 갱신 빌드했다.
- 파서 **1,100개**, native1~83/workspace1~4를 통과했다. 새 영속 필드는 없다. 두 실제 원장 프로젝트는83으로 정확 저장/재열기하며, fixture 작업공간의 기존 최소 writer는2다. 기존 공유 가용기간4 등의 계약도 유지한다.
- 두 문서가 작업 A/B·자재 M·계획 입고/소모·진척·거래 ID까지 같은 자료를 사용했다. 실제 시작09:00을 보존하고 P 잔여11:00~12:00/후속13:00~14:00, Q 잔여14:00~15:00/후속15:00~16:00을 확인했다. 공유 가용량200%에서는 두 A를11:00에 병행한다. 공유 부하는 과부하/미정0이다.
- 프로젝트별 재고0개·총 계획 입고3개 중 실제 도착1개·미입고2개를 보존한다. Q의 입고를15:00으로 늦추면 P의 먼저 도착한 자재로 Q를 실행하지 않는다. P 기록 리비전5/Q3의 정정 사슬을 함께 연결해 P 재고0.5/미입고1.5개와 Q 재고0/미입고2개를 유지한다. 다음 상태일의 정정은 이전 공통 상태일에 적용하지 않는다. 프로젝트 시작 기준이 다른 자료도 통과한다.
- 프로젝트 간 FS 링크는 Q의 실제 시작을 바꾸지 않고 잔여 시작을 지연시킨다. 원본 진척 Override 정책을 선택하면 실제 끝점의 링크 처리도 유지한다. 기본 FS0/PDM/제약 PDM/공수, 공수 곡선·균등 소모, 날짜별 잔여 공수·공유 가용 시작11:30·완료 작업의 잔여0·자재 없는 문서 혼합을 확인했다.
- 날짜별 잔여 공수와 동일 작업의 다중 로컬 자원을 같은 풀로 합치는 모호한 부분 대체는 명시 거절한다. 미확인 Q의 오류는 원래 프로젝트 Q로 표시한다. 상태일 불일치·전체 공급 부족·지난 미입고 계획·전역 순환·협력 취소를 검사했다. 합계100,008행의 원장은 `WORKSPACE_LIMIT`로 거절하며 후보 날짜/완료/재고 근거를 부분 노출하지 않는다.
- UI에 별도 통합 잔여 시작과 프로젝트별 확인 재고·잔여 소모·미입고 잔량을 표시한다. 원장 정정 뒤 날짜/재고/내보내기가 무효화되고, 재확인 전 재계산은 확정하지 않으며, 재확인 후 Q 재고0.5로 다시 계산한다. 옵션 변경도 무효화한다. 계산은 원본 문서의 입력/원장/dirty 상태를 바꾸지 않는다. `review.png`.
- 독립 Python은 **근무분 후보9,559개**를 열거해24사례/96작업의 **실제 시작·잔여 시작·완료288개**, 프로젝트별 개시/입고/소모/확인 재고/잔여 소모/미입고 **288개**를 모두 대조했다. 세 우선순위·준비 작업/프로젝트 시작 기준 노드·FS/독립·교차 FS15분·100/200% 공유 공급·세 소모 방식·정정/재확인·다른 잔여 기간/입고 시각을 조합했다.
- 독립 판독은 두 native83 체크섬/원장·작업공간2, XLSX 재고 표의 P/Q 각각 `2+1−3=0`, 잔여2/미입고2와 PDF4쪽의 새 표/정책을 확인했다. JSON `materialStocks`의 milli 문자열을 정수로 대조하고, C++ 계약에서2^53+1인 `9007199254740993`도 정확히 출력됨을 검사했다. `independent.json`.
- PDF2쪽을 Poppler로 렌더링해 `balance-page.png`의 두 프로젝트 재고와 정책/출처를 확인했다. 긴 ID/시각의 전체 값을 tooltip으로 확인하도록 보완한 뒤 앱을 다시 빌드하고 UI1/1,2.81초를 추가 통과했다. C++6파일 형식·scoped whitespace·Python AST·새 문서 링크를 검사했다. `sha256.json`에 소스/산출물 해시를 보존한다.

집중 재실행:

```powershell
cmake --build build-numbered-summary --parallel 8 --target JOBS jobs_shared_material_tests jobs_material_actual_tests jobs_integrated_workspace_tests jobs_resource_workspace_ui_tests
ctest --test-dir build-numbered-summary --output-on-failure --parallel 2 -R '^(shared_material_|material_actual_|integrated_workspace_contract$|resource_workspace_ui$)'
build/validation-python/Scripts/python.exe scripts/verify-shared-materials.py docs/validation/shared-material-actuals-2026-11-17
```

전체61개 영향 범위는 [직전 MAT의59개 빌드/재실행 대상](../actual-materials-2026-11-17/README.md)에 `jobs_shared_material_tests`를 빌드하고 `shared_material_contract/ui`를 추가한 부분집합이다. 초기/최종 빌드·집중/회귀·독립 판독과 소스/산출물 해시를 함께 보존한다.

통합 결과는 읽기 전용이다. 원본 파일 일괄 날짜 채택·프로젝트 간 자재 재고 풀·자재 제약을 포함한 여유/임계·구매/비용/전역 배치 최적화와 외부 제품 실행은 이번 범위가 아니다.
