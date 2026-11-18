# 프로젝트·자원 필드 로컬 검증

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/object-fields-2026-11-05)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

2026-11-05 / Windows / Qt6.8.3 / MinGW13.1 / Release. [계약](../../object-custom-fields.md).

기존 작업 폴더의 미커밋 구현과 이전 결정 원장 증분을 보존한 상태에서 검사했다. 원시 로그와 소스 식별값을 이 폴더에 보존한다.

```powershell
cmake -S . -B build-decisions -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=.deps/Qt/6.8.3/mingw_64
cmake --build build-decisions --parallel 4
ctest --test-dir build-decisions --output-on-failure
python scripts/verify-object-fields.py build-decisions
```

Qt/MinGW와 CMake/Ninja 경로를 PATH에 추가하며, Python 독립 판독에는 기존 optional openpyxl/pypdf 환경을 사용한다. 애플리케이션 자체는 Python/Office 설치를 요구하지 않는다.

새 회귀는 같은 키의 다른 범위/형식, 네 형식/소수 정밀도/빈값·0·미설정, 옵션 이름 변경·삭제 차단, 자원 일괄 실패의 원자성·undo/dirty, 기준선/시나리오 동결·구 엔진 검산·schema16/구버전·사본/복구, 평준화 무효화 원장, worker 취소/session/닫기·읽기 전용과 실제 메뉴를 검사한다. 독립 reader는 native checksum과 값/동결 입력, 프로젝트/자원 XLSX/PDF, WBS 범위·미배정 자원 포함 여부·수치 셀·수식 없음과 기존 일정/계획 원가90을 확인한다.

## 결과

- **전체 CTest 97/97 통과**, 328.60초. [전체 로그](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/object-fields-2026-11-05/ctest.log), [테스트별 원시 로그](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/object-fields-2026-11-05/ctest-details.log), [결과 JSON](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/object-fields-2026-11-05/results.json).
- 파서 **132 seeds**, clang-format18.1.8의 **168파일** 검사 통과. 새 schema16·잘못된 범위와 구 schema 거절을 포함한다. 새 데이터가 없는 writer와 기존 기준선 엔진 재현도 통과했다.
- [독립 판독 8종](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/object-fields-2026-11-05/independent-readers.json) 통과: 새 객체 필드, 기존 작업 분류, 평준화 결정 원장, 진척+평준화, 채택 출력, 자원 여유, 재분할 여유, 교차 실행 창 출력. 새 검사에서 프로젝트4행·자원8행·WBS 자원4행, 소수6자리·빈값/미설정/0·수식 없음·계획 원가90·PDF12페이지를 확인했다.
- [프로젝트 다중행 텍스트](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/object-fields-2026-11-05/project-fields.png), [자원 일괄 입력](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/object-fields-2026-11-05/resource-fields.png), [800×600 자원 화면](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/object-fields-2026-11-05/resource-fields-small.png)을 직접 확인했다. 값·단위·선택·입력·닫기/적용 버튼이 보이고 작은 화면에 겹치거나 잘리지 않는다. 실제 Windows 데스크톱 조작은 아니며 오프스크린 검사다.
- [1만 자원 관측](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/object-fields-2026-11-05/scale.json): 로드된 문서에서 모달 최초 콜백/모델 확인6ms, 전체 선택 이후 적용 완료69ms를 한 번 관측했다. 10,000개 값을 모두 확인하고 undo로 원래 값과 저장 상태를 복원했다. 파일 준비/로드·수동 자원 등록·최초 paint 전체·일정 계산·다른 OS·반복 표본/응답시간 보장은 포함하지 않는다.

소스 SHA-256: `d4defd12af918b5bd39da035423cfc7521ecdc851c0a2ee201b7b24a9b3a9aaf`. [소스/최종 테스트/reader/CI 식별값](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/object-fields-2026-11-05/source-manifest.json), [빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/object-fields-2026-11-05/build.log), [최종 증분 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/object-fields-2026-11-05/build-final.log)를 보존한다. `+dirty`는 기존 미커밋 구현과 이번 확장을 포함한다.

독립 판독 원본: [native](../../../tests/fixtures/native-history/object-fields-2026-11-05/objects.ppm), [전체 XLSX](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/object-fields-2026-11-05/objects.xlsx), [WBS XLSX](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/object-fields-2026-11-05/wbs.xlsx), [PDF](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/object-fields-2026-11-05/objects.pdf). 실제 업무 데이터가 아닌 고정 검증 예제다.

실제 MSP/P6 교환, 다른 OS/배포, 실제 데스크톱 조작·파일 신원 인증·sanitizer/coverage 퍼징은 이번 로컬 검사로 완료 처리하지 않는다.
