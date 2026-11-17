# 조직·위험·자동화 검증

2026-11-05, Windows / Qt 6.8.3 / MinGW 13.1 / Release. [기능 계약](../../organization-risk-automation.md).

기존 build 캐시는 이전 JOBS 저장소 경로를 포함하므로 별도 build-governance에서 현재 소스를 구성/빌드했다. 기존 미커밋 작업은 보존했다.

```powershell
$env:PATH="$pwd/.deps/Qt/6.8.3/mingw_64/bin;$pwd/.deps/Qt/Tools/mingw1310_64/bin;$env:APPDATA/Python/Python314/Scripts;$env:PATH"
cmake -S . -B build-governance -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$pwd/.deps/Qt/6.8.3/mingw_64" "-DCMAKE_CXX_COMPILER=$pwd/.deps/Qt/Tools/mingw1310_64/bin/c++.exe"
cmake --build build-governance --parallel 4
ctest --test-dir build-governance --output-on-failure --output-log build-governance/full-ctest.log
python scripts/verify-governance-risk.py build-governance
python scripts/check-cpp-quality.py format --tool .deps/cpp-quality/Lib/site-packages/clang_format/data/bin/clang-format.exe
python -m unittest discover -s tests -p test_msix_packaging.py
```

## 최종 결과

- Release 전체 CTest **110/110** 통과, 396.38초. [원시 로그](full-ctest.log).
- Windows 기본 QPA 새 화면 시험 통과. Qt offscreen UI 회귀와 별도 실행했다.
- 파서 **210 seeds** 통과. [결과](parser.txt).
- clang-format 18.1.8 **247파일**, 변경 없음. [결과](format.json).
- 새 위험/schema3/receipt를 포함한 독립 reader **12종** 통과.
- MSIX Python 계약 **8/8**, 수정한 패키징 스크립트와 독립 reader Python 구문 검사 통과.

## 검사 내용

- governance_risk_automation_contract: EPS/OBS 계층·소유자·범위 역할/기본 거부, 추가 전용 요청/승인·revision·한 번 처리, schema3 저장/엄격 JSON/외부 변경, 통화별 포트폴리오의 미로드 범위, 확률 기대값/seed/원본 보존/그룹/퇴화/발생0/교차 프로젝트 ID/닫힘/충돌/취소, 원자 batch/미리보기/undo/오래된 요청/권한 철회/취소, 실제 CLI 별도 프로세스의 조회/미리보기/저장/재실행/정책 거부.
- governance_ui: 역할 전환과 문서 쓰기 통제, 요청→다른 검토자 승인, 위험/포트폴리오 백그라운드 결과, JSON 미리보기→적용/undo·감사, 권한 철회/오래된 설정·불변 원장, 실행 중 창 파괴의 취소를 검증한다.
- 새 workspace3 정상/오류 seed 8개를 기존 파서 corpus에 추가했다. 기존 workspace1/2와 프로젝트 파일·복구의 지원 범위는 유지한다.
- 독립 reader는 기존 native/XLSX/PDF의 자재·통합 작업공간, 객체 필드, 기간별 배정, 활동 확장, 인쇄·네트워크, Usage, 기간 실적, 진척 정책, 작업 분류, 잔여 여유와 새 위험/schema3/receipt를 확인했다. 기본 보고서의 XLSX5종/PDF6종도 독립 판독했다. [독립 검사](independent-verification.json), [출력 회귀](reader-regressions.json).
- [Windows 네이티브 화면](governance-risk-ui.png)에서 한글/다크 테마/작은 창의 스크롤과 닫기 버튼을 확인했다. offscreen 시험용 글꼴도 명시 등록했다. [실행 결과](native-ui.txt).

## 독립 수치

동일 FS 작업 2개, 원래 각 480분, 첫 작업 위험 triangular(480,960,1440), 발생100%, seed12345/2000회다. 입력을 바꾸지 않았다. 두 번째 고정 기간480분을 더한 삼각 분포 이론 분위수와 별도 Python 근무 달력의 판독값이다.

| 분위수 | 이론 근무 분 | 관측 근무 분 |
|---|---:|---:|
| P10 | 1174.663 | 1174 |
| P50 | 1440.000 | 1436 |
| P80 | 1616.421 | 1623 |
| P90 | 1705.337 | 1707 |

2026-11-04 18:00 이내 1023/2000, 양 절반 중앙값 차이9 근무 분이다. 허용오차50분 안이며 이 허용오차를 모든 모델의 정확도나 수렴 보장으로 확대하지 않는다. [원시 결과](risk-result.json), [자동화 receipt](receipt.json), [workspace3 fixture](governance.ppmworkspace). fixture는 parser용이고 참조 P/Q 파일을 이 폴더에 포함하지 않는다.

## 검증 경계

로컬 역할 선택은 인증/기밀성/변조 방지를 검증하지 않는다. 협업 기록은 동시 병합이나 실제 기준선 승인을 대신하지 않는다. GUI 프로젝트 저장과 작업공간 감사 저장은 각각의 파일 트랜잭션이며 다중 파일 원자성을 주장하지 않는다. CLI receipt 저장은 호출 측 책임이다. 확률 모델은 계획 고정 기간 CPM의 삼각 분포·공통 순위 그룹이며 일반 상관/확률 자원/비용/날씨는 지원하지 않는다. 실제 P6/MSP·다른 OS·서버 운영·스토어 설치본과 HTTP/반복 스케줄러는 실행/완료로 표시하지 않는다. MSIX Python 계약 검사는 실제 패키지 설치 검증이 아니다. Clang 정적 분석은 CI 범위이며 이번 로컬 실행에 포함하지 않았다.
