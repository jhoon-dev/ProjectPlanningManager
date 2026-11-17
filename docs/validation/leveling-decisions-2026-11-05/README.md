# 평준화 결정 원장 로컬 검증

2026-11-05, Windows / MinGW 13.1 / Qt 6.8.3 / Release. [계약](../../leveling-decisions.md), [미개발 점검](../../planning/17-remaining-development.md).

## 재현

현재 저장소의 `.deps/Qt/6.8.3/mingw_64/bin`, `.deps/Qt/Tools/mingw1310_64/bin`과 CMake/Ninja를 PATH에 추가한다.

```powershell
cmake -S . -B build-decisions -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=.deps/Qt/6.8.3/mingw_64
cmake --build build-decisions --parallel 4
ctest --test-dir build-decisions --output-on-failure
python scripts/verify-leveling-decisions.py build-decisions
```

`leveling_decision_contract`는 계획/잔여 채택·교체·해제·입력 무효화·직접/worker undo/redo, 결정 보존·dirty, 취소/stale/session/읽기 전용, schema15와 기존 schema12 선택, 사본의 원본 출처·복구, 원장 엄격 검증·10,000건 한도의 원자적 거절을 검사한다. XML/XER 손실 진단과 source.ppm 보존도 검사한다. 조회 화면은 모델 기반이며 문서 교체 후 이전 기록을 지운다.

독립 Python 검사는 native payload checksum과 **15건의 수기 동작 순서**, 이전 ID/단조 revision/전후 선택 연결, 계획→잔여 전환, 사본 출처와 기준선/누계 실적 보존을 판정한다. Qt reader를 재사용하지 않는다.

## 실행 결과

- 전체 **95종 회귀**를 실행했다(446.69초). 첫 실행은 92종 통과, 3종 실패였다. 두 기준선 archive 검사가 새 원장 데이터를 schema14로 읽었고, 파일 수명 검사가 새로 지원한 schema15를 미지원 버전으로 기대했다. 기준선 엔진 검산은 유지하면서 fixture reader를 실제 native schema로, 미지원 경계를 16으로 갱신했다. 제품 계산 코드는 이 보정에서 바꾸지 않았다.
- 수정한 3종과 결정 원장 검사를 재실행해 **4/4 통과**했다(17.54초). 최종 미해결 실패는 0건이다. 단일 전체 실행의 95/95 결과로 바꾸어 기록하지 않는다. [첫 전체 로그](ctest-initial.log), [최종 재검사 로그](ctest-final-recheck.log), [상세 결과](results.json)를 보존한다.
- 파서 **122 seeds**, 고정 clang-format 18.1.8의 **165파일** 검사를 통과했다. schema1~15와 기존 엔진 기준선 재현을 포함한다.
- [독립 reader 6종](independent-readers.json): 결정 원장 native checksum/15건 순서/설정·출처 연결, 진척+평준화 조합, 기존 채택 출력, 자원 여유 출력, 재분할 여유의 독립 분 격자, 교차 실행 창의 native/XLSX/PDF를 통과했다. 새 writer의 원장을 실제 채택 metadata와 대조하며, 원장 없는 기존 fixture의 schema12 검사는 유지한다.
- [일반 화면](history.png)과 [800×600 화면](history-small.png)을 직접 확인했다. 오프스크린 환경에는 Windows의 맑은 고딕을 명시해 한국어/날짜를 판독했다. 행·전후 설정과 가로/세로 스크롤·닫기 버튼을 확인했다. 실제 데스크톱 조작 증거는 아니다.

소스 SHA-256은 `94e04accad332fcc388f6e76c392a29743cb4494550c4dc4dfbe52ea7e291262`다. [소스·최종 검사 식별값](source-manifest.json), [빌드](build.log), 전체/재검사 상세 로그, [독립 판독한 native 파일](source.ppm)을 보존한다. Git revision의 `+dirty`는 기존 미커밋 개발과 이번 증분을 함께 포함한다.

실제 MSP/P6·macOS/Linux·물리 프린터·서명/배포·sanitizer/coverage 퍼징·신원 인증/파일 변조 방지는 이번 로컬 검증 범위에 포함하지 않는다. 기존 미커밋 구현을 포함한 작업 폴더에서 검사하며 소스 식별값을 별도로 보존한다.
