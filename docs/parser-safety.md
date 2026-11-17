# 외부 파일 파서 검증

2026-11-17: [자재 제약 여유·임계](material-float.md)의 새 승인 기준선 엔진을 조건부 native84로 구분한다. 구 envelope83의 새 엔진·미지원 미래85·알 수 없는 엔진과 결과 변조를 거절하고 과거 기준선을 재현한다. 아래 버전 상한은 과거 증분 기록이다.

2026-11-09 [MPP 수동 계획 곡선](mpp-manual-curves.md)은 고정 decoder의 정규 원본 자료만 허용하고 실제/초과근무·생성 자료를 구분한다. protocol2·정확 메타데이터/행 한도·총공수 대사·분 단위 순서와 기존 XML 검증을 적용한다. MPP 경로에서 writer가 생략한 검증된0값을 명시0으로 복원한다. XML 일반 입력의 누락 값을0으로 바꾸지 않는다. native 상한36은 유지한다.

2026-11-09 [자원 생산성·외부 고유 곡선 교환](resource-productivity-curves.md)은 조건부 schema36이다. 정확 필드·정수/범위·날짜/UTC·21값 합계/정밀도·형식/코드·정렬된 구간과 사본을 검증한다. XML 시간대별 계획/실제 분리와 XER 곡선 참조/총공수/lag를 검사한다. 현재 상한36, 미지원 미래37이며 기존1~35 최소 writer를 유지한다. [결정적 시드·전체 회귀](validation/resource-productivity-curves-2026-11-09/README.md)를 따른다.

2026-11-08 [FixedWork/FixedUnits 날짜 고정 잔여 목표](dated-effort-targets.md)의 새 조합/정책은 schema35다. 기존 날짜 입력의 정확한 필드·날짜/정수·배정/작업 참조·행 한도와 스냅샷 재실행을 유지하고, 구 envelope의 새 조합·미지원 미래36을 거절한다. 날짜/총공수·실행 가능성은 계산에서 충돌로 진단하며 stale 입력을 버리지 않는다. 현재 상한35이며 아래 상한은 과거 증분 기록이다. [최신 전체 회귀·결정적 시드 검증](validation/dated-effort-2026-11-08/README.md)을 따른다.

2026-11-08 조건부 schema34 [프로젝트 전체 통화 전환](project-currency-conversion.md)은 정확한 필드/양의 정수 환율·지원 통화/정책·기준일/작성 정보·전후 전체 문서 재실행과 즉시 현재 루트를 검사한다. 통화/ID 이관 스냅샷의 중첩 이력을 거절하고 이력16건·주요 행 합계300,000개·native64MiB를 제한한다. XER 원본은 보존하며 추가 통화 환산 단계를 순서대로 검증한다. 현재 상한34·미지원 미래35이며 아래 상한은 과거 증분 기록이다. [최신 전체 회귀·504개 결정적 시드 검증](validation/project-currency-2026-11-08/README.md)을 따른다.

2026-11-08 [MPP 읽기](mpp-import.md)는 OLE 형식·32MiB 원본과 별도 JVM의 heap512MiB·60초/최대120초·XML16MiB/metadata8KiB/stderr64KiB를 제한한다. 암호 보호를 우회하지 않고 parser 누락 오류·잘못된 형식/참조·기존 XML 한도를 거절한다. runtime/각 JAR의 지문과 목록 밖 classpath도 검사한다. binary MPP decoder는 별도 프로세스이며 native/XML/XER fuzz harness에 JVM을 직접 넣지 않는다.

2026-11-08 조건부 schema33 [관측 원장 ID 이관](ledger-id-migration.md)은 정확한 필드/형식·정수 revision·명시 일대일 연결과 전후 전체 문서 재실행 결과를 검사한다. 중첩 이력을 거절하고 이력16건·연결20,000개·주요 스냅샷 합계300,000행과 native64MiB를 제한한다. 현재 지원 상한33이며 미래34를 거절한다. 전체137개 회귀와478개 결정적 시드를 통과했다. [검증 기록](validation/ledger-id-migration-2026-11-08/README.md)을 따른다.

