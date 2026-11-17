# 프로젝트 파일과 전달·복구 계약

2026-11-17 후속: [MF-01~07](../material-float.md)의 새 자재 평준화 엔진 승인 기준선이 현재 문서/통화·ID 이관 전후 사본에 있을 때만 조건부 native84다. 이전 엔진 기준선은 당시 결과로 재현하고 일반 자재 문서는81/82/83 최소 writer를 유지한다. 현재 상한84, 미지원 미래85이며 작업공간 형식은 유지한다.

2026-11-17 후속: [SMAT-01~07](../shared-material-actuals.md)은 파생 통합 결과만 확장하며 native83과 기존 작업공간1~4 최소 writer를 유지한다. JSON materialStocks의 합계 milli 수량은 십진 문자열이며 원장 원본 리비전과 통합 정책을 함께 출력한다.

2026-11-17 후속: [MAT-01~08](../actual-materials.md)의 실제 자재 원장/잔여 옵션·과거 정책 이력이 있으면 조건부 native83, 미지원 미래84다. 시나리오의 원장과 통화/ID 이관 전후 전체 사본·복구도 검사하며 미사용82 이하 최소 writer와 작업공간4는 유지한다.

2026-11-17 후속: [입고 자동 대기](../material-arrival-leveling.md)는 조건부 native **schema82**,미지원 미래83이다. 옵션/새 정책 결정 이력·현재/승인/시나리오·일정 채택·통화/ID 전후를 검사한다. 이전81의 명시 검산 의미와 미사용 최소 writer·workspace4를 유지한다. 옵션 제거 후 이력만 남아도82다.

2026-11-17 후속: [계획 입고·개시 재고 시각](../material-inventory.md)은 조건부 native **schema81**,미지원 미래82다. ResourcePlan.receipts 또는 Material.stockAt가 현재/승인/시나리오·일정 채택·통화/ID 전후 사본에 있으면81을 쓴다. 미사용 파일의 기존 최소 writer와 작업공간4를 유지한다.80 이하의 새 필드를 거절한다.

2026-11-16 후속: [공유 가용기간](../shared-resource-availability.md)에 한해 작업공간 writer를 조건부 **schema4**로 확장한다. Resource의 availableFrom/To·availability와 기존 링크/통합/운영 필드를 함께 보존한다. 새 값이 없으면1~3 최소 writer를 유지하고 미래5/과거 schema의 새 필드 주입은 거절한다. 프로젝트 native 상한80은 그대로다.

2026-11-16 후속: [공유 잔여 가용량](../shared-remaining-availability.md)은 파생 조회이며 native80/기존 작업공간 schema1~3을 변경하지 않는다. 별도 JSON 결과 계약 `ppm-shared-remaining-availability-v1`에 시각·작업공간 세대·입력 revision/상태일·미정 수치와 확인된 하한을 담는다. 이 출력은 프로젝트 저장 파일이 아니다.

2026-11-16 후속: [현재 부하 함수](../remaining-resource-availability.md)의 활성 PPMResourceLoad/PPMRemainingAvailability는 조건부 schema80이다. 현재/승인/시나리오·일정 채택·통화/ID 전체 사본을 검사하며79 이하에서 새 활성 의미를 거절한다. 비활성 원문과 과거 최소 writer를 유지한다. **현재 상한80·미지원 미래81**이며 아래 상한은 당시 기록이다.

2026-11-16 후속: [가용기간](../resource-availability.md)의 Resource.availableFrom/availableTo/availability가 있을 때만 조건부 schema79다. 현재·승인/시나리오·채택·통화/ID 전체 사본에서 감지하고78 이하의 새 필드를 거절한다. 과거 최소 writer를 유지한다. **현재 상한79·미지원 미래80**이며 아래 상한은 당시 기록이다.

2026-11-15 후속: [Units·가용률 참조](../xml-unit-fields.md)의 안정 unit 바인딩(비활성 포함) 또는 활성 PPMUnitAvailability는 조건부 schema78이다. 현재/승인/시나리오·일정 채택·통화/ID 전체 사본을 검사하며 schema77 이하에서 새 의미를 거절한다. 비활성 원문만 있는 함수는 기존44 최소 writer다. **현재 상한78·미지원 미래79**이며 아래 상한은 각 증분 당시 기록이다.

2026-11-15 후속: [native 상세 시간분포](../native-baseline-timephased.md)는 XML 출력 시 파생하고, 부모 위치 자료의 명시 채택/병합에서만 XmlBaselineRow.timephasedOutside와 조건부 schema77을 사용한다. 현재/승인/시나리오·일정 채택·통화/ID/병합 이력 전체 사본을 검사하고 schema76 이하의 새 위치 필드를 거절한다. 기존 native 승인본만 있는 파일과 내부 시간분포의 최소 writer는 유지한다.

2026-11-15 후속: [XML 기준선 병합](../xml-baseline-merge.md)의 번호별 출처와 전후 이력은 조건부 schema76이다. 이전 schema75 이하는 새 필드를 거절하고 기존 단일 사본 채택의 최소 schema75는 유지한다. 현재/기준선/시나리오/채택과 통화·ID 이관 전체 사본을 검사한다. 병합 당시 통화/ID의 이력은 불변이며 현재 값의 전환과 구별한다.

2026-11-15 후속: [외부 XML 기준선](../xml-baseline-exchange.md)의 PlanInput.xmlBaselines를 조건부 schema75로 보존한다. 번호/객체별 명시 날짜·고정점 기간/공수·최소 단위 원가·표시/추가 금액/시간분포, 출처 SHA·검토 정보·동결 프로젝트 달력을 저장한다. 현재/기준선/시나리오/채택/통화·ID 이력 전체 사본을 검사하며 schema74 이하의 새 필드를 거절한다. 자료가 없는 기존 파일은 최소 writer를 유지한다.

