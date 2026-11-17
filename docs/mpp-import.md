# Microsoft Project MPP 읽기

2026-11-09 후속: [MPP 수동 계획 곡선 읽기](mpp-manual-curves.md)를 지원한다. 원본 정규 계획·0구간과 총공수를 채택하고 실제/초과근무 제외 및 분 경계/기간 창 차이를 미리보기에 표시한다. 앱/reader bundle은 함께 protocol2로 갱신한다. [최신 검증](validation/mpp-manual-curves-2026-11-09/README.md)을 따른다.

2026-11-09: reader의 XML에 제공되는 기본 WorkContour/계획 공수는 [자원 생산성·외부 고유 곡선 교환](resource-productivity-curves.md)의 native 검사로 연결된다. 바이너리 MPP의 수동 곡선 전체 복원과 MPP 쓰기는 지원하지 않는다.

2026-11-08 · MPP-01~06 · GAP-09. **파일 → Microsoft Project MPP 읽기…**에서 원본을 읽고 지원되는 기간 기반 계획을 새 PPM 사본으로 연다. MPP 쓰기, Microsoft Project 재계산 동등성, 원본의 모든 필드를 native 의미로 채택하는 기능과 구분한다.

## MPP-01 읽기 엔진과 버전

MPXJ16.10.0의 `MPPReader`를 별도 Java21 프로세스에서 사용한다. `MSProject.MPP8/9/12/14` OLE 형식만 허용한다. 확장자만 바꾼 XML, 다른 OLE 형식, 손상·부분 파일과 암호 보호 파일을 거절한다. 암호 해독·보호 무시 옵션은 제공하지 않는다. 원본 경로를 자식 프로세스에 주지 않고 읽은 바이트를 표준 입력으로 전달한다. 외부 하위 프로젝트를 찾아 열거나 원본을 변경하지 않는다.