2026-11-08 조건부 schema32는 [새 XER 일정·WBS·달력 채택](xer-schedule-adoption.md)의 정확한 필드/형식·정수 revision·원본/전후 PlanInput·파일 지문·일대일 작업 연결·계층/달력 참조와 재실행 결과를 검사한다. 이력128건·3개 계획 합계300,000행·진단1MiB와 native64MiB 한도를 유지하고 구 envelope의 새 의미·미래33을 거절한다. 총458개 seed와 전체136개 회귀를 통과했다.

2026-11-08 조건부 schema31은 [XER 통화·자원 종류 전환](xer-source-conversion.md)의 정확한 키/정수 환산비·양수/상한·종류별 중복/참조·기준일/근거·정수 공수와 재실행 결과를 검사한다. 기존64MiB/128revision/300,000행 한도를 유지하고 각 집계의 종류 규칙은 최대2개다. 구 envelope의 전환 필드와 미래33를 거절한다. 새/이전 원본의 전환 규칙과 결과는 schema31 저장/복구와 총424개 파서 seed로 검증했다.

2026-11-08 조건부 schema30은 [사용자 기간/마감](custom-financial-periods.md)의 정확한 키/형식·전이/겹침·통화·기준선/작업·정수/nullable 금액·지식 revision과 마감 당시 코호트를 검사한다. 512이벤트·누적100,000작업 참조·native64MiB·보고1M셀을 제한하며 원장 revision 색인으로 과거 마감 잠금 준수도 검증한다. 구 envelope의 새 필드와 미래33을 거절한다.

2026-11-08 조건부 schema29는 [XER EV/PV native 채택](external-xer-evm-adoption.md)의 원본/revision·기준선·참조 작업/기간·명시 개시/합계·공유 경계·지표/확인·작성자/시각을 검증한다. 정확한 키/형식·원본 상태일/nullable native 상태일·128건·작업/기간 참조 합계100,000개와 기존64MiB 한도를 적용한다. 구 envelope의 새 필드와 미래33을 거절한다. schema1~29와 정상/손상 채택을 포함한348개 seed를 재생했다. 구/미래 envelope는 별도 파일 decoder에서 거절하고, 같은 정상 본문의 raw schema29 경로는 독립적으로 검사한다. 새 coverage-guided fuzz/sanitizer 실행은 이번 범위가 아니다.

2026-11-08 조건부 schema28은 [새 XER 버전·삭제 ID 재연결](xer-source-relink.md)의 후속 재무/ID 사전·파일 지문·프로젝트/nullable 상태일·새/이전 연결표·확인/누락 정책을 검사한다. 기준별20,000개 연결과 총300,000개 스냅샷 행 한도를 적용하며 종류·일대일 대상·관측값 누락과 당시 원장/연결표로 재구성한 결과를 대조한다. 구 envelope의 새 필드와 미래33을 거절한다. 정상/손상 연결 이력과 schema1~28을 포함한326개 seed 재생을 통과했다. 새 coverage-guided fuzz/sanitizer 실행은 이번 범위가 아니다. [전체 검증](validation/xer-relink-2026-11-08/README.md).

2026-11-07 조건부 schema27은 [XER 원장 병합](xer-ledger-merge.md)의 정책/기준 지문·기간 교체/누계 선택과 원장 참조를 검증한다. 병합 시점의 알려진 최신 날짜 행을 빠짐없이 연결하고, 실제 누계 AC와 대조한다. 외부 요약과 경비의 독립 구간 중첩만 허용하며 같은 차원의 중복은 거절한다. 해당 구간만 있는 재가져온 스냅샷도27로 저장한다. 구 envelope의 새 의미·필드와 미래33을 거절한다.

2026-11-07 조건부 schema26의 `xerFinancialRevisions`는 원본이 있는 경우에만 허용한다. 정정128건·정정 스냅샷 총300,000행, 날짜 비중1,000줄·양의 정수/정확한 기간 범위·중복 없는 소유 원장 ID를 제한하고 작성자/사유/UTC/revision과 생성 기록의 값·메타데이터를 대조한다. 구 envelope의 새 필드와 미래33을 거절한다. 원본 schema25와 정정 없는 구 writer는 유지한다. [계약](xer-aggregate-editing.md).

