# XML 계산식 Format·StrConv

2026-11-13 · 사용자 필드의 **XML 계산식…**에서 숫자·날짜 표시와 문자 변환을 실행한다. 결과는 Text이다. 공수·원가·날짜 원본이나 통화 단위는 바꾸지 않는다.

```text
Format([입력금액], "#,##0.00")
Format([Start], "yyyy-mm-dd hh:nn:ss")
PPMFormat([Start], "Long Date", 1042)
StrConv("hello WORLD", 3, 1042)
StrConv("ｶﾞ ABC", 36, 1041)
```

| 함수 | 규칙 |
|---|---|
| `Format(value[, mask[, firstDay[, firstWeek]]])` | 고정 en-US 표시. PC 로캘을 조회하지 않는다. |
| `PPMFormat(value, mask, LCID[, firstDay[, firstWeek]])` | PPM 확장. 고정 프로필1033(en-US)·1041(ja-JP)·1042(ko-KR)를 명시한다. |
| `StrConv(text, conversion[, LCID])` | 기본1033. 고정 Unicode16.0 자료와 명시 변환을 사용한다. |

Microsoft의 [Format](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/format-function-visual-basic-for-applications), [StrConv](https://learn.microsoft.com/en-us/office/vba/language/reference/user-interface-help/strconv-function) 문서를 바탕으로 구현한 **명시 지원 부분**이다. OS 기본 로캘·코드페이지·사용자 지역 설정·달력 설정을 재현하는 전체 VBA 실행기가 아니다. PPMFormat은 Microsoft Project 표준 함수가 아니다. 외부 제품 전체와의 계산 동등성은 미검증이다.

## 표시 형식

숫자는 General Number, Fixed, Standard, Percent, Scientific, Currency, Yes/No, True/False, On/Off를 지원한다. Currency는 프로필의 달러·엔·원 표시이며 문서 통화 변경이나 환산이 아니다. en-US는2자리와 괄호 음수, ja-JP/ko-KR는0자리와 마이너스를 사용한다. 숫자는 고정6자리 입력을 정확한 십진 문자열로 계산하며 표시 반올림은 절댓값을 올리는 반올림이다. 기존 Round/CInt의 짝수 반올림과 구분한다.

사용자 숫자 형식은 `0/#`, 소수점, 정수부 그룹 쉼표, 정수부 마지막의1000배 축소 쉼표, `%`, `E+/E-/e+/e-`를 지원한다. 소수 최대6자리, 필수 정수 최대32자리, 배율 최대8개, 지수1~3자리이다. 소수점 앞에 자리표시자가 필요하다. 숫자 자리표시자 사이의 임의 리터럴, 소수부 쉼표, 조건·색상·채움 형식은 거절한다. 예: `0,,.00`은백만 단위, `0.00%`는100배 표시다. `0.##`에서 선택 소수 숫자가 모두 없어져도 소수점은 남는다.

최대4개 구간은 양수·음수·0·Null이다. 음수/0 구간이 비면 첫 구간을 적용한다. 명시 음수 구간은 절댓값을 받고 자동 마이너스가 붙지 않는다. Null 구간이 없으면 미설정이다. 리터럴은 큰따옴표 또는 역슬래시로 감싼다. 식 문자열 안의 큰따옴표는 두 번 쓴다.

Date 값은 General/Long/Medium/Short Date와 Long/Medium/Short Time을 지원한다. 사용자 토큰은 `yyyy/yy/y`, `m/mm/mmm/mmmm`, `d/dd/ddd/dddd/ddddd/dddddd`, `h/hh`, `n/nn`, `s/ss`, `q`, `w/ww`, `AM/PM`, `a/p`, `AMPM`, `c`, `ttttt`이다. h/hh 다음의 m/mm은 분이다. AM/PM·A/P의 첫 글자가 소문자이면 am/pm·a/p로 표시한다. 프로필별 월·요일 이름과 날짜 구분자를 사용한다. 사용자 리터럴은 대소문자를 보존한다. Gregorian1~9999년 범위이며 소수 초는 표시하지 않는다. 소수 초를 보존하려면 CStr, 오프셋을 보존하려면 PPMTimeZoneText를 사용한다. 숫자 OLE 날짜·숫자 문자열을 날짜로 자동 해석하지 않는다. ISO 문자열은 날짜 표시 전에 CDate로 명시 변환한다.

firstDay는1(일)~7(토), firstWeek는1(1월1일 포함)·2(최소4일)·3(온전한 첫 주)이다. 0은각 고정 프로필의 기본값1이다. 이전 연도에 속하는 첫 주 이전 날짜는 이전 연도의 주 번호를 사용한다. 날짜 여러 구간과 미지원 토큰은 오류다.

문자열은 `@`(빈 자리 공백)·`&`(선택 자리), `!`(왼쪽부터 채움), `<`/`>`(유니코드 소문자/대문자), 빈 문자열/Null을 위한 두 번째 구간을 지원한다. 기본은 오른쪽 맞춤이며 초과 문자는 마지막 자리 뒤에 보존한다. `!`에서 입력이 자리 수보다 길면 잘라내지 않고 오류다. 문자열의 숫자/날짜 자동 형식 변환은 하지 않는다.

## 문자 변환

conversion은0(유지),1(대문자),2(소문자),3(단어 첫 문자),4(전각),8(반각),16(가타카나),32(히라가나) 및 유효한 합계이다. 4와8,16과32를 함께 지정하면 오류다. 4/8은1041·1042,16/32는1041에서만 실행한다. Unicode/FromUnicode의64/128 및 그 밖의 비트는 지원하지 않는다. 기본 코드페이지나 LCID0을 추측하지 않는다.

단어 첫 문자 변환은 공백·탭·개행·전각 공백을 경계로 삼는다. 하이픈·아포스트로피는 경계가 아니다. 숫자로 시작하면 뒤 문자는 소문자가 된다. 전체 유니코드 대소문자 매핑과 문맥에 따른 그리스어 마지막 시그마를 지원한다. 대문자 변환에서 ß→SS처럼 길이가 늘어날 수 있다. 대소문자→전각→가나→반각 순서로 적용한다. 반각 가나를 히라가나로 바꾸려면36(전각+히라가나)을 쓴다. 반각 한글은 호환 자모로 매핑하며 다른 호환 문자를 NFKC로 일괄 정규화하지 않는다. 결합 탁점·반탁점은 전각 가나의 합성에 반영한다.

입력은 유효한 Unicode scalar UTF-8이어야 한다. 보충 평면과 이모지를 보존한다. 문자열 입력·결과는4,096 bytes, 형식은256 UTF-8 bytes이다. 잘못된 옵션·범위·UTF-8은 오류이며 저장된 스냅샷으로 대체하지 않는다. Null과 계산 오류를 구분한다. 모든 인수는 기존과 같이 평가한다.

자료는 [Unicode16.0 UCD](https://www.unicode.org/Public/16.0.0/ucd/)이며 [manifest](../resources/text/manifest.json), [생성기](../scripts/generate-unicode-format-data.py), [Unicode License V3](../resources/text/LICENSE.txt)를 보관한다. 실행 파일에 내장하므로 Python·OS Unicode 자료는 실행 시 필요 없다. Windows 패키지의 `licenses/unicode`에도 출처와 라이선스를 포함한다. OS NLS 버전에 따른 매핑 차이를 자동 추종하지 않는다. Windows LCMapStringEx의 en-US 제목 변환은 하이픈과 숫자 단어를 다르게 처리할 수 있다. 이 구현은 VBA 문서의 명시 경계와 첫 문자 규칙을 고정하며 해당 차이를 검증 기록에 남긴다.

## 저장·교환·화면

활성 식은 schema54가 필요하다. 프로젝트·작업·자원·배정과 동결 기준선·시나리오·채택/통화/ID 이력의 식에도 같은 버전 제한을 적용한다. 비활성 원문은 기존 보존 규칙대로 남기며 자동 활성화하지 않는다. 편집기의 예시·명시 프로필 선택, 비동기 미리보기, 저장·취소·실행 취소, 오래된 편집 방지, 읽기 전용 검증을 사용한다.

XML은 지원 식 정의와 재계산된 문자열 스냅샷을 내보내고 참조를 실제 필드 이름으로 재작성한다. 가져오기는 지원 식을 활성화하며 지원하지 않는 식은 비활성 보존한다. 형식이나 옵션이 잘못된 개별 행은 계산 오류로 남는다. XER와 XLSX/PDF 보고서는 기존 사용자 필드 경로를 통해 계산 결과를 사용하며 XER에서 실행 가능한 VBA로 변환하지 않는다.
