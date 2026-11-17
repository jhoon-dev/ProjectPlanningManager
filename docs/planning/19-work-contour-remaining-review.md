# 곡선 역산 후 잔여 항목 재점검

2026-11-17 후속: [MF-01~07 자재 제약 여유·임계](../material-float.md)를 완료로 반영한다. 계획/잔여·공유 통합의 총/자유여유·임계·검산된 배치·출력/승인 기준선 재현을 연결했다. 아래 자재 여유 미지원 기록은 이 후속으로 대체하며, 전역/구매 최적화·자재 풀·여러 파일 날짜 일괄 채택은 남는다.

2026-11-17 후속: [SMAT-01~07 공유 통합 실제 자재](../shared-material-actuals.md)를 구현했다. 아래 MAT 당시 남긴 공유 잔여 원장 투영·실제 재고와 공유 자원/교차 링크 자동 대기·근거 출력/세대 보존을 완료로 반영한다. 자재 제약 여유/임계·전역/구매 최적화·자재 풀·여러 파일 날짜 채택은 남는다.

2026-11-17 후속: [MAT-01~08 실제 자재 원장·잔여 자동 대기](../actual-materials.md)를 구현했다. 실제 입고/소모 정정·현재 상태일 확인/잔여 수량·입고 잔량과 네 계산 프로필의 단일 문서 잔여 비교/채택·동결/복구/schema83을 완료로 반영한다. 공유 통합 잔여 원장 투영·자재 제약 여유·구매/원가 최적화·여러 파일 날짜 일괄 채택은 남는다.

2026-11-17 후속: [MAD-01~07 자재 입고 자동 대기](../material-arrival-leveling.md)를 구현했다. 아래 MINV 당시 남긴 자동 날짜 지연의 명시 옵션·직렬 예약·세 기본/공수 엔진·공유 평준화·채택/이력/schema82 범위를 완료로 반영한다. 실제 재고/잔여 소모 원장·자재 제약 여유·구매/비용 전역 최적화·여러 파일 날짜 일괄 채택은 남는다.

2026-11-17 후속: [MINV-01~07 시간축 계획 입고·잔여 자재 재고](../material-inventory.md)를 구현했다. 아래 자재 입고 잔여 중 개시 시각/입고·계획 소모·최초 부족·평준화 후보 검산·편집/출력/schema81 범위를 완료로 반영한다. 입고에 맞춘 자동 일정 지연·구매/비용 최적화·실제 재고 원장·초과근무·통합 날짜 일괄 채택은 남는다.

2026-11-16 후속: [SPA-01~06 공유 풀 가용기간](../shared-resource-availability.md)을 구현했다. 아래 SRA 당시 남긴 부분/열린 공유 가용기간의 모델·편집·조건부 작업공간4·분 시점/일별 부하·통합 평준화/출력 범위를 완료로 반영한다. 초과근무/시간축 자재/비용 최적화·여러 파일 날짜 일괄 채택 등은 남는다.

2026-11-16 후속: [SRA-01~06 공유 잔여 가용량](../shared-remaining-availability.md)을 구현했다. 아래 RRA 기록 당시 남긴 다중 프로젝트 공유 풀 차감 중 명시 연결·독립 프로젝트 일정의 시점 조회·미정/하한·기여와 출력 범위를 완료로 반영한다. 교차 링크 통합 일정의 채택·공유 부분 가용기간·초과근무/자재/최적화 등은 남는다.

2026-11-16 후속: [RRA-01~06 현재 부하 차감 잔여 가용량](../remaining-resource-availability.md)을 구현했다. 현재 프로젝트 전체 수요·유효 공급·분 시점 조회·예측/채택 문맥·UI/계산식/교환/보고·schema80의 지원 부분을 완료로 반영한다. 다중 프로젝트 공유 풀 차감·초과근무/자재/최적화 등 다른 GAP는 남는다.

2026-11-16 후속: [RAV-01~06 부분일·무기한 가용기간](../resource-availability.md)을 구현했다. 자원의 가용 시작/종료 경계·기간별 예외·구간 과부하/평준화·시점 조회·XML 정규화/schema79의 지원 부분을 완료로 반영한다. 현재 부하 차감·초과근무/자재/최적화와 다른 GAP는 남는다.

2026-11-15 후속: [XUF-01~06 Units·가용률 참조](../xml-unit-fields.md)를 구현했다. 외부 계산식 잔여의 Units/가용률은 기본 비율과 명시 날짜 조회·XML 날짜 예외·schema78의 지원 부분으로 대체한다. 현재 부하 차감·부분일/무기한 가용량·실제 제품 재계산 동등성은 별도로 남는다. [검증](../validation/xml-unit-fields-2026-11-15/README.md).

2026-11-15 후속: [NBT-01~06 native 기준선 상세 시간분포](../native-baseline-timephased.md)를 구현했다. native 공수/원가 시간분포 투영 잔여는 동결 실행 공수·일별 반올림 원가·다섯 범위 표준 출력/재채택·schema77의 지원 부분으로 대체한다. 실제 제품 왕복과 나머지 GAP는 별도로 남는다. [검증 기록](../validation/native-baseline-timephased-2026-11-15/README.md).

2026-11-15 후속: [XBM-01~06 기존 문서에 외부 XML 기준선 병합](../xml-baseline-merge.md)을 구현했다. 기존 문서 병합 잔여는 번호별 전체 교체·명시 연결/제외·번호별 출처/동결 달력·불변 전후 이력/schema76의 지원 부분으로 대체한다. 별도 위치 시간분포의 추가 해석·native 상세 시간분포 투영·실제 제품 왕복과 나머지 GAP는 계속 남는다. [검증 기록](../validation/xml-baseline-merge-2026-11-15/README.md).

2026-11-15 후속: [XBE-01~07 외부 XML 기준선 채택·내보내기](../xml-baseline-exchange.md)를 구현했다. 이전의 해당 잔여 표기는 새 사본의 번호별 명시 채택·표준 Baseline 값/표시·내부 시간분포 보존·native 승인본 투영 지원으로 대체한다. 기존 문서 병합·별도 위치 시간분포의 추가 해석·native 상세 시간분포 투영·실제 MS Project/P6 재계산·재저장과 나머지 GAP는 잔여다. [검증 기록](../validation/xml-baseline-exchange-2026-11-15/README.md).

2026-11-15 후속: [XBS-01~05 번호별 기준선](../xml-numbered-baselines.md)과 [XSD-01~06 요약 실제·잔여·기준선 기간](../xml-summary-durations.md)을 구현했다. 아래 과거의 번호별 참조·요약 기간 잔여 표기는 이 지원 부분으로 대체한다. 외부 XML 기준선 자체의 채택·내보내기, 다른 함수와 실제 MS Project/P6 재계산·재저장 및 나머지 GAP는 계속 잔여다. [검증 기록](../validation/numbered-baselines-summary-2026-11-15/README.md).