2026-11-07 조건부 schema25는 XER 재무기간·누계의 [별도 보존 스냅샷](xer-financial-snapshots.md)을 추가한다. 필수/미지 키·정수 최소단위·출처/지문 형식·중복/깨진 참조·동일 소유자의 겹치는 civil 기간·초과 배분과 작업/자원1만·배정10만·기간 합계10만/고유 구간1만을 제한한다. 후속26의 정정 이력을 지원하며 구 schema의 새 필드와 미래33을 거절한다. 기존 파일/복구/실제 decoder 및 raw-body 경로를 함께 검증한다.

2026-11-03 기간 실적 증분은 schema 11을 추가한다. 새 원장 객체의 필수/알 수 없는 키·정수 %·분·원가·누계 링크·정정 체인·전 상태일 대사와 50,000기록 한도를 검증한다. schema 1~11 seed 재생과 원장 정상/분수값 거절 seed를 추가하며 실제 coverage-guided fuzzing/sanitizer 실행 여부는 별도 검증 기록으로 남긴다.

2026-11-02 작업 분류 증분: native envelope/payload harness는 schema 1~10을 재생한다. 네 형식의 필드 값·코드 계층·저장된 보기·열 배치를 가진 schema 10 seed와 중첩 알 수 없는 필드의 거절 seed를 추가했다. 기존 bytes/개수/참조/순환 제한을 유지한다. seed replay와 sanitizer/libFuzzer 실행 결과는 별개로 기록한다.

2026-10-28 사용자 후속 개발 요청에 따라 PARSER-SAFE-01~04를 구현 전에 확정한다. 파일 파서의 coverage-guided 입력 변이와 ASan/UBSan 런타임 검사가 이번 범위다. 일반 Release 옵션과 Windows/macOS 제품 지원 범위는 유지한다.

`.jobs`의 메모리 decode는 파일 저장·게시·잠금 없이 load와 저장 전 검증의 실제 envelope/해시/schema/도메인 검사를 공유한다. XML/XER harness도 실제 import 함수를 사용한다. 변이 시 checksum 때문에 native 본문 검증이 가려지는 문제는 별도 payload 경로로 보완하고 schema1~9에 동일 본문을 시도한다. envelope 원본 경로의 무결성 검사는 우회하지 않는다.

퍼징은 Clang에서 명시적으로 켜는 도구다. JOBS 코드에 coverage와 ASan/UBSan을 적용하고 사전 빌드 Qt 내부를 검사했다고 주장하지 않는다. 일반 테스트는 정상/손상 seed 재생, Linux CI는 계측된 전체 CTest와 3개 타깃의 유한 실행을 수행한다. 실행 수/seed·크기/시간/RSS 제한·revision·컴파일러·명령/반환 코드·로그·발견 입력을 기록한다. 정상적인 입력 거부만 허용하며 sanitizer/비정상 종료/시간 초과를 성공으로 바꾸지 않는다.

유한 smoke는 보안 보증이나 상용 제품 왕복 호환 판정이 아니다. Linux는 검증 실행기이며 신규 배포 플랫폼이 아니다. 퍼징 자체가 끝난 뒤에도 앱이나 사용자 파일을 지속 감시하지 않는다.

