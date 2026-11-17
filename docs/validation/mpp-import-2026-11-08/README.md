# MPP 읽기 검증 · 2026-11-08

Windows x64 · Qt6.8.3 · MinGW13.1 · Release/Ninja. 전체 **139/139 통과, 219.71초**. 파서 **478개 시드**, 형식 **390개 파일**, 직접 객체 대조 **128개 검사**, 별도 패키징 **28개 검사**를 통과했다(macOS 플랫폼 조건2개 제외). [기능 계약](../../mpp-import.md) · [소스/검증 지문](verification-summary.json) · [전체 CTest](ctest.txt)·[JUnit](ctest.xml)·[개별 출력](all-test-output.txt) · [빌드](build.txt) · [형식 검사](format.txt).

## 실제 형식과 보존

공식 MPXJ commit에 고정한 [공개5개 표본](../../../tests/fixtures/mpp-public/manifest.json)을 사용한다. MPP8/9/12/14의 읽기, 정확한 original.mpp·native 저장/재열기, 기존 폴더 거절과 실적·원가·기준선/상태일을 생성하지 않는 경계를 검사했다. 비OLE/XML 오표기·부분 손상·암호 보호·32MiB 초과·없거나 잘못된 runtime·통화/시간 한도와 취소를 검사한다.

별도 프로세스에는 원본 경로를 넘기지 않는다. 읽기 전후 원본 바이트와 SHA256을 대조하고 원본 파일은 수정하지 않는다. MPP writer나 Microsoft Project 계산 동등성을 주장하지 않는다.

## 직접 객체 대조

[대조 스크립트](../../../scripts/verify-project-mpp.py)는 원본을 직접 `MPPReader`로 읽고 bridge/MSPDIWriter를 거치지 않은 객체의 작업 이름·근무분, 선행 UID/종류·분 지연, 자원/가용량·배정을 C++ native 출력과 대조한다. [실행 결과](independent-projection.json)를 보존한다. 같은 binary decoder를 사용하는 투영 검산이며 제3의 MPP decoder나 Microsoft Project 실행 증거와 구분한다.

원본 네 파일은 작업16개와 자원/배정10개를 포함하며128개 대조를 통과했다. 원본/사본 통화, 원본 지문과 native 관측값의 부재도 확인한다. JSON·reader XML·변환 보고는 [mpp-qa](mpp-qa/)에 남긴다.

## Windows와 오프라인 배포

[Windows native 실행](native-ui.txt)은 실제 메뉴와 파일 선택·통화 설정·읽기 진행·확인·게시·새 사본 열기를 조작한다. 미리보기/작업 중 취소, 편집으로 오래된 결과 무효화, worker 중 같은 파일 읽기 전용 재열기, 읽기 전용 원본에서 새 쓰기 가능한 사본 가져오기를 확인했다. [기본 미리보기](mpp-qa/preview.png)와 [800×600](mpp-qa/preview-small.png)에서 한국어·원본 통화/손실·스크롤·확인/게시 footer를 확인했다.

Windows 공통 payload를 실제 준비하고 [전체 파일/바이트 한도](standalone-payload.json)와 reader의216개 파일·대응 source archive를 검사했다. 개발 테스트 실행 파일만 QA 사본에 추가한 뒤 **Java·Python·Qt가 PATH에 없는 환경**에서 네 형식 판독·거부 계약을 다시 실행했다. [배포 로그](standalone-deploy.txt)·[독립 실행 결과](standalone-test.txt)를 보존한다. QA 실행 파일/QtTest는 배포 대상 payload의 파일/이름·크기 guard가 끝난 뒤에만 QA 사본에 추가했다.

별도 [MPP payload7개 검사](mpp-packaging.txt)는 source 누락, JAR/manifest 변조, 목록 밖 classpath, 프로토콜, create-only 복사와 Mac Mach-O 서명 지문 갱신을 검사했다. [MSIX](msix-packaging.txt)·[macOS 패키징](mac-packaging.txt)·[Apple 서명 준비](apple-signing.txt)의 기존21개 검사도 함께 실행한다. 실제 스토어 제출/서명 배포는 수행하지 않는다.

실제 Microsoft Project 재계산/재저장, 다른 제작 버전/언어/필드 조합 전체, macOS/JVM/App Sandbox 실행과 새 MPP fuzzer 캠페인은 미실행이다. 기존 native/XML/XER 파서 시드 재생은 전체 회귀에 포함한다.

## 재현

```powershell
$taskRoot = (Get-Location).Path
$evidence = Join-Path $taskRoot 'docs/validation/mpp-import-2026-11-08'
$env:PATH = "$taskRoot/.deps/Qt/6.8.3/mingw_64/bin;$taskRoot/.deps/Qt/Tools/mingw1310_64/bin;$env:APPDATA/Python/Python314/Scripts;$env:PATH"
cmake --build build-governance --parallel 8
ctest --test-dir build-governance --output-on-failure --parallel 4 --output-junit "$evidence/ctest.xml" --output-log "$evidence/ctest.txt"
.deps/xer-reader-verification/Scripts/python.exe scripts/verify-project-mpp.py build-governance/mpp-qa
python scripts/check-cpp-quality.py format --tool .deps/cpp-quality/Lib/site-packages/clang_format/data/bin/clang-format.exe
python tests/test_mpp_payload.py
python tests/test_msix_packaging.py
python tests/test_mac_store_packaging.py
python tests/test_prepare_apple_signing.py
Push-Location $evidence
& "$taskRoot/build-governance/jobs_project_mpp_tests.exe" -platform windows
Pop-Location
```

reader 최초 구성과 소스 lock은 [기능 계약](../../mpp-import.md#MPP-05-오프라인-런타임과-배포)을 따른다.