2026-11-15 후속: [XML SLN·SYD](../xml-depreciation-functions.md)의 감가상각·비음수 원가/잔존가·Null 오류·정확한 중간 계산과 조건부 schema72를 구현했다. [검증](../validation/xml-depreciation-2026-11-15/README.md). 다른 함수·번호별 기준선·요약 기간과 기존 GAP는 계속 잔여다. 최종 검증: 일반 CTest 230/230 통과·시간 초과 0건·703.60초, 대용량 단독1/1·156.69초로231개 테스트 확인(표준 시간 실패 없음), 집중 부분집합62/62·파서938개·형식565파일, 독립 유리수9032(overflow100)/Windows8714(정확8713·0.000001 차이1건)·직접 식25/공통 오류5·XML/XER20값·XLSX66행/PDF19쪽·schema72 실행 패키지를 확인했다.

2026-11-14 후속: [XML Partition](../xml-partition-function.md)의 수치 구간·범위 전후·반올림·공백과 조건부 schema71를 구현했다. [검증](../validation/xml-partition-2026-11-14/README.md). 다른 함수·번호별 기준선·요약 기간과 기존 GAP는 계속 잔여다. 최종 검증: 일반 CTest 228/228 통과·시간 초과 0건·565.37초, 대용량 CTest180초 시간 초과/동일 실행 파일 직접1/1·160.24초로229개 테스트 확인(표준 시간 실패 1건 유지), 집중 부분집합60/60·파서934개·형식562파일, 독립 구간5332/Windows5332·직접 인수21/오류5·XML/XER22값·XLSX68행/PDF19쪽·schema71 실행 패키지를 확인했다.

2026-11-14 후속: [XML RGB·QBColor](../xml-color-functions.md)의 색상 정수·반올림·16색 팔레트와 조건부 schema70를 구현했다. [검증](../validation/xml-colors-2026-11-14/README.md). 다른 함수·번호별 기준선·요약 기간과 기존 GAP는 계속 잔여다. 최종 검증: 일반 CTest 226/226 통과·시간 초과 0건·769.76초, 대용량 단독1/1·167.64초로227개 테스트 확인(표준 시간 실패 없음), 집중 부분집합58/58·파서930개·형식559파일, 독립 색상5609/Windows RGB5593·직접 식17/오류8·QBColor16·XML/XER20값·XLSX66행/PDF19쪽·schema70 실행 패키지를 확인했다.

2026-11-14 후속: [XNF-01~06 XML 숫자 표시3개](../xml-numeric-display.md)의 정확한 십진 표시·TriState·반올림/음수0/빈 문자열과 조건부 schema69를 구현했다. 네 범위·전체 사본·교환/보고를 연결한다. 다른 함수·번호별 기준선·요약 기간·전체 VBA/제품 왕복과 기존 GAP는 잔여다. [검증](../validation/xml-numeric-display-2026-11-14/README.md). 최종 검증: 일반 CTest 224/224 통과·시간 초과 0건·658.42초, 대용량 단독1/1·168.89초로225개 테스트 확인(표준 시간 실패 없음), 집중 부분집합56/56·파서926개·형식556파일, 독립 십진16200/VBScript7128·직접 식28/오류8·XML/XER20값·XLSX66행/PDF19쪽·schema69 실행 패키지를 확인했다.

2026-11-14 후속: [XDT-01~06 XML FormatDateTime](../xml-format-datetime.md)의 고정 en-US 날짜·시각 표시0~4·기준일/자정·옵션 반올림과 조건부 schema68을 구현했다. 네 범위·전체 사본·교환/보고를 연결한다. 다른 함수·번호별 기준선·요약 기간·전체 VBA/제품 왕복과 기존 GAP는 잔여다. [검증](../validation/xml-date-display-2026-11-14/README.md). 최종 검증: 일반 CTest 222/222 통과·시간 초과 0건·664.49초, 대용량 단독1/1·174.28초로223개 테스트 확인(표준 시간 실패 없음), 집중 부분집합54/54·파서922개·형식553파일, 독립 표시7370/VBScript7370·직접 식19/오류4·XML/XER20값·XLSX66행/PDF19쪽·schema68 실행 패키지를 확인했다.

2026-11-14 후속: [XDN-01~06 XML MonthName·WeekdayName](../xml-date-name-functions.md)의 고정 en-US 월·요일 이름/약어·주 시작일·반올림과 조건부 schema67을 구현했다. 네 범위·전체 사본·교환/보고를 연결한다. 다른 함수·번호별 기준선·요약 기간·전체 VBA/제품 왕복과 기존 GAP는 잔여다. [검증](../validation/xml-date-names-2026-11-14/README.md). 최종 검증: 일반 CTest 220/220 통과·시간 초과 0건·697.84초, 대용량 단독1/1·152.60초로221개 테스트 확인(표준 시간 실패 없음), 집중 부분집합52/52·파서918개·형식550파일, 독립 이름5992/VBScript5992·직접 식26/오류8·XML/XER20값·XLSX66행/PDF19쪽·schema67 실행 패키지를 확인했다.

2026-11-14 후속: [XUC-01~06 XML AscW·ChrW](../xml-unicode-functions.md)의 native/XML-safe BMP 문자 코드·부호/음수 별칭·짝수 반올림과 조건부 schema66을 구현했다. 기존 Asc/Chr는 유지하며 네 범위·전체 사본·교환/보고를 연결한다. 다른 함수·번호별 기준선·요약 기간·전체 VBA/제품 왕복과 기존 GAP는 잔여다. [검증](../validation/xml-unicode-2026-11-14/README.md). 최종 검증: 일반 CTest 218/218 통과·시간 초과 0건·728.70초, 대용량 CTest180초 시간 초과/동일 실행 파일 직접1/1·273.18초로219개 테스트 확인(표준 시간 실패 1건 유지), 집중 부분집합50/50·파서914개·형식547파일, 독립 문자63456/VBScript63456·직접 식24·XML/XER20값·XLSX66행/PDF19쪽·schema66 실행 패키지를 확인했다.

2026-11-14 후속: [XLK-01~06 XML Like](../xml-like-operator.md)의 BMP 패턴 비교를 구현했다. 목록/범위/부정·패턴 전체 검증·유한 작업량/Null·기존 문법 보존과 네 범위·요약/전파·미리보기/교환/보고·조건부 schema65를 지원한다. 번호별 기준선·요약 기간·전체 VBA/제품 왕복과 기존 GAP는 잔여다. [검증](../validation/xml-like-2026-11-14/README.md). 최종 검증: 일반 CTest 216/216 통과·시간 초과 0건·805.89초, 대용량 단독1/1·161.81초로217개 테스트 확인(표준 시간 실패 없음), 집중 부분집합48/48·파서910개·형식544파일, 독립 패턴8932/.NET8932·XML/XER20값·XLSX66행/PDF19쪽·schema65 실행 패키지를 확인했다.

2026-11-14 후속: [XLG-01~06 XML 논리 연산](../xml-logical-operators.md)의 Eqv/Imp를 구현했다. Boolean/32비트·Null·우선순위와 과거 문법 보존·네 범위·요약/전파·미리보기/교환/보고·조건부 schema64를 지원한다. 번호별 기준선·요약 기간·전체 VBA/제품 왕복과 기존 GAP는 잔여다. [검증](../validation/xml-logical-2026-11-14/README.md). 최종 검증: 일반 CTest211/214 통과·시간 초과3건·637.02초, 동일 코드 순차 확장3/3 통과, 대용량 CTest180초 시간 초과/동일 실행 파일 직접1/1·224.57초로215개 기능 확인(표준 시간 실패4건 유지), 집중 부분집합46/46·파서906개·형식541파일, 독립 비트4096/진리표200·VBScript4296·XML/XER20값·XLSX66행/PDF19쪽·schema64 실행 패키지를 확인했다.

