#include "domain/XmlCalendar.h"
#include "ui/XmlFormulaDialog.h"
#include "ui/EditorUi.h"
#include "domain/Classification.h"
#include "domain/FormulaFields.h"
#include "domain/XmlFormula.h"
#include "domain/XmlRollDown.h"
#include "calculation/ScheduleFormulaScope.h"
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <array>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
namespace jobs {
namespace {
QString q(const std::string &s) {
    return QString::fromStdString(s);
}
std::string reference(const std::string &prefix, const std::string &id) {
    std::string out = "[" + prefix;
    for (char c : id) {
        if (c == ']' || c == '\\')
            out += '\\';
        out += c;
    }
    return out + "]";
}
} // namespace
void editXmlFormula(Document &doc, FieldScope scope, const std::string &key, QWidget *parent) {
    if (!doc.project() || doc.readOnly())
        return;
    const auto input = doc.snapshot();
    const auto *field = customField(*input, key, scope);
    if (!field)
        return;
    const auto session = doc.sessionGeneration(), revision = input->revision;
    auto guard = [&] {
        if (!doc.project() || doc.readOnly() || doc.sessionGeneration() != session ||
            doc.project()->documentId != input->documentId || doc.project()->revision != revision)
            throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
    };
    QDialog dialog(parent);
    dialog.setObjectName("xmlFormulaDialog");
    dialog.setWindowTitle("XML 외부 계산식 · " + q(field->name));
    auto *body = new QVBoxLayout(&dialog);
    auto *enabled = new QCheckBox("native에서 지원 계산식 실행");
    enabled->setObjectName("xmlFormulaEnabled");
    enabled->setChecked(field->xmlFormula ? field->xmlFormula->enabled : true);
    body->addWidget(enabled);
    auto *source = new QPlainTextEdit;
    source->setObjectName("xmlFormulaSource");
    source->setPlainText(field->xmlFormula                      ? q(field->xmlFormula->source)
                         : field->type == CustomFieldType::Text ? "\"\""
                                                                : "0");
    source->setMaximumHeight(110);
    body->addWidget(source);
    auto *row = new QHBoxLayout;
    auto *refs = new QComboBox;
    refs->setObjectName("xmlFormulaReferences");
    auto add = [&](FieldScope s, const std::string &prefix) {
        for (const auto &f : customFields(*input, s))
            if (f.type != CustomFieldType::Choice && !(s == scope && "field:" + f.id == key))
                refs->addItem(q(prefix + f.name), q(reference(s == scope                 ? "field:"
                                                              : s == FieldScope::Project ? "project:"
                                                              : s == FieldScope::Task    ? "task:"
                                                                                         : "resource:",
                                                              f.id)));
    };
    add(scope, "");
    if (scope == FieldScope::Assignment)
        refs->addItem("기본 배정률 · 1=100%", "[Units]");
    if (scope == FieldScope::Resource || scope == FieldScope::Assignment) {
        refs->addItem("기본 자원 가용률 · 1=100%",
                      scope == FieldScope::Resource ? "[Max Units]" : "[Resource.Max Units]");
        refs->addItem("명시 날짜 가용률 · 예외/기본값", "PPMUnitAvailability([Project Start])");
        refs->addItem("현재 일정의 배정 수요 · 1=100%", "PPMResourceLoad([Project Start])");
        refs->addItem("현재 부하 차감 잔여 가용량 · 음수는 부족", "PPMRemainingAvailability([Project Start])");
    }
    if (scope != FieldScope::Project)
        add(FieldScope::Project, "프로젝트 · ");
    if (scope == FieldScope::Assignment) {
        add(FieldScope::Task, "작업 · ");
        add(FieldScope::Resource, "자원 · ");
    }
    if (scope == FieldScope::Task || scope == FieldScope::Project) {
        refs->addItem("계산된 시작", "[Start]");
        refs->addItem("계산된 종료", "[Finish]");
    }
    refs->addItem("프로젝트 계산 시작", "[Project.Start]");
    refs->addItem("프로젝트 계산 종료", "[Project.Finish]");
    if (scope == FieldScope::Assignment) {
        refs->addItem("작업 계산 시작", "[Task.Start]");
        refs->addItem("작업 계산 종료", "[Task.Finish]");
    }
    if (scope == FieldScope::Task || scope == FieldScope::Project) {
        refs->addItem("실제 시작", "[Actual Start]");
        refs->addItem("실제 완료", "[Actual Finish]");
    }
    refs->addItem("프로젝트 실제 시작", "[Project.Actual Start]");
    refs->addItem("프로젝트 실제 완료", "[Project.Actual Finish]");
    if (scope == FieldScope::Assignment) {
        refs->addItem("작업 실제 시작", "[Task.Actual Start]");
        refs->addItem("작업 실제 완료", "[Task.Actual Finish]");
        refs->addItem("작업 보고 실제 기간", "[Task.Actual Duration]");
        refs->addItem("작업 보고 실제 공수", "[Task.Actual Work]");
        refs->addItem("자원 보고 실제 공수", "[Resource.Actual Work]");
    }
    if (scope == FieldScope::Task || scope == FieldScope::Project)
        refs->addItem("보고 실제 기간", "[Actual Duration]");
    refs->addItem("보고 실제 공수", "[Actual Work]");
    refs->addItem("프로젝트 보고 실제 공수", "[Project.Actual Work]");
    if (scope == FieldScope::Task || scope == FieldScope::Project)
        refs->addItem("예측 잔여 기간", "[Remaining Duration]");
    refs->addItem("예측 잔여 공수", "[Remaining Work]");
    refs->addItem("프로젝트 예측 잔여 공수", "[Project.Remaining Work]");
    if (scope == FieldScope::Assignment) {
        refs->addItem("작업 예측 잔여 기간", "[Task.Remaining Duration]");
        refs->addItem("작업 예측 잔여 공수", "[Task.Remaining Work]");
        refs->addItem("자원 예측 잔여 공수", "[Resource.Remaining Work]");
    }
    if (scope == FieldScope::Task || scope == FieldScope::Project) {
        refs->addItem("최초 기준선 시작", "[Baseline Start]");
        refs->addItem("최초 기준선 완료", "[Baseline Finish]");
    }
    if (scope == FieldScope::Task || scope == FieldScope::Project)
        refs->addItem("최초 기준선 기간", "[Baseline Duration]");
    refs->addItem("최초 기준선 공수", "[Baseline Work]");
    refs->addItem("최초 기준선 원가", "[Baseline Cost]");
    refs->addItem("프로젝트 최초 기준선 원가", "[Project.Baseline Cost]");
    refs->addItem("프로젝트 실제 요약 기간", "[Project.Actual Duration]");
    refs->addItem("프로젝트 잔여 요약 기간", "[Project.Remaining Duration]");
    refs->addItem("프로젝트 최초 기준선 요약 기간", "[Project.Baseline Duration]");
    refs->addItem("프로젝트 최초 기준선 시작", "[Project.Baseline Start]");
    refs->addItem("프로젝트 최초 기준선 완료", "[Project.Baseline Finish]");
    refs->addItem("프로젝트 최초 기준선 공수", "[Project.Baseline Work]");
    if (scope == FieldScope::Assignment) {
        refs->addItem("작업 최초 기준선 시작", "[Task.Baseline Start]");
        refs->addItem("작업 최초 기준선 완료", "[Task.Baseline Finish]");
        refs->addItem("작업 최초 기준선 기간", "[Task.Baseline Duration]");
        refs->addItem("작업 최초 기준선 공수", "[Task.Baseline Work]");
        refs->addItem("자원 최초 기준선 공수", "[Resource.Baseline Work]");
        refs->addItem("작업 최초 기준선 원가", "[Task.Baseline Cost]");
        refs->addItem("자원 최초 기준선 원가", "[Resource.Baseline Cost]");
    }
    for (int slot = 0; slot <= 10; ++slot) {
        const auto prefix = "Baseline" + QString::number(slot);
        const auto label = prefix + " · 승인본 v" + QString::number(slot + 1);
        for (const auto *metric : {"Start", "Finish", "Duration", "Work", "Cost"}) {
            const auto suffix = " " + QString::fromLatin1(metric);
            const bool quantity = suffix == " Work" || suffix == " Cost";
            if (quantity || scope == FieldScope::Task || scope == FieldScope::Project)
                refs->addItem(label + suffix, "[" + prefix + suffix + "]");
            refs->addItem("프로젝트 " + label + suffix, "[Project." + prefix + suffix + "]");
            if (scope == FieldScope::Assignment) {
                refs->addItem("작업 " + label + suffix, "[Task." + prefix + suffix + "]");
                if (quantity)
                    refs->addItem("자원 " + label + suffix, "[Resource." + prefix + suffix + "]");
            }
        }
    }
    row->addWidget(refs, 1);
    for (const auto *name : {"Early Start", "Early Finish", "Late Start", "Late Finish"}) {
        const auto label = QString::fromLatin1(name);
        if (scope == FieldScope::Task || scope == FieldScope::Project)
            refs->addItem(label, "[" + label + "]");
        refs->addItem("프로젝트 · " + label, "[Project." + label + "]");
        if (scope == FieldScope::Assignment)
            refs->addItem("작업 · " + label, "[Task." + label + "]");
    }
    auto *insert = new QPushButton("참조 삽입");
    row->addWidget(insert);
    auto *preview = new QPushButton("수식 검사·미리보기");
    preview->setObjectName("previewXmlFormula");
    row->addWidget(preview);
    body->addLayout(row);
    auto *calendarRow = new QHBoxLayout;
    auto *calendarFunction = new QComboBox;
    calendarFunction->setObjectName("xmlCalendarFunction");
    calendarFunction->addItem("근무분 차이", "ProjDateDiff");
    calendarFunction->addItem("근무기간 더하기", "ProjDateAdd");
    calendarFunction->addItem("근무기간 빼기", "ProjDateSub");
    auto *calendar = new QComboBox;
    calendar->setObjectName("xmlFormulaCalendar");
    calendar->addItem("필드 기본 달력", "");
    for (const auto &c : input->calendars)
        calendar->addItem(q(c.name), q(c.name));
    auto *insertCalendar = new QPushButton("달력 식 삽입");
    insertCalendar->setObjectName("insertXmlCalendarFormula");
    calendarRow->addWidget(calendarFunction, 1);
    calendarRow->addWidget(calendar, 1);
    calendarRow->addWidget(insertCalendar);
    body->addLayout(calendarRow);
    QObject::connect(insertCalendar, &QPushButton::clicked, &dialog, [&] {
        const QString start = scope == FieldScope::Assignment ? "[Task.Start]"
                              : scope == FieldScope::Resource ? "[Project.Start]"
                                                              : "[Start]";
        const QString finish = scope == FieldScope::Assignment ? "[Task.Finish]"
                               : scope == FieldScope::Resource ? "[Project.Finish]"
                                                               : "[Finish]";
        auto name = calendar->currentData().toString();
        name.replace("\"", "\"\"");
        auto fn = calendarFunction->currentData().toString();
        source->insertPlainText(fn + "(" + (fn == "ProjDateSub" ? finish : start) + ", " +
                                (fn == "ProjDateDiff" ? finish : "\"1d\"") +
                                (name.isEmpty() ? QString{} : ", \"" + name + "\"") + ")");
    });
    auto *functionRow = new QHBoxLayout;
    auto *function = new QComboBox;
    function->setObjectName("xmlAdditionalFunction");
    for (const auto &example :
         {QString("Choose(2, \"첫째\", \"둘째\")"), QString("InStr(1, \"ABa\", \"a\", 0)"),
          QString("StrComp(\"A\", \"a\", 1)"), QString("LTrim(\"  값  \")"), QString("RTrim(\"  값  \")"),
          QString("Space(3)"), QString("String(3, \"가\")"), QString("Asc(\"A\")"), QString("Chr(65)"),
          QString("CByte(2.5)"), QString("CCur(1.23456)"), QString("DateValue(#2026-11-13T09:30:00#)"),
          QString("Sqr(2)")})
        function->addItem(example.left(example.indexOf('(')), example);
    if (scope == FieldScope::Resource || scope == FieldScope::Assignment)
        function->addItem("PPMUnitAvailability · 날짜별 가용률", "PPMUnitAvailability([Project Start])");
    auto *insertFunction = new QPushButton("추가 함수 예시 삽입");
    insertFunction->setObjectName("insertXmlAdditionalFunction");
    functionRow->addWidget(function, 1);
    functionRow->addWidget(insertFunction);
    body->addLayout(functionRow);
    QObject::connect(insertFunction, &QPushButton::clicked, &dialog,
                     [&] { source->insertPlainText(function->currentData().toString()); });
    auto *projectConversionRow = new QHBoxLayout;
    auto *projectConversionFunction = new QComboBox;
    projectConversionFunction->setObjectName("xmlProjectConversionFunction");
    for (const auto &example :
         {QString("ProjDateValue(\"2026-11-14T09:30:12.123456\")"), QString("ProjDateConv(#2026-11-14T09:30:00#, 20)"),
          QString("ProjDurConv(90, 5)"), QString("ProjDurConv(\"2ed?\", 40)")})
        projectConversionFunction->addItem(example, example);
    auto *insertProjectConversion = new QPushButton("Project 변환 예시 삽입");
    insertProjectConversion->setObjectName("insertXmlProjectConversionFormula");
    projectConversionRow->addWidget(projectConversionFunction, 1);
    projectConversionRow->addWidget(insertProjectConversion);
    body->addLayout(projectConversionRow);
    QObject::connect(insertProjectConversion, &QPushButton::clicked, &dialog,
                     [=] { source->insertPlainText(projectConversionFunction->currentData().toString()); });
    auto *conversionRow = new QHBoxLayout;
    auto *conversionFunction = new QComboBox;
    conversionFunction->setObjectName("xmlConversionFunction");
    for (const auto &example : {QString("CDec(\"1.234567\")"), QString("CSng(16777217)"), QString("CSng(1.234567)"),
                                QString("CVar(\"원문\")")})
        conversionFunction->addItem(example, example);
    auto *insertConversion = new QPushButton("형 변환 함수 예시 삽입");
    insertConversion->setObjectName("insertXmlConversionFormula");
    conversionRow->addWidget(conversionFunction, 1);
    conversionRow->addWidget(insertConversion);
    body->addLayout(conversionRow);
    QObject::connect(insertConversion, &QPushButton::clicked, &dialog,
                     [=] { source->insertPlainText(conversionFunction->currentData().toString()); });
    auto *stringRow = new QHBoxLayout;
    auto *stringFunction = new QComboBox;
    stringFunction->setObjectName("xmlStringFunction");
    for (const auto &example :
         {QString("Replace(\"abcabc\", \"ab\", \"X\")"), QString("Replace(\"ABCabc\", \"ab\", \"X\", 2, 1, 1)"),
          QString("InStrRev(\"ababa\", \"aba\", 4)"), QString("StrReverse(\"가나다\")")})
        stringFunction->addItem(example, example);
    auto *insertString = new QPushButton("문자열 함수 예시 삽입");
    insertString->setObjectName("insertXmlStringFormula");
    stringRow->addWidget(stringFunction, 1);
    stringRow->addWidget(insertString);
    body->addLayout(stringRow);
    QObject::connect(insertString, &QPushButton::clicked, &dialog,
                     [=] { source->insertPlainText(stringFunction->currentData().toString()); });
    auto *depreciationRow = new QHBoxLayout;
    auto *depreciation = new QComboBox;
    depreciation->setObjectName("xmlDepreciationFunction");
    for (const auto &example :
         {QString("SLN(100,10,5)"), QString("SYD(100,10,5,1)"), QString("SYD(100,10,5,5)"), QString("SLN(1,0,3)")})
        depreciation->addItem(example, example);
    auto *insertDepreciation = new QPushButton("감가상각 예시 삽입");
    insertDepreciation->setObjectName("insertXmlDepreciationFormula");
    depreciationRow->addWidget(depreciation, 1);
    depreciationRow->addWidget(insertDepreciation);
    body->addLayout(depreciationRow);
    QObject::connect(insertDepreciation, &QPushButton::clicked, &dialog,
                     [=] { source->insertPlainText(depreciation->currentData().toString()); });
    auto *partitionRow = new QHBoxLayout;
    auto *partition = new QComboBox;
    partition->setObjectName("xmlPartitionFunction");
    for (const auto &example : {QString("Partition(12,0,99,10)"), QString("Partition(-1,0,99,5)"),
                                QString("Partition(1010,100,1010,20)"), QString("Partition(2.5,0,9,2.5)")})
        partition->addItem(example, example);
    auto *insertPartition = new QPushButton("수치 구간 예시 삽입");
    insertPartition->setObjectName("insertXmlPartitionFormula");
    partitionRow->addWidget(partition, 1);
    partitionRow->addWidget(insertPartition);
    body->addLayout(partitionRow);
    QObject::connect(insertPartition, &QPushButton::clicked, &dialog,
                     [=] { source->insertPlainText(partition->currentData().toString()); });
    auto *colorRow = new QHBoxLayout;
    auto *color = new QComboBox;
    color->setObjectName("xmlColorFunction");
    for (const auto &example :
         {QString("RGB(255,0,0)"), QString("RGB(0,255,0)"), QString("RGB(0,0,255)"), QString("QBColor(15)")})
        color->addItem(example, example);
    auto *insertColor = new QPushButton("색상 코드 예시 삽입");
    insertColor->setObjectName("insertXmlColorFormula");
    colorRow->addWidget(color, 1);
    colorRow->addWidget(insertColor);
    body->addLayout(colorRow);
    QObject::connect(insertColor, &QPushButton::clicked, &dialog,
                     [=] { source->insertPlainText(color->currentData().toString()); });
    auto *numericDisplayRow = new QHBoxLayout;
    auto *numericDisplay = new QComboBox;
    numericDisplay->setObjectName("xmlNumericDisplayFunction");
    for (const auto &example : {QString("FormatNumber(1234.5)"), QString("FormatPercent(0.125,1)"),
                                QString("FormatCurrency(-1234.5)"), QString("FormatNumber(-0.5,2,0,-1,0)")})
        numericDisplay->addItem(example, example);
    auto *insertNumericDisplay = new QPushButton("숫자 표시 예시 삽입");
    insertNumericDisplay->setObjectName("insertXmlNumericDisplayFormula");
    numericDisplayRow->addWidget(numericDisplay, 1);
    numericDisplayRow->addWidget(insertNumericDisplay);
    body->addLayout(numericDisplayRow);
    QObject::connect(insertNumericDisplay, &QPushButton::clicked, &dialog,
                     [=] { source->insertPlainText(numericDisplay->currentData().toString()); });
    auto *dateDisplayRow = new QHBoxLayout;
    auto *dateDisplay = new QComboBox;
    dateDisplay->setObjectName("xmlDateDisplayFunction");
    for (const auto &example :
         {QString("FormatDateTime(#2024-02-29T13:04:05#,3)"), QString("FormatDateTime(#2024-02-29#,1)"),
          QString("FormatDateTime(TimeValue(\"13:04:05\"))"), QString("FormatDateTime(#2024-02-29T13:04:05#,4)")})
        dateDisplay->addItem(example, example);
    auto *insertDateDisplay = new QPushButton("날짜·시각 표시 예시 삽입");
    insertDateDisplay->setObjectName("insertXmlDateDisplayFormula");
    dateDisplayRow->addWidget(dateDisplay, 1);
    dateDisplayRow->addWidget(insertDateDisplay);
    body->addLayout(dateDisplayRow);
    QObject::connect(insertDateDisplay, &QPushButton::clicked, &dialog,
                     [=] { source->insertPlainText(dateDisplay->currentData().toString()); });
    auto *dateNameRow = new QHBoxLayout;
    auto *dateName = new QComboBox;
    dateName->setObjectName("xmlDateNameFunction");
    for (const auto &example : {QString("MonthName(11)"), QString("MonthName(11,True)"),
                                QString("WeekdayName(1,False,2)"), QString("WeekdayName(7,True,1)")})
        dateName->addItem(example, example);
    auto *insertDateName = new QPushButton("월·요일 이름 예시 삽입");
    insertDateName->setObjectName("insertXmlDateNameFormula");
    dateNameRow->addWidget(dateName, 1);
    dateNameRow->addWidget(insertDateName);
    body->addLayout(dateNameRow);
    QObject::connect(insertDateName, &QPushButton::clicked, &dialog,
                     [=] { source->insertPlainText(dateName->currentData().toString()); });
    auto *unicodeRow = new QHBoxLayout;
    auto *unicode = new QComboBox;
    unicode->setObjectName("xmlUnicodeFunction");
    for (const auto &example :
         {QString("AscW(\"가\")"), QString("ChrW(44032)"), QString("ChrW(-21504)"), QString("AscW(ChrW(32768))")})
        unicode->addItem(example, example);
    auto *insertUnicode = new QPushButton("Unicode 코드 예시 삽입");
    insertUnicode->setObjectName("insertXmlUnicodeFormula");
    unicodeRow->addWidget(unicode, 1);
    unicodeRow->addWidget(insertUnicode);
    body->addLayout(unicodeRow);
    QObject::connect(insertUnicode, &QPushButton::clicked, &dialog,
                     [=] { source->insertPlainText(unicode->currentData().toString()); });
    auto *likeRow = new QHBoxLayout;
    auto *like = new QComboBox;
    like->setObjectName("xmlLikeOperator");
    for (const auto &example : {QString("\"ABC123\" Like \"[A-Z]*###\""), QString("\"가나다\" Like \"가?다\""),
                                QString("\"b\" Like \"[!0-9]\""), QString("\"*\" Like \"[*]\"")})
        like->addItem(example, example);
    auto *insertLike = new QPushButton("패턴 비교 예시 삽입");
    insertLike->setObjectName("insertXmlLikeFormula");
    likeRow->addWidget(like, 1);
    likeRow->addWidget(insertLike);
    body->addLayout(likeRow);
    QObject::connect(insertLike, &QPushButton::clicked, &dialog,
                     [=] { source->insertPlainText(like->currentData().toString()); });
    auto *logicalRow = new QHBoxLayout;
    auto *logical = new QComboBox;
    logical->setObjectName("xmlLogicalOperator");
    for (const auto &example :
         {QString("True Eqv False"), QString("False Imp Null"), QString("5 Eqv 3"), QString("5 Imp 3")})
        logical->addItem(example, example);
    auto *insertLogical = new QPushButton("논리 연산 예시 삽입");
    insertLogical->setObjectName("insertXmlLogicalFormula");
    logicalRow->addWidget(logical, 1);
    logicalRow->addWidget(insertLogical);
    body->addLayout(logicalRow);
    QObject::connect(insertLogical, &QPushButton::clicked, &dialog,
                     [=] { source->insertPlainText(logical->currentData().toString()); });
    auto *arithmeticRow = new QHBoxLayout;
    auto *arithmetic = new QComboBox;
    arithmetic->setObjectName("xmlArithmeticOperator");
    for (const auto &example : {QString("2 ^ 3"), QString("7.5 \\ 2.5"), QString("-2 ^ 2"), QString("2 ^ -3")})
        arithmetic->addItem(example, example);
    auto *insertArithmetic = new QPushButton("산술 연산 예시 삽입");
    insertArithmetic->setObjectName("insertXmlArithmeticFormula");
    arithmeticRow->addWidget(arithmetic, 1);
    arithmeticRow->addWidget(insertArithmetic);
    body->addLayout(arithmeticRow);
    QObject::connect(insertArithmetic, &QPushButton::clicked, &dialog,
                     [=] { source->insertPlainText(arithmetic->currentData().toString()); });
    auto *mathRow = new QHBoxLayout;
    auto *mathFunction = new QComboBox;
    mathFunction->setObjectName("xmlMathFunction");
    for (const auto &example : {QString("Atn(1)"), QString("Cos(0)"), QString("Exp(1)"), QString("Log(2)"),
                                QString("Sin(1)"), QString("Tan(1)")})
        mathFunction->addItem(example, example);
    auto *insertMath = new QPushButton("수학 함수 예시 삽입");
    insertMath->setObjectName("insertXmlMathFormula");
    mathRow->addWidget(mathFunction, 1);
    mathRow->addWidget(insertMath);
    body->addLayout(mathRow);
    QObject::connect(insertMath, &QPushButton::clicked, &dialog,
                     [=] { source->insertPlainText(mathFunction->currentData().toString()); });
    auto *numberTextRow = new QHBoxLayout;
    auto *numberTextFunction = new QComboBox;
    numberTextFunction->setObjectName("xmlNumberTextFunction");
    for (const auto &example : {QString("Str(0.5)"), QString("Val(\"12.5h\")"), QString("Val(\"&HFFFF&\")"),
                                QString("Hex(255)"), QString("Oct(-1)")})
        numberTextFunction->addItem(example, example);
    auto *insertNumberText = new QPushButton("숫자 문자열 함수 예시 삽입");
    insertNumberText->setObjectName("insertXmlNumberTextFormula");
    numberTextRow->addWidget(numberTextFunction, 1);
    numberTextRow->addWidget(insertNumberText);
    body->addLayout(numberTextRow);
    QObject::connect(insertNumberText, &QPushButton::clicked, &dialog,
                     [=] { source->insertPlainText(numberTextFunction->currentData().toString()); });
    auto *timeRow = new QHBoxLayout;
    auto *timeFunction = new QComboBox;
    timeFunction->setObjectName("xmlTimeFunction");
    for (const auto &example :
         {QString("TimeSerial(16, 35, 17)"), QString("TimeSerial(0, 75, 0)"), QString("TimeSerial(-1, 0, 0)"),
          QString("TimeValue(\"2:24PM\")"), QString("TimeValue(\"09:30:12.123456\")"),
          QString("TimeValue(\"2024-02-29T09:30:12.123456\")")})
        timeFunction->addItem(example, example);
    auto *insertTime = new QPushButton("시각 함수 예시 삽입");
    insertTime->setObjectName("insertXmlTimeFormula");
    timeRow->addWidget(timeFunction, 1);
    timeRow->addWidget(insertTime);
    body->addLayout(timeRow);
    QObject::connect(insertTime, &QPushButton::clicked, &dialog,
                     [=] { source->insertPlainText(timeFunction->currentData().toString()); });
    auto *dateRow = new QHBoxLayout;
    auto *dateFunction = new QComboBox;
    dateFunction->setObjectName("xmlDateFunction");
    dateFunction->addItem("DatePart · 주 번호", "DatePart(\"ww\", #2026-11-13#, %1, %2)");
    dateFunction->addItem("Weekday · 요일", "Weekday(#2026-11-13#, %1)");
    dateFunction->addItem("DateAdd · 월말 이동", "DateAdd(\"m\", 1, #2024-01-31T09:30:12.123456#)");
    dateFunction->addItem("DateDiff · 주 경계", "DateDiff(\"ww\", #2026-11-07T23:59:59#, #2026-11-08#, %1, %2)");
    auto *dateFirstDay = new QComboBox;
    dateFirstDay->setObjectName("xmlDateFirstDay");
    for (const auto &day : {"일요일", "월요일", "화요일", "수요일", "목요일", "금요일", "토요일"})
        dateFirstDay->addItem(day, dateFirstDay->count() + 1);
    dateFirstDay->setCurrentIndex(1);
    auto *dateFirstWeek = new QComboBox;
    dateFirstWeek->setObjectName("xmlDateFirstWeek");
    dateFirstWeek->addItem("1월1일 포함 주", 1);
    dateFirstWeek->addItem("연초 최소4일인 주", 2);
    dateFirstWeek->addItem("연초 온전한 주", 3);
    dateFirstWeek->setCurrentIndex(1);
    auto *insertDate = new QPushButton("날짜 함수 예시 삽입");
    insertDate->setObjectName("insertXmlDateFormula");
    dateRow->addWidget(dateFunction, 1);
    dateRow->addWidget(dateFirstDay);
    dateRow->addWidget(dateFirstWeek);
    dateRow->addWidget(insertDate);
    body->addLayout(dateRow);
    const auto dateControls = [=] {
        dateFirstDay->setEnabled(dateFunction->currentIndex() != 2);
        dateFirstWeek->setEnabled(dateFunction->currentIndex() == 0);
    };
    QObject::connect(dateFunction, &QComboBox::currentIndexChanged, &dialog, dateControls);
    dateControls();
    QObject::connect(insertDate, &QPushButton::clicked, &dialog, [=] {
        auto expression = dateFunction->currentData().toString();
        if (expression.contains("%1"))
            expression = expression.arg(dateFirstDay->currentData().toInt());
        if (expression.contains("%2") || expression.contains("%1"))
            expression = expression.arg(dateFirstWeek->currentData().toInt());
        source->insertPlainText(expression);
    });
    auto *formatRow = new QHBoxLayout;
    auto *formatFunction = new QComboBox;
    formatFunction->setObjectName("xmlFormattingFunction");
    formatFunction->addItem("Format · 숫자", "Format(1234.5, \"#,##0.00\")");
    formatFunction->addItem("Format · 날짜", "Format(#2026-11-13T09:30:12#, \"yyyy-mm-dd hh:nn:ss\")");
    formatFunction->addItem("PPMFormat · 로캘 표시", "PPMFormat(#2026-11-13T09:30:12#, \"Long Date\", %1)");
    formatFunction->addItem("StrConv · 단어 첫 문자", "StrConv(\"hello WORLD\", 3, %1)");
    formatFunction->addItem("StrConv · 전각", "StrConv(\"ABC 123 ｶﾞ\", 4, %1)");
    formatFunction->addItem("StrConv · 반각", "StrConv(\"ＡＢＣ ガ\", 8, %1)");
    formatFunction->addItem("StrConv · 히라가나", "StrConv(\"ｶﾞ ガ\", 36, 1041)");
    auto *formatLocale = new QComboBox;
    formatLocale->setObjectName("xmlFormattingLocale");
    formatLocale->addItem("ko-KR ·1042", 1042);
    formatLocale->addItem("ja-JP ·1041", 1041);
    formatLocale->addItem("en-US ·1033", 1033);
    auto *insertFormat = new QPushButton("표시·문자 변환 예시 삽입");
    insertFormat->setObjectName("insertXmlFormattingFormula");
    formatRow->addWidget(formatFunction, 1);
    formatRow->addWidget(formatLocale);
    formatRow->addWidget(insertFormat);
    body->addLayout(formatRow);
    QObject::connect(insertFormat, &QPushButton::clicked, &dialog, [=] {
        auto expression = formatFunction->currentData().toString();
        if (expression.contains("%1"))
            expression = expression.arg(formatLocale->currentData().toInt());
        source->insertPlainText(expression);
    });
    auto *zoneRow = new QHBoxLayout;
    auto *zoneFunction = new QComboBox;
    zoneFunction->setObjectName("xmlTimeZoneFunction");
    zoneFunction->addItems({"PPMTimeZone", "PPMTimeZoneText", "PPMTimeZoneOffset", "PPMParseUtc"});
    auto *zoneFrom = new QComboBox;
    auto *zoneTo = new QComboBox;
    zoneFrom->setObjectName("xmlTimeZoneFrom");
    zoneTo->setObjectName("xmlTimeZoneTo");
    for (auto *combo : {zoneFrom, zoneTo}) {
        combo->setEditable(true);
        combo->addItems({"UTC", "2026f:Asia/Seoul", "2026f:America/New_York", "2026f:Europe/London",
                         "2026f:Australia/Lord_Howe", "2026f:Pacific/Apia", "+09:00", "+05:45"});
        combo->setMaximumWidth(250);
    }
    zoneTo->setCurrentIndex(1);
    zoneFrom->setAccessibleName("원본 시간대 · 버전 포함 IANA 또는 명시 오프셋");
    zoneTo->setAccessibleName("대상 시간대 · 버전 포함 IANA 또는 명시 오프셋");
    zoneRow->addWidget(zoneFunction);
    zoneRow->addWidget(zoneFrom, 1);
    zoneRow->addWidget(zoneTo, 1);
    body->addLayout(zoneRow);
    auto *zonePolicyRow = new QHBoxLayout;
    auto *overlap = new QComboBox;
    overlap->setObjectName("xmlTimeZoneOverlap");
    overlap->addItem("중복 시각 거절", "reject");
    overlap->addItem("중복 시각의 이른 실제 시점", "earlier");
    overlap->addItem("중복 시각의 늦은 실제 시점", "later");
    auto *insertZone = new QPushButton("시간대 변환 예시 삽입 · PPM 확장 함수");
    insertZone->setObjectName("insertXmlTimeZoneFormula");
    zonePolicyRow->addWidget(overlap, 1);
    zonePolicyRow->addWidget(insertZone);
    body->addLayout(zonePolicyRow);
    const auto zoneControls = [zoneFunction, zoneFrom, zoneTo, overlap] {
        const auto index = zoneFunction->currentIndex();
        zoneFrom->setEnabled(index != 3);
        zoneTo->setEnabled(index < 2);
        overlap->setEnabled(index != 3);
    };
    QObject::connect(zoneFunction, &QComboBox::currentIndexChanged, &dialog, zoneControls);
    zoneControls();
    QObject::connect(insertZone, &QPushButton::clicked, &dialog, [=] {
        const auto escaped = [](QString s) {
            s.replace("\"", "\"\"");
            return "\"" + s + "\"";
        };
        const QString start = scope == FieldScope::Assignment ? "[Task.Start]"
                              : scope == FieldScope::Resource ? "[Project.Start]"
                                                              : "[Start]";
        const auto fn = zoneFunction->currentText();
        QString expression;
        if (fn == "PPMParseUtc")
            expression = "PPMParseUtc(\"2026-11-13T09:30:00.123456+09:00\")";
        else {
            expression = fn + "(" + start + ", " + escaped(zoneFrom->currentText());
            if (fn != "PPMTimeZoneOffset")
                expression += ", " + escaped(zoneTo->currentText());
            expression += ", " + escaped(overlap->currentData().toString()) + ")";
        }
        source->insertPlainText(expression);
    });
    auto *help = new QLabel(
        "사칙·비교·논리 연산, IIf/Switch, 문자열·ISO 날짜 함수의 지원 범위를 실행합니다. IIf는 "
        "양쪽 식을 모두 평가합니다.\n[Start]/[Finish]는 현재 계획·예측 일정, XML 내보내기는 계획 일정입니다. "
        "Actual Start/Finish는 상태일 기준 실적이며 없는 날짜는 미설정입니다.\n"
        "Actual Duration은 작업의 보고 기간, Actual Work는 보고된 날짜별 공수의 합계(분)입니다. 미보고분은 포함하지 "
        "않습니다.\n"
        "Remaining Duration/Work는 상태일 기준 잔여 예측입니다. 완료 작업은0이며 실제 공수를 빼서 추정하지 않습니다.\n"
        "Baseline Start/Finish/Duration/Work는 기본으로 최초 native 승인본을 사용하며, 외부 번호0을 채택했다면 그 명시 "
        "값을 사용합니다. "
        "기준선이 없거나 해당 객체/필드가 미보고이면 미설정입니다.\n"
        "Early는 현재 배치, Late는 검증된 후진 날짜입니다. 예측은 미완료 잔여 구간만 사용하며 미산정 Late는 "
        "미설정입니다. XML은 계획 기준입니다.\n"
        "[Duration]/[Work]는 명시 입력 분입니다. ProjDateAdd/Sub/Diff는 근무 달력을 반영합니다. 현재 시각 함수는 "
        "지원하지 않습니다.\n비활성 상태는 native에 "
        "보존합니다. XML에는 식 정의가 남으므로 재가져오기 시 지원식이 다시 활성화될 수 있습니다.");
    help->setObjectName("xmlFormulaHelp");
    help->setText(help->text() +
                  "\nBaseline0~10은 해당 번호의 외부 값을 채택하면 그 값을, 아니면 승인본 v1~v11을 사용합니다. 없는 "
                  "값은 미설정입니다. "
                  "요약 실제/잔여/기준선 기간은 포함 작업의 날짜 범위를 프로젝트 근무 달력으로 계산합니다. "
                  "실제 요약에서 진행 중 작업의 끝은 상태일이며 미보고 작업은 미설정입니다.");
    help->setText(help->text() + "\nStr는 양수 앞의 공백과 점 소수를 사용합니다. Hex/Oct는 짝수 반올림 후32비트 진수, "
                                 "Val은 숫자 접두부·지수·&H/&O를 읽고 소수6자리로 반올림합니다. 단위나 통화를 환산하지 "
                                 "않으며 !/@/$/^ 접미사는 지원하지 않습니다. PC 로캘로 숫자 표기를 추측하지 않습니다.");
    help->setText(help->text() + "\nTimeSerial/TimeValue는1899-12-30 기준 Date를 반환합니다. TimeSerial은인수별 "
                                 "짝수 반올림·16비트 범위와 상대 넘침을 지원합니다. 음수는OLE 규칙이므로 "
                                 "TimeSerial(-1,0,0)은기준일01시입니다. TimeValue는24시간·영문 AM/PM·ISO 입력의 "
                                 "날짜를 제거하고 소수 초6자리를 보존합니다. 현재 날짜나 PC 로캘을 추측하지 않습니다.");
    help->setText(help->text() +
                  "\nDatePart·Weekday와 DateAdd/DateDiff의 yyyy/q/m/y/d/w/ww/h/n/s를 지원합니다. "
                  "월·연 이동은 월말을 보정하고 마이크로초를 보존합니다. w는 근무일이 아닙니다. "
                  "DateDiff는 경계 수이며 w는 온전한7일 묶음, ww는 주 시작일 경계입니다. "
                  "주 옵션0은 고정 기본값1입니다. 주 번호는 기존 Format과 같고 연말을 다음 해1주로 바꾸지 않습니다.");
    help->setText(help->text() + "\nFormat은고정 en-US 표시입니다. PPMFormat(값, 형식, LCID)은1033/1041/1042를 "
                                 "명시합니다. StrConv는Unicode16.0 대소문자·전각/반각·가나 지원 부분을 실행합니다. "
                                 "전각/반각은1041/1042, 가나는1041이며 코드페이지64/128은지원하지 않습니다. "
                                 "결과는문자열이며 원래 공수·원가·날짜는변경하지 않습니다.");
    help->setText(help->text() + "\nPPM 시간대 함수는 명시 오프셋 또는2026f:Asia/Seoul처럼 버전을 포함한 IANA 이름을 "
                                 "사용합니다. PC 시간대를 추측하지 않습니다. 중복은 정책 선택, 없는 시각은 거절합니다. "
                                 "IANA는 실제 UTC1970~2100년이며 PPMTimeZoneText는 오프셋을 보존합니다. 날짜 결과 "
                                 "자체에는 시간대 표지가 없습니다.");
    help->setText(help->text() +
                  "\nChoose는 모든 선택지를 평가합니다. InStr/StrComp의 비교0은 BMP 정확 비교, 비교1은 "
                  "ASCII 대소문자 무시입니다. Asc/Chr·숫자 String은 표시 가능한 ASCII32~126, "
                  "DateValue는 ISO 날짜만 받습니다. CByte/CCur는 짝수 쪽 반올림, Sqr는 소수6자리입니다.");
    help->setText(help->text() +
                  "\nAtn/Cos/Sin/Tan의 각도는라디안입니다. Exp는 e의 거듭제곱, Log는양수의 자연로그입니다. "
                  "부동소수 근사 결과를 소수6자리로 반올림하며 정의역/범위 오류는값을 대신하지 않습니다.");
    help->setText(help->text() +
                  "\nReplace는시작 위치 이후만 반환합니다. InStrRev는검색어 끝이 시작 위치 이내여야 합니다. "
                  "StrReverse는BMP 문자 순서를 뒤집습니다. 비교1은ASCII, 기본0은정확 비교입니다. "
                  "Replace/StrReverse의Null은오류이고 InStrRev 문자열의Null은미설정입니다.");
    help->setText(help->text() +
                  "\nCDec는native 소수6자리 숫자, CSng는단정밀도 짝수 반올림 후 소수6자리입니다. "
                  "CVar는원래 값의 형식과 기간 구분을 유지합니다. 전체 Decimal/Variant 범위는지원하지 않습니다.");
    help->setText(help->text() +
                  "\nProjDateValue는ISO 일시를 보존하고 NA는미설정입니다. ProjDateConv는고정 영문0~20/255 형식, "
                  "ProjDurConv는3~12/35~44 단위 표시입니다. 기본은분, 1근무일480분·월20근무일/30경과일입니다. "
                  "결과는표시 문자열이며 원래 기간/계획은변경하지 않습니다.");
    help->setText(help->text() +
                  "\n^/정수 나눗셈(\\) 포함 식은거듭제곱→단항 부호→곱셈/나눗셈→정수 나눗셈→Mod 순서입니다. "
                  "\\는피연산자를32비트 짝수 반올림한 뒤 몫을0방향으로 버립니다. 결과는단위 없는 숫자입니다. "
                  "새 연산 없는 기존 식의 우선순위는유지합니다.");
    help->setText(help->text() +
                  "\nEqv/Imp는Boolean 또는32비트 정수에 적용합니다. Eqv의Null은전파하고 Imp는Boolean/숫자별 "
                  "Null 규칙을 따릅니다. 포함 식의 논리 순서는Not→And→Or→Xor→Eqv→Imp입니다. "
                  "기존 식은기존 문법을 유지하며 분수는CLng 등으로 명시 변환하세요.");
    help->setText(help->text() +
                  "\nLike는BMP 정확 비교로 *, ?, #, 문자 목록/범위/부정 목록을 지원합니다. #은ASCII 숫자이고 "
                  "패턴 전체를 먼저검증합니다. 포함 식은VBA 우선순위를 적용하며 기존 식은기존 문법을 유지합니다.");
    help->setText(
        help->text() +
        "\nBaseline Cost는 채택한 외부 번호0의 명시 금액을, 없으면 최초 native 승인본의 동결 원가를 사용합니다. "
        "자원/배정은배정 원가, 작업/프로젝트는직접비도 포함하며 미확정은0으로 바꾸지 않습니다.");
    help->setText(help->text() +
                  "\nAscW/ChrW는 native/XML 텍스트에 허용되는 BMP 문자를 사용합니다. AscW는 부호 있는 16비트 코드, "
                  "ChrW는 짝수 반올림 후 -32768~65535를 받습니다. Null·서로게이트·보조 평면은 오류입니다.");
    help->setText(help->text() +
                  "\nMonthName/WeekdayName은 고정 en-US의 월·요일 이름/약어를 반환합니다. 주 시작일 0은 일요일, "
                  "1~7은 일~토입니다. 번호는 짝수 반올림하고 Null·범위 오류는 미설정으로 바꾸지 않습니다.");
    help->setText(help->text() +
                  "\nFormatDateTime은 고정 en-US의 표시 형식 0~4를 사용합니다. ISO 날짜/Date만 받으며 소수 초는 "
                  "표시에서 생략합니다. 0에서 1899-12-30은 시각만 표시하며 Null·잘못된 옵션은 오류입니다.");
    help->setText(
        help->text() +
        "\nFormatNumber/Percent/Currency는 고정 en-US 숫자 표시입니다. 소수 자릿수 -1/0~6, "
        "선행 0·음수 괄호·그룹 옵션 -2/-1/0을 받습니다. Currency의 $는 문서 통화와 별개이며 환산이 아닙니다.");
    help->setText(help->text() +
                  "\nSLN/SYD는 감가상각 Number입니다. cost/salvage>=0, life>0, SYD는 0<period<=life이며 "
                  "Null은 오류입니다. 중간 반올림 없이 소수6자리에서 절반을 절댓값 증가로 반올림합니다."
                  "\nPartition은 짝수 반올림 후 수치를 구간 Text로 분류하며 정렬용 공백을 보존합니다. "
                  "start>=0, stop>start, interval>=1이며 Null은 미설정입니다."
                  "\nRGB/QBColor는 색상 코드 Number를 반환합니다. RGB의 하위 바이트부터 빨강·초록·파랑이며 "
                  "성분은 짝수 반올림 후 0~32767, 255 초과는 255로 제한합니다. QBColor는 0~15이며 Null은 오류입니다.");
    help->setWordWrap(true);
    body->addWidget(help);
    auto *summary = new QLabel;
    summary->setObjectName("xmlFormulaSummary");
    summary->setWordWrap(true);
    body->addWidget(summary);
    auto *table = new QTableWidget(0, 4);
    table->setObjectName("xmlFormulaPreview");
    table->setHorizontalHeaderLabels({"객체", "값", "출처", "상태"});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    body->addWidget(table, 1);
    auto *error = new QLabel;
    error->setObjectName("xmlFormulaError");
    error->setWordWrap(true);
    error->setTextFormat(Qt::PlainText);
    body->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText("저장");
    buttons->button(QDialogButtonBox::Cancel)->setText("취소");
    auto *remove = buttons->addButton("식 제거 · 원본 값 사용", QDialogButtonBox::DestructiveRole);
    remove->setObjectName("removeXmlFormula");
    remove->setEnabled(field->xmlFormula.has_value());
    body->addWidget(buttons);
    auto probe = [&] {
        guard();
        auto p = *input;
        std::vector<XmlFormulaAlias> aliases;
        if (field->xmlFormula)
            for (const auto &r : field->xmlFormula->references)
                aliases.push_back({r.token, r.key, r.scope, true});
        auto candidate = bindXmlFormula(p, scope, source->toPlainText().toUtf8().toStdString(), aliases);
        candidate.enabled = enabled->isChecked();
        auto &fields = customFields(p, scope);
        auto f = std::find_if(fields.begin(), fields.end(), [&](const auto &v) { return "field:" + v.id == key; });
        f->xmlFormula = std::move(candidate);
        validate(p);
        return p;
    };
    struct PreviewResult {
        std::vector<std::array<QString, 4>> rows;
        QString summary, error;
    };
    std::shared_ptr<CalculationControl> control;
    std::uint64_t generation = 0;
    auto cancel = [&] {
        ++generation;
        if (control)
            control->cancel();
        dialog.setProperty("xmlFormulaBusy", false);
    };
    auto display = [&](const PreviewResult &out) {
        table->setRowCount(0);
        for (const auto &cells : out.rows) {
            int at = table->rowCount();
            table->insertRow(at);
            for (int c = 0; c < 4; ++c) {
                auto *item = new QTableWidgetItem(cells[c]);
                item->setToolTip(cells[c]);
                table->setItem(at, c, item);
            }
        }
        summary->setText(out.summary);
        error->setText(out.error);
    };
    auto render = [&] {
        cancel();
        try {
            auto p = probe();
            const bool active = enabled->isChecked();
            const bool needsSchedule = hasXmlScheduleReferences(p, true) || hasXmlCalendarFunctions(p, true);
            const auto ticket = generation;
            control = std::make_shared<CalculationControl>();
            auto run = [p = std::move(p), active, scope, key, control = control]() {
                PreviewResult out;
                try {
                    EvaluationScope evaluation(control);
                    ScheduleFormulaScope schedule(p);
                    auto append = [&](const std::string &name, const Task *task, const Resource *resource,
                                      const Assignment *assignment, const std::vector<TaskAttribute> &raw) {
                        if (out.rows.size() >= 20)
                            return;
                        XmlFormulaResult v;
                        if (active)
                            v = xmlFormulaValue(p, scope, key, task, resource, assignment);
                        else
                            v.value = attributeValue(raw, key);
                        out.rows.push_back({q(name), v.value ? q(attributeText(p, key, *v.value, scope)) : "미설정",
                                            active ? "XML 계산 결과" : "보존 스냅샷", q(v.issue)});
                    };
                    if (scope == FieldScope::Task)
                        for (const auto &t : p.tasks)
                            append(t.name, &t, nullptr, nullptr, t.attributes);
                    else if (scope == FieldScope::Resource)
                        for (const auto &r : p.resources)
                            append(r.name, nullptr, &r, nullptr, r.attributes);
                    else if (scope == FieldScope::Assignment)
                        for (const auto &a : p.assignments)
                            append(a.taskId + " / " + a.resourceId, nullptr, nullptr, &a, a.attributes);
                    else
                        append(p.name, nullptr, nullptr, nullptr, p.projectAttributes);
                    const auto issue = xmlFormulaProblem(p, scope, key);
                    out.summary = active ? "실행 가능 · 최대 20행 미리보기 · 직접 값은 원본 스냅샷으로 보존"
                                  : issue.empty() ? "비활성 · 지원식을 보존하고 원본 값을 표시"
                                                  : "비활성 · " + q(issue);
                    if (hasXmlScheduleReferences(p, true))
                        out.summary += " · " + q(xmlScheduleCaption(p));
                } catch (const CalculationCancelled &) {
                } catch (const std::exception &e) {
                    out.error = q(e.what());
                }
                return out;
            };
            if (needsSchedule) {
                dialog.setProperty("xmlFormulaBusy", true);
                summary->setText("일정 계산·미리보기 중…");
                table->setRowCount(0);
                auto *watcher = new QFutureWatcher<PreviewResult>(&dialog);
                QObject::connect(watcher, &QFutureWatcher<PreviewResult>::finished, &dialog, [&, watcher, ticket] {
                    auto out = watcher->result();
                    watcher->deleteLater();
                    if (ticket != generation)
                        return;
                    dialog.setProperty("xmlFormulaBusy", false);
                    try {
                        guard();
                        display(out);
                    } catch (const std::exception &e) {
                        showEditorError(*error, e);
                    }
                });
                watcher->setFuture(QtConcurrent::run(std::move(run)));
            } else
                display(run());
        } catch (const std::exception &e) {
            showEditorError(*error, e);
        }
    };
    QObject::connect(source, &QPlainTextEdit::textChanged, &dialog, cancel);
    QObject::connect(&dialog, &QDialog::finished, &dialog, cancel);
    QObject::connect(insert, &QPushButton::clicked, &dialog,
                     [&] { source->insertPlainText(refs->currentData().toString()); });
    QObject::connect(preview, &QPushButton::clicked, &dialog, render);
    QObject::connect(enabled, &QCheckBox::toggled, &dialog, render);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            auto p = probe();
            doc.setXmlFormula(scope, key, customField(p, key, scope)->xmlFormula);
            dialog.accept();
        } catch (const std::exception &e) {
            showEditorError(*error, e);
        }
    });
    QObject::connect(remove, &QPushButton::clicked, &dialog, [&] {
        try {
            guard();
            doc.setXmlFormula(scope, key, {});
            dialog.accept();
        } catch (const std::exception &e) {
            showEditorError(*error, e);
        }
    });
    dialog.resize(880, 700);
    prepareEditorDialog(dialog);
    render();
    dialog.exec();
}
} // namespace jobs
