# 검증 기록 보관 방식

저장소에는 검증 요약과 현재 PM 시험 자료만 둔다. 스크린샷·접근성 덤프·로그·생성 JSON/XML·PDF·XLSX는
`build*/` 아래에 만들고 해당 실행의 GitHub Actions 산출물로 보관한다. 각 요약에는 소스 커밋, 실행 환경,
재현 명령, 결과와 미실행 범위를 기록한다. 실제 사용자 시험과 합성 사례 자동 검사를 구분한다.

## 이전 기록 조회

정리 전 전체 4,868개 파일은 커밋 `77bb5a1cead1cb2c47f36e57b00d478746f32434`에 보존되어 있다.

- [원본 폴더와 파일](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation)
- [고정 커밋 전체 소스 ZIP](https://github.com/jhoon-dev/ProjectPlanningManager/archive/77bb5a1cead1cb2c47f36e57b00d478746f32434.zip)
- [현재 CI 실행과 산출물](https://github.com/jhoon-dev/ProjectPlanningManager/actions)

각 증분의 README는 남겼고, 이동한 파일 링크는 고정 원본 또는 보존된 테스트 입력을 가리킨다.
기존 Git 이력을 보존하므로 과거 객체의 압축 용량은 남는다. 이번 정리는 현재 체크아웃과 이후의 증가량을 줄인다.
CI 산출물은 보관 기한이 있으므로 장기 회귀에 필요한 입력을 산출물 링크에만 의존하지 않는다.

## 저장소에 남기는 입력

- [native 호환성 회귀 입력](../../tests/fixtures/native-history/README.md): 원본 Git blob과 동일한 63개 파일·69개 기준선.
  `tracking_file`에서 해시·loader·과거 엔진 재현·원본 불변을 검사한다.
- [PM 시험용 30개 작업 자료](weekly-pm-pilot-2026-11-18/README.md): 참여자 과업지·빈 관찰 양식·작은 입력·생성기.
  실제 PM 시험은 사용자가 별도로 진행한다.

`.gitignore`가 원시 산출물의 추가를 막고, `scripts/check-validation-evidence.py`가 강제로 추적한 파일도 CI에서 검사한다.
요약 Markdown은 파일당 64KiB 이내로 유지한다. 새로운 회귀 입력은 `tests/fixtures/`에 목적과 기대값을 함께 둔다.
