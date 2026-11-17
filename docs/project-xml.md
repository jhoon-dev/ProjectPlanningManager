# Microsoft Project XML 교환 계약

2026-11-16 후속: [PPMResourceLoad/PPMRemainingAvailability](remaining-resource-availability.md)의 계획 문맥 숫자 결과와 확장 식을 교환한다. 지원 계획으로 재계산하며 공급 자료를 정확히 읽지 못하면 새 식도 비활성 보존한다. 현재 예측 문맥은 native/source.ppm을 따른다.

2026-11-16 후속: [부분일·무기한 가용기간](resource-availability.md)과 자원 AvailableFrom/To를 명시 분 단위로 채택한다. 새 native 자료는 날짜/기간/기본값/전체 경계를 비중첩 전체 기간표로 출력한다. 분 미만 경계·겹침/잘못된 자료는 진단·원본 보존을 따른다.

2026-11-15 후속: [Units·가용률 참조](xml-unit-fields.md)와 명시 날짜 전체 AvailabilityPeriods 교환을 지원한다. 중복/부분일/무기한·자원 경계를 정확히 옮기지 못하면 해당 예외를 적용하지 않고 날짜 조회식을 비활성 보존한다. 기간 밖 native 기본 가용률 적용과 외부 reader 해석 차이를 진단한다.

2026-11-15 후속: [native 기준선 상세 시간분포](native-baseline-timephased.md)를 일반/실적 XML에 출력한다. 동결 실행 공수·일별 원가를 작업/프로젝트/WBS/자원/배정 부모의 표준 Type/UID로 기록하고 재채택/병합에는 조건부 schema77을 연결한다.

2026-11-15 후속: [기존 문서에 XML 기준선 병합](xml-baseline-merge.md)을 별도 파일 메뉴로 지원한다. 명시 객체 연결/제외·번호 전체 교체 확인·번호별 출처/달력·전후 이력을 보존하고 조건부 schema76과 일반 저장/복구에 연결한다.

2026-11-15 후속: [외부 번호별 기준선](xml-baseline-exchange.md)을 별도로 검토해 새 사본에 채택하며 일반/표준 실적 XML에 표준 Baseline 요소를 함께 출력한다. 원본 명시 값·0/미설정·지원 표시/시간분포와 native 승인 이력을 구분하고 게시 전 원본을 대조한다.

2026-11-15 후속: [SLN·SYD](xml-depreciation-functions.md)의 감가상각 Number·정의/현재 값·재수입 실행과 조건부 schema72를 지원한다. 실제 원가 원장을 자동 기입하지 않는다.

2026-11-14 후속: [Partition](xml-partition-function.md)의 구간 Text·정의/현재 값·재수입 실행과 조건부 schema71를 지원한다. 구간 앞뒤 공백을 보존한다.

2026-11-14 후속: [RGB·QBColor](xml-color-functions.md)의 색상 정수·정의/현재 값·재수입 실행과 조건부 schema70를 지원한다. 색상 번호는 필드 값이며 화면 서식을 변경하지 않는다.

2026-11-14 후속: [FormatNumber/Percent/Currency](xml-numeric-display.md)의 정확한 숫자 표시·정의/현재 값·재수입 실행과 조건부 schema69를 지원한다. Currency는 고정 $ 표시이며 환산이 아니다.

2026-11-14 후속: [FormatDateTime](xml-format-datetime.md)의 고정 en-US 날짜·시각 표시5종·정의/현재 값·재수입 실행과 조건부 schema68을 지원한다.

2026-11-14 후속: [MonthName·WeekdayName](xml-date-name-functions.md)의 고정 en-US 월·요일 이름/약어·정의/현재 값·재수입 실행과 조건부 schema67을 지원한다.

2026-11-14 후속: [AscW·ChrW](xml-unicode-functions.md)의 BMP 문자 코드 변환·정의/현재 값·재수입 실행과 조건부 schema66을 지원한다. native/XML에 표현할 수 없는 문자는 명시 오류다.

