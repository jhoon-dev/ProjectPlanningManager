# 서버 인증·동기화·HTTP API·주기 실행 검증

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/server-collaboration-2026-11-05)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

2026-11-05, Windows / Qt6.8.3 / MinGW13.1 / Release / Python3.12.10 / aiohttp3.14.5. [계약·실행법](../../../server/README.md), [요약 JSON](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/server-collaboration-2026-11-05/summary.json).

## 최종 결과

- **전체 CTest112/112**, 426.10초. [원시 로그](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/server-collaboration-2026-11-05/full-ctest.log).
- 최종 로그인 실패 집계 보정 뒤 서버 영향 범위 **2/2**를 다시 통과했다(64.53초). 정상 로그인31회가 peer 실패 예산을 소모하지 않는 회귀를 추가했다. [최종 로그](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/server-collaboration-2026-11-05/final-server-ctest.log).
- `server_http_auth_sync_scheduler`: 실제 C++ PPM-Automation 별도 프로세스를 사용하는 Python **23개** 통합 검사. [로그](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/server-collaboration-2026-11-05/server-http.txt). SQL trigger의 의도된 실패와500 응답은 rollback 검증이며 테스트 실패가 아니다.
- `server_native_realtime_ui`: 두 Qt HTTP 클라이언트의 충돌, change feed와 Qt 편집 화면·초안 보존·미리보기/적용·로그아웃, 신뢰하지 않는 TLS 인증서 거부. 전체 offscreen와 별도로 Windows 기본 QPA에서도 통과했다.
- 브라우저 로그인→미리보기→원자 적용, 두 번째 클라이언트의 갱신과 작성 중 초안 보존, 실제 worker의 예약 실행(성공·serverVersion4)→취소를 확인했다. localStorage/sessionStorage0, 세션 비밀을 저장하지 않았다. 검증 계정은 로그아웃하고 fixture process를 종료했다.
- clang-format18.1.8 **252파일**, 변경 없음. [결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/server-collaboration-2026-11-05/format.json). Python Black/compile, JS 구문, PowerShell launcher AST, OpenAPI JSON과 Mac network metadata를 확인했다.
- 기존 파서 **210 seeds** 통과. [결과](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/server-collaboration-2026-11-05/parser.txt).
- MSIX Python8/8, Mac packaging10개 통과·실제 macOS 의존2개 skip. 실제 Store 설치 검증으로 확대하지 않는다.
- Windows runtime74파일을 배치하고 Qt6Network/Schannel TLS backend를 확인했다. Qt/MinGW가 PATH에 없는 환경에서도 동봉 PPM-Automation --help 실행을 확인했다. [runtime inventory](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/server-collaboration-2026-11-05/runtime.json). 이 폴더는 로컬 실행용이며 Store 서명/외부 게시를 하지 않았다.

## 통합 검사 범위

production 기본 scrypt(131072/8/1·salt16·verifier32), opaque token의 DB hash, logout/reset/disabled/TTL/throttle, 서버 권한/프로젝트 열거 차단, source/native 버전, 변경 전후와 request SHA의 일치, batch 마지막 오류의 원자성, 동시 클라이언트 CAS와 승자 보존, 같은 요청의 멱등 영수증/내용 재사용 충돌, 미리보기 만료·권한 철회, native 등록의 overwrite/무결성/구형 파일 차단, WebSocket ACL/철회/replay와 HTTP long-poll, Origin/JSON/바이트 한도/응답 헤더, worker 자동 실행·재시작·lease 중복 차단·중단 복구·missed interval 병합·외부 변경 정지·소유자 권한 철회·실행 중 취소·DB 실패 시 프로젝트/이벤트/영수증 rollback과 online backup, HTTPS/인증서 검증을 대조했다.

테스트의 비밀번호·인증서·개인키는 공개 test fixture이며 production launcher가 사용하지 않는다. 테스트에 KDF를 낮춘 격리 Store를 쓰되 별도 production 기본값 검사를 포함한다. 인증서 검증을 끄지 않고 Python의 격리 신뢰 컨텍스트와 네이티브의 기본 거부를 검증했다.

## 재현

```powershell
python -m venv .deps/server
.deps/server/Scripts/python.exe -m pip install -r server/requirements-lock.txt
# Qt/MinGW/CMake PATH를 설정한 뒤
cmake -S . -B build-governance -DJOBS_SERVER_TEST_PYTHON=$pwd/.deps/server/Scripts/python.exe
cmake --build build-governance --parallel 4
ctest --test-dir build-governance --output-on-failure
```

[Windows 화면](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/server-collaboration-2026-11-05/server-collaboration-native.png), [브라우저 화면](https://github.com/jhoon-dev/ProjectPlanningManager/blob/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/server-collaboration-2026-11-05/browser-collaboration.png)을 시각 검토했다. 기존 로컬 작업공간/파일 schema와 편집·복구 회귀를 보존했다.

## 검증 경계

서버 협업은 현재 native batch의 작업 이름/기간을 대상으로 한다. 일반 필드 자동 병합이나 실적/승인 원장 직접 덮어쓰기는 제공하지 않는다. local workspace actor는 server identity가 아니다. 실제 외부 TLS 도메인/인증서 발급·운영 계정/서비스 설치·독립 보안 감사·SSO/MFA·cron·여러 서버 HA는 실행/검증 완료로 표시하지 않는다. 원격 서버를 배포하거나 실제 사용자 계정을 생성하지 않았다. Qt client의 실제 macOS/Store 네트워크 승인·패키지 설치는 해당 환경 과제로 남긴다.