2026-11-15 후속: [번호별 기준선](../xml-numbered-baselines.md)은 바인딩이 있으면 조건부 schema73, [요약 기간](../xml-summary-durations.md)은 프로젝트 기간 바인딩 또는 작업 요약 기간 설정이 있으면 schema74로 저장한다. 과거에 허용한 작업 요약 설정은 읽은 뒤 새 계산 의미를 적용하고 다음 저장 시 승격한다. 새 프로젝트 바인딩은 구버전 태그에서 거절하며 기존 작업별 기능의 최소 writer는 유지한다.

2026-11-15 후속: [SLN·SYD](../xml-depreciation-functions.md)의 활성 식은 동결/이력 사본을 포함해 조건부 schema72다. 비활성 원문은 기존 schema44부터 유지하며 native 필드는 추가하지 않는다.

2026-11-14 후속: [Partition](../xml-partition-function.md)의 활성 식은 동결/이력 사본을 포함해 조건부 schema71이다. 비활성 원문은 기존 schema44부터 보존하며 native 필드는 추가하지 않는다.

2026-11-14 후속: [RGB·QBColor](../xml-color-functions.md)의 활성 함수는 이력/동결 사본을 포함해 조건부 schema70이며 비활성 원문은 schema44부터 유지한다. native 필드는 추가하지 않는다.

2026-11-14 후속: [XML 숫자 표시3개](../xml-numeric-display.md) 활성 식이 현재/기준선/시나리오·일정 채택·통화/ID 이력 전후 전체 사본에 있으면 조건부 schema69를 사용한다. 비활성 원문은 기존 schema44부터 보존하며 새 native 필드는 추가하지 않는다.

2026-11-14 후속: [XML FormatDateTime](../xml-format-datetime.md) 활성 식이 현재/기준선/시나리오·일정 채택·통화/ID 이력 전후 전체 사본에 있으면 조건부 schema68을 사용한다. 비활성 원문은 기존 schema44부터 보존하며 새 native 필드는 추가하지 않는다.

2026-11-14 후속: [XML MonthName·WeekdayName](../xml-date-name-functions.md) 활성 식이 현재/기준선/시나리오·일정 채택·통화/ID 이력 전후 전체 사본에 있으면 조건부 schema67을 사용한다. 비활성 원문은 기존 schema44부터 보존하며 새 native 필드는 추가하지 않는다.

2026-11-14 후속: [XML AscW·ChrW](../xml-unicode-functions.md) 활성 식이 현재/기준선/시나리오·일정 채택·통화/ID 이력 전후 전체 사본에 있으면 조건부 schema66을 사용한다. 비활성 원문은 기존 schema44부터 보존하며 새 native 필드는 추가하지 않는다.

2026-11-14 후속: [XML Like](../xml-like-operator.md)의 활성 연산과 전체 사본은 조건부 schema65다. 상한65·새 native 필드 없음·이전 활성 태그 거절·비활성44 읽기와 기존 기능 최소 버전을 유지한다.

2026-11-14 후속: [XML 논리 연산](../xml-logical-operators.md)의 활성 Eqv/Imp와 전체 사본은 조건부 schema64다. 상한64·새 native 필드 없음·이전 활성 태그 거절·비활성44 읽기와 기존 기능 최소 버전을 유지한다.

2026-11-14 후속: [XML 산술 연산](../xml-arithmetic-operators.md)의 활성 ^/정수 나눗셈과 전체 사본은 조건부 schema63이다. 상한63·새 native 필드 없음·이전 활성 태그 거절·비활성44 읽기와 기존 기능의 최소 버전을 유지한다.

2026-11-14 후속: [Baseline Cost](../xml-baseline-cost.md)의 새 바인딩과 전체 사본은 활성/비활성 모두 조건부 schema62다. 상한62·새 native 필드 없음·이전 태그의 새 바인딩 거절과 기존 네 기준선 참조의 최소50을 유지한다.

2026-11-14 후속: [Project 변환 함수](../xml-project-conversion-functions.md)의 활성 ProjDateValue/ProjDateConv/ProjDurConv와 전체 사본은 조건부 schema61이다. 상한61·새 native 필드 없음·이전 활성 태그 거절·비활성44 읽기와 기존 기능 최소 버전을 유지한다.

2026-11-14 후속: [XML 형 변환](../xml-conversion-functions.md)의 활성 CDec/CSng/CVar와 전체 사본은 조건부 schema60이다. 상한60·새 native 필드 없음·과거 활성 태그 거절·비활성 원문44 읽기와 기존 기능 최소 버전을 유지한다.

2026-11-14 후속: [XML 문자열 함수](../xml-string-functions.md)의 활성 Replace/InStrRev/StrReverse와 전체 보관 사본은 조건부 schema59이다. 상한59·새 native 필드 없음·이전 활성 태그 거절·비활성 원문44 읽기·기존 기능 최소 버전을 유지한다.

2026-11-13 후속: [XML 추가 수학 함수](../xml-math-functions.md)의 활성 Atn/Cos/Exp/Log/Sin/Tan과 전체 보관 사본은 조건부 schema58이다. 상한58·새 native 필드 없음·이전 활성 태그 거절·비활성 원문44 읽기·기존 기능의 최소 버전을 유지한다.

2026-11-13 후속: [Str·Val·Hex·Oct](../xml-number-text.md)의 활성 현재/전체 보관 사본은 조건부 schema57을 사용한다. 새 native 필드는 없으며 비활성 원문은 기존44부터 읽는다. 공유 최대 버전을57로 올리고 기존 날짜 식 읽기·최소 writer·파일/전체 사본/복구 규칙을 유지한다.