2026-11-14 후속: [Like](xml-like-operator.md)의 식·참조·현재 값과 XML 재수입 실행을 지원한다. 활성 새 연산/전체 사본은 schema65이며 비활성 원문은 기존 계약을 유지한다.

2026-11-14 후속: [Eqv/Imp](xml-logical-operators.md)의 정의·참조·현재 값과 XML 재수입 실행을 지원한다. 활성 새 연산/전체 사본은 schema64이며 비활성 원문은 기존 계약을 유지한다.

2026-11-14 후속: [거듭제곱·정수 나눗셈](xml-arithmetic-operators.md)의 식·참조·현재 값과 XML 재수입 실행을 지원한다. 활성 새 연산/전체 사본은 schema63이며 비활성 원문은 기존 계약을 유지한다.

2026-11-14 후속: [Baseline Cost](xml-baseline-cost.md)의 최초 native v1 동결 원가를 네 범위·요약/전파·식/정적 값으로 내보낸다. 바인딩/전체 사본은 schema62이며 native 기준선 자체는 XML에 없으므로 재수입 후 값은 미설정이다.

2026-11-14 후속: [ProjDateValue/ProjDateConv/ProjDurConv](xml-project-conversion-functions.md)의 정의·참조 이름·현재 값·재가져오기 재계산을 지원한다. ISO/NA·고정 날짜/기간 프로필이며 활성 식/전체 사본은 schema61이다.

2026-11-14 후속: [CDec/CSng/CVar](xml-conversion-functions.md)의 정의·참조 이름·현재 값·재가져오기 재계산을 지원한다. native 고정점·binary32 변환과 CVar 형식/기간 보존을 사용하며 활성 식/전체 사본은 schema60이다.

2026-11-14 후속: [Replace/InStrRev/StrReverse](xml-string-functions.md)의 정의·참조 이름·현재 값과 재가져오기 재계산을 지원한다. BMP 위치·ASCII 비교·함수별 Null 구분과 native 활성 식/전체 사본 schema59를 사용한다.

2026-11-13 후속: [Atn/Cos/Exp/Log/Sin/Tan](xml-math-functions.md)의 정의·참조 이름·현재 값과 재가져오기 재계산을 지원한다. 라디안·자연로그·부동소수 근사/소수6자리이며 정의역/범위 오류를 스냅샷으로 대체하지 않는다. native 활성 식/전체 사본은 schema58이다.

2026-11-13 후속: [Str·Val·Hex·Oct](xml-number-text.md)의 식과 현재 값을 교환한다. 고정 소수6자리·지원 형식 접미사·32비트 진수·선행 공백을 진단/스냅샷에 연결하며 활성 식/보관 사본은 schema57이다. 단위나 통화를 추측해 변환하지 않는다.

2026-11-13 후속: [TimeSerial·TimeValue](xml-time-functions.md)의 지원 식과 현재 Date 결과를 교환한다. 기준일1899-12-30은 시각 표현용 사용자 필드이며 계획/실제 작업 날짜가 아니다. 고정 입력 문법·OLE 음수 규칙·소수6자리를 진단에 기록하고 활성 식/보관 사본에는 schema56을 사용한다.

2026-11-13 후속: [날짜 함수 확장](xml-date-functions.md)의 DatePart/Weekday·DateAdd/DateDiff 확장 간격과 주 옵션을 가져오기/내보내기에 연결했다. 고정 Gregorian 민간 시각·월말/윤년·경계 수를 사용하며 PC 지역 설정이나 근무일로 추정하지 않는다. 정의/원래 입력은 보존하고 새 기능 저장은 schema55이다.

2026-11-13 후속: [Format·StrConv](xml-format-strconv.md)의 지원 식과 계산된 Text 스냅샷을 가져오기/내보내기에 연결한다. 고정 en-US Format·명시 LCID PPMFormat/StrConv·Unicode16.0 지원 부분을 진단에 기록한다. 외부 전체 VBA/NLS 동등성은 미검증이며 미지원 식은 비활성 보존한다. 활성 현재/보관 사본은 schema54이다.

