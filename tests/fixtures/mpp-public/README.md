# 공개 MPP 판독 표본

MPXJ v16.10.0의 고정 commit `7ef9e72813d33378c55ec6ceb148cc831ed22033`에서 내려받았다. 원본 경로·URL·SHA256·크기는 [manifest.json](manifest.json)에, 저장소 라이선스는 [LICENSE-MPXJ.txt](LICENSE-MPXJ.txt)에 보존한다.

| 파일 | 실제 형식 | 검증 용도 |
|---|---|---|
| mpp9relations.mpp | MPP9 | 5개 작업, FS/SS/FF/SF·양수/음수 지연, 미배정 placeholder |
| mpp12relations.mpp | MPP12 | 동일 관계/기간과 GBP 원본/명시 사본 통화 구분 |
| mpp14relations.mpp | MPP14 | 관계/기간·UI 읽기/취소·새 사본 게시 |
| assigned-resources-mpp8-project2003.mpp | MPP8 | 1개 작업·10개 자원/배정·가용량 |
| password-protected-mpp9.mpp | MPP9 암호 보호 | 보호 우회 없이 거절 |

MPP8 파일은 Project2003에서 MPP8로 저장한 표본이며 실제 Project98 앱을 실행한 증거가 아니다. 다양한 버전 전체·외부 프로그램 왕복 동등성을 대표하는 자료로 확대하지 않는다. 테스트는 원본을 수정하지 않는다.

2026-11-09 수동 곡선 후속 표본:

| 파일 | 검증 용도 |
|---|---|
| timephased-multi-rate.mpp | 실제 MPP14 · 정규 계획9배정/13구간·수동0구간2곡선·초과근무3배정 제외 |
| timephased-actual-work-resource.mpp | 실제공수 분포를 계획으로 채택하지 않음 |
| timephased-actual-overtime-work.mpp | 실제 초과근무/생성 placeholder를 native 관측으로 만들지 않음 |
| mpp9/12/14timephasedsegments.mpp | 추가 근무일이 있는 달력은 기존 제한에 따라 전체 변환 거절. 원본 곡선의 존재만으로 달력 제한을 우회하지 않음 |
