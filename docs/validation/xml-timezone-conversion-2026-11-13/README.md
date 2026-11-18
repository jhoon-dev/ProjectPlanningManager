# XML 계산식 시간대 변환 검증

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-timezone-conversion-2026-11-13)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

2026-11-13 · [XTZ-01~06 지원 계약](../../xml-formula-timezones.md). PPM 확장 네 함수의 명시 고정 오프셋/버전 포함 IANA2026f, 누락/중복과 정밀도·네 범위·schema53·기존 문서·교환/보고를 검증한다. 일반 XML 일정/날짜 필드의 자동 오프셋 해석이나 실제 Microsoft Project/P6 재계산 동등성을 검증한 결과는 아니다.

## 자료와 독립 계산

IANA2026f 원본과 Python tzdata2026.6 패키지의 SHA-256을 확인해 전환 자료를 생성했다. 597개 이름/별칭·306개 전환 패턴·30,397개 경계를 내장한다. 초기 설정용 Factory는 제외하며 UTC 범위는1970-01-01 이상2101-01-01 미만이다. 고정 UTC/±HH:MM은 native1~9999년에서 결과가 표현되는 범위를 지원한다.

C++ 결과 **92,079행**은 모든 패턴의 경계 직전1µs/정각/직후1µs와 모든 이름의 두 표본을 포함한다. 독립 CPython ZoneInfo는 시스템 TZPATH를 쓰지 않고 동반 TZif 바이너리를 직접 읽어 오프셋 초와 현지 일시를 대조한다. 저장된 경계만 검사해 누락된 전환을 놓치지 않도록597개 이름 전체의1970~2100년 매월 UTC 표본 **938,484개**도 전환표와 독립 대조한다. 원본으로부터 별도 폴더에 생성한 표/ZIP·라이선스/manifest가 바이트 단위로 같다.

표본 A는 서울2026-11-13의09:30:12.123456을 UTC00:30:12.123456·전날 뉴욕19:30:12.123456-05:00으로 변환한다. B는 명시+05:45를 UTC03:45:12.123456·서울12:45:12.123456+09:00으로 변환한다. 오프셋은32400/20700초이고 문자열 UTC 왕복 차이는0이다. 원래 UTC 스냅샷1999년·없는 시각 스냅샷2000년은 native에 그대로 남지만 활성 결과/오류를 대체하지 않는다.

## 경계와 연결

`xml_timezone_contract`는 뉴욕의 누락/중복, Lord Howe의30분 전환, Dublin의 음수 DST, Apia의 건너뛴 하루와 Monrovia의 역사적 초 단위 오프셋을 확인한다. UTC에서 중복 시각으로 가는 두 Text 결과는 -04:00/-05:00을 구분하고 원래 UTC로 정확히 돌아온다. 잘못된 버전/이름·비명시 시간대·지원 UTC 범위 밖·±14:00/알 수 없는-00:00·윤초/정밀도·형식/인수/Null과 취소를 검사한다. 호스트 TZ를 바꿔도 결과가 같다.

네 범위와 Date/Start/Finish·Text·Number·Flag 결과, native 숫자 식·요약 식/roll-down·이름 변경 뒤 바인딩을 확인한다. 활성53/이전52 거절·추가 함수52 유지·비활성 레거시44와 호출 오인 방지, 기준선/시나리오/보관 사본만 남은 경우·통화/ID 이관·저장/복구·읽기 전용을 검사한다. 공유 버전 상한으로 파일/전체 사본/복구·파서·미래 버전 거절을 연결한다.

독립 검증기는 XML36칸/정의15개(자동 roll-down 메타데이터 수신2칸 포함), XER 정적34값, XLSX 작업 필드32행과 PDF16쪽의 값/정의·오프셋 문자열을 확인한다. 실제/계획 날짜에 자동 보정이나 원장 추가가 없다.

