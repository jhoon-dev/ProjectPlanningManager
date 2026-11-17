# 자재 제약 여유·임계 검증

2026-11-17 Windows / Qt6.8.3 MinGW Release · `build-numbered-summary`. [MF-01~07 계약](../../material-float.md).

- **관련 회귀74/74, 231.01초**. 직전 자재/잔여/공유61개 영향 범위에 새 자재 여유2개와 기존 곡선·재분할·전파·증인/여유 화면·일정 인쇄 검사를 추가했다. 전체 suite 실행을 의미하지 않는다. 앱 `PPM.exe`, `PPM-Automation.exe`를 갱신했다. `material-float-regression.log`, `regression-details.log`, `material-float-final-build.log`.
- 출력 확인 중 통합 재배치 진단의 내부 작업/시작 기준 ID 노출을 제거하고, 유효한 자재 대기/여유 근거에 입력 수정 권고가 붙지 않도록 보완했다. 이후 **표시 영향 검사16/16, 32.23초**를 통과했다. 이 보완은 계산값을 바꾸지 않는다. `material-float-display-build.log`, `material-float-display-tests.log`, `display-details.log`.
- 파서 **1,110개 시드**, native1~84를 통과했다. 새 자재 승인 기준선84와 기존 엔진 기준선82의 정확한 저장/재열기·체크섬, 구 envelope83의 새 엔진 거절·미래85/알 수 없는 엔진 거절을 확인했다. 기존 기간별 배정/고정 공수 승인 엔진도 당시 자재 여유 미산정 결과로 재현한다. 새 엔진 기준선이 없는 일반 자재 문서는81/82/83 최소 writer를 유지한다.
- 기본 FS0/PDM/제약 PDM/공수 네 프로필에서 입고09:02, A/B 각2분, 완료 기준10분의 날짜를 유지한다. A 총여유4/자유여유0, B 총/자유여유4, 기준 작업 임계0분을 확인했다. 자원 배정 없는 자재 작업·세 소모 방식·작업 결과 순서 변경·정확한 부분/전체 재고 인증과 최대 배치의 자원 인증도 검사했다.
- **비단조 반례:** 공수 곡선/자원 달력에서 시작09:00이면 완료09:07, 시작09:01이면 완료09:05가 된다. 고정 SS 이정표가 허용하는 자원만의 총여유3분은09:07 입고 전에 소모하는 후보를 거절한 뒤 자재 총여유0/임계로 바뀐다. 자원만 유효한 잘못된 후보를 재고 인증이 거절함을 확인했다.
- 공유 계획의 자동 대기 문서 P와 수동 문서 Q를 혼합해 날짜를 그대로 두고 각 A/B의 총여유2분과 Q/B 자유여유2분을 확인했다. 프로젝트별 재고를 구분하며 원본을 변경하지 않는다. 자재10,002개가 여유용 통합 입력 한도를 넘으면 유효한 날짜/완료를 유지하고 여유·임계만 미산정 처리한다. 협력 취소를 전달한다.
- 실제 자재 잔여 일정은 네 프로필에서 확인 재고/미입고/명시 잔여 소모로 여유를 계산하고 실제 시작을 유지한다. 공유 실제 원장 예제는 총/자유여유0인 네 임계 작업을 표시한다. 기존 정정/재확인·날짜별 잔여 공수·곡선/가용량·교차 관계·완료 실적·부족/미정·stale 무효화 계약도 회귀 범위에 포함한다.
- 독립 Python이 **27사례/84작업, 가능한 지연 조합1,913개(유효616개)**를 열거해 총여유·자유여유·임계 **252값**을 대조했다. 제약/관계·자원 순서/용량·입고와 정확한 milli 누적을 별도 구현하고 비단조 공수를 직접 적분했다. 공유 JSON/XLSX6행의 여유/임계와 PDF4쪽의 정책을 판독했다. `independent.json`, `cases.json`, `shared.json/xlsx/pdf`.
- 기존 독립 검사도 갱신 산출물에서 통과했다. 공유 실제 원장24사례/96작업은 날짜288개·재고288개/후보9,559개, 계획 자재24사례/48작업은 날짜96개/후보5,443개, 단일 실제 자재24사례/48작업은 날짜144개/후보4,746개를 대조했다. 잔여 재고0.5개·원장9행/동결5행, schema83 및 새 승인 계획84를 보존한다. `shared-remaining`, `arrival`, `actual`의 `independent-float.json`.
- `witness.png`에서 A/B 동반 재배치와 현재 날짜/실제 날짜 고정을, `shared-remaining/review.png`에서 총/자유여유·임계와 확인 재고를 확인했다. Poppler로 렌더링한 `float-page.png`에서 PDF 표의 값·정렬/행 구분을 확인했다. 내부 그래프 ID는 통합 PDF에 노출하지 않는다. C++ 형식·scoped whitespace·Python AST/문서 링크와 `sha256.json`에 최종 소스/앱/산출물을 기록했다.

집중 재실행:

```powershell
cmake --build build-numbered-summary --parallel 8 --target JOBS PPM-Automation jobs_material_float_tests jobs_shared_material_tests jobs_material_actual_tests jobs_material_delay_tests jobs_material_inventory_tests
ctest --test-dir build-numbered-summary --output-on-failure --parallel 2 -R '^(material_float_|shared_material_|material_actual_|material_delay_|material_inventory_)'
build/validation-python/Scripts/python.exe scripts/verify-material-float.py docs/validation/material-float-2026-11-17
build/validation-python/Scripts/python.exe scripts/verify-shared-materials.py docs/validation/material-float-2026-11-17/shared-remaining
```

현재 완료일/자원 반환 순서를 유지하는 여유다. 전역 자원 순서·구매/원가 최적화·프로젝트 간 자재 풀·여러 원본 날짜 일괄 채택과 실제 MS Project/P6 실행은 이 기록의 완료 범위가 아니다. 과거 검증 디렉터리는 수정하지 않았다.