2026-11-14 후속: [XOP-01~06 XML 산술 연산](../xml-arithmetic-operators.md)의 ^/정수 나눗셈을 구현했다. 우선순위·32비트 짝수 반올림·거듭제곱/범위/Null, 기존 문법 보존과 네 범위·요약/전파·미리보기/교환/보고·조건부 schema63을 지원한다. 번호별 기준선·요약 기간·전체 VBA와 실제 제품 왕복/기존 GAP는 잔여다. [검증](../validation/xml-arithmetic-2026-11-14/README.md). 최종 검증: 일반212/212·487.22초, 대용량 CTest180초 시간 초과/동일 실행 파일 직접1/1·208.46초(표준 시간 실패1건 유지), 집중 부분집합44/44·파서902개·형식538파일, 독립 거듭제곱2048/정수 몫2048·VBScript4096·XML/XER20값·XLSX66행/PDF19쪽·schema63 실행 패키지를 확인했다.

2026-11-14 후속: [XBC-01~06 XML Baseline Cost](../xml-baseline-cost.md)를 구현했다. 최초 native v1의 날짜별 동결 원가와 네 범위·그룹/전파·미리보기/교환/보고·통화/전체 사본·조건부 schema62를 지원한다. 이전의 Cost 잔여 표기는 이 최초 기준선 지원 부분으로 대체한다. 번호별 기준선 대응·요약 기간·다른 Project 함수·실제 제품 왕복과 기존 GAP는 남는다. [검증](../validation/xml-baseline-cost-2026-11-14/README.md). 최종 검증: 일반210/210·452.48초, 대용량 CTest180초 시간 초과/동일 실행 파일 직접1/1·227.84초(표준 시간 실패1건 유지), 집중 부분집합42/42·파서898개·형식535파일, 독립128사례/1408값·XML/XER32값·XLSX45행/PDF24쪽·schema62 실행 패키지를 확인했다.

2026-11-14 후속: [XPC-01~06 XML Project 변환](../xml-project-conversion-functions.md)의 ProjDateValue/ProjDateConv/ProjDurConv를 구현했다. ISO/NA·고정 en-US 날짜/기간 표시와 네 범위/요약/전파·미리보기/교환/보고·조건부 schema61을 지원한다. PC/Project 옵션·현재 시각·실제 월 길이를 추측하지 않는다. 번호별 기준선/Cost·요약 기간·다른 함수/제품 왕복과 기존 GAP는 남는다. [검증](../validation/xml-project-conversions-2026-11-14/README.md). 최종 검증: 일반208/208·335.90초, 대용량 단독1/1·152.38초로209/209, 집중 부분집합40/40·파서894개·형식532파일, 독립 날짜2816표시·기간10240표시/재파싱·직접20식·XML/XER26값·XLSX76행/PDF20쪽·schema61 실행 패키지를 확인했다.

2026-11-14 후속: [XCV-01~06 XML 형 변환](../xml-conversion-functions.md)의 CDec/CSng/CVar를 구현했다. native 고정점·binary32 변환·형식/기간 보존과 네 범위/미리보기/교환/보고·조건부 schema60을 지원한다. 전체 Decimal/Single/Variant·NLS는 지원하지 않는다. 다른 Project 함수·번호별 기준선/Cost·요약 기간과 실제 제품 왕복/기존 GAP는 남는다. [검증](../validation/xml-conversions-2026-11-14/README.md). 최종 검증: 일반206/206·396.59초, 대용량 단독1/1·147.23초로207/207, 집중 부분집합38/38·파서890개·형식529파일, 독립 유리수2058건·.NET2058건·직접26식·XML/XER22값·XLSX72행/PDF19쪽·schema60 실행 패키지를 확인했다.

2026-11-14 후속: [XST-01~06 XML 문자열 함수](../xml-string-functions.md)를 구현했다. Replace/InStrRev/StrReverse·BMP 위치/ASCII 비교·시작 이후 치환·끝 경계 역검색·함수별 Null 구분과 네 범위/미리보기/교환/보고·조건부 schema59를 지원한다. 추가 문자열 잔여는 이3개 지원 부분으로 대체한다. 다른 Project 함수·번호별 기준선/Cost·요약 기간·실제 제품 왕복과 기존 GAP는 남는다. [검증](../validation/xml-string-functions-2026-11-14/README.md). 최종 검증: 일반204/204·299.23초, 대용량 단독1/1·167.80초로205/205, 집중 부분집합36/36·파서886개·형식526파일, 독립 문자열3072건·VBScript3072건·직접32식·XML/XER24값·XLSX74행/PDF20쪽·schema59 실행 패키지를 확인했다.

2026-11-13 후속: [XMH-01~06 XML 추가 수학 함수](../xml-math-functions.md)를 구현했다. Atn/Cos/Exp/Log/Sin/Tan의 라디안·자연로그·근사 계산/소수6자리·정의역/범위·네 범위/미리보기/교환/보고와 조건부 schema58을 지원한다. 앞의 추가 수학 함수 잔여는 이 명시 지원 부분으로 대체한다. 추가 Project/문자열 함수·실제 제품 왕복과 기존 GAP는 남는다. [검증](../validation/xml-math-functions-2026-11-13/README.md). 최종 검증: 일반202/202·309.14초, 대용량 단독1/1·146.42초로203/203, 집중 부분집합34/34·파서882개·형식523파일, 80자리 독립 수치6189건(정확한 반올림 일치6177건/최대 차이0.000001)·XML/XER24값·XLSX72행/PDF19쪽·schema58 실행 패키지를 확인했다.

2026-11-13 후속: [XNT-01~06 XML 숫자 문자열 함수](../xml-number-text.md)를 구현했다. Str/Val/Hex/Oct의 고정 소수6자리·접두부/지수·형식 접미사 지원 부분·32비트 진수·선행 공백과 네 범위/미리보기/교환/보고·조건부 schema57을 지원한다. 추가 문자열/변환 잔여를 이 명시 지원 부분으로 대체한다. 다음 후보는 Atn/Cos/Exp/Log/Sin/Tan 등 수학 함수이며 전체 VBA/NLS·제품 왕복과 다른 GAP는 남는다. [검증](../validation/xml-number-text-2026-11-13/README.md). 최종 검증: 일반200/200·291.99초, 대용량 단독1/1·167.44초로201/201, 집중 부분집합32/32·파서878개·형식520파일, 독립 변환1024/고정점왕복1024/접두부1024·.NET4096·XML/XER22값·XLSX72행/PDF19쪽·schema57 실행 패키지를 확인했다.

2026-11-13 후속: [XTM-01~06 XML 시각 함수](../xml-time-functions.md)를 구현했다. TimeSerial/TimeValue의 16비트/짝수 반올림·상대 넘침/음수 OLE·1899-12-30 기준 Date·명시 시각 입력/마이크로초, 네 범위/미리보기/교환/보고와 조건부 schema56/전체 사본을 지원한다. 아래 TimeSerial/TimeValue 잔여를 이 명시 지원 부분으로 대체한다. 다음 후보는 추가 수학/문자열 함수이며 전체 VBA/NLS·제품 왕복과 다른 GAP는 남는다. [검증](../validation/xml-time-functions-2026-11-13/README.md). 최종 검증: 일반198/198·300.17초, 대용량 단독1/1·159.40초로199/199, 집중 부분집합30/30·파서874개·형식517파일, 독립 생성1024/추출3072·VBScript4111·XML/XER22값·XLSX74행/PDF19쪽·schema56 실행 패키지를 확인했다.