`xml_timezone_ui`는 네 함수 선택·출발/대상/중복 정책·ParseUtc의 전용 인수, 예시 삽입·중복 시각의 늦은 UTC06:30:00.123456Z 미리보기와 저장/undo·취소/문서 변경을 확인한다.800×850의 편집/하단 결과를 각각 보존한다.

최종 일반 회귀 **192/192·267.51초**, 집중 **24/24**를 통과했다. 대용량 단독 **1/1·139.03초**까지 합계 **193/193**, 실패/건너뜀0이다. 파서 **862개**(schema1~53), C++ 형식 **510파일** 차이0, 독립 전환92079/월별938484·XML36칸/정의15개·XER34값·XLSX32행/PDF16쪽을 확인했다. [빌드](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-timezone-conversion-2026-11-13/build.txt), [192개 CTest](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-timezone-conversion-2026-11-13/ctest.txt), [JUnit](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-timezone-conversion-2026-11-13/ctest.xml), [단독](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-timezone-conversion-2026-11-13/isolated.txt), [단독 JUnit](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-timezone-conversion-2026-11-13/isolated.xml), [집중](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-timezone-conversion-2026-11-13/focused.txt), [형식](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-timezone-conversion-2026-11-13/quality.json), [독립 검증](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-timezone-conversion-2026-11-13/independent.json), [자료 재생](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-timezone-conversion-2026-11-13/regenerated.json), [결과/해시](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-timezone-conversion-2026-11-13/verification-summary.json).

Windows 실행 패키지 **295파일**의 PPM/CLI 해시가 최종 빌드와 같다. Qt/MinGW가 없는 PATH에서 CLI가 schema53 파일을 읽고 종료0을 반환했다. 내장 데이터의 자료 manifest와 라이선스4파일이 licenses/timezones에 같은 해시로 포함된다. [패키지](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-timezone-conversion-2026-11-13/runtime.json), [배포](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-timezone-conversion-2026-11-13/runtime-deploy.txt), [읽기](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-timezone-conversion-2026-11-13/runtime-inspect.json).

[native 입력](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-timezone-conversion-2026-11-13/native.json), [동결/시나리오/환산 사본](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-timezone-conversion-2026-11-13/archived.json), [XML](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-timezone-conversion-2026-11-13/timezone.xml), [XER](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-timezone-conversion-2026-11-13/timezone.xer), [매핑/진단](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-timezone-conversion-2026-11-13/mapping.json), [시각 사례](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-timezone-conversion-2026-11-13/cases.json), [전환92079행](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-timezone-conversion-2026-11-13/transitions.tsv), [native 파일](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-timezone-conversion-2026-11-13/source.ppm), [XLSX](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-timezone-conversion-2026-11-13/fields.xlsx), [PDF](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-timezone-conversion-2026-11-13/fields.pdf), [PDF 화면](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-timezone-conversion-2026-11-13/timezone-report-page.png), [800×850 편집](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-timezone-conversion-2026-11-13/timezone-small.png), [800×850 결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/xml-timezone-conversion-2026-11-13/timezone-values-small.png).

## 재실행

```powershell
cmake --build build-governance --parallel 10
ctest --test-dir build-governance -E '^exchange_async_recovery_diagnostics$' --parallel 4 --output-on-failure
ctest --test-dir build-governance -R '^exchange_async_recovery_diagnostics$' --output-on-failure
ctest --test-dir build-governance -R '^xml_(timezone|additional_functions|formula|calendar|early_late|baseline|remaining|quantity|actual|schedule|roll_up|roll_down)_(contract|ui)$' --parallel 4 --output-on-failure
.deps/report-verification/Scripts/python.exe scripts/verify-xml-timezones.py build-governance/xml-timezone-qa
```

기존10,000작업 교환 검사는180초 제한에 걸린 이력이 있어 일반192개와 해당1개 단독으로 나눠 검사한다. 표준 제한을 바꾸지 않고, 시간 초과가 생기면 같은 코드/실행 파일의300초 한도 직접 재검증과 구별해 기록한다. 외부 제품의 실제 재계산과 macOS 실행은 미실행이다.
