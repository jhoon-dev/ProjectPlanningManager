# XML 잔여 기간·공수 참조 검증

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-remaining-references-2026-11-12)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

2026-11-12 · XMLR-01~05 · Windows / Qt6.8.3 MinGW13.1 Release.

[지원 계약](../../xml-remaining-references.md), [C++ 검사](../../../tests/XmlRemainingTests.cpp), [fixture](../../../tests/XmlRemainingFixture.h), [독립 검사기](../../../scripts/verify-xml-remaining-references.py).

## 결과

최종 전체 **171/171 · 280.92초**, 실패/건너뜀0이다. 결정적 파서 시드 **774개(schema1~49)**, C++ 형식 **463파일** 차이0을 통과했다. [빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-remaining-references-2026-11-12/build.txt), [최종 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-remaining-references-2026-11-12/final-build.txt), [검사 빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-remaining-references-2026-11-12/test-build.txt), [전체 CTest](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-remaining-references-2026-11-12/ctest.txt), [JUnit](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-remaining-references-2026-11-12/ctest.xml), [형식](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-remaining-references-2026-11-12/quality.json).

독립 Python은 native 진척 원장을 상태일/추가 순서로 선택하고, 예측 날짜 창을 civil minute별 작업/자원 달력·중단·배정 곡선/잔여 끝부분과 교차해 적분한다. 고정 공수의 마지막 부분 단위와 FixedUnits 공수 기준도 대조한다. XML **64칸(미설정3칸)**·수식11개·XER 숫자52값·보고서64칸, XLSX27시트와 PDF22페이지를 확인했다. MPXJ strict reader는 오류0·작업9행·배정6개다. [보고서 대조](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-remaining-references-2026-11-12/independent-reports.json), [리더 대조](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-remaining-references-2026-11-12/independent-reader.json).

검증 값은 작업별 잔여 기간0/4/3/8/3/0분과 공수0/5.32/1.50/5.01/3/0분, 프로젝트14.83분·자원10.51/4.32/0분이다. 고정 공수의 보고 잔여 기간99를 사용하지 않고 실제 예측 기간8분을 얻는다. 날짜 고정 목표201을 다음 날 전체 가용 구간에 분배하면 잔여 공수5.01분은 보존되고 기간은13분이 된다. 보고 실제 공수가 계획보다 커도 잔여를 차감하지 않는다.

Windows800×730에서 한글·수식/참조 삽입·worker 미리보기·예측 출처와 완료0, 소수 공수, 상태일 제거/undo 후 재계산을 검사한다. [화면](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-remaining-references-2026-11-12/remaining-small.png). [XML](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-remaining-references-2026-11-12/remaining.xml), [XER](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-remaining-references-2026-11-12/remaining.xer), [입력](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-remaining-references-2026-11-12/native.json), [예측 날짜](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-remaining-references-2026-11-12/forecast.json), [매핑/진단](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-remaining-references-2026-11-12/mapping.json), [원본](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-remaining-references-2026-11-12/source.ppm), [XLSX](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-remaining-references-2026-11-12/fields.xlsx), [자원 XLSX](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-remaining-references-2026-11-12/resources.xlsx), [PDF](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-remaining-references-2026-11-12/fields.pdf).

실행 패키지291파일의 PPM/CLI 해시가 최종 빌드와 일치한다. Qt·MinGW 경로 없는 PATH에서 CLI가 schema49 원본을 읽고 종료0을 반환했다. [배포/파일 해시](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-remaining-references-2026-11-12/runtime.json), [배포 로그](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-remaining-references-2026-11-12/runtime-deploy.txt), [CLI 읽기](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-remaining-references-2026-11-12/runtime-inspect.json).

[verification-summary.json](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-remaining-references-2026-11-12/verification-summary.json)에 최종 검사/독립 판독 결과와 관련 소스·자료 SHA-256을 기록한다.

## 범위와 수정

완료·미보고·이정표, 세 작업 유형·작업/자원 달력·수동 중단·배정률 곡선의 잔여 끝부분·날짜 고정 잔여 목표, 네 범위와 명시 소유 참조를 검사한다. WBS/필터·빈 그룹·미사용 자원과 엄격한 미설정 전파, 요약 기간 미지원을 확인한다. 선택 진척/잔여 평준화 경로도 같은 계산 값을 사용한다.

없음/불완전/취소와 충돌/stale/다른 입력·중복/누락 행·상태일 이전 잔여 시작을 구별한다. 제공된 오래된 계획 결과를 새 잔여 계산으로 묵시 대체하지 않는다. XML 계획 날짜와 현재 잔여 예측을 분리하며, 미보고 작업의 계획 시작09:00과 예측 시작09:02가 다르면서 정적 잔여 값은 현재 예측을 사용하는지 독립 확인한다. schema49/구버전 거절·이전 최소 writer, 동결/시나리오·통화/ID 전후 사본과 복구를 검사한다.

교환 검사에서 기존의 **수동 중단+계획 곡선** 조합이 내보내기 자기 판독의 시간대 창 한도를 초과하는 문제를 발견했다. XML에는 native 중단을 작업 분할로 쓰지 않으므로, 해당 곡선의 기간은 중단을 생략한 달력의 전체 날짜 창으로 투영한다. 원본 기간8분·중단을 native에 보존하고 XML 기간9분과 배정 계획 공수4.00/2.64분의 보존을 독립 대조한다. 실제 작업 분할 왕복을 지원한다는 의미는 아니다.

## 재현과 경계

Qt/MinGW/CMake를 PATH에 준비한다.

```powershell
cmake --build build-governance --parallel 12
ctest --test-dir build-governance --output-on-failure --parallel 4
.deps/report-verification/Scripts/python.exe scripts/verify-xml-remaining-references.py build-governance/xml-remaining-qa --reports
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-xml-remaining-references.py build-governance/xml-remaining-qa --reader
```

상태일/유효 예측이 없으면 Remaining은 Null이다. 배정 없는 작업의 공수0은 미배정 계획 작업량을 추정한 값이 아니다. 요약 잔여 기간·Baseline/Early/Late는 후속이다. XML 진척 원장 자동 채택과 실제 MS Project/P6 재계산·재저장, macOS/스토어 실행은 미검증이다. 이전 증분의 검증 자료는 변경하지 않는다.