2026-11-13 후속: [XDT-01~06 XML 날짜 함수 확장](../xml-date-functions.md)을 구현했다. DatePart/Weekday와 DateAdd/DateDiff의 연/분기/월/주·월말/윤년/마이크로초·주 규칙, 네 범위/미리보기/교환/보고·조건부 schema55와 기존3인수 읽기를 지원한다. 과거의 다른 날짜 함수 잔여는 이 지원 부분으로 대체한다. 다음 후보는 TimeSerial/TimeValue·추가 수학/문자열 함수이며 전체 VBA/로캘·제품 왕복과 다른 GAP는 남는다. [검증](../validation/xml-date-functions-2026-11-13/README.md). 최종 검증: 일반196/196·285.32초, 대용량 단독1/1·144.34초로197/197, 집중 부분집합28/28·파서870개·형식516파일, 독립 DatePart4608/DateAdd5120/DateDiff8192·.NET17920·XML/XER22값·XLSX70행/PDF19쪽·schema55 실행 패키지를 확인했다.

2026-11-13 후속: [XFS-01~06 XML 계산식 Format·StrConv](../xml-format-strconv.md)를 구현했다. 숫자/날짜/문자열 표시와 Unicode16.0 문자 변환, 명시 LCID1033/1041/1042·조건부 schema54·네 범위/미리보기/교환/보고/보관 사본을 지원한다. 아래의 Format/StrConv 잔여 표기는 이 명시 지원 부분으로 대체한다. 전체 VBA 형식·다른 로캘/코드페이지·실제 제품 왕복과 다른 GAP는 잔여다. [검증](../validation/xml-format-strconv-2026-11-13/README.md). 최종 검증: 일반194/194·295.05초, 대용량 단독1/1·147.78초로195/195, 집중 부분집합26/26·파서866개·형식513파일, 독립 숫자2560/날짜1344/Unicode31158·XML/XER12칸·Windows25건과 NLS 차이1건·XLSX/PDF·schema54 실행 패키지를 확인했다.

2026-11-13 후속: [XTZ-01~06 XML 계산식 시간대 변환](../xml-formula-timezones.md)을 구현했다. 명시 고정 오프셋/버전 포함 IANA2026f·597이름, 누락 거절·중복 실제 시점 선택·정수 마이크로초·오프셋 Text와 UTC 왕복, 네 범위/요약/전파·미리보기/교환/보고와 조건부 schema53을 지원한다. 앞의 시간대 변환 잔여는 이 명시 계산식 지원 부분으로 대체한다. 전역 일정/실적 시간대 자동 추론·미지원 자료 버전/범위·윤초·다른 함수/로캘과 실제 제품 왕복은 잔여다. [검증](../validation/xml-timezone-conversion-2026-11-13/README.md). 최종 검증: 일반192/192·267.51초, 대용량 단독1/1·139.03초로193/193, 집중24/24·파서862개·형식510파일, 고정 TZif 전환92079/월별938484·XML36칸·XER34값·XLSX/PDF와 Windows schema53 실행 패키지/자료 라이선스를 확인했다.

2026-11-13 후속: [XFN-01~06 XML 계산식 추가 함수13개](../xml-additional-functions.md)를 구현했다. 선택·문자열/ASCII·변환·날짜 추출·제곱근과 네 범위·정수/고정점·Null/오류·원본/요약/전파·교환/보고를 연결한다. 활성 함수와 전체 사본은 조건부 schema52, 과거 비활성 원본은 기존 최소 버전으로 보존한다. 앞의 추가 함수 잔여는 이13개 지원 부분으로 대체한다. Format/StrConv·다른 수학/날짜 함수·시간대·로캘·실제 제품 왕복과 다른 GAP는 잔여다. [검증](../validation/xml-additional-functions-2026-11-13/README.md). 최종 검증: 일반 회귀190/190·388.55초, 대용량 단독 CTest180초 시간 초과 후 코드/실행 파일 변경 없이300초 한도 직접 재검증1/1·164.13초로191개 기능 확인(표준 시간 제한 실패1건 유지), 집중22/22, 파서850개·형식505파일, 독립512제곱근/22스칼라·XML53칸/정의21개·XER49값·XLSX48행/PDF16쪽과 Windows schema52 패키지 읽기를 통과했다.

2026-11-13 후속: [XPR-01~06 다일 실적 비례 배분](../xml-multiday-proportional-actuals.md)을 구현했다. 구간 전체 총량 확인·두 가지 분 가중 기준·정수 총량 보존·기존 보고/누계 대사와 척도별 추정 기원을 연결한다. 아래 다일 실적 배분 잔여는 이 명시 검토 지원으로 대체한다. 단위당 반복의 자동 해석·추가 함수/시간대·실제 제품 왕복과 다른 GAP는 잔여다. [검증](../validation/xml-multiday-proportional-2026-11-13/README.md). 최종 검증: 전체188/188·296.93초와 대용량 교환 단독1/1·160.69초로189/189, 집중14/14, 파서838개·형식503파일, 독립 두 기준/16사례·총326u/배분5702센트·척도별 정정 기원·XLSX/PDF와 Windows schema11 패키지 읽기를 통과했다.

2026-11-13 후속: [XWP-01~06 공수 기반 작업 기간 병합](../xml-work-driven-progress.md)을 구현했다. 명시 잔여 공수·기간 보고·계산된 계획 기간 스냅샷과 현재 달력/곡선/날짜 목표에 따른 예측을 연결하며 원래 계획/실제 공수·원가와 정정/미래 원장을 유지한다. 앞 잔여의 공수 기반 작업 병합은 이 지원 부분으로 대체한다. 새 저장 구조 없이 기존 schema24·상한51과 잠금/취소·저장/복구/감사를 사용한다. 다일 실적 비례 배분·추가 함수/시간대·실제 제품 왕복과 다른 GAP는 잔여다. [검증](../validation/xml-work-driven-progress-2026-11-13/README.md). 최종 검증: 전체1차186/187·411.81초(기존 대용량 교환 시간초과1)와 동일 코드 단독재검증1/1·159.95초로187개 확인, 집중12/12, 파서832개·형식495파일, 독립10사례·자원별569분·수신잔여123분/예측37분/공수3501u·원본실제123u·XLSX/PDF와 Windows schema24 패키지 읽기를 통과했다.

2026-11-13 후속: [XAE-01~06](../xml-standard-actual-export.md)의 표준 실적 XML 내보내기를 구현했다. 명시 작업 진척/누계·배정/자원 보고 합계·일별 Type2/6·0/미보고와 누계/배분 대사, 전체 native 이력/계획 보존·새 폴더 게시·재생 검증·취소/변경 검사로 아래의 표준 실적 XML 쓰기 잔여를 이 지원 범위로 대체한다. 자원 없는 배분/현재 없는 배정은 자동 생성하지 않고 거절한다. 다일 배분·공수 기반 작업 기간 병합·추가 함수/시간대·실제 제품 왕복과 나머지 GAP는 잔여다. [검증](../validation/xml-standard-actual-export-2026-11-13/README.md). 최종 검증: 전체185/185·298.87초, 집중10/10, 파서826개·형식493파일, 일별10값·공수26001u·배분4875/누계14000센트·독립 XML/native/MPXJ 및 Windows schema11 패키지 읽기를 통과했다.

