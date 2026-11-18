# 부분일·무기한 가용기간 검증

> 당시 원시 자료·화면·로그는 [고정 원본](https://github.com/jhoon-dev/ProjectPlanningManager/tree/77bb5a1cead1cb2c47f36e57b00d478746f32434/docs/validation/resource-availability-2026-11-16)에 보존되어 있습니다. 새 실행 결과는 CI 산출물로 보관합니다.

2026-11-16 Windows/Qt6.8.3 MinGW Release, `build-numbered-summary`. [기능 계약](../../resource-availability.md).

- 관련 회귀 **44/44**,204.21초. 새 가용기간/편집기·Units·세 일정 엔진·기존 자원/재분배 여유·XML 기준선/병합/상세 시간분포·요약·통화/ID·XER 일정 채택·undo/복구·파일·원가/Usage·MPP 가져오기/수동 곡선을 확인했다. 전체 프로젝트 CTest의 영향 범위 부분집합이다.
- XER 일괄 교환 **1/1**,2.21초. 새 가용기간을 공유 풀로 자동 연결하지 않는 진단 문구를 보완한 뒤 별도로 통과했다.
- 파서 **1,014개**,schema1~79. 새 경계/기간 정상 파일·겹침·잘못된 시각/단위·필수 키 누락·절단, schema78 이하 거절과 과거 seed를 포함한다. `resource-availability-final-detail.log`에 출력을 보존한다.
- 새 계약은 포함 종료 분·다음 분·무기한 방향·경계 밖0·날짜/기간/기본값 우선순위·Null/0·캐시 변경·중복/역전 거절을 확인한다. 세 엔진 모두11:00/13:00 평준화, 실행 중10:00~10:29 가용량 감소 시10:30으로 이동, 구간 과부하120분/초과공수9,000 백분율·분(90자원분)를 대조한다.
- schema79 exact JSON·과거 최소 writer·승인/시나리오/채택/통화·ID 사본 감지, 실제 저장·편집 undo/redo·복구 checkpoint를 확인했다. 장기간 XML은 일별 행으로 팽창하지 않는다.
- 독립 Python civil-minute/Decimal/ElementTree: XML 출력/재출력의 **11,520분 지점**,완전한 비중첩 기간4개, 가용량 명세6행·시점 조회0.5의 XLSX와 PDF **12쪽**을 대조했다. `independent.json`.
- MPXJ16.10.0 strict reader: ignored errors0, **5,760분 지점**의 가용량이 독립 native oracle과 일치했다. 무기한 양끝과 종료초59를 포함한다. 실제 MS Project/P6 제품 실행을 의미하지 않는다.
- UI의 기존 값 채움·가용 시작 변경·저장과 포함 경계/무기한 입력 안내를 확인했다. `review.png`를 눈으로 확인했다. 테스트 프로세스에 Windows 맑은 고딕을 명시해 UI/PDF 글자 표시·텍스트 추출을 확인했다.
- 변경 C++28파일 clang-format·tracked whitespace·Python AST와 산출물 해시를 확인했다. `sha256.json`에 소스/산출물을 보존한다.

첫 회귀는43/44였으며 마지막 지원 날짜의 `cost_day_boundaries`가 실패했다. 용량 타임라인이 최대 좌표의 다음 해 자정을 조회한 것을 마지막 유효 분으로 수정하고, 논리 계획은 사용하지 않는 가용량 타임라인을 만들지 않도록 보완했다. 새 최종 분 테스트와 기존 원가 경계 검사를 포함해44개 전체를 재실행해 통과했다. 초기 테스트의 폰트 누락으로 PDF 텍스트가 비어 있던 문제도 테스트 폰트를 지정해 해결했다.

재실행 예:

```powershell
ctest --test-dir build-numbered-summary --output-on-failure --parallel 2 -R '^(resource_availability|xml_unit_fields|cost_day_boundaries$)'
build/validation-python/Scripts/python.exe scripts/verify-resource-availability.py docs/validation/resource-availability-2026-11-16 --reader
```

실제 제품 실행 환경은 없으며 MS Project/P6의 재계산·재저장은 미검증이다. MPP 회귀는 기존 protocol2/MPXJ16.10.0·Java21 reader bundle을 활성 빌드에 복사해 수행했다. 원본 읽기와 독립 reader의 수치 일치를 제품 수용으로 확대하지 않는다. 외부 XML 빈 구간의 기본값 의미, XER/공유 풀 정책과 분 단위 지원 범위는 기능 계약을 따른다.
