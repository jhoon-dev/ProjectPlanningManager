# 과거 native 파일·기준선 회귀 입력

검증 기록의 원시 산출물을 정리하면서 실제 호환성 검사에 필요한 63개 `.ppm`과 기준선 69개를 분리했다.
`manifest.json`에 원본 커밋·경로·SHA-256·기준선 수를 기록한다. 파일은 원본 Git blob과 동일하며
줄바꿈도 변환하지 않는다. 화면·로그·PDF는 테스트 입력에 포함하지 않는다.

`tracking_file`은 각 파일의 해시, loader, 모든 동결 기준선의 과거 엔진 재현과 원본 불변을 검사한다.
새 호환성 사례는 특정 회귀를 재현하는 작은 입력으로 추가하고, 생성 출력의 복사본은 추가하지 않는다.

[원본 검증 기록](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation)