2026-11-12 후속: [XAF-01~05](../xml-daily-actual-fragments.md)의 하루 내 여러 명시 실적 구간 합산을 구현했다. 종류별 반열린 구간의 겹침/중복 거절·입력 순서/빈 시간 불변·정수 공수/원가 한도·일자 전체 오류 처리·검토 구간 수와 기존 native 정정/누계/잠금·저장/복구/감사 보고를 연결한다. 앞 잔여의 하루 내 여러 레코드는 이 지원 부분으로 대체한다. 다일 자동 배분·표준 실적 XML 쓰기·공수 기반 작업 기간 병합·추가 함수/시간대·실제 제품 왕복과 나머지 GAP는 남아 있다. 최종 [검증](../validation/xml-daily-actual-fragments-2026-11-12/README.md): 전체183/183·272.33초, 집중8/8, 파서820개·형식489파일, 원본12구간/4종류별 그룹·정수 공수25501u·배분4775센트·독립 XML/native/XLSX/PDF와 Windows schema11 실행 패키지 읽기를 통과했다.

2026-11-12 후속: [XAP-01~06](../xml-period-actual-adoption.md)의 배정·일별 실제 공수/원가 채택을 구현했다. 정확한 소수 공수/통화·0/미보고·수동 native 배정 연결·관측 보존·XML 작업 누계/날짜별 원가 대사·출처별 정정/중복 방지·원자적 batch/마감/취소·Usage/저장/복구/감사 보고를 연결한다. 앞 잔여의 배정/기간 실적은 이 지원 부분으로 대체한다. 다일/하루 내 여러 레코드 합산·표준 실적 XML 쓰기·작업 기간의 공수 기반 병합·추가 함수/시간대·실제 제품 왕복과 나머지 GAP는 남아 있다. 최종 [검증](../validation/xml-period-actual-adoption-2026-11-12/README.md): 전체181/181·254.38초, 집중6/6, 파서814개·형식487파일, 정수 공수25501u·배분4775센트·누계14000센트·독립 XML/native/XLSX/PDF와 Windows schema11 실행 패키지 읽기를 통과했다.

2026-11-12 후속: [XPM-01~06](../xml-progress-merge.md)의 기존 native 문서 XML 작업 진척 병합을 구현했다. 수동 UID/대상 ID 연결·기존/수신 보고 대사·선택적 정정 추가·중복 재수입과 후속 정정 보호·한 revision 반영·마감/취소/변경 검사·저장/복구/감사 보고를 연결한다. 앞 기록의 기존 문서 병합 잔여는 이 지원 부분으로 대체한다. 배정/기간별 실제 공수·원가, 공수 기반 작업, 표준 실적 XML 쓰기·추가 함수/시간대·실제 제품 왕복과 나머지 GAP는 남아 있다. 최종 [검증](../validation/xml-progress-merge-2026-11-12/README.md): 전체179/179·271.78초, 파서808개·형식482파일, 원장1→5→10건·독립 XML/JSON/XLSX/PDF와 Windows UI·schema48 실행 패키지 읽기를 통과했다.

2026-11-12 후속: [XPA-01~06](../xml-progress-adoption.md)의 XML 작업 진척을 새 사본에 명시 채택하는 지원 부분을 구현했다. UID 연결·명시 상태일/작성자/사유·불완전 입력 비추정·반복 준비/재가져오기 정책·원본 재검증·기존 원장 보존과 후속 정정·보고/복구를 연결한다. [검증 기록](../validation/xml-progress-adoption-2026-11-12/README.md). 기존 native 문서로의 ID 매핑/실적 대사·병합, 배정/기간 실제 공수·원가와 실적 XML 쓰기, 추가 Project 함수·시간대 변환과 나머지 GAP는 계속 잔여다. 최종 [검증](../validation/xml-progress-adoption-2026-11-12/README.md): 전체177/177·259.24초, 파서808개·형식481파일, 원장4건/48항목·수식/보고서36칸·정정5건·MPXJ와 Windows UI·기존 schema48 실행 패키지 읽기를 통과했다.

2026-11-12 후속: [XMLE-01~05](../xml-early-late-references.md)의 Early/Late Start/Finish·계획/예측 잔여 구간·완료 제외·논리/자원 순서 가용 판정·두 시계/이정표·그룹 요약과 schema51을 구현했다. 아래의 과거 Early/Late 미지원 표기는 이 지원 부분으로 대체한다. [검증 기록](../validation/xml-early-late-references-2026-11-12/README.md). 다음 기존 잔여 후보는 XML 실적의 명시적 native 원장 채택과 추가 Project 함수·시간대 변환이다. 실제 제품 왕복과 나머지 GAP 잔여는 유지한다. 최종 [검증](../validation/xml-early-late-references-2026-11-12/README.md): 전체175/175·259.48초, 파서800개·형식471파일, 독립 계획 XML60칸/XER48값·예측 보고서60칸·MPXJ와 Windows UI·실행 패키지 schema51 읽기를 통과했다.

2026-11-12 후속: [XMLB-01~05](../xml-baseline-references.md)의 최초 native v1 Baseline Start/Finish/Duration/Work·동결 달력/곡선·네 범위 공수·엄격한 그룹 요약과 schema50을 구현했다. 아래의 과거 Baseline 미지원 표기는 이 지원 부분으로 대체한다. [검증 기록](../validation/xml-baseline-references-2026-11-12/README.md). 다음 계산식 증분은 Early/Late다. 외부 번호별 기준선 슬롯·Cost·요약 기간·실제 제품 왕복과 나머지 GAP 잔여는 유지한다. 최종 [검증](../validation/xml-baseline-references-2026-11-12/README.md): 전체1차171/173·306.03초와 코드 변경 없는 제한 밖 서버 재검증2/2·47.70초로173개를 확인했다. 파서788개·형식467파일, 독립 XML/XER·XLSX/PDF·MPXJ와 Windows UI·실행 패키지 schema50 읽기를 통과했다.

2026-11-12 후속: [XMLR-01~05](../xml-remaining-references.md)의 Remaining Duration/Work·현재 상태일 예측/계획 날짜 투영 분리·네 범위 공수·엄격한 요약과 schema49를 구현했다. 아래의 Remaining 미지원 표기는 이 지원 부분으로 대체한다. 다음 계산식 후보는 Baseline → Early/Late이며, 요약 잔여 기간·실적 XML 자동 채택·제품 왕복과 다른 GAP 잔여는 유지한다.

XMLR 최종 [검증](../validation/xml-remaining-references-2026-11-12/README.md): 전체171/171·280.92초, 파서774개·형식463파일, 독립 XML/XER·XLSX/PDF·MPXJ와 Windows UI·실행 패키지 schema49 읽기를 통과했다.