2026-11-13 후속: [TimeSerial·TimeValue](../xml-time-functions.md)의 활성 현재/전체 보관 사본은 조건부 schema56을 사용한다. 새 native 필드는 없으며 기준일·식·참조·원래 입력/스냅샷을 보존한다. 비활성 원문은 기존44부터 읽고 기존 날짜 함수의 레거시3인수 읽기를 유지한다. 파일/복구/전체 사본·파서의 공유 최대 버전은56이다.

2026-11-13 후속: [날짜 함수 확장](../xml-date-functions.md)은 새 DatePart/Weekday·선택 인수 DateDiff 문법을55부터 읽고 확장/동적 간격도55로 저장한다. 기존3인수 식은 과거에도 저장 가능했으므로 기존44~54 태그에서 읽는다. 읽기 최소 버전과 보수적 writer 버전을 구분하되 다른 기능의 버전 제한은 유지한다. native 필드 추가 없이 전체 사본/복구·공유 최대 버전55를 연결한다.

2026-11-13 후속: [Format·StrConv](../xml-format-strconv.md)의 활성 현재/전체 보관 사본은 조건부 schema54를 사용한다. 새 native 필드는 없으며 비활성 원문은 기존44부터 보존한다. 파일·전체 사본·복구·파서의 공유 상한을54로 올린다. Unicode16.0 자료의 출처/해시/생성기·라이선스를 보관하고 Windows payload에도 동반한다.

2026-11-13 후속: [계산식 시간대 변환](../xml-formula-timezones.md)은 활성 현재/전체 보관 사본에 조건부 schema53을 사용한다. 새 native 필드 없이 식·명시 시간대 입력·원래 값/설정과 동결/복구를 보존하며 비활성 레거시 식은 기존44부터 읽는다. 최대 지원 버전 상수를 파일/전체 사본/복구·파서/미래 버전 검사에서 공유한다. 내장 IANA2026f의 출처·SHA·라이선스와 독립 TZif 자료를 보관하고 실행 시 호스트 시간대/네트워크를 사용하지 않는다.

2026-11-13 후속: [추가 XML 계산식 함수](../xml-additional-functions.md)의 활성 실행은 조건부 schema52를 사용한다. 현재/기준선/시나리오/일정 채택·통화/ID 이관의 전체 사본을 검사하고 오래된 태그의 활성 호출을 거절한다. 새 native 필드는 없으며, 과거 schema44부터 비활성 보존한 같은 원본 식은 자동 활성화 없이 읽을 수 있다. 기존 문서의 최소 writer와 저장/복구를 유지하고 지원 상한을52로 올린다.

2026-11-13 후속: [다일 실적 비례 배분](../xml-multiday-proportional-actuals.md)은 새 native 필드 없이 기존 PeriodActualEntry/CostEntry와 구조화 사유를 사용한다. 척도별 추정 기원은 불변 이력과 동일 수량의 정정에서 계산한다. 선택적인 새 검토 폴더는 채택 전 source.ppm·original.xml·allocation.json을 보관한다. 표본 schema11·상한51과 저장/복구·이전/미래 보고를 유지한다.

2026-11-13 후속: [공수 기반 기간 병합](../xml-work-driven-progress.md)은 기존 ProgressEntry의 명시 잔여 공수와 보고 당시 계획 기간을 사용한다. 단순 일별 원장 표본은 기존 schema11, 공수 곡선 표본은 기존 schema24이며 새 형식/속성을 추가하지 않는다. 상한51·원본/정정/미래 이력·기존 저장/복구를 유지한다.

2026-11-13 후속: [표준 실적 XML 출력](../xml-standard-actual-export.md)은 새 native 구조 없이 현재 상태일의 기존 원장 값을 출력한다. source.ppm은 원래 계획과 이전/미래 보고 전체를 유지하고 conversion.json은 입력/출력 해시·ID 매핑·선택 원장과 대사를 기록한다. native로 XML/기록을 다시 생성해 대조한 뒤 새 폴더에 원자 게시한다. 기본 표본 schema11·지원 상한51을 유지한다.

2026-11-12 후속: [XML 일별 실적 구간 합산](../xml-daily-actual-fragments.md)은 일별 후보에만 구간 수/합계를 두고 native에는 기존 날짜별 원장 한 건을 기록한다. 기본 schema11·지원 상한51과 기존 출처/정정·복구 구조를 유지한다. 각 합산의 구성은 함께 보관한 원본 XML로 검증한다.

2026-11-12 후속: [XML 배정 일별 실제 공수/원가](../xml-period-actual-adoption.md)는 기존 PeriodActualEntry/CostEntry·출처 사유/누계 연결을 사용하며 기본 schema11·지원 상한51을 유지한다. 원본 XML은 동반 보관하고 native 파일에는 선택한 날짜별 관측과 정정/UTC·해시/배정 UID를 보존한다.

2026-11-12 후속: [XML 진척 기존 문서 병합](../xml-progress-merge.md)은 기존 ProgressEntry에 선택 행을 한 revision으로 추가하며 최소 native schema/지원 상한51을 변경하지 않는다. SHA-256/UID/원본 파일명과 native 작업/이전 보고 연결은 원장에 보존한다. 원본 바이트와 미선택 연결은 내장하지 않으며 원본 XML을 함께 보관해야 한다. 변경은 일반 native 저장과 복구를 사용하고 원본 XML/현재 파일을 자동 덮어쓰지 않는다.

