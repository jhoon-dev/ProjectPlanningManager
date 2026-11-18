# native 기준선 상세 시간분포 검증

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/native-baseline-timephased-2026-11-15)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

2026-11-15 · Windows · Qt6.8.3 / MinGW13.1 / Release · 부모 위치 자료를 명시 보존할 때 조건부 schema77.

[NBT-01~06](../../native-baseline-timephased.md)을 구현했다. 일반/표준 실적 XML에서 동결 실행 공수 구간과 native 일별 원가를 작업·프로젝트 UID0·WBS·자원·배정의 표준 부모 위치에 출력한다. 재채택·기존 문서 병합에서도 해당 위치와 원본 값을 보존한다.

## 실행 결과

- 최종 집중 회귀 **48/48 통과·109.66초**. 상세 시간분포와 기존 기준선 교환/병합/번호/원가·요약·실제/잔여·진척·식/전파, 통화·ID·XER 일정 채택·파일/undo/복구·prepared edit·평준화 계약/UI를 포함한다. [로그](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/native-baseline-timephased-2026-11-15/final-tests.log).
- 별도 native 원가·일 경계·Usage **3/3 통과·1.64초**. 위48개와 중복되지 않는 테스트다. [로그](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/native-baseline-timephased-2026-11-15/cost-usage-tests.log).
- **988 parser seeds·schema1~77** 통과. 부모 위치 자료의 정상·형식/범위/음수 오류와 잘린 입력을 포함하고 schema76 envelope의 새 자료 거절도 직접 검사했다. [상세 로그](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/native-baseline-timephased-2026-11-15/LastTest.log).
- Baseline0~10의 전체 Type 대응과 v12 생략, 잘못된 부모 UID·내부/부모 위치 중복·같은 종류의 겹침 거절, 다른 번호의 선택 가능성을 확인했다.
- 곡선·중단·달력 교집합·부분 공수·유휴0, 배정별/일별 단가 반올림, 균등/완료/시작 고정비, 마일스톤307·미배정 자재/경비244를 포함한다. 최초 기준선 작업 A의 공수5.64분/원가289, 프로젝트10.65분/1294를 기존 독립 기대값과 대조했다.
- 현재 날짜·휴일·단가·배정률 변경이 동결 분포를 바꾸지 않으며, 삭제된 작업의 프로젝트 기여 보존과 미동결 구성원이 있는 WBS의 상세 생략을 확인했다. 외부 채택 번호는 native 분포로 보충하지 않는다.
- 미확정 원가는 Value를 생략한 채 XML 재채택하고, 알려진0과 구별한다. USD/JPY/KRW의 전송 단위와 상세 합계, 일반/실적 출력의 기준선 동등성, schema77 저장·재출력·기존 문서 병합 이력과 schema76 거절을 확인했다.
- 취소가 입력 변경 없이 전파되며100,000개 한도에서 잘라 출력하지 않고 실패한다. 기존 공개 내보내기의 원본 재검증·새 폴더 게시 흐름을 사용한다.
- Release 앱을 빌드하고 변경 C++15파일 clang-format과 독립 Python 검사기 구문 검사를 통과했다. [빌드 로그](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/native-baseline-timephased-2026-11-15/final-build.log).

## 독립 판독

[검사기](../../../scripts/verify-native-baseline-timephased.py)는 writer와 별도로 XML/Decimal을 읽고 native 동결 실행 배정/일별 원가 자료로 예상 분포를 구성한다. **26개 기준선 행·98개 시간분포·7,273개 civil 분 지점·명시 유휴0 블록9개**를 대조했다. 공수는 분 지점의 정수 밀도와 전체 적분, 원가는 일별 배정/직접비 합계와 스칼라 총량을 검사한다. 원본/명시 채택/재내보내기와 일반/실적 출력의 일치도 확인했다. [결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/native-baseline-timephased-2026-11-15/independent.json), [자료](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/native-baseline-timephased-2026-11-15/artifacts).

검증 자료의 `execution.json`은 **기존 native 계산**에서 추출한 동결 occupancy/day-charge 결과다. Python 검사는 이 자료를 입력으로 XML 투영을 독립 검산하며, underlying 일정/원가 알고리즘 전체의 독립 재구현을 주장하지 않는다. 기존 기준선/원가의 독립 유리수·달력 검사도 아래처럼 다시 실행했다.

MPXJ16.10.0 strict reader는 **원래 `project.xml` 그대로** 읽어 오류0건과 배정6건(3배정×2번호)의 raw baseline 공수/원가 합계를 확인했다. 이번 자료에는 소수 초가 없어 이전 증분의 판독기 전용 날짜 절삭 사본은 사용하지 않았다. Task/Resource의 원시 시간분포는 XML/Decimal 검사 범위이며 MPXJ 배정 검사와 구분한다.

기존 독립 검사7개도 통과했다. 기준선 병합7행/연결5개·외부 교환16원본/6채택/26native 행, 기준선 XML77/XER32·보고77칸/PDF20쪽, 잔여 XML64/XER52·보고64칸/PDF21쪽, 실제 XML50/XER30·보고50칸/PDF23쪽, 원가128사례/1408값·XML/XER32·보고45행/PDF24쪽, 요약 기간129사례다. 세부 수치는 위 결과 JSON에 남긴다.

## 범위와 재현

원가의 상세 해상도는 native 일별 반올림 모델이다. 분 단위 원가를 추정하지 않는다. 실제 MS Project/P6 실행 환경은 없어 제품 왕복은 미실행이며, 전체 CTest·macOS/스토어 실행을 완료로 표시하지 않는다. 새 화면은 없으며 기존 두 XML 내보내기와 검토/병합 화면을 사용한다.

처음 빌드에서 새 시간분포 함수 선언 헤더가 추가되기 전에 시작된 `ProjectXml.cpp` 컴파일이 실패했다. 헤더가 포함된 소스로 재빌드한 뒤 새 집중6/6, 최종48/48 및 별도3/3을 통과했다. 아래는 Qt/MinGW/CMake가 PATH에 설정된 현재 체크아웃 빌드의 재현 명령이다.

```powershell
ctest --test-dir build-numbered-summary -R '^(xml_(baseline|summary_duration|remaining|quantity|actual_|early_late|formula_|roll_up|roll_down|progress_|work_progress)|project_currency_conversion_contract$|ledger_identity_migration_contract$|xer_schedule_adoption_contract$|undo_contract$|recovery_|tracking_file$|file_lifecycle$|parser_|prepared_edit|leveling_selection)' --output-on-failure -j 3
ctest --test-dir build-numbered-summary -R '^(timephased_usage_contract|cost_contract_file|cost_day_boundaries)$' --output-on-failure -j 3
build/validation-python/Scripts/python.exe scripts/verify-native-baseline-timephased.py docs/validation/native-baseline-timephased-2026-11-15/artifacts --reader
```

`--reader`에는 Java와 MPXJ/JPype가 필요하다. XML/Decimal 검사는 이 옵션 없이 실행할 수 있다.