2026-11-12 후속: [XMLQ-01~05](../xml-actual-quantities.md)의 작업 Actual Duration과 네 범위의 보고 Actual Work 참조·상태일/정정·그룹 공수 요약·schema48을 구현했다. 아래의 과거 실제 기간/공수 미지원 표기를 이 지원 부분으로 대체한다. 다음 계산식 개발 후보는 Remaining → Baseline/Early/Late이며, 요약 실제 기간·실적 XML 자동 채택·실제 제품 왕복과 다른 GAP 잔여 범위는 유지한다.

XMLQ 최종 [검증](../validation/xml-actual-quantities-2026-11-12/README.md): 전체169/169·254.72초, 파서762개·형식461파일, 독립 XML/XER·XLSX/PDF·MPXJ와 Windows800×700·실행 패키지 schema48 파일 읽기를 통과했다.

2026-11-11 후속: [XMLA-01~05](../xml-actual-references.md)의 Actual Start/Finish·상태일/정정·전체 완료 요약·schema47을 구현했다. 실제 기간/공수·Remaining/Baseline/Early/Late 참조와 실적 XML 자동 채택·제품 왕복은 후속 범위다.

2026-11-11 후속: [XMLC-01~05](../xml-calendar-functions.md)의 ProjDateAdd/Sub/Diff·기본/명시 달력·프로젝트 달력 연결·schema46을 구현했다. 근무 달력 함수의 이 지원 부분을 잔여 범위에서 제외한다. 전체 VBA/Project 함수와 실제 제품 왕복은 후속이다.

2026-11-11 후속: [XMLS-01~05](../xml-schedule-references.md)의 계산된 Start/Finish 참조를 구현했다. 활성 계획/예측·배정 소유 작업·WBS/필터 요약·미리보기/보고서와 schema45를 연결한다. 전체 VBA/Project 함수, 근무 달력 함수, 실제 제품 왕복은 잔여 범위다.

2026-11-11 후속: [XMLF-01~05](../xml-external-formulas.md)의 외부 계산식 지원 부분을 구현했다. 네 범위의 typed 결과·지원 함수·안정 바인딩·native/요약/배정 연계·schema44와 검증을 연결한다. 전체 VBA/Project 함수, 계산된 Start/Finish·근무 달력, 실제 제품 왕복은 잔여 범위다.

2026-11-11 후속: [RUP-01~05](../xml-roll-up.md)의 XML 요약 roll-up을 구현했다. WBS/루트 및 작업·자원 분류 그룹에 형식별 8개 방식과 원본 스냅샷 보존·schema43을 적용한다. [검증 기록](../validation/xml-roll-up-2026-11-11/README.md). 외부 식 실행과 실제 제품 재계산 동등성은 잔여 범위다.

2026-11-11 후속: [RDOWN-01~05](../xml-roll-down.md)의 작업/자원→배정 roll-down과 직접 값/명시 미설정 우선·schema42를 구현했다. [전체157/157와 독립 판독/Windows UI 검증](../validation/xml-roll-down-2026-11-11/README.md)을 통과했다. 요약 roll-up·외부 식 실행·제품 실행 동등성은 별도 범위다.

2026-11-11 후속: [XMASK-01~05](../xml-code-masks.md)의 필드 마스크·코드 생성과 WBSMasks/자동 생성·schema41을 구현했다. [전체155/155와 독립 판독/Windows UI 검증](../validation/xml-code-masks-2026-11-11/README.md)을 통과했다. 외부 식/roll-down·제품 실행 동등성과 지원 외 문자 대소문자 규칙은 별도 범위다.

2026-11-10 후속: [XDATE-01~05](../xml-date-time-fields.md)의 Start/Finish·Date 시각·압축 GUID 날짜·schema40 지원을 구현했다. [전체153/153·파서630개·형식423파일 검증](../validation/xml-date-time-fields-2026-11-10/README.md)을 통과했다. 시간대 변환과 실행 의미·제품 왕복은 잔여 범위다.

2026-11-10 후속: [XTYPE-01~05](../xml-additional-field-types.md)의 Flag·Cost·Duration XML 형식, 목록/GUID와 schema39·금액 전환·편집/보고서 지원을 구현했다. 추가 시각 형식과 실행 의미·제품 왕복은 아래 잔여 범위다.

2026-11-10 후속: [LOOKUP-01~05 값 목록·GUID 참조](../xml-lookup-references.md)의 Text/Number/Date 목록, 단일/다중·계층/기본값·네 범위 편집과 native schema38을 구현했다. 추가 형식·외부 식 실행과 실제 제품 왕복은 계속 잔여 범위다.

2026-11-10 후속: [기업·배정 UDF와 XML 프로젝트 필드](../enterprise-assignment-udf.md)의 텍스트/숫자/날짜·외부 ID/GUID, SecondaryPID와 XER 배정 소유/결합, 배정 편집·보고·schema37 저장/복구를 구현했다. [전체147/147·582개 파서 seed·독립 XML/XER·XLSX/PDF·Windows 화면 검증](../validation/enterprise-assignment-udf-2026-11-10/README.md)을 따른다. 값 목록/GUID 값 참조·외부 계산식 실행·추가 형식과 실제 제품 왕복은 계속 남아 있다.

점검일 2026-11-06. 현재 작업 폴더의 미커밋 구현을 포함해 [앞 점검](18-remaining-priorities-review.md)·GAP-01~14와 domain/application/calculation/persistence/UI 및 CTest를 다시 대조했다. 과거 스냅샷의 ‘후속’을 현재 미구현으로 자동 판정하지 않는다.

## 이번 우선순위

사용자가 지정한 다음 native 증분인 **FixedWork/FixedUnits 곡선 역산**을 구현했다. [WCI-01~05](../work-contour-inverse.md)의 공수/기간/배정률 유지값, 실제 달력 적분·정수 공수, 비단조 후보, 일괄/worker 편집·동결 보존, schema24·저장/복구·Usage/원가/출력과 [검증](../validation/work-contour-inverse-2026-11-06/README.md)을 따른다. 기간별 목표 공수 하위 창은 기존 FixedDuration 범위이며, 고정 공수/단위는 작업 기간/공수 입력과 곡선 편집에서 역산한다. 관측 실적을 곡선으로 추정하지 않는다.

이미 구현한 자원 보기·숫자 계산식·날짜별 잔여 입력, 날짜 고정 수요의 자원 여유, XML/XER 스칼라 UDF 매핑, 조직/위험/자동화와 선택 서버 인증/실시간/HTTP/주기 실행은 해당 계약의 지원 부분을 완료 상태로 유지한다. 기능군 전체나 외부 제품 동등성의 완료를 뜻하지 않는다.

최종 전체 회귀123/123(523.44초), 집중5/5, 독립 근무분 oracle80사례와 파서266 seeds를 통과했다. Windows 기본/800×600 UI, schema24 저장/복구·독립 XLSX/PDF 판독, Qt/MinGW가 없는 PATH에서의 실행 패키지 파일 읽기도 확인했다. 원시 로그와 재현 명령은 위 검증 문서에 남겼다.

## 남은 구체 항목과 순서