2026-11-12 후속: [XML 작업 진척 새 사본 채택](../xml-progress-adoption.md)은 기존 ProgressEntry/statusAt/revision 구조를 사용한다. sourceDocumentId/보고 ID·작성자/UTC·불변 사유에 출처 SHA-256/UID를 보존하며 원본 바이트·선택 매핑은 교환 동반 파일에 저장한다. 새 native schema는 추가하지 않고 기본 입력은9·실제 기간/공수 수식이 있으면48 등 기존 최소 writer를 따른다. 지원 상한51·미지원 미래52를 유지한다.

2026-11-12 후속: [XML Early/Late 바인딩](../xml-early-late-references.md)의 builtin:earlystart/earlyfinish/latestart/latefinish가 현재·비활성·동결·시나리오·채택·통화/ID 전후 사본에 있으면 조건부 schema51이다. 날짜 결과는 세션 값이며 원래 원장/기준선은 유지한다. schema50 이하는 새 바인딩을 거절한다. 현재 상한51·미지원 미래52이며 아래 상한은 당시 기록이다.

2026-11-12 후속: [XML 최초 기준선 바인딩](../xml-baseline-references.md)의 builtin:baselinestart/baselinefinish/baselineduration/baselinework가 현재·비활성·동결·시나리오·채택·통화/ID 전후 사본에 있으면 조건부 schema50이다. 원래 기준선 스냅샷은 유지하며 schema49 이하는 새 바인딩을 거절한다. 현재 상한50·미지원 미래51이고 아래 상한은 각 증분 당시 기록이다.

2026-11-12 후속: [잔여 참조](../xml-remaining-references.md)의 builtin:remainingduration/remainingwork가 현재·동결·채택·통화/ID 전후 사본에 있으면 schema49다. schema48 이하는 새 바인딩을 거절하고 이전 최소 writer를 유지한다. 현재 상한49·미지원 미래50이며 아래 상한은 각 증분 당시 기록이다.

2026-11-12 후속: [XML 실제 기간·보고 공수 바인딩](../xml-actual-quantities.md)의 builtin:actualduration/actualwork가 현재·동결·채택·통화/ID 전후 사본에 있으면 조건부 schema48이다. 원장은 기존 저장 구조를 유지하며 schema47 이하에서 새 바인딩을 거절한다. 현재 지원 상한48·미지원 미래49이며, 아래 상한 표기는 각 증분 당시 기록이다.

2026-11-11 후속: [XML 실적 날짜 바인딩](../xml-actual-references.md)이 현재/동결·채택·전체 전후 사본에 있으면 조건부 schema47이다. 상태일·원장은 기존 저장 구조를 사용하며 새 바인딩의 구버전 거절·복구를 지원한다. 지원 상한47·미지원 미래48이다.

2026-11-11 후속: [XML 근무 달력 함수](../xml-calendar-functions.md) 또는 PlanInput.xmlFormulaCalendarId가 현재/동결·채택·전체 전후 사본에 있으면 조건부 schema46이다. 이전 최소 writer·복구·구버전 거절을 유지한다. 지원 상한46·미지원 미래47이다.

2026-11-11 후속: [계산된 일정 참조](../xml-schedule-references.md)의 builtin:start/finish 바인딩이 현재/동결·채택·전체 전후 사본에 있으면 조건부 schema45다. 계산 결과는 세션 값이며 이전 최소 writer와 구버전 거절을 유지한다. 현재 상한45·미지원 미래46이다.

2026-11-11 후속: [XML 계산식](../xml-external-formulas.md)의 CustomField.xmlFormula(source/references/enabled)가 현재·동결·채택·통화/ID 전후 사본에 있으면 schema44를 사용한다. 기존 최소 writer와 구버전 envelope의 새 속성 거절은 유지한다.

2026-11-11 후속: [요약 roll-up](../xml-roll-up.md)의 CustomField.xmlRollUp·WbsNode.attributes·PlanInput.taskSummaryAttributes가 현재/동결·채택·전체 전후 사본에 있으면 schema43을 사용한다. 기존 최소 writer와 구버전의 새 데이터 거절을 유지한다.

2026-11-11 후속: [배정 roll-down](../xml-roll-down.md)의 CustomField.xmlRollDown과 Assignment.rollDownMissing이 현재/동결·채택·전체 전후 사본에 있을 때 schema42를 사용한다. 구버전은 새 필드를 거절하고 기존 최소 writer·복구·undo 계약을 유지한다.

2026-11-11: [XML 마스크·코드](../xml-code-masks.md)가 현재/동결·채택·전체 전후 사본에 있을 때 schema41을 사용한다. XmlLookup.codeMask와 PlanInput.xmlWbs를 저장하고 이전 버전은 새 필드를 거절한다. 기존 최소 writer와 복구·일반 undo/redo 계약을 유지한다.

2026-11-10 조건부 schema40은 [Start/Finish와 Date 시각](../xml-date-time-fields.md)을 현재·동결·채택·전체 전후 사본/복구에 보존한다. 기존 날짜 전용 파일의 최소 writer를 유지한다. 오래된 envelope에 새 형식/시각이 포함되면 거절한다.

2026-11-10 조건부 schema39는 [Flag·Cost·Duration](../xml-additional-field-types.md)의 새 형식 토큰과 목록 valueType을 허용한다. 새 형식이 없는 파일의 최소 writer를 유지하며 현재·동결·일정 채택·전체 전후 사본과 복구에 같은 검사를 적용한다.

2026-11-10 조건부 schema38은 [XML 목록 메타데이터](../xml-lookup-references.md)를 추가한다. `xmlLookup`은 값 형식·테이블/필드 GUID·선택 한도·계층/기본값·옵션별 외부 ID/GUID/원시 값/설명을 보존한다. 다중 선택은 정렬한 옵션 ID를 길이 접두사 문자열로 저장한다. 현재/동결 사본·전환 전후·복구에 같은 검증을 적용하며 목록 없는 기존 파일의 최소 writer를 유지한다.