2026-11-13 후속: [계산식 시간대 변환](xml-formula-timezones.md)을 PPM 확장으로 지원한다. 명시 UTC 오프셋 또는2026f:Asia/Seoul 같은 버전 포함 IANA 이름으로 변환하며 PC 시간대를 추측하지 않는다. 일반 날짜 결과/요소는 시간대 표지가 없고, 오프셋은 PPMTimeZoneText의 Text 결과·식/원본 입력에 보존한다. Project 표준 함수·일반 XML 날짜의 자동 오프셋 해석은 아니다. 활성 식/전체 사본은 schema53, 비활성 원본과 기존 식의 최소 writer는 유지한다.

2026-11-13 후속: [추가 XML 함수13개](xml-additional-functions.md)를 지원한다. Choose·InStr/StrComp·LTrim/RTrim·Space/String·Asc/Chr·CByte/CCur·DateValue·Sqr를 네 범위에서 실행하고 원본 바인딩/현재 값·재수입·XER 정적 값과 연결한다. BMP/ASCII/ISO·정수/고정점·전체 인수 평가의 명시 부분이며 전체 VBA/로캘 동등성은 미검증이다. 활성 새 함수의 native writer/지원 상한은52다. 기존 함수와 비활성 보존 식의 이전 최소 writer는 유지한다.

2026-11-13 후속: [다일 실적 비례 배분](xml-multiday-proportional-actuals.md)을 별도 검토로 제공한다. Value를 구간 전체 총량으로 확인한 Type2/6만 경과분 또는 현재 작업·자원 달력 공통 근무분으로 배분한다. 자동 단위당 반복 해석을 하지 않으며 기본 일별 채택은 그대로다. 날짜별 가중치/정수량·추정 기원을 native 사유와 검토 기록에 보존하고 표준 XML 영수증에 척도별 원래 보고 ID를 추가한다. XML 단독 재수입은 추정 구분을 복원하지 않는다.

2026-11-13 후속: [공수 기반 작업의 기간 진척 병합](xml-work-driven-progress.md)을 지원한다. 현재 native 공수 기준 대상에는 명시 Task RemainingWork를 함께 채택하고, 기간 보고와 현재 달력/배정/곡선으로 재계산하는 예측을 구분한다. 기본 계획/새 사본 가져오기·기간 기준 대상·실제 원장 채택의 범위는 유지한다.

2026-11-13 후속: [표준 실적 포함 내보내기](xml-standard-actual-export.md)를 별도 메뉴로 제공한다. 현재 상태일의 명시 작업 진척·누계 AC·배정별 실제 공수/원가와 일별 Type2/6을 출력한다. 보고 작업 Duration은 실제+잔여 기간이고 전체 원래 계획/이력은 source.ppm에 보존한다. 0/미보고와 누계/배분을 구분하며 기본 계획 전용 내보내기는 유지한다. 실제 제품의 재계산/재저장 호환은 미검증이다.

원가 단위 보정: ActualCost와 Type6은 표시 통화 금액×100의 XML wire 값을 사용한다. 기존 일별 채택 초안의 표시 금액 해석도 이 규칙으로 수정했다. 이전 native 값은 자동 수정하지 않는다. 이전 XML 원가 채택 문서는 원본·대사와 비교해 명시 정정한다. 과거 검증 폴더는 당시 결과로 보존하며 최신 지원 판단은 XAE 검증을 따른다.

2026-11-12 후속: [일별 실제 공수/원가의 여러 구간 합산](xml-daily-actual-fragments.md)을 지원한다. 같은 배정/날짜/종류에서 겹치지 않는 명시 구간을 합산하고 겹침·오류·한도 초과는 일자 전체를 거절한다. 원본과 구간 수/합계 대조 후 기존 일별 원장으로 반영하며 다일 비례 배분은 하지 않는다.