2026-11-09 다음 증분: [MPP 수동 정규 계획 곡선 읽기](../mpp-manual-curves.md)를 구현했다. 실제/초과근무 제외·분 경계/기간 창 투영과 원본/총공수/0구간·protocol2/기존 schema36을 연결했다. [전체 회귀·직접 source/독립 자원분 대조](../validation/mpp-manual-curves-2026-11-09/README.md)를 따른다. 최종 전체145/145(250.91초), 파서566개·형식417파일, 직접 source/10,080자원분·XML/XER reader와 Windows 기본/800×600 검증을 통과했다. 다음 독립 native 개발 후보는 기업/배정 UDF·XML 프로젝트 필드다.

2026-11-09 후속: 사용자 지정 [자원 생산성·외부 고유 곡선 교환](../resource-productivity-curves.md)을 구현했다. 물량/생산성→계획 공수와 작업 유형별 명시 유지값, XML 계획 시간대별 공수·XER21지점 원본 정의와 손실 진단, schema36·검산/확인/undo·보고서를 연결한다. [전체 회귀·독립 검증](../validation/resource-productivity-curves-2026-11-09/README.md)을 따른다. 최종 전체143/143(223.86초), 파서566개·형식416파일·독립 수치/교환/보고서와 Windows 기본/800×600 검증을 통과했다.

2026-11-08 후속: 사용자 지정 [FixedWork/FixedUnits 날짜 고정 잔여 목표](../dated-effort-targets.md)를 구현했다. 명시 잔여 총공수와 모든 자원의 고정 날짜 목표를 보존하고 미입력 날짜의 기존 곡선으로 완료를 역산한다. 기간 기준은 잔여 기간을 유지한다. schema35·저장/복구·채택/시나리오, 독립72개 분 단위/3개 전수 배치 여유·원장/기준선 보존과 Windows 기본/800×600·보고서 판독을 [검증 기록](../validation/dated-effort-2026-11-08/README.md)에 남긴다.

2026-11-08 후속: 사용자 지정 [프로젝트 전체 통화 전환](../project-currency-conversion.md)을 구현했다. 명시 환율/기준일·정확/최소 단위 반올림, 현재/기준선/시나리오·관측 원장·기간 마감/외부 EVM·XER 재무 투영을 함께 환산하고 schema34 전후 원본을 보존한다. raw XER·기존 ID 이관/일정 원본과 수량/날짜/작성 정보를 유지한다. [전체 회귀·독립 수치/전체 필드·출력·Windows 검증 기록](../validation/project-currency-2026-11-08/README.md)을 따른다.

2026-11-08 후속: 사용자 지정 [MPP 파일 읽기](../mpp-import.md)를 구현했다. MPXJ16.10.0/Java21의 오프라인 별도 프로세스와 native XML 계획 투영·손실 검토·원본/진단 동반 새 사본 열기를 지원한다. 실제 MPP8/9/12/14·암호 거절과 원본 보존, 대응 소스/라이선스 지문·배포 검사를 포함한다. 전체139/139(219.71초), 파서478개·형식390파일, 직접 객체128개 대조·기본/800×600 UI와 Java/Python/Qt 없는 PATH 실행을 통과했다. [실행 근거](../validation/mpp-import-2026-11-08/README.md)를 따른다.

2026-11-08 후속: 사용자 지정 [관측 원장 ID 이관](../ledger-id-migration.md)을 구현했다. 새 ID/미관측 대상의 명시 일대일 연결과 원장·기준선·시나리오·마감/외부 EVM·XER 참조 이관을 지원한다. 수치·작성/정정 정보를 유지하고 schema33 전체 전후 원본을 재실행해 대조한다. 전체137/137(172.66초), 파서478개·형식384파일, 독립16,239개 필드/MPXJ strict reader·Windows 기본/800×600을 통과했다. [실행 근거](../validation/ledger-id-migration-2026-11-08/README.md)를 따른다.

2026-11-08 후속: 사용자 지정 [새 XER 일정·WBS·달력 채택](../xer-schedule-adoption.md)을 구현했다. 명시 작업 연결/신규 추가·범위별 채택과 날짜/계층/달력 정의 전후 검토를 제공한다. 현재 원장·기준선·마감/재무 원본을 보존하고 schema32 이력을 재실행해 검증한다. 전체136/136(184.47초), 파서458개·형식376파일, 독립133개 검사/16개 날짜·MPXJ strict 계획/계층/달력과 Windows 기본/800×600을 통과했다. [실행 근거](../validation/xer-schedule-adoption-2026-11-08/README.md)를 따른다.

2026-11-08 후속: 사용자 지정 [XER 통화·자원 종류 전환](../xer-source-conversion.md)을 구현했다. 새/이전 원본의 명시 환율·종류별 공수비·기준일/근거와 전후 미리보기·저장 이력 조회를 제공한다. 원본·현재 계획/관측 원장을 보존하고 schema31 재실행 검증과 기존 병합 검토로 연결한다. 전체135/135(160.15초), 파서424개·형식366파일, 독립760개 수치 사례/844개 검사와 MPXJ strict reader·Windows 기본/800×600을 통과했다. [실행 증거](../validation/xer-source-conversion-2026-11-08/README.md)를 따른다.

2026-11-08 후속: 사용자 지정 [사용자 정의 기간·기간 마감](../custom-financial-periods.md)을 구현했다. 기준선별 비중첩 기간·작업별 기간 PV/EV와 native 경계/날짜배분 대사, 당시 지식 revision 마감·과거 보고·최신 기간부터 재개방/재마감·실적 날짜 잠금·schema30 저장/복구를 지원한다. 전체134/134(163.78초), 집중29/29, 파서390개·형식362파일, 독립 수치/실제 XLSX/PDF·Windows UI와 기존 native/XER reader를 통과했다. [실행 증거](../validation/custom-financial-periods-2026-11-08/README.md)를 따른다.

2026-11-08 후속: 사용자 지정 [외부 XER EV/PV native 채택](../external-xer-evm-adoption.md)을 구현했다. 기준선/작업/재무기간·명시 개시 누계를 확인해 선택 지표를 대체하고 재채택/철회·불변 이력/schema29·원본/AC/물리 진척 보존을 지원한다. native 원가/기간 EVM과 일반 보고서가 같은 정책을 사용하며, 새 원본 병합/재연결이 기존 채택값을 자동 변경하지 않는다. 전체132/132(167.06초), 집중27/27(34.64초), 파서348 seeds·형식350파일, 독립2,106개 수치·XLSX/PDF·Windows UI와 기존 native/XER reader를 통과했다. [실행 증거](../validation/external-xer-evm-2026-11-08/README.md)를 따른다.

2026-11-08 후속: 사용자 지정 [native 기간 EVM](../native-period-evm.md)을 구현했다. 동결 기준선·native 원장의 일/주/월 PV/EV/AC 증감·기간 CV/SV/CPI/SPI, 마감 누계 EAC/ETC와 현재 날짜 배분 대사를 구분한다. 원본·원장/schema를 유지하고 누락 개시·음수 정정·기간 경계/상태일·WBS/Steps를 검증했다. 전체130/130(167.66초), 집중21/21(26.85초), 파서326 seeds·형식336파일, 독립810개 수치·실제 XLSX/PDF·Windows 기본/800×600을 통과했다. [실행 증거](../validation/native-period-evm-2026-11-08/README.md)를 따른다.