2026-11-10 조건부 schema37은 [기업 XML 필드 신원과 배정 사전/값](../enterprise-assignment-udf.md)을 추가한다. 이전 파일의 최소 writer와 현재/동결·전체 전후 사본·복구를 보존한다.

2026-11-09 [자원 생산성·외부 고유 곡선 교환](../resource-productivity-curves.md)의 현재/기준선/시나리오·XER 채택/원본 및 통화/ID 이관 전후 사본에 새 메타데이터가 있으면 schema36이다. 생산성/산정 snapshot·원본 곡선 정의/투영 근거를 저장·재열기·사본·복구에서 보존한다. **현재 상한36·미지원 미래37**이며 기존1~35 최소 writer를 유지한다.

2026-11-08 [FixedWork/FixedUnits 날짜 고정 잔여 목표](../dated-effort-targets.md)는 기존 날짜 입력 구조에 새 총공수/완료 의미를 적용하므로 조건부 schema35다. 현재/시나리오·평준화 결정 이력과 통화/ID 이관 전후 원본의 새 의미도 감지한다. 기존1~34 최소 writer를 유지한다. **현재 상한35·미지원 미래36**이며 아래 상한은 각 증분 당시 기록이다.

2026-11-08 [프로젝트 전체 통화 전환](../project-currency-conversion.md) 이력 또는 XER 원본의 추가 통화 환산 단계가 있으면 조건부 schema34다. 환율/정책·작성 근거·전체 전후 문서와 raw XER 원본을 보존하고 재실행·현재 루트를 대조한다. 통화/ID 이관 전후 원본에는 두 전환 이력을 중첩하지 않으며 루트에서 각각 보존한다. 기존1~33 최소 writer를 유지한다. **현재 지원 상한34, 미지원 미래35**이며 아래 상한 표기는 각 증분 당시의 기록이다.

2026-11-08 [관측 원장 ID 이관](../ledger-id-migration.md) 이력이 있으면 조건부 schema33이다. 명시 연결/확인/작성자/근거와 중첩 이력 없는 전체 전후 문서를 저장·복구·사본에 보존하고 원본부터 이관을 재실행해 모든 값을 대조한다. 기존1~32 writer는 유지한다. 현재 지원 상한33, 미지원 미래34다.

2026-11-08 [새 XER 일정·WBS·달력 채택](../xer-schedule-adoption.md) 이력이 있으면 조건부 schema32다. 원본 typed 계획/지문/프로젝트/진단과 작업 연결/범위·작성자/사유/기록 revision·전후 계획을 저장·복구·독립 사본에 보존한다. 재실행·연속 채택과 채택 직후 현재 입력의 대사를 수행하며 기존1~31 writer와 과거 재무/원장을 유지한다. 현재 상한32, 미지원 미래33이다.

2026-11-08 [XER 통화·자원 종류 전환](../xer-source-conversion.md) 감사 기록이 있으면 조건부 schema31이다. 전환 전 후속 원본과 새/이전 집계별 통화·공수 환산비, 기준일/근거·정확도 정책을 저장/복구/사본에 보존하고 재실행해 검증한다. 전환 없는 기존1~30 writer를 유지한다. 현재 지원 상한은31이며 미래33·구 envelope의 전환 필드는 거절한다.

2026-11-08 [사용자 기간·기간 마감](../custom-financial-periods.md)의 정의/마감/재개방 이력이 있으면 조건부 schema30이다. 기간·작업 지정액·기준선·지식/기록 revision·출처/작성자와 당시 native 경계/원가 근거를 보존한다. 이력이 없는 기존1~29 writer를 유지하고 미래33·구 envelope의 새 필드를 거절한다.

2026-11-08 [외부 XER EV/PV native 채택/철회](../external-xer-evm-adoption.md) 이력이 있는 파일은 조건부 schema29다. 당시 원본/재무 revision·기준선/작업과 기간 참조·개시 누계·대체 선택·기록 근거를 보존한다. 저장/복구/독립 사본에서 원본과 당시 최신 집계를 대조하며 구 envelope의 새 필드와 미래33을 거절한다. 이력이 없으면 기존1~28 writer를 유지한다.

2026-11-08 [native 기간 EVM](../native-period-evm.md)은 보존된 기준선과 native 비용/진척/Steps·기간 원장으로 다시 계산하는 읽기 전용 파생 보고다. 별도 기간 EVM 상태를 파일/복구에 추가하지 않으며 기존 조건부 schema1~28 writer를 유지한다.

2026-11-08 후속: [새 XER 버전·삭제 ID 재연결](../xer-source-relink.md)이 있는 문서는 조건부 schema28이다. 최초 원본과 각 후속 원본의 typed 재무/ID 사전·SHA256·프로젝트/상태일·명시 새/이전 ID 연결표·정책·병합 근거를 파일/복구/사본에 보존한다. 원본 XER 전체 바이트는 선택한 파일을 별도 보관한다. 현재 계획·원장·AC와 기존 writer를 유지하며 구 envelope의 새 의미/필드와 미래33을 거절한다.

2026-11-07 후속: [XER 원장 병합](../xer-ledger-merge.md)이 있거나 독립 외부 요약/날짜 경비가 중첩되는 문서는 조건부 schema27이다. 정책·기준 투영 SHA256·명시 선택과 최신 원장 ID/당시 native AC 대사를 저장·복구·사본에 유지한다. 없는 문서는 기존 writer를 사용하며 구 envelope의 새 의미·필드와 미래33을 거절한다.

2026-11-07 후속: [XER 집계 정정·날짜 배분](../xer-aggregate-editing.md)이 있는 문서는 조건부 schema26이다. 원본 스냅샷과 별도로 각 제출의 전체 집계·비중·생성 원장 ID·작성자/사유/시각을 native 저장/복구/사본에 보존한다. 정정 없는 문서는 기존9~25 writer를 유지하고 미래33·구 envelope의 새 필드는 거절한다.

