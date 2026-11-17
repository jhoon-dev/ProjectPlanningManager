# 날짜 고정 자원 여유·외부 UDF 검증

2026-11-06 Windows 64-bit, Qt6.8.3/MinGW13.1 Release/Ninja. 사용자 요청의 날짜 고정 입력 자원 여유 → 외부 UDF 매핑을 순차 구현했다. 기존 미커밋 작업은 보존했다. [DFL-01~04](../../dated-resource-float.md)·[UDF-01~05](../../external-udf-mapping.md)와 [잔여 우선순위](../../planning/18-remaining-priorities-review.md)를 따른다.

## 수용 검사

| 기능 | 확인한 내용 |
|---|---|
| 날짜 고정 자원 여유 | 실제 후보에서 날짜 목표 재분배·부하/상한 검산, 창 밖 목표 후보 제외, 상대 수요에만 유효한 단조성 최적화 제외, 총/자유여유·임계와 개별 증인 인증 |
| 독립 유한 oracle | 4분 근무일의 모든 배치를 직접 분 단위 열거. 목표100/200/300/400의 총여유3/2/1/0, 자유0, 후속 작업 총/자유6분. production TaskExecution·후보 탐색/Timeline을 oracle로 쓰지 않음. 실제 증인의 목표/용량/프로젝트 끝과 취소 확인 |
| UDF 형식/값 | XML 작업/자원 Text/Number/Date·XER 작업/프로젝트/자원, 숫자6자리/음수/0·미설정/빈 문자열, 날짜 자정, 목록 이름·계산식 결과의 정적 투영, 오류/미설정 결과 생략과 손실 진단 |
| UDF 오류/원본 | 정의/값 중복·dangling/잘못된 소유·XML 형식/범위·숫자 지수/NaN·날짜 시각·슬롯 초과·XER 제어문자, 정수형의 소수와 혼합 값 열 거절. 사본 재열기·내보내기 전후 입력 불변·source.ppm 원자 게시 |
| 다중 XER | UDF ID와 범위별 fk_id 재배정, 프로젝트별 값/자원 격리, 제외 자료의 잘못된 참조도 선택 전 거절. 단일 파일의 빈 정의 보존과 다중 선택에서 소유를 알 수 없는 빈 전역 정의의 제외 명세 |
| UI/기존 기능 | 실제 XML/XER 메뉴에서 매핑 미리보기·출력 재가져오기, 취소/stale·readonly 원본 불변. 기존 저장/복구·계산식/보기·날짜 입력·평준화/진척/원가·서버 회귀 |

## 실행 증거

- 최종 전체 **121/121 통과**, **507.24초**. 새 UDF 검사와 확장된 날짜 고정 전수 oracle, 갱신한 객체 필드 계약, 기존 계획/잔여·평준화·진척·Usage·교환·성능/취소·저장/복구·출력/원가·서버 회귀를 포함한다. 서버 내부23개도 통과했다. [검증 요약](verification-summary.json).
- [구현 빌드](build.txt), [최종 빌드](build-final.txt), [최종 집중 검사5/5](focused-final.txt), [전체 CTest](ctest.txt), [형식 검사](format.json).
- [첫 전체 회귀](ctest-before-hardening.txt)에서는 기존 객체 필드 검사의 ‘외부 필드 미지원 진단’ 기대가1건 실패했다. 이 기대를 XML 자원4개/XER 프로젝트4개·자원4개 매핑과 실제로 남는 프로젝트 XML·옵션 사전 손실의 검사로 갱신했다. [검사 재빌드](build-object-contract.txt)·[수용 조건 확인](object-contract.txt). 검사를 삭제하거나 실패를 무시하지 않았다.
- [독립 reader 판독](independent-readers.json): ElementTree/직접 TSV가 정의8/9개·숫자/날짜·빈 값·미설정·동적 식 없음과 결합2프로젝트의 UDF·객체 외래 키를 확인했다. MPXJ16.10.0/JPype1.6.0으로 단일 XML·단일 XER·결합 XER의 프로젝트1/2를 오류 무시 없이 읽었다. 작업 -2.123456/0·계산 결과 -4.246912, 자원0·날짜, XER 프로젝트123.000001을 대조했다.
  [XML 매핑 명세](xml-mapping.json)·[XER 매핑 명세](xer-mapping.json)에 필드별 ID/형식과 실제 손실을 보존했다. 파서 **252 seeds**(정상/비정상 UDF와 잘린 입력 포함)를 통과했다.
