# Qt 한국어 번역

Qt 6.8.3의 `qtbase_ko.qm`을 앱 리소스에 포함하므로 빌드·실행 시 네트워크나 Linguist 설치가 필요 없습니다.
입력칸 메뉴, Qt 파일 대화상자, 메시지 상세 버튼과 표준 버튼에 적용합니다.
제품의 ‘저장 안 함’ 표현은 작은 후순위 번역기로 유지하며 날짜·숫자 로캘은 변경하지 않습니다.

원본 revision·SHA-256·생성 옵션은 [manifest.json](manifest.json), 출처와 라이선스는 [NOTICE.txt](NOTICE.txt)를 따릅니다.
Qt 공식 번역의 미완료 항목은 포함하지 않으며 모든 Qt 문구의 번역 완료를 의미하지 않습니다.
실행 파일 옆 `translations/qtbase_ko.qm`에 호환 카탈로그를 두면 내장본보다 우선합니다.

재생성에는 Qt 6.8.3의 `lrelease`가 필요합니다. 아래 명령은 고정 원본과 생성 결과 해시를 확인합니다.

```powershell
python scripts/prepare-qt-translations.py --lrelease path/to/lrelease.exe
```