2026-11-07 구현 확장: [XER 보존 재무기간·누계](../xer-financial-snapshots.md)가 있는 native 파일은 조건부 schema25다. 현재 계획·실적 원장과 별도의 typed 스냅샷을 파일 하나에 포함하며 저장/사본/복구로 보존한다. 없는 문서는 기존 schema9~24를 유지한다. 이전 앱은25를 지원하지 않으며 다른 형식의 교환에는 source.ppm 보존/손실 진단을 따른다.
FILE-01~10 · 사용자 확정 요구와 설계 제안 분리

## 확정된 제품 요구
FILE-01 CONFIRMED: Office처럼 프로젝트별 독립 JOBS 프로젝트 파일을 생성·관리한다. 새로 만들기/열기/저장/다른 이름으로 저장을 제공한다.
FILE-02 CONFIRMED: 파일을 이동한 뒤 Windows/macOS에서 이어 연다. 계정·서버 없이 기본 계획·계산·저장 작업을 한다.
FILE-03 CONFIRMED: 기본 형식은 JOBS 자체 프로젝트 파일이다. Microsoft Project/Primavera 가져오기·내보내기는 별도 호환성 단계다.
확장자·내부 컨테이너/DB·압축·암호화·서명 형식은 미확정이다. 이하 구현 방식과 예외 정책은 PROPOSED다.

## 단일 전달 파일의 경계
FILE-04: 명시 저장을 완료한 정상 JOBS 파일 하나가 동일 지원 버전의 다른 PC에서 기본 계획·WBS·달력·관계·배정·기준선·실적·프로파일 참조를 복원하는 전달 단위다. OS 절대경로·계정·로컬 DB가 기본 열기의 숨은 의존성이 되어서는 안 된다.
자동 복구 checkpoint·세션 UI 상태·잠금·임시 파일·계산 cache는 전달 필수 파일로 요구하지 않는 안을 채택한다. cache가 없어도 저장된 사실과 버전으로 재계산할 수 있어야 한다.
첨부는 포함(embed) 또는 외부 링크를 구분한다. 포함 첨부는 전달 단위와 무결성·용량 정책에 포함한다. 외부 첨부는 경로/URI·선택 checksum·설명·해결 상태를 저장하고 이동 후 누락을 표시한다. 외부 자료가 없는데 내용을 보존한 것처럼 표시하지 않는다.
외부 프로젝트 링크는 원본 identity·필수 endpoint snapshot·기준시각·profile을 보존한다. 누락/만료는 INCOMPLETE/STALE이며 다른 프로젝트까지 최신 계산한 것으로 표시하지 않는다.

## 저장·다른 이름·이동
FILE-05: 저장은 동일 문서의 revision을 갱신한다. 다른 이름으로 저장은 현재 내용을 새 경로의 독립 파일로 보존하고 원본 파일을 바꾸지 않는다. 외부 namespace 충돌을 피할 독립 document ID와 derived-from 계보를 부여하는 안을 제안한다. 세부 복제 규칙은 향후 fixture로 확정한다.
쓰기 전 외부 revision/파일 식별·잠금을 확인하고, 임시 완성본→검증→원자적 교체/commit→성공 통지 순서로 설계한다. 파일시스템별 원자성·내구성 전제를 고정한다. 직접 덮어쓰기 fallback을 무음 적용하지 않는다.
이동한 파일은 안정 ID·계산 의미·내부 상대 참조를 보존한다. Windows 대소문자/예약명과 macOS 경로·정규화 차이, 긴 경로·Unicode·시간대·권한을 별도 시험한다. 외부 상대 참조의 기준은 프로젝트 파일 위치로 명시하고 깨진 링크를 복구 UI로 연결한다.
권한 거부·용량 부족·장치 제거·검증 실패·교체 실패 시 이전 정상본은 보존하고 저장 실패와 미저장 revision을 드러낸다. 성공 통지 전에 파일이 완성되었다고 간주하지 않는다.

## 자동저장·잠금·동기화
FILE-06: 자동저장은 복구 checkpoint다. 명시 저장 원본·승인 baseline·backup·undo를 대신하지 않는다. 문서ID/revision·완료 marker·checksum·기준 정상본과 연결하고 복구 선택 시 차이와 미복구 변경을 보여준다.
FILE-07: 기본은 단일 writer 계약 제안이다. 중복 실행·다른 앱 변경·클라우드 동기화 충돌은 감지 후 읽기전용/복사/재열기 선택을 제공한다. stale lock의 소유·시각·프로세스 종료 상태를 검토하되 타 writer의 파일을 무음 탈취하지 않는다.
공유 드라이브/동기화 폴더는 파일 전달 장소일 수 있으나 팀 동시 편집을 보장하지 않는다. 충돌 사본을 자동 최신본으로 합치지 않고 각 파일을 보존해 비교·수동 선택한다. 협업/동시 편집은 별도 서비스/동기화 프로토콜 후보로 유지한다.

## 내부 DB 후보와 일관성
FILE-08: SQLite는 후보이며 확정이 아니다. 사용한다면 열린 DB의 main 파일만 복사하고 WAL/shm의 미반영 변경을 누락하는 저장/전달을 금지한다. 지원되는 backup/snapshot 또는 검증된 checkpoint/close/export 절차로 일관된 단일 전달 파일을 생성해야 한다.
'저장 완료' 결과가 열기 시점과 동일 revision을 재현하는지 검사한다. 잠금·임시·WAL/shm은 전달 필수 부속파일이 되지 않도록 lifecycle을 설계한다. SQLite 버전·래퍼·파일시스템 조건은 별도 고정한다. 단순 파일복사를 online backup으로 간주하지 않는다.