2026-11-12 후속: [배정 일별 실제 공수·원가 채택](xml-period-actual-adoption.md)을 별도 메뉴로 제공한다. 명시 일별 Type2/6과 native 배정·작업 누계 AC를 검토해 선택 반영하고, 누계/초과/요약 값을 중복 합산하지 않는다. 기본 계획 가져오기와 계획 기준 내보내기는 그대로 사용한다.

2026-11-12 후속: [현재 native 문서의 작업 진척 병합](xml-progress-merge.md)을 별도 파일 메뉴로 제공한다. 수동 UID→작업 ID 연결과 현재/수신 값 대사 후 선택한 행만 정정 원장으로 추가한다. 현재 상태일 유지·동일 출처 중복 방지·마감/worker 원자성·기존 저장/복구를 사용하며 원본 XML은 함께 보관한다.

2026-11-12 후속: [작업 진척 새 사본 채택](xml-progress-adoption.md)을 선택할 수 있다. 기본 계획 변환은 유지하며 ActualStart/Finish·명시 ActualDuration/RemainingDuration만 새 사본 원장에 채택한다. 현재 문서의 기존 원장과 병합하지 않고 실제 공수/원가·진척률을 추정하지 않는다. 원본/매핑/채택 메타데이터를 동반 폴더에 보존하고 게시 전 재생 검증한다.

2026-11-12 후속: [Early/Late 참조](xml-early-late-references.md)의 정적 결과와 식 정의를 현재 계획 기준으로 교환한다. 보고서의 상태일 예측·미완료 잔여 구간과 구분하고, 미산정 Late를 날짜로 추정하지 않는다. 실제 제품 재계산 동등성·진척 원장 자동 채택은 지원 범위에 포함하지 않는다.

2026-11-12 후속: [최초 기준선 수식 참조](xml-baseline-references.md)는 최초 native 기준선(v1)의 동결 날짜·기간·배정 공수를 정적 사용자 필드 결과로 내보낸다. 현재 계획/화면 비교 기준선 선택과 독립이며 기준선 자체와 번호별 외부 슬롯은 XML에 매핑하지 않는다. 원본은 source.ppm에 보존하고 재가져오기 후 사유를 가진 Null이 되는 경계를 진단한다.

2026-11-11 후속: [Actual Start/Finish 계산식 참조](xml-actual-references.md)를 지원한다. 정적 값은 상태일 기준 원장에서 계산하며 원장은 source.ppm에 보존한다. XML 재가져오기에서는 원장을 자동 채택하지 않아 활성 실적 참조가 미설정일 수 있음을 진단한다.

2026-11-11 후속: [근무 달력 함수](xml-calendar-functions.md)를 실행한다. 원본 프로젝트 CalendarUID를 프로젝트/요약 수식의 기본값으로 보존하고 작업/자원별 달력과 함께 교환한다.

2026-11-11 후속: [XML 계산식의 계산된 일정 참조](xml-schedule-references.md)를 추가했다. 화면/보고서는 활성 일정, XML의 정적 값은 현재 계획을 사용한다. 아래 과거의 계산된 Start/Finish 제외 표기를 이 지원 범위로 대체한다.

2026-11-11 후속: [Formula 실행](xml-external-formulas.md)은 지원식을 활성화하고 미지원 식을 비활성 보존한다. 참조를 실제 출력 FieldName에 맞춰 재작성하며 원본/현재 값을 구분한다. XML 재가져오기와 native의 실행 설정 보존 범위가 다름을 진단한다.

2026-11-11 후속: [CalculationType/RollupType 요약 roll-up](xml-roll-up.md)을 보존·실행한다. UID0의 Task/Project 값과 WBS 명시 값, 원본 스냅샷/자동 결과를 분리한다. 활성 요약은 현재 값을 XML로 내보내고 차이/오류를 진단한다.