- [앞 증분의 독립 회귀 판독](regression-independent-readers.json)도 통과했다. 기존 자원 고유 행/미설정·원가/공수·실적/동결·계산식 결과, XLSX/PDF와 Decimal400건을 유지한다.
- 독립 reader는 XML 빈 텍스트를 빈 문자열로, XER 빈 텍스트를 null로 읽는다. 이를 일치로 위장하지 않고 변환 진단·[지원 계약](../../external-udf-mapping.md)에 명시했다. native 원본은 구분을 보존한다.
- [4종 전수 기대값](dated-float-oracle.json), Windows 기본 UI **2/2**([XML](native-xml-ui.txt)·[XER](native-xer-ui.txt))를 확인했다. [XML800×580](xml-udf-preview-native.png)·[XER800×580](xer-udf-preview-native.png)의 실제 매핑/손실·새 폴더·변환 버튼을 시각 검토했다. clang-format18.1.8 **289파일** 변경 없음, Python 구문과 `git diff --check`도 통과했다.
- 자체 실행 [runtime inventory](runtime.json)·[배포 로그](runtime-deploy.txt)·[깨끗한 PATH의 CLI 실행](runtime-help.txt)에 최종 실행 경로, 파일 해시·동봉 QtNetwork/Schannel·MinGW/라이선스를 기록한다. 로컬 패키지이며 서명/스토어 배포는 수행하지 않는다.
  최종 실행 파일은 `build-governance/dated-float-udf-runtime-4cdd2e44/payload/PPM.exe`다. 동봉74파일과 원본 빌드의 PPM/PPM-Automation SHA256 일치, Qt/MinGW 없는 PATH에서 CLI 종료0을 확인했다.

## 재현

Qt/MinGW/CMake/Ninja PATH 설정 후:

```powershell
cmake --build build-governance --parallel 4
ctest --test-dir build-governance --output-on-failure
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-dated-float-udf.py build-governance
python scripts/check-cpp-quality.py format --tool .deps/cpp-quality/Lib/site-packages/clang_format/data/bin/clang-format.exe
# build-governance 작업 디렉터리에서 Windows 기본 UI
.\jobs_project_xml_ui_tests.exe -platform windows
.\jobs_primavera_xer_ui_tests.exe -platform windows
```

`build-governance/dated-udf-qa`에 분 단위 oracle JSON, mapped.xml/xer·combined.xer, 변환 보고/미리보기와 native 동반 사본을 만든다. 검사는 임시 문서를 사용하며 사용자 프로젝트나 원격 계정/서버를 변경하지 않는다.

## 검증 경계

기존 고정 기간 날짜별 잔여 모델의 자원 순서 여유다. 자원 순서 재최적화·FixedWork/FixedUnits 날짜별 역산은 포함하지 않는다. UDF는 명시한 스칼라와 정적 값 투영이며, 외부 계산식 실행·기업/배정 UDF·XML 값 목록/GUID·XML 프로젝트 필드는 별도 범위다. XER의 빈 텍스트 해석, 단위/목록/계산식 손실과 사전/슬롯/조회 한도를 숨기지 않는다. 실제 Microsoft Project/P6 재계산·재저장, macOS·물리 프린터·스토어/실제 서버 배포·현장 수용은 이번 Windows 로컬 증거로 완료 처리하지 않는다.
