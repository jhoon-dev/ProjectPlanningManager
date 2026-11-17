# 다일 실적 비례 배분 검증

2026-11-13 · [XPR-01~06 지원 계약](../../xml-multiday-proportional-actuals.md). 기본 일별 수입과 구별한 명시 구간 총량/추정 확인, 두 분 가중 기준, 정수 총량·기존 보고/누계 보존, 원자 채택과 척도별 추정 기원을 검증한다. 새 native 구조 없이 기존 schema11·지원 상한51을 유지한다.

## 독립 계산 표본

원본 A는 금요일11시~월요일14시의 공수101u·원가5001센트, B는 일요일 자정~화요일 자정의 공수3u·원가1센트다. 서로 다른 Unit 값도 확인한 전체 총량을 한 번만 사용한다. 현재 작업/자원 달력은 점심 휴게·주말과 A의 금요일 중단을 포함한다. native 배정률250%/75%, 단가99999·용량 예외0은 가중치에 사용하지 않는다.

| 기준/구간 | 날짜별 가중분 | 날짜별 공수 | 날짜별 원가(센트) |
|---|---|---|---|
| 공통 근무분 A | 금180·토0·일0·월180 | 금51·월50 | 금2501·월2500 |
| 공통 근무분 B | 일0·월420 | 월3 | 월1 |
| 경과분 A | 금780·토1440·일1440·월840 | 18·32·32·19 | 867·1600·1600·934 |
| 경과분 B | 일1440·월1440 | 2·1 | 1·0 |

근무분 결과에서 기존 토요일 실측222u/700센트는 유지한다. 현재 총 공수326u, 전기간 배분5702센트이며 누계25000센트를 배분에 다시 더하지 않는다. A는 누계15000=배분5701+미배분9299, B는10000=1+9999다. 기존 기간4건/누계2건은 모두 남기고 기간3건/누계3건을 한 revision으로 추가한다. 미리 기록된 미래 보고도 보존한다. 금요일 공수만80u로 수동 정정하면 원가2501센트의 추정 기원은 원래 배분 보고를 가리킨다.

Python `Fraction`과 민간 시계 분 열거는 C++ 계산기를 사용하지 않고 두 기준·16개 수량 사례의 최대 나머지 배분을 재계산한다. XML·native 원장과 기존 계획/미래/주말 값·누계 대사·표준 XML 영수증과 부분 정정 기원, XLSX의 추정 근거3행/전체 이력7행과 PDF를 독립 확인한다.

## 경계와 화면

`xml_proportional_contract`는 원본/미리보기/제출 변조, 확인 누락·잘못된 연결·중복/다일과 일별 겹침·상태일 잘라내기·미래일·전체 가중분0·정밀도/음수/수량 한도·USD/KRW 통화/wire 척도·휴일/중단·마감 기간, 같은 출처 재시도와 기준/달력 변경, 수동 정정·ID 이관·취소/변경·schema11 저장/복구를 확인한다. 선택 검토 기록의 원본 native/XML·재대조·기존 폴더 거절·취소/실패 후 미게시도 검사한다.

`xml_proportional_ui`는 기존 일별 화면 진입, 미선택/미연결/미확인 초기값, 두 기준 선택, 일별 가중분/정수량·기존/반영/누계 대사, 편집 시 미리보기 무효화·재생, 새 검토 기록 게시, 채택/취소/오래된 문서/읽기 전용과 작은 화면을 확인한다. Windows 폴더 게시 전에 native 파일 읽기 핸들을 닫고, 모달 종료 전에 참조를 캡처한 화면 연결을 해제한다.

최종 회귀 **188/188·296.93초 + 대용량 교환 단독1/1·160.69초**, 합계 **189/189**, 실패/건너뜀0이다. 집중 **14/14**, 파서 **838개**, C++ 형식 **503파일** 차이0을 통과했다. 독립 두 기준/16사례·총 공수326u/배분5702센트·척도별 정정 기원·XLSX3행/이력7행·PDF를 확인했다. [전체 빌드](build.txt), [188개 CTest](ctest.txt), [JUnit](ctest.xml), [단독 교환](isolated.txt), [단독 JUnit](isolated.xml), [집중](focused.txt), [형식](quality.json), [독립 계산/보고](independent.json), [결과/해시](verification-summary.json).

Windows 실행 패키지 **291파일**의 PPM/CLI 해시가 최종 빌드와 같다. Qt/MinGW가 없는 PATH에서 CLI가 schema11 native 파일을 읽고 종료0을 반환했다. [패키지](runtime.json), [배포](runtime-deploy.txt), [읽기](runtime-inspect.json).

[원본 XML](original.xml), [기존 문서](before.json), [근무분 배분](working.json), [경과분 배분](elapsed.json), [16사례](cases.json), [채택](merged.json), [부분 수동 정정](corrected.json), [native](source.ppm), [표준 실적 XML](project.xml), [추정 영수증](conversion.json), [정정 후 기원](corrected-conversion.json), [감사 XLSX](audit.xlsx), [PDF](audit.pdf), [PDF 근거 화면](audit-allocation-page.png), [800×850 검토](allocation-small.png), [800×850 반영값](allocation-values-small.png).

## 재실행

```powershell
cmake --build build-governance --parallel 10
ctest --test-dir build-governance -E '^exchange_async_recovery_diagnostics$' --parallel 4 --output-on-failure
ctest --test-dir build-governance -R '^exchange_async_recovery_diagnostics$' --output-on-failure
ctest --test-dir build-governance -R '^xml_(proportional|work_progress|actual_export|period_fragment|period_actual|progress_merge|progress_adoption)_(contract|ui)$' --parallel 4 --output-on-failure
.deps/report-verification/Scripts/python.exe scripts/verify-xml-proportional.py build-governance/xml-proportional-qa
```

기존 10,000작업 교환 검사는 네 작업 동시 실행에서180초 제한에 걸린 이력이 있어 최종 회귀는 나머지188개와 해당1개 단독으로 나누어 검사한다. 건너뛰지 않는다. 실제 MS Project/P6의 열기·재계산·재저장과 일반 MSPDI 단위당 반복의 자동 해석은 이 검증의 결과가 아니다.