도구 계약은 [LLVM libFuzzer](https://llvm.org/docs/LibFuzzer.html), [Clang AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html), [Clang UndefinedBehaviorSanitizer](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html)를 기준으로 한다. 구현·명령·검증 결과는 후속 절에 기록한다.

## 실행과 경계

기본 CTest의 `parser_seed_replay`는 정상/잘린 입력·빈 입력/이진 입력, schema1~9 envelope와 raw payload, PDM/제약/공수, XML, XER UTF-8/Windows-1252를 포함한 56개 seed를 검사한다. 무결성 오류 envelope가 파일 경로에서 거절되더라도 별도 body 경로를 실행할 수 있음을 구분한다. 알 수 없는 필드, 64MiB 초과 native 입력, 메모리 decoder와 실제 load/save 일치, harness의 logic/cancellation 예외 전파도 확인한다.

`ProjectFile.h`의 `jobs::decodeProjectFile`은 실제 load와 저장 전 검증에서 호출한다. `ParserHarness`는 성공/일반 입력 거부를 세고 `std::runtime_error`만 처리한다. `bad_alloc`, `logic_error`/`out_of_range`, 별도 `CalculationCancelled` 및 프로세스 sanitizer 실패는 거부 결과로 바꾸지 않는다. 라이브러리 파서는 파일을 쓰지 않으며 seed 도구만 명시한 테스트 corpus/임시 파일을 쓴다.

Clang·Qt 6.8.3 개발 환경에서 실행한다(Windows/MinGW opt-in은 명시 실패한다).

```sh
cmake -S . -B build-sanitized -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DJOBS_ENABLE_SANITIZERS=ON -DJOBS_BUILD_FUZZERS=ON
python3 scripts/verify-parser-instrumentation.py
cmake --build build-sanitized --parallel 2
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir build-sanitized --output-on-failure
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  python3 scripts/run-parser-fuzz-smoke.py --runs 5000
```

계측 옵션은 모든 JOBS 번역 단위에 ASan/UBSan·비복구 오류·프레임 포인터를 적용한다. domain/application/calculation/persistence/harness 다섯 정적 라이브러리에 `fuzzer-no-link`, 세 진입점에 fuzzer를 적용하고 compile_commands에서 누락을 검사한다. CMake는 두 옵션 모두 기본 OFF이며 fuzz만 켜고 sanitizer를 끈 구성은 거절한다. 계측되지 않은 사전 빌드 Qt 내부의 coverage를 주장하지 않는다.

`run-parser-fuzz-smoke.py`는 seed 재생 후 각 타깃에 `-runs=5000 -seed=20261028`, 64KiB 입력·입력당10초·RSS2048MiB·단일 할당1024MiB 제한을 적용한다. 타깃별 프로세스 한도는 180초이며 시간 초과/비정상 반환은 CI 실패다. native는 각 입력의 파일 envelope와 raw payload를 구분하고 9개 schema 본문 검증을 시도하며, XER는 두 인코딩을 시도한다. 요청 실행 수에는 libFuzzer의 corpus 처리도 포함되고, 고정 PRNG seed만으로 모든 머신의 실행 순서/coverage가 같다고 주장하지 않는다. 실제 corpus·명령·반환 코드·환경·revision과 libFuzzer 통계를 `build-sanitized/parser-fuzz`에 보존한다. 발견 입력이 있으면 `artifacts/<parser>/`의 파일을 해당 binary에 인수로 주어 재현한다.

```sh
build-sanitized/jobs_fuzz_jobs build-sanitized/parser-fuzz/artifacts/jobs/crash-SHA1
```

64KiB는 유한 smoke의 자원 예산이며 제품 파일 한도를 낮추지 않는다. 더 큰 입력·장기 퍼징·저장/게시/복구 I/O·ThreadSanitizer/동시성 검사는 이번 퍼징 경계 밖에 있다. 실제 사용자 프로젝트를 CI corpus에 넣지 않는다.


## 최종 검증

PRD/계약은 `4434568`에서 먼저 커밋하고 실제 decoder·harness·seed/CMake·runner는 `ea97d44`에서 구현했다. Linux CI 설치의 Qt 아키텍처 이름을 배포 목록 `linux_gcc_64`로 수정한 workflow는 `ec9de2e`다. 두 revision의 제품/CMake module/테스트/스크립트 소스가 동일함을 diff로 확인했다.

[제품 CI 40637538292](https://github.com/jhoon-dev/JOBS/actions/runs/40637538292)는 Windows/MSVC·macOS 각각 **42/42** 전체 회귀, native projected/bulk/model/async/responsive/XML/XER 검사, 독립 XLSX 5·PDF 6·Project XML·XER TSV/MPXJ 판독을 통과했다. 로컬 MinGW Release도 **42/42, 119.90초**와 독립 reader·미지원 sanitizer 옵션의 명시 실패를 확인했다. 두 OS의 1천/1만 기본·공수 일정 SHA-256 8개는 기존 golden과 일치했다.

[Linux CI 40637696271](https://github.com/jhoon-dev/JOBS/actions/runs/40637696271)는 Ubuntu 24.04·Clang 18.1.3·Qt 6.8.3·RelWithDebInfo에서 ASan/UBSan 전체 회귀 **42/42, 195.14초**를 통과했다. 컴파일 명령에서 확인한 번역 단위는 63개이며 다섯 의존 라이브러리의 coverage와 세 진입점을 검사했다. `detect_leaks=1`/비복구 오류 설정에서 56개 초기 seed와 각 타깃 **5,000회**, 총 **15,000회** 실행이 모두 0으로 종료했다. 이 유한 실행에서 crash/sanitizer/timeout/OOM 발견 입력은 없었다.

| 타깃 | 실행 수 | libFuzzer cov / ft | new_units_added | peak RSS(MiB) |
|---|---:|---:|---:|---:|
| .jobs | 5,000 | 1,078 / 3,947 | 79 | 266 |
| Project XML | 5,000 | 1,805 / 2,915 | 151 | 415 |
| Primavera XER | 5,000 | 2,215 / 4,634 | 243 | 423 |

cov/ft는 libFuzzer의 계측 카운터이며 형식/분기 검증률(%)이 아니다. new_units_added는 실행 중 추가 이벤트 수이며 최종 파일 수와 같지 않다. [검증 묶음](validation/parser-safety-2026-10-28/verification.json)에 revision·OS별 시간·일정 해시를 기록했다. Linux 하위 폴더에 실행 명령/반환 코드·환경·컴파일러·계측 목록·CTest와 seed/fuzz 원시 로그, 실제 corpus 319개 압축본과 파일별 SHA-256 manifest를 보존했다. 초기 재생 seed 56개가 모두 압축본에 포함됐음을 확인했다. 원시 입력/명령/도구 버전으로 후속 발견을 조사하며, 이번 통과를 취약점 부재나 외부 제품 전체 호환으로 확대하지 않는다.

평준화 채택 증분은 schema1~12와 유효 채택·알 수 없는 정책/필드 seed를 재생한다. 이는 seed 회귀이며 coverage fuzz 실행으로 간주하지 않는다. [채택 계약](leveling-adoption.md)을 따른다.

2026-11-03 다중 XER 선택 경로를 harness의 각 인코딩 분기에 추가했다. 프로젝트 목록 검증 후 명시 ID 선택 가져오기를 호출하며 전체 파일 한도/ID 검사와 선택 격리를 거친다. 다중 프로젝트 fixture와 잘린 사본 seed를 추가했고 native schema 범위 1~12는 유지한다. 새 coverage-guided fuzz/sanitizer 실행 여부는 [선택 계약](xer-project-selection.md)의 별도 검증 기록을 따른다.

선택 증분의 [로컬 전체 회귀/파서 기록](validation/xer-project-selection-2026-11-03/README.md)에서 92개 seed를 통과했다. 이는 원래 90개에 다중 XER와 잘린 사본을 더한 수이며 새 coverage-guided fuzz 실행은 아니다.

진척 정책 단독 선택은 schema13부터, 평준화와의 조합/시나리오 정책은 schema14부터 허용한다. 파서 corpus는 schema1..14, 유효/알 수 없는 정책·필드/이전 schema 거절 seed를 포함한다. 정책 객체의 정확한 키·문자열 형·revision 정수 범위·UTC·담당자/사유 및 조합의 버전 경계·시나리오 정책을 검증한다.

2026-11-05 기간별 배정 증분은 schema1..17 경로와 schema17 곡선·명시 0%·이전 schema/겹침/배정률 범위/미지원 작업 조합의 seed를 추가한다. 구조화 검사에는 배정당 1,024개·계획당 100,000개 한도와 소수/null/문자열/부울 거절도 포함한다. 이는 결정적 seed/계약 회귀이며 새 coverage-guided fuzz 실행은 아니다. [계약](assignment-periods.md).

인쇄·네트워크 표시 정보는 schema18부터 허용한다. schema1..18, 유효 설정/좌표·구 schema envelope 거절 및 schema18 본문 경로·없는 작업 참조/좌표 초과 seed를 추가했다. [확장 계약](print-network-extension.md).
