# 다중 XER 일괄 작업공간·내보내기와 코드 교환 검증

2026-11-05 / Windows / Qt6.8.3 / MinGW13.1 / Release. [기능 계약](../../xer-batch-exchange.md).

기존 미커밋 작업을 보존한 체크아웃에서 다중 파일·프로젝트 선택 가져오기, 원자적 작업공간 패키지, 프로젝트별/통합 XER 내보내기, 작업 코드 계층·배정 교환을 구현했다. native Project 최대 schema18·Workspace schema1을 유지한다. 기본 코드만 사용하는 예제 사본은 필요한 최소 버전인 schema10으로 저장된다.

## 결과

- **전체 CTest 103/103 통과**, 368.18초. [전체 로그](ctest.log), [테스트별 로그](ctest-details.log).
- **파서 170 seeds 통과**. 코드 계층/배정·다중 코드 사전, 순환·누락 값·중복 배정 seed를 추가했다. schema1..18 및 기존 네 파서 회귀를 포함한다. 로컬 유한 seed 재생이며 ASan/libFuzzer 실행을 뜻하지 않는다.
- **clang-format18.1.8 196파일**, 변경 필요0개. [형식 결과](format.json), [공백 검사](whitespace.log). [최종 빌드](build.log)에 compiler 경고·오류가 없다. 분류 회귀 테스트를 새 교환 계약에 맞춘 [추가 빌드](classification-build.log)도 통과했다.
- [새 독립 TSV/native/MPXJ 판독](independent-reader.json): 2개 프로젝트·12개 작업, 코드 종류4개·값8개·배정6개, 공유 연결2개, 전체 숫자 ID/외래 키·프로젝트 소유권, 계층 부모·작업 코드 참조, native 파일 해시·상대 경로·통합 작업 ID 매핑을 확인했다. MPXJ16.10.0/JPype1.6.0/Java21.0.11/Python3.12.10을 사용했다.
- 기존 [단일 XER TSV/MPXJ 판독](legacy-xer-reader.log)과 [프로젝트 선택 TSV/MPXJ 판독](selection-reader.json)도 통과했다. MPXJ는 통합 파일의 전체 코드 사전을 보존하므로 프로젝트 scope ID로 사전을 분리해 확인했다.

초기 회귀에서는 이전의 “작업 코드 미지원” 손실 경고를 기대한 분류 검사가 실패했다. 사용자 필드 손실 경고와 실제 코드 계층·작업 배정의 왕복을 검사하도록 수정한 뒤 전체103개를 다시 실행했다. [초기 로그](initial-ctest.log). 기존 선택 판독기도 원본에 없는 선택적 코드 테이블의 0행 집계를 명시적으로 허용하도록 보완했다. 제품 소스 식별값은 최종 전체 검사 동안 동일하다.

## 확인한 경계

원본별 ID 격리·동일 원본 중복/프로젝트 중복/없는 ID, 두 번째 원본 실패, 양 끝을 모두 선택한 외부 선후행 관계 차단, 원본 바이트 보존, 필요한 달력/자원 범위, 같은 원본 자원의 공유 연결과 달력 충돌 시 연결 제외를 확인했다. 다른 원본의 같은 자원 ID/이름을 합치지 않는다.

게시 직전 실패·취소·대상 폴더 경합에서 부분 결과·임시 폴더 누출·기존 파일 덮어쓰기가 없음을 검사했다. 모든 native 사본의 객체 동등성, 내보내기 원본 문서 불변, 통합 ID 재배정과 재가져오기, 통화 혼합 거절, 명시적 공유 카탈로그 보존과 부분 집합 필터, 폴더 이동 후 상대 참조를 확인했다.

UI는 선택 기본값 없음·외부 관계 차단·가능 항목 모두 선택·건수, 메뉴 연결·미리보기·선택/미리보기 취소, 다른 프로젝트 창 변경 시 stale 차단, 일괄 게시와 기존 문서를 유지한 전체 작업공간 열기를 검사했다. [선택 화면](selection.png), [미리보기](preview.png)를 직접 확인했다. Qt offscreen 검사이며 실제 데스크톱/P6 설치본 조작은 아니다.

## 재현

Qt/MinGW와 CMake/Ninja를 PATH에 추가한 뒤 실행한다.

```powershell
cmake -S . -B build-decisions -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=.deps/Qt/6.8.3/mingw_64
cmake --build build-decisions --parallel 8
ctest --test-dir build-decisions --output-on-failure
./build-decisions/jobs_xer_batch_tests <존재하는-상위-폴더/새-패키지-경로>
python scripts/verify-xer-batch.py <새-패키지-경로>
python scripts/verify-primavera-xer.py build-decisions
python scripts/verify-xer-project-selection.py build-decisions
```

독립 판독에는 고정 MPXJ/JPype와 Java가 필요하며 앱 실행 의존성은 아니다. 기존 경로는 덮어쓰지 않는다. [검증 패키지](package/conversion.json), [작업공간](package/workspace.ppmworkspace), [통합 XER](package/workspace.xer), [소스·검사·실행 파일/패키지 식별값](source-manifest.json), [빌드 식별 헤더](BuildIdentity.h), [결과 JSON](results.json)을 보존한다. CI에 일괄 패키지 생성·독립 판독·화면/패키지 보존 단계를 추가했으며 CI 원격 실행을 로컬 검증으로 대체 주장하지 않는다.

소스 SHA-256: `a121a1e14a52f8286722f4376b1d02a91d142e94c0dc673f9a25ae53d13a4948` (src/CMake 235개). `+dirty`는 이전 미커밋 구현과 이번 변경을 함께 포함한다.

실제 P6/MSP 재계산·재저장 왕복, MPP, 실적·기간 원가/단가/UDF 교환, 다른 OS 실행, 프로젝트 간 논리 링크·통합 일정/평준화는 이번 검증 범위에 포함하지 않는다. 실적·원가는 가져오기 원본 또는 내보내기 native 사본에 보존하는 계약이다.