2026-11-11 후속: [AutoRollDown/SecondaryPID](xml-roll-down.md)를 작업·자원→배정 연결로 보존·실행한다. 직접 값/명시 미설정 우선과 자체 XML 왕복의 자동 연결 유지를 지원한다. 외부 식 실행은 별도 범위다. 요약 roll-up은 위 후속 계약을 따른다.

2026-11-11: [XML 코드 마스크·생성](xml-code-masks.md)을 지원한다. OutlineCode Masks/AllLevelsRequired와 WBSMasks의 GenerateCodes·VerifyUniqueCodes·Prefix, WBS/WBSLevel을 교환한다. 마스크에 맞는 코드 생성과 기존 값/참조 보존을 연결한다.

2026-11-10 후속: [Start·Finish와 Date 시각](xml-date-time-fields.md)을 추가했다. 기존 날짜 전용 표현과 함께 초·소수 초·자정/미설정을 보존한다. 시간대 없는 일정 시각이며 오프셋은 거절한다. 아래 과거의 Start/Finish·Date 시각 제외 표기를 이 계약 범위로 대체한다.

2026-11-10 추가: [Flag·Cost·Duration](xml-additional-field-types.md)의 공식 로컬/기업 필드와 형식별 값·목록/GUID 참조를 지원한다. Cost 배율·통화 정밀도와 Duration의 분 정규화·경과/추정 의미를 구분한다. 아래 이전 세 형식 제외 표기는 이 계약 범위로 대체한다.

2026-11-10 후속: [값 목록·GUID 참조](xml-lookup-references.md)는 네 객체 범위의 Text/Number/Date 목록, 단일/다중 선택·계층·기본값·명시 직접 값을 지원한다. 이전 목록 제외 표기를 이 계약 범위로 대체한다. 외부 식 실행·추가 형식과 실제 제품 왕복은 별도 범위다.

2026-11-10 확장: [기업·배정 UDF와 프로젝트 필드](enterprise-assignment-udf.md)의 스칼라·외부 신원/요약 작업·배정 소유 매핑을 추가한다. 기업 서버 등록·값 목록·외부 식 실행은 별도 범위다.

2026-11-09: [자원 생산성·외부 고유 곡선 교환](resource-productivity-curves.md)으로 MS 기본 WorkContour와 Type1 계획 시간대별 공수의 수입/내보내기를 확장했다. 실제공수와 계획을 구분하고 수동/변경 구간은 명시 계획 값으로 교환한다. 생산성 산정 근거는 native에 보존한다.

2026-11-08 [MPP 읽기](mpp-import.md)는 별도 메뉴의 MPXJ reader와 이 XML 지원 부분 검증을 결합한다. 원본 MPP를 보관하고 기간 기반 계획의 새 사본을 연다. MPP 쓰기나 Microsoft Project 재계산 동등성은 제공하지 않는다.

[사용자 기간·기간 마감](custom-financial-periods.md)의 정의/마감/재개방 이력은 XML에 매핑하지 않고 source.ppm에 보존한다. 내보내기 진단에서 이 보존 경계를 안내한다.

2026-11-06 [작업·자원 UDF 매핑](external-udf-mapping.md)을 추가했다. Text/Number/Date 스칼라와 목록 표시 이름·계산식 결과를 미리보기 후 변환한다. 아래 과거의 사용자 필드 미지원 표기는 당시 범위이며 현재 제한은 새 계약을 따른다. 기업/프로젝트 필드·값 목록/GUID 실행 의미와 실제 Microsoft Project 왕복은 미지원/미검증을 유지한다.

2026-11-03 기간별 실제공수·원가 원장은 이 교환 형식에 매핑하지 않으며 손실 진단을 추가한다. `source.ppm`에는 schema 11로 원본/정정·누계 링크를 보존한다. 원장이 없는 사본은 기존 schema 9/10이다. [기간 실적 계약](period-actuals.md)을 따른다.

