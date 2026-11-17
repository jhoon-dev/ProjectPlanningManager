# 인쇄 설정·WBS/관계선·네트워크 확장 로컬 검증

2026-11-05 / Windows / Qt6.8.3 / MinGW13.1 / Release. [기능·저장·출력 계약](../../print-network-extension.md).

기존 미커밋 구현을 보존한 현재 체크아웃에서 설정 영속화, 일정표 WBS 요약/관계선, 네트워크 관계 편집·배치 저장/초기화·벡터 출력을 연결했다. 설정과 배치는 Project 표시 정보이며 PlanInput·기준선/시나리오 입력·실적·원가·평준화 선택/결정 원장을 바꾸지 않는다.

```powershell
cmake -S . -B build-decisions -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=.deps/Qt/6.8.3/mingw_64
cmake --build build-decisions --parallel 8
ctest --test-dir build-decisions --output-on-failure --stop-on-failure
ctest --test-dir build-decisions -R '^(presentation_.*|network_diagram_.*|schedule_print_.*|parser_seed_replay|file_lifecycle)$' --output-on-failure
python scripts/verify-print-network.py build-decisions
```

Qt/MinGW·CMake/Ninja를 PATH에 추가한다. 독립 판독은 optional pypdf6.10.0/openpyxl3.1.5, 실제 PDF 렌더링은 Poppler pdftoppm을 사용했다. 애플리케이션 자체는 Python/Office를 요구하지 않는다.

## 결과

- **전체 회귀 101/101 통과**, 342.12초. [전체 로그](ctest.log), [테스트별 로그](ctest-details.log).
- 전체 회귀 후 JSON 설정 직렬화의 매크로를 명시 코드로 정리하고, 진행 중 작업의 관계 끝점을 잔여 재개 시각에 연결하며 완료 작업의 수신선을 참고 관계로 표시하도록 보완했다. 최종 생산 실행 파일과 관련 테스트를 다시 빌드하고 **영향 범위 8/8**, 23.63초로 재검증했다. [최종 빌드](final-build.log), [최종 검사](final-checks.log), [최종 테스트별 로그](final-check-details.log). 전체 회귀 단계와 최종 단계의 소스 식별값은 [manifest](source-manifest.json)에 구분한다.
- **파서 160 seeds**, schema1..18 및 구 schema의 새 필드 거절, 없음/잘못된 작업 참조·좌표 범위를 확인했다. 새 필드 없는 writer와 기존 엔진/기준선/교환 경로도 기존 전체 회귀를 통과했다.
- **clang-format18.1.8 190파일**, 변경 필요 0개. [형식](format.json), [공백](whitespace.log). [주 빌드](build.log)와 최종 생산 빌드에 compiler 경고/오류가 없다.
- [독립 native/XLSX/PDF 판독 10종](independent-readers.json) 통과. 새 판독은 schema18 checksum·설정/좌표·기준선 입력 분리, 일정표3쪽·WBS2행/관계1개, 네트워크 맞춤1쪽/타일3쪽/출처 추가1쪽, 긴 출처 보존과 취소 시 파일 없음·기존 파일 보존을 확인했다.
- PDF writer72dpi와 QPrinter PDF144dpi에서 동일 카드 **225×112.5pt**, 제목 **10pt**를 확인했다. Poppler로 렌더링한 [PDF 첫 페이지](network-tiles-page1.png)와 [프린터 PDF 첫 페이지](network-printer-page1.png)의 PNG SHA-256도 동일하다: `7cf8e76cb80a39cb45aef9edcfebf684165c03d5921d20edc6933d1267427625`. 단순 쪽 수 비교를 넘어 실제 벡터 도형·글자 크기를 검사했다.

## 기능별 확인

설정 저장/재열기·사본·복구·dirty/undo/redo·읽기 전용, 좌표 중복/범위/없는 작업·잘못된 JSON 형식, 삭제와 좌표의 원자적 undo, 기존 평준화 선택/불변 결정 원장 및 유효 일정 캐시 보존을 확인했다. 기간별 배정·객체 필드 등 앞선 증분의 회귀와 독립 출력도 유지했다.

WBS 요약은 표시/선택 작업과 조상만 집계하고 원본 행 정렬을 유지한다. 선택을 1작업으로 줄였을 때 두 조상 요약의 작업 수가 각각1이고 범위 밖 관계를 누락 수로 구분했다. FS/SS/FF/SF의 S/F 끝점, 진행 중 작업의 잔여 재개 시각과 완료 수신 관계의 참고 표시를 검사했다. 기간/여유·관측 실적을 합산/추정하지 않는다.

네트워크 화면에서 원본 수신 관계를 SS+60분으로 변경하고, 자기 참조 실패가 문서를 부분 변경하지 않는지 확인했다. 카드를 옮긴 좌표의 저장/재열기와 자동 배치 초안의 문서 불변, 저장 배치 초기화·읽기 전용을 확인했다. [관계·배치 화면](network-edit.png), [인쇄 미리보기](network-print.png), [실제 일정표 PDF 렌더](schedule-page1.png)를 직접 확인했다. Qt offscreen 검사이며 실제 데스크톱 조작은 아니다.

소스 SHA-256: `67b0590f392bcf4d527c455bd413753eb8e8a3399030b2e457dfec7de33a3fab`. [231개 소스·최종 검사/reader/CI/출력 식별값](source-manifest.json), [빌드 식별 헤더](BuildIdentity.h), [결과 JSON](results.json). `+dirty`는 기존 미커밋 작업과 이번 확장을 포함한다.

검증 예제: [native](presentation.ppm), [일정표 PDF](schedule.pdf), [망 한 장](network-fit.pdf), [망 타일](network-tiles.pdf), [가상 프린터](network-printer.pdf), [전체 출처](network-notes.pdf). 네트워크 수치 기하 예제는 3,120×180 장면에 두 카드와 선을 직접 기록한 고정 fixture이며 실제 업무 데이터가 아니다.

물리 프린터·다른 OS·설치/배포·MSP/P6 실행은 이번 로컬 검사로 완료 처리하지 않는다. 네트워크 인쇄는 정방향1부이며 고급 선 묶기/충돌 없는 배치 최적화·다중 프로젝트 네트워크는 후속이다.