## 버전 이관·백업
FILE-09: schema version을 열기 전에 검사한다. 지원하지 않는 신버전은 원본 변경 없이 안전 거절한다. 이관은 원본 백업→별도 대상 변환→ID/참조/단위/기준선/실적 검증→열기다. 실패 시 원본·백업을 보존한다.
앱 업데이트·문서 이관·backup 보존·자동복구는 별도 정책이며 이전 앱으로 새 schema를 연다고 rollback이 보장되지 않는다.

## 수용조건
| 시험 | 입력/행동 | 통과 기준 |
|---|---|---|
| FILE-T01 | 계정·네트워크 없이 새 프로젝트→입력→계산→저장→재열기 | 같은 stable ID·저장revision·데이터·계산 전제, 서버 요청 없이 핵심 흐름 |
| FILE-T02 | 다른 이름 저장 후 새 파일만 편집 | 원본 bytes/revision 유지, 새 문서 계보·ID 규칙 확인 |
| FILE-T03 | Windows→macOS→Windows로 정상 저장 파일 한 개 전달 | 지원matrix에서 의미·참조·기준선·실적 보존, cache 없는 재계산 비교 |
| FILE-T04 | embed 첨부/외부 첨부/외부 프로젝트 포함 후 이동 | embed 보존, 외부 누락·만료 명시, 전체forecast 불완전 전파 |
| FILE-T05 | 저장 단계별 강제종료·디스크부족·권한거절·장치제거 | 이전 정상본 보존, 성공 오표시 없음, 복구 가능한revision과 손실 구간 설명 |
| FILE-T06 | 두 프로세스 열기·외부수정·stale lock·동기화 충돌 | 무음 덮어쓰기/자동병합 없음, 각 사본 보존과 선택 경로 |
| FILE-T07 | DB후보의 미반영 변경이 있는 상태에서 전달 파일 생성 | 보조파일 없이도 완료revision 재현, main-only복사 오류를 검출 |
| FILE-T08 | 구버전 이관·신버전 미지원·이관 중단 | 원본/백업 유지, 참조·단위·actual/baseline 검증, 안전 거절 |
| FILE-T09 | Unicode/긴경로/대소문자/상대링크/OS 권한 차이 | 명시 오류 또는 지원 범위 내 복원, 원본 의미·외부누락 보존 |

FILE-10: 위 시험은 설계 수용조건이며 아직 실행하지 않았다. 단계 A에서 파일 형식·오류 계약, B에서 로컬 lifecycle·복구, C에서 충돌·선택 협업 경계, F에서 OS별 이관·전달·배포 검증을 수행한다. 파일 기능 구현·구매·배포는 현재 승인 범위가 아니다.

## 2026-10-26 구현 요청과 검증 범위 갱신

FILE-10의 미실행/구현 미승인은 초기 기획 시점 기록이다. 당일 사용자 요청으로 새로 만들기/열기/저장과 후속 기능의 구현 착수가 승인되었으며, 이번 자동 저장·비정상 종료 복구·실행 취소 요청은 해당 범위의 이전 미승인 상태를 대체한다. 구매·서명·제품 배포나 전체 FILE-T01~09 완료를 뜻하지 않는다.

FILE-06은 별도 사용자 로컬 완료/해시 체크포인트, 정상 3세대·원본 저장 지점/변경 요약, 프로세스 잠금, 선택 후 독립 사본 복구로 구현했다. FILE-T05/06의 일부를 실제 프로세스 kill과 flush/commit 사이 kill, 이전 정상본 보존·살아 있는 프로세스 제외·손상/미지원·원본 변경/누락·저장 경쟁으로 검증한다. 물리 전원/디스크 고장·네트워크 파일시스템·동기화·첨부/DB/WAL 등 나머지 조건은 별도 게이트다. 세부 계약은 [자동 저장·복구·편집 이력](../recovery-undo.md)을 따른다.

PDM·작업 달력 후속 구현은 schema 7이며 schema 1~6을 기존 계산 정책으로 읽고 원본 보존 SaveAs를 요구한다. 구 승인 기준선 입력/결과와 새 달력 스냅샷을 함께 검증한다. 복구 envelope schema 1의 projectSchema 6/7을 지원한다. [PDM·달력 계약](../pdm-calendars.md)에 확장 필드와 계산 좌표/호환 범위를 기록한다.

제약/마감 증분은 schema 8로 작업 날짜 제약·마감을 저장한다. schema 1~7 원본·당시 계산 정책을 보존하며, 복구 envelope schema 1의 내부 projectSchema 6/7/8을 지원한다. [제약·간트 파일/편집 계약](../constraints-gantt.md)을 따른다.

공수/작업 유형·자원 달력·분할 증분은 schema 9다. 새 계획 정책의 입력과 기준선·시나리오, 명시 잔여 공수를 보존한다. schema 1~8은 기록된 기준선/시나리오 엔진과 원본을 보존해 SaveAs로 이관하며 복구 projectSchema 6/7/8/9를 지원한다. [공수·분할 파일/계산 계약](../effort-calendars-splits.md)을 따른다.

2026-10-30 마일스톤 경계 보정은 현재 계산에 `jobs-pdm-1.2` / `jobs-effort-1.1`과 새 평준화 v2 정책을 사용한다. 기존 기준선의 `jobs-pdm-1.0/1.1`, `jobs-effort-1.0`과 기존 평준화 v1은 별도 재현 경로로 검증한다. 입력 정책과 schema 9는 유지하며 지원하지 않는 엔진/정책, 입력 정책과 맞지 않는 엔진은 거절한다. 과거 승인 결과와 메타데이터를 새 엔진으로 덮어쓰지 않는다.