2026-11-02 분류 데이터: native 작업 사용자 필드·계층형 코드·저장된 보기는 이 교환 형식에 매핑하지 않는다. 내보내기 미리보기/변환 보고에서 손실을 명시하고 `source.ppm`에는 사전·값·보기와 이력을 모두 보존한다. 분류 데이터가 있으면 native 사본은 schema 10, 없으면 schema 9다. 외부 파일만 전달하면 이 데이터는 전달되지 않는다. [분류 계약](task-classification.md)과 [검증 기록](validation/task-classification-2026-11-02/README.md)을 따른다.

2026-10-30 응답성 보완: 읽기/변환과 내보내기 준비, 교환 사본 저장·재열기·기준선 검증은 공통 worker에서 수행한다. 진행 창의 취소와 창 닫기는 취소 요청이며 worker의 임시 파일 정리가 끝난 후 창을 닫는다. 최종 새 폴더 rename 직전에 취소를 검사한다. rename이 이미 끝난 후에는 완성된 산출물을 보존하고 완료로 처리한다. 처리 중 문서 revision/session 변경은 오래된 요청을 취소하며, 가져오기 완료 후에도 문서 출처를 다시 확인해 최신 문서를 교체하지 않는다. 일반 저장은 같은 입력이면 허용한다. 명시 저장 확인, 변환 미리보기 및 새 사본 열기 정책은 유지한다.

`exchange_async_recovery_diagnostics`는 실제 XML/XER 메뉴의 worker 실행과 GUI 계산 checkpoint 0개, 1만 작업/기준선의 heartbeat, 게시 직전/완료 후 취소, 임시 폴더 정리·기존 목적지/원본 보존, 편집·같은 ID/revision 재열기·일반 저장 경합을 검사한다. 전체 파일 사본과 기준선 재검산을 생략하지 않는다.

2026-10-27 · J-15 / XML-01~04 · 첫 구현 범위. PRD 확정 후 개발한다.

