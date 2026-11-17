# 2026-11-17 작업 보존과 저장소 정리

## 보존 기준점

기존 `main`의 HEAD는 `6a1cd0a5115f3400d6cee4e5d5df795f351195ea`다.
기존 작업을 `codex/checkpoint-2026-11-17` 브랜치의 `36f0f2f` 커밋으로 보존했다.
`main` 포인터는 이동하지 않았으며 원격 push는 실행하지 않았다.

작업 전 추적 파일 수정은 121개(+13,642/-579줄), 미추적 파일은 실제 파일 기준
3,868개였다. Git status의 축약된 디렉터리 항목 수와 실제 파일 수는 다르다.
체크포인트에는 기존 수정 121개와 새 파일 3,850개, 총 3,971개 변경을 기록했다.
새 소스·테스트·도구·서버·패키징·문서·검증 자료를 함께 보존했다.
여러 기능이 공통 도메인·저장 형식·UI 파일을 함께 수정한 상태이므로, 현재 상태를
임의의 기능별 완성 커밋으로 분해하지 않았다. 이 커밋은 작업 보존 기준점이며
전체 기능 검증 완료나 실제 MSP/P6 호환성 인증을 의미하지 않는다.

## 외부 백업과 복원 확인

저장소와 같은 상위 디렉터리의 다음 위치에 백업을 만들었다.

```text
../ProjectPlanningManager-backups/20261117-144618/
  workspace.zip
  history.bundle
  manifest.json
  working-tree.patch
  index.patch
  original-index
  status-before.nul
  RESTORE.txt
  restore-verification.json
```

`workspace.zip`에는 모든 추적·미추적 파일과 기존 설치 도구 로그를 원본 바이트로
보존했다. 5,677개 파일, 193,018,812바이트이며 ZIP은 53,190,793바이트다.
`manifest.json`은 파일별 크기·SHA-256, 원래 브랜치·HEAD와 백업 산출물 해시를 기록한다.
재생성 가능한 기존 ignored 빌드·의존성·배포 캐시와 Python bytecode는 제외했다.

`history.bundle`은 작업 전 Git 참조와 이력을 보존한다. Git bundle 검증 후
`restore-verification/`이라는 별도 폴더에 실제 clone·원래 HEAD checkout·ZIP 복원을
수행했다. 복원된 5,677개 파일의 SHA-256이 모두 원본과 일치했고,
복원 저장소의 `git fsck --full --no-dangling`도 통과했다.
복원은 활성 작업 폴더를 덮어쓰지 말고 `RESTORE.txt`에 따라 새 폴더에서 수행한다.

이 백업은 같은 디스크의 로컬 사본이다. 디스크 장애에 대비한 원격 또는 다른 장치의
백업은 아니며, 원격 게시 여부는 별도 결정한다.

## 이번 정리 범위

- 루트의 `.playwright-mcp/`, `tmp/`, `leveling-selection-qa/`를 `.gitignore`에 추가했다.
  기존 파일은 삭제하거나 이동하지 않았다. 앞의 백업에는 기존 비어 있지 않은 두
  디렉터리의 18개 파일도 포함된다. `tmp/`에는 파일이 없었다.
- 기존 `build*`, `.deps`, `dist`와 설치 도구 로그 제외 규칙은 유지했다.
  빌드 캐시에 절대 경로가 있으므로 빌드 디렉터리를 이동하지 않았다.
- `docs/validation/`의 검증 근거는 계속 추적한다. 루트의 임시 출력과 구분한다.
- 소스·계산 엔진·파일 형식·README 제품 소개는 이번 정리에서 변경하지 않았다.
- 일반적인 인증 토큰·개인 키 패턴 점검에서는 공개 테스트용 TLS 키 한 건만 확인했다.
  `tests/fixtures/server-tls/README.md`에 공개·폐기 가능한 테스트 전용 키라고 명시되어 있다.
  이 점검은 모든 종류의 비밀정보 부재를 보증하는 감사가 아니다.

## 확인 결과

기존 `build-numbered-summary`에서 다음 8개 검사가 통과했다.

```text
file_lifecycle
workspace_file_contract
undo_contract
recovery_crash_file
recovery_ui
parser_seed_replay
material_float_contract
material_float_ui
```

첫 실행에서 기간 편집 검사 2개는 실행 파일이 없어 미실행이었다.
`jobs_duration_display_tests`, `jobs_duration_followup_tests`를 빌드한 뒤
`duration_edit_display`, `duration_paste_pending_sort` 두 검사를 재실행해 모두 통과했다.
최종 결과는 선택한 10개 검사 모두 통과다. 전체 테스트 suite나 새 환경에서의 전체
빌드는 수행하지 않았다. 로그는 외부 백업 디렉터리의 `checkpoint-*.log`에 보존했다.

이후 변경은 이 체크포인트를 기준으로 기능·수정 목적별 작은 커밋으로 기록한다.
제품 대상과 대표 업무 결정, UX 개선, 실제 MSP/P6 왕복 검증은 후속 작업이다.