2026-11-08 후속: 사용자 지정 [새 XER 버전·삭제 ID 재연결](../xer-source-relink.md)을 구현했다. 비동기 파일/다중 프로젝트 선택, 새/이전 ID의 명시 연결과 안전한 제외, 재무 병합 검토·원본/계획/원장/AC 불변·schema28 및 후속 편집을 지원한다. 전체128/128(161.21초), 집중15/15(20.79초), 파서326 seeds·형식327파일, Windows 기본/800×600과 독립 SHA256/전체 native 동일성/ID oracle/TSV/MPXJ·기존 교환/공개7개 표본을 통과했다. [실행 증거](../validation/xer-relink-2026-11-08/README.md)를 따른다.

2026-11-07 후속: 사용자 지정 [XER 원본·현재 원장 병합](../xer-ledger-merge.md)을 구현했다. 현재 원장·누계 AC 불변, 비겹침 잔량의 자동 연결, 모호한 기간/정규·초과 누계의 명시 선택, schema27·당시 최신 기록/AC 대사와 후속 편집을 지원한다. 전체127/127(134.30초), 관련 집중14/14(15.80초), 파서312 seeds·형식319파일, Windows 기본/800×600 및 독립 native 동일성/SHA256/상태일·revision 참조/TSV/MPXJ 검사를 통과했다. [실행 증거](../validation/xer-merge-2026-11-07/README.md)를 따른다.

2026-11-07 후속: 사용자 지정 [XER 집계 편집·날짜별 배분](../xer-aggregate-editing.md)을 구현했다. 원본 불변·정정 이력/schema26, 누계/기간 편집·미지정 잔량의 기간 지정·명시 날짜 비중, native AC/원장 원자적 대사·조건부 XER 재내보내기를 연결했다. 최종 전체126/126(140.54초), 관련 집중13/13(14.73초), 파서298 seeds·형식313파일과 Windows 기본/800×600·독립 Decimal/native SHA256/TSV/MPXJ를 검증했다. [실행 기록](../validation/xer-allocation-2026-11-07/README.md)을 따른다. 현재 원장과 원본의 자동 병합·native 기간 EVM·MPP와 실제 P6 실행은 후속으로 유지한다.

2026-11-07 후속: [XER 재무기간·누계 스냅샷](../xer-financial-snapshots.md)의 native schema25 보존·조회·일치 판정·재내보내기를 구현했다. 집계 편집/일별 배분·자동 병합·native 외부 EVM/MPP와 실제 제품 왕복은 후속으로 유지한다. [검증 기록](../validation/xer-financials-2026-11-07/README.md)을 따른다.

이번 증분은 최종 전체125/125(153.51초), XER/파서/공개 표본 집중10/10(17.99초), 파서284 seeds, 형식305파일과 Windows 기본/800×600 조회·TSV/Decimal/native·투영 SHA256/MPXJ 독립 판독을 통과했다. native 입력 진단 순서 호환과 서버 long-poll 테스트의 준비 순서를 수정한 첫/두 번째 실행 기록도 보존한다.

2026-11-06 후속: 사용자 지정 다음 증분인 **XER 실적·원가 상세 교환**의 [XAC-01~06 지원 부분](../xer-actual-cost-exchange.md)을 구현했다. 상태일/실제 날짜·누계 AC/단가·직접비·일 단위 재무기간 명세와 다중 프로젝트 참조를 연결한다. 여러 날짜 집계의 native 원장화·날짜 없는 누계 공수의 배분·외부 EVM과 실제 P6 실행, MPP reader까지 완료한 것으로 확대하지 않는다.

이 증분은 전체124/124(158.52초), XER/파서 집중8/8·272 seeds, 형식298파일, Windows 기본 UI·TSV/Decimal/native SHA256/MPXJ 독립 판독을 통과했다. [실행 증거](../validation/xer-actual-cost-2026-11-06/README.md)를 따른다.

| 우선순위 | 잔여 항목 | 판정·선행조건 |
|---|---|---|
| P0/P1 · GAP-02/05 | 실제 MS Project/P6 재계산·재저장·옵션/빌드별 비교와 손실 수용 | 이번에도 제품 실행 검증은 미실행. 독립 reader·native 수치·파서가 대체하지 않는다. 실행 환경/실제 사례를 별도로 확보해야 한다. |
| P2 · GAP-09 | 외부 제품 실행 검증·다른 제작/필드 조합 | XAC/XFS/XAE/XFM/XRL·native 기간 EVM/외부 EV/PV 채택·사용자 기간 마감, XER 통화/종류 전환·일정/WBS/달력 채택·관측 원장 ID 이관·MPP 읽기와 [프로젝트 전체 통화 전환](../project-currency-conversion.md)의 계약 범위를 지원한다. 실제 P6/MS Project 재계산·재저장과 다른 제작/필드 조합·OS/스토어 실행 검증은 남아 있다. |
| P2 · GAP-04/11 | 생산성·외부 곡선의 추가 확장 | 물량 산정·기본/사용자 곡선의 XML/XER 교환은 위 계약으로 구현. 실제 MS Project/P6의 달력/보간/재저장 동등성과 MPP 정규 계획 수동 곡선은 위 계약으로 지원한다. 실제/초과근무 혼합 분포·임의 달력의 전체 복원은 추가 범위다. |
| P2 · GAP-03/09 | 추가 형식·외부 계산식 의미 | 기업/배정·XML 프로젝트 스칼라와 LOOKUP-01~05의 값 목록/GUID·다중 값 지원 부분을 구현했다. Flag/Cost/Duration은 XTYPE-01~05로 확장했다. Start/Finish·Date 시각은 XDATE-01~05로 확장했다. 마스크·코드 생성은 XMASK-01~05로 확장했다. 배정 roll-down은 RDOWN-01~05로 확장했다. 시간대 변환·단위/코드 실행 의미·요약 roll-up/외부 식 실행과 실제 제품 왕복은 남아 있다. |
| P2 · GAP-10 | 자원/원가를 직접 갖는 LOE/WBS Summary 지원 활동 | 현재 지원 구간·Steps와 구분한다. 기간/선행/실제·집계 중복과 직접 배정 의미를 정해야 한다. |
| P2 · GAP-11 | 초과근무·시간축 자재 입고·비용 최적화·자원 순서 재최적화 | 현재 자재/역할/비용계정·공유 부하·현재 인계 순서 여유의 확장이다. 시간축 단위와 보존·최적성/실행 가능 증거가 필요하다. |
| P2 · GAP-12 | 통합 일정 편집·여러 파일 일괄 날짜 채택 | 현재 읽기 전용 통합 일정·공유 평준화와 분리한다. 파일 간 잠금/원자성·실패 복구가 선행한다. |
| P3 · GAP-13/14 | 승인/타임시트, 일반 상관·날씨/자원/원가 확률, 서버 편집 확대·SSO/MFA·cron/HA | 현재 로컬 감사·삼각 분포 계획 CPM·CAS 동기화/간격 실행의 지원 부분을 보존하고 독립 증분으로 다룬다. |
| 운영/외부 | macOS/다른 OS·물리 프린터·스토어/실제 서버 배포·보안 감사·현장 수용 | Windows 로컬 회귀와 별도의 환경별 실행 증거로 판정한다. |

개발되지 않은 항목을 삭제하거나 근거 없는 전체 완성도 비율을 제시하지 않는다. 외부 원본337행/39개 OPEN 확인과제도 이 증분만으로 해결 처리하지 않는다.