공식 근거: Microsoft의 [Project XML schema](https://github.com/MicrosoftDocs/office-developer-msproject-xml-docs/blob/main/project-xml-data-interchange/xml-schema-for-the-project-element.md), [작업 schema](https://github.com/MicrosoftDocs/office-developer-msproject-xml-docs/blob/main/project-xml-data-interchange/xml-schema-for-the-tasks-element.md), [달력 schema](https://github.com/MicrosoftDocs/office-developer-msproject-xml-docs/blob/main/project-xml-data-interchange/xml-schema-for-the-calendars-element.md), [LinkLag](https://github.com/MicrosoftDocs/office-developer-msproject-xml-docs/blob/main/project-xml-data-interchange/linklag-element.md). LinkLag는 분의 1/10이며 Type은 0=FF, 1=FS, 2=SF, 3=SS다. 수입 대상 namespace는 `http://schemas.microsoft.com/project`, 내보내기는 SaveVersion 12의 MSPDI 지원 부분이다. 형식이 같아도 제품/빌드별 계산 동등성은 미검증이다.

## 지원과 경계

- XML 최대 16 MiB, 10,000 task/resource/calendar/assignment 요소, 깊이 32·총 300,000 요소. XML 요소 이름은 128자, 경로는 4,096자, 모든 저장 경로의 합은 UTF-16 코드 단위 16 Mi자로 제한하여 중첩 경로의 메모리 증폭을 차단한다. 상속을 펼친 모든 달력의 휴일 합은 100,000일로 제한한다. DTD/entity와 외부 namespace 요소는 차단한다. 알 수 없는 일반 필드는 경로별 진단하고 원본에 보존한다. XML을 실행하거나 외부 참조를 가져오지 않는다.
- UID를 식별자로 사용하고 ID는 표시 순서 정보다. Task UID 0은 프로젝트 요약, Summary=1은 WBS 그룹이며 OutlineLevel 계층을 사용한다. 요약 작업 선행/배정은 leaf로 임의 확장하지 않고 차단한다. OutlineNumber/WBS 사용자 코드는 원본에만 남는다. 내보내기는 WBS 순회 순서와 신규 정수 UID를 사용하며 JOBS stable ID와의 매핑을 보고한다.
- Duration은 PT의 시간/분/초 표기에서 정수 근무분만 허용한다. 누락은 미정, 0은 마일스톤이다. 경과시간/백분율 기간·지연과 소수 분 지연은 차단한다. 추정 표시형식은 근무분 값만 매핑하며 JOBS에 없는 추정 표시의 손실을 진단한다. 변환 보고서는 대상 계산 정책·후행 작업 달력 기준 지연·09:00 시작 기준을 명시한다. 제약은 0 ASAP, 1 ALAP, 2 MSO, 3 MFO, 4 SNET, 5 SNLT, 6 FNET, 7 FNLT를 매핑한다. civil minute 날짜만 허용한다. timezone/초 정밀도를 임의 절삭하지 않는다.
- 달력은 모든 근무 요일에 같은 일중 구간을 갖는 주간 달력과 비반복 전일 휴일만 허용한다. 상속을 펼치고 누락/순환을 차단한다. 요일별 상이한 시간·근무 예외·반복 예외·WorkWeeks는 차단한다. 프로젝트 달력은 모든 수입 작업/자원에 명시 배정한다. JOBS의 프로젝트 시작은 해당 날짜 09:00이므로 원본 시작 시각 차이와 향후 새 작업의 기본 달력 차이는 진단한다.
- work 자원(Type=1)의 이름/MaxUnits와 양수 정률 Assignment Units를 1% 정밀도로 매핑한다. 이름 없는 UID 0 자원은 미배정 placeholder로 보존 원본에만 남기며 이를 참조하는 배정은 차단한다. material/cost 자원·외부 프로젝트·수동 일정은 차단한다. 자원 달력은 보존하지만 첫 수입 작업은 기간 기반 고정 기간으로 변환한다. 작업 유형/공수·자원 달력에 따른 외부 일정 차이는 진단한다. 자원 단가/가용량 곡선·실제 시간대별 공수와 고정비·기준선/실적/EVM·사용자 필드는 매핑하지 않고 원본에 보존한다.
- 내보내기도 기간 기반 계획 투영이다. 공수 기반 계획은 명목 기간을 사용하고 작업 유형/공수/수동 분할의 손실을 작업별 진단한다. 계산된 날짜는 내보내지 않는다. 미정 값을 0으로 바꾸지 않으며 미정 기간/가용량 필드는 생략하고 외부 앱의 기본값 해석 가능성을 진단한다. 자원 종류 equipment는 MSPDI work 자원으로 투영하며 진단한다. 자원/작업 달력, 네 관계, 제약/마감일과 정률 배정은 유지한다. 비용/승인 이력은 source.ppm에만 보존한다.

## 파일과 UI

파일 메뉴의 XML 가져오기/내보내기는 건수·손실 진단·대상 새 폴더를 미리 보여 준다. 진단을 확인하고 승인한 뒤 같은 부모의 임시 폴더에서 모든 파일을 기록/재열기 검증하고 새 폴더로 rename한다. 기존 목적지는 거절한다. 오류/취소 때 현재 문서는 그대로다. 가져오기는 미저장 변경 처리 후 project.ppm를 새 세션으로 연다. 내보내기는 읽기 전용 문서에서도 가능하며 전체 프로젝트를 사용한다.

conversion.json은 contract `jobs-mspdi-plan-v1`, 방향, 원본 SHA-256/이름/SaveVersion/BuildNumber, 문서 ID/revision, UID 매핑과 경로/설명별 진단을 담는다. `import`, `originalPreservation`, `jobsRecomputation`, `externalRoundTrip` 네 판정은 독립이다. 수입 original.xml은 원본 바이트 exact이고 수출 source.ppm는 현재 입력/이력의 정확한 새 직렬화 스냅샷이다(기존 디스크 파일의 바이트 사본이라는 뜻이 아니다). 외부 재계산/왕복은 unverified다. .ppm schema 9는 그대로 유지한다. 동반 원본/진단 파일은 해당 교환 폴더에 보관되며 .ppm 파일만 별도 저장/이동하면 동반 이동하지 않는다.

## 완료 검사

도메인 골든 fixture와 XML 오류/한도/DTD/누락·중복 UID/고아·요약 참조/순환, 달력 상속·휴일/불가 패턴, 네 관계·음수 지연·정밀도·제약, 자원/배정, 미정 기간, 한국어/escape, 원본 바이트/hash 보존, 실패 시 기존 폴더 보호를 검사한다. 지원 부분의 내보내기→재수입은 ID 정규화 후 입력과 일정 의미를 검사하고 Python 표준 XML reader로 namespace/UID/참조/값을 독립 검사한다. 네이티브 앱에서 미리보기 취소·가져오기/내보내기·읽기 전용 내보내기와 기존 회귀를 실행한다. Microsoft Project 설치본이 없는 CI에서는 실제 제품 호환을 완료로 표시하지 않는다.

## 사용 순서와 검사 도구

1. 파일 → Project XML 가져오기에서 .xml을 선택한다. 작업/자원 수·매핑하지 않는 경로와 미검증 판정을 확인한다.
2. 아직 없는 새 교환 폴더의 전체 경로를 지정하고 `변환 확인 · 새 사본 열기`를 누른다. 기존 문서가 미저장이면 저장/버림/취소를 선택한다. 취소는 교환 폴더를 만들지 않는다.
3. 해당 폴더의 `project.ppm`가 새 문서로 열린다. F9로 JOBS 일정을 계산한다. `original.xml`/`conversion.json`은 원본과 판단 근거로 함께 보관한다.
4. 내보내기는 파일 → Project XML 내보내기에서 같은 미리보기 흐름을 사용한다. `project.xml`/`source.ppm`/`conversion.json`이 생성된다. 읽기 전용 문서와 미저장 입력도 내보낼 수 있으며 열린 원본의 편집 이력/저장 지점을 바꾸지 않는다.

`project_xml_contract`/`project_xml_ui`를 추가해 전체 CTest는 30개다. 고정 fixture는 실제 제품에서 저장한 파일로 위장하지 않은 수작업 골든 입력이다. `scripts/verify-project-xml.py`는 Python 표준 XML reader로 고정 기대값을 독립 검사한다. `scripts/capture-project-xml-ui.py`는 네이티브 대화상자의 취소/가져오기/내보내기/오래된 입력/읽기 전용 동작과 `project-xml-import-native.png`/`project-xml-export-native.png`를 남긴다. CI artifact `project-xml-Windows`/`project-xml-macOS`에서 XML/진단/화면을 확인한다. 공식 근거 조회 시 MicrosoftDocs 소스 revision은 `c77602db4d0733ce57cb0e5514c6fc898c7fe2c2`였다. 내보내기는 schema의 프로젝트 이름 255자, 작업/WBS/자원/달력 이름 512자, 일중 구간 5개 한도도 검사한다.

평준화 채택 정책/실행 날짜는 기간 기반 XML 투영에 보존되지 않는 손실로 진단한다. `source.ppm`에 조건부 schema12 native 설정을 보존한다. [채택 계약](leveling-adoption.md).

schema13의 진척 정책 선택과 근거는 XML에 매핑하지 않고 교환 손실 진단을 표시한다. source.ppm에는 전체 선택을 보존한다. XML은 원본 기간 기반 논리 계획이다.

schema14 조합과 시나리오의 동결 진척 정책은 native source.ppm에 보존한다. XML은 계속 원본 기간 기반 논리 계획이며 선택 정책/평준화의 날짜를 외부 일정으로 확정하지 않는다.