공식 [MPP 형식/reader 문서](https://www.mpxj.org/howto-read-mpp/)와 [16.10.0 source](https://github.com/joniles/mpxj/tree/7ef9e72813d33378c55ec6ceb148cc831ed22033)를 따른다. 실제 MPP8·9·12·14 공개 표본의 검증 범위는 [표본 목록](../tests/fixtures/mpp-public/README.md)에 기록한다. 다른 모든 제작 버전·지역·필드 조합의 호환을 주장하지 않는다.

## MPP-02 계획 투영과 손실 경계

reader가 읽은 객체를 MSPDI XML로 투영하고 기존 [Project XML 검증](project-xml.md)을 적용한다. 지원 부분은 작업 이름·기간/마일스톤·WBS, FS/SS/FF/SF 관계와 정수 분 지연, 제약/마감, 달력·근무 구간/지원 휴일, 작업 자원과 배정·가용량 및 지원되는 Text/Number/Date 사용자 필드다. 원본 UID와 native ID의 연결표를 변환 보고에 남긴다.

기간 기반 고정 기간으로 변환한다. 원본 FixedWork/FixedUnits 계산 규칙·예상 기간 표시·완전한 외부 고유 곡선 계산 규칙·표시/보기·문서/내장 데이터·시간축·기준선·상태일·실적·원가/단가를 현재 native 관측값으로 생성하지 않는다. 제외한 값은 원본 MPP를 따른다. writer가 만든 미배정 placeholder 배정은 원래 개수와 제외 건수를 별도로 기록하고 제거한다. 실제 자원이 있는 배정을 조용히 제외하지 않는다.

기존 XML 정책과 마찬가지로 수동/외부/하위 작업, elapsed 기간·지연, 지원되지 않는 자재/비용 자원·달력 의미, 요약 작업의 관계·배정, 참조 누락/중복과 선행 순환 등을 거절한다. native의 프로젝트 날짜 시작 기준은09:00이며 시작 시각/계산 정책 차이를 진단한다. 외부 표시 날짜를 native 계산값으로 오인하지 않는다.

## MPP-03 통화와 원본

처음에 새 계획 사본의 KRW/USD/EUR/JPY 통화를 명시한다. 원본 통화와 선택한 사본 통화를 미리보기와 `currencyPolicy`에 함께 표시한다. 이 단계는 환율 적용이나 원본 금액의 단위 변경이 아니다. 원본 실적·원가·단가를 가져오지 않으므로 해당 숫자를 선택 통화의 금액으로 재해석하지 않는다.

확인 후 새 교환 폴더에 `original.mpp`의 정확한 바이트, `project.ppm` 사본, `conversion.json`을 원자적으로 게시한다. 보고에는 원본 이름·크기/SHA256, reader 형식/버전·구성 지문·중간 XML 지문, 연결표와 손실 진단을 포함한다. `.ppm`만 다른 위치/서버로 저장하면 원본과 진단은 자동 복제되지 않는다. 원본이 필요한 경우 교환 폴더 전체를 보관한다. native 파일 schema는 새 필드 없이 기존 최소 writer를 사용한다.

## MPP-04 응답성과 제출

읽기·프로세스·계획 검증·교환 폴더 저장은 기존 교환 worker에서 실행한다. 미리보기의 보존/손실·통화를 확인해야 새 사본을 열 수 있다. 기존 문서의 저장/취소 확인을 따른다. 기존 폴더를 덮어쓰거나 준비 도중 현재 문서를 바꾸지 않는다.

취소·창 닫기·문서/session/revision 변경 시 작업을 중단하고 자식 프로세스가 종료된 뒤 임시 폴더를 정리한다. 읽기 전용 원본 문서에서 가져오면 기존 파일은 유지하고 새 쓰기 가능한 사본을 연다. 미리보기 이후 현재 문서가 바뀌면 오래된 결과로 교체하지 않는다. 게시 최종 rename 전에 취소를 검사하고, rename이 끝난 후에는 완성된 산출물을 보존하는 기존 교환 계약을 따른다.

입력32MiB, 중간 XML16MiB, helper metadata8KiB·stderr64KiB·heap512MiB·읽기 기본60초/최대120초, 기존 XML 깊이32·노드300,000·각 native 컬렉션 한도를 적용한다. 이는 격리된 별도 프로세스와 자원 한도이며 OS 권한 sandbox를 새로 만드는 기능은 아니다.

## MPP-05 오프라인 런타임과 배포

앱 옆 `mpp-reader`(macOS는 `Contents/Resources/mpp-reader`)에 Java 런타임·별도 JAR·bridge를 배치한다. 사용자의 PATH에 Java/Python이 없어도 실행된다. 앱은 가져오기 중 네트워크로 구성 요소를 내려받지 않는다. 구성 요소가 없거나 지문이 다르면 안내하고 중단한다. 기존 XML/XER 읽기에는 이 런타임이 필요 없다.

[engine lock](../packaging/mpp-reader/engine-lock.json)에 MPXJ와22개 JAR의 SHA256, Eclipse Temurin21.0.11+10 및 대응 source archive의 URL/지문을 고정한다. 분리된 JAR와 bridge 원본·의존성 LICENSE/NOTICE, jlink runtime의 원래 고지와 **MPXJ/OpenJDK의 대응 전체 소스 archive**를 동반한다. [MPXJ LGPL2.1 원문](https://github.com/joniles/mpxj/blob/v16.10.0/LICENSE)과 각 의존성의 고지는 배포 파일에 포함한다. source archive는 실행하지 않는다.

최초 구성은 로컬 JDK와 고정 JAR/소스 파일을 준비한 뒤 실행한다. 같은 버전/플랫폼·아키텍처의 JDK를 사용해야 한다.

```powershell
python scripts/prepare-mpp-reader.py --jdk C:/Users/dow/.jdks/jdk21 --libraries .deps/xer-reader-verification/Lib/site-packages/mpxj/lib --sources .deps/mpp-research/sources --output build-governance/mpp-reader
cmake --build build-governance --parallel 8
```

builder는 다운로드하지 않으며 모든 JAR/source 입력을 lock과 대조한다. `manifest.json`은 생성한 파일과 runtime 지문을 기록한다. 앱은 실행 전에 이를 검사하고 목록에 없는 classpath JAR를 거절한다. manifest는 디지털 서명이 아니다. 변경한 라이브러리로 개인 빌드를 사용할 때는 분리된 JAR와 `MppBridge.java`를 다시 빌드하고 manifest 지문을 갱신할 수 있다. 공식 배포 준비는 lock의 원래 엔진·소스 일치를 다시 검사한다.

Windows 공통 payload는 reader와 source를 함께 복사·검증하며, 누락/변조된 구성으로 패키징하지 않는다. CMake install도 준비된 reader를 포함한다. Mac 서명은 runtime Mach-O 서명 뒤 manifest 지문을 갱신하고 나서 앱을 최종 서명한다. macOS의 실제 실행·Universal/JVM 서명·App Sandbox/스토어 수용은 해당 환경에서 별도 검증해야 한다.

## MPP-06 검증

[실행 기록](validation/mpp-import-2026-11-08/README.md)에 실제 네 형식의 native 저장/원본 동일성, 명시 미변환 경계·오류/크기/취소/시간 제한, Windows 기본/800×600·stale/session/읽기 전용, 독립 객체 대조와 런타임 배포 검사를 보존한다. 독립 객체 검사는 같은 MPXJ decoder를 쓰되 bridge/XML writer를 거치지 않는 경로다. 실제 Microsoft Project 실행·재계산/재저장을 대신하지 않는다.