평준화 채택 증분은 조건부 schema12다. 현재 또는 동결 기준선/시나리오의 계획 채택 설정이 있으면 12를 쓰고, 없으면 기존 11/10/9 선택을 유지한다. schema1~12 읽기와 복구 projectSchema12를 지원한다. 채택 정책·출처·재생/무효화는 [상세 계약](../leveling-adoption.md)을 따른다.

GAP-07 인쇄는 불변 메모리 스냅샷의 파생 출력이다. 페이지 설정은 대화상자 범위이며 프로젝트 파일/schema·기준선·실적·일반 편집 이력을 바꾸지 않는다. 조건부 schema9/10/11/12 writer를 유지한다. [인쇄 계약](../schedule-printing.md).

진척 정책 채택은 조건부 schema13이다. 프로젝트 전용 progressPolicy에 정책 ID·담당자·사유·UTC·검토 문서 ID/revision을 저장하고 PlanInput/기준선에는 포함하지 않는다. schema1~14와 복구 projectSchema14까지 읽으며 선택이 없으면 종전 조건부 writer를 유지한다. 알 수 없는 정책/필드·null·잘못된 메타데이터는 거절한다. 조합/시나리오 정책은 schema14에서만 허용한다. [채택 계약](../progress-policy-adoption.md).

조합 증분은 조건부 schema14다. 현재 progressPolicy와 planLeveling/remainingLeveling이 함께 있거나 어느 저장 시나리오에 progressPolicy가 있으면 14를 쓴다. LevelingScenario.progressPolicy의 정책 메타데이터는 원본 snapshot revision 범위에서 검증한다. schema13 이하는 조합/시나리오 정책을 거절하며 복구 projectSchema14를 지원한다. [상세 계약](../progress-policy-leveling.md).

자원 순서 여유는 입력 schema를 추가하지 않는다. 새 평준화 계획 기준선 엔진 jobs-adopted-leveling-1.8을 허용하고 기존1.0은 미산정, 1.1은 전체 구간, 1.2는 실행 창 유지, 1.3은64개 제한, 1.4는 이동 가능128개 제한, 1.5는 당시 후보 결합/인증 방식, 1.6은 당시 상한·이웃 전파/인증 공유 방식, 1.7은 당시 비단조 필수 부하·연쇄 전파/상한으로 별도 재현한다. 교차 분할 실행 창·인계 시각·후보/고정/탐색 단위·전체 재배치/진단 미리보기·1.6 가역 후보 제외/끝점 캐시/인증 공유와1.7 점별 필수 수요/연쇄 전파·1.8 비용 추정/실패 관측/효과·재시도 상태는 비영속 파생 정보다. 과거 승인 수치를 새 엔진으로 덮어쓰지 않는다. [계약](../leveling-float.md).

총여유 근거 창의 구조화된 levelingWitnesses도 비영속 정보다. 원본 입력/정책으로 다시 계산하며 파일 schema나 기준선 수치 계약을 바꾸지 않는다. 근거 탐색만으로 dirty/revision·기준선·실적 원장이 변경되지 않는다. [화면·입력 보존 계약](../leveling-witness.md).

2026-11-05 결정 원장이 있으면 조건부 schema15다. Project.levelingDecisions에 전후 선택·상태일·문서/revision·UTC·사유·이전 결정 ID를 보존한다. schema1~15와 복구 projectSchema15를 읽고 원장이 없는 입력은 기존 writer 조건을 유지한다. undo/redo는 원장 삭제 없이 새 결정을 추가한다. 알 수 없는 필드·잘못된 타입/순서/연결·현재 선택과의 불일치·10,000건 초과를 거절한다. [계약](../leveling-decisions.md).

2026-11-05 조건부 schema16: PlanInput.projectFields/resourceFields/projectAttributes와 Resource.attributes를 선택적으로 저장한다. 현재 또는 어느 기준선/시나리오 입력에 이 데이터가 있으면 16을 쓴다. schema1~16과 복구 projectSchema16을 읽고 데이터가 없으면 기존 9~15 writer 조건을 유지한다. 구 schema의 객체 필드·잘못된 참조/형식/중복/범위/미지원 속성은 거절한다. [계약](../object-custom-fields.md).

2026-11-05 기간별 배정 증분: 현재/기준선/시나리오의 Assignment.periods가 있으면 조건부 schema17을 사용한다. 정렬된 반열린 작업 근무분 구간과 정수 대체 배정률을 보존하며 구 schema는 해당 속성을 거절한다. schema1~17 및 recovery projectSchema17을 지원한다. 구간이 없는 입력의 writer/엔진 선택은 유지한다. [기간별 배정·곡선 계약](../assignment-periods.md).

2026-11-05 조건부 schema18: Project.printPreferences/networkPositions는 계산 입력 밖의 표시 정보다. 기본 페이지 설정과 안정 작업 ID의 정수 좌표를 저장·검증하고 undo/dirty/사본/복구에 포함한다. schema1..18 및 recovery projectSchema18을 지원하고 구 schema는 새 필드를 거절한다. [상세 계약](../print-network-extension.md).

2026-11-05: [자재·비용·역할과 통합 일정](../resource-plan-integrated-workspace.md)의 schema20/작업공간 schema2 native 확장과 지원 범위를 추가했다.

2026-11-05: [조직·위험·자동화 계약](../organization-risk-automation.md)의 작업공간 schema3는 선택적 governance(조직/구성원/역할/포트폴리오/협업/감사/위험)를 보존한다. 작업공간1/2 호환과 기존 프로젝트/복구 schema를 유지한다. 로컬 구성원 선택과 계산 결과는 비영속이다.
