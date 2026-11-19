#include "calculation/ScheduleFormulaScope.h"
#include "ui/XmlRollUpDialog.h"
#include "ui/EditorUi.h"
#include "domain/Classification.h"
#include "domain/XmlRollUp.h"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
namespace ppm {
void editXmlRollUp(Document &doc, FieldScope scope, const std::string &key, QWidget *parent) {
    if (!doc.project() || doc.readOnly() || (scope != FieldScope::Task && scope != FieldScope::Resource))
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
    dialog.setObjectName("xmlRollUpDialog");
    dialog.setWindowTitle("요약 roll-up · " + QString::fromStdString(field->name));
    auto *body = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    body->addLayout(form);
    auto *calculation = new QComboBox;
    calculation->setObjectName("rollUpCalculation");
    calculation->addItem("없음 · 명시 요약 값 사용", 0);
    calculation->addItem("하위 값으로 자동 계산", 1);
    if ((field->xmlFormula && field->xmlFormula->enabled) ||
        (field->xmlRollUp && field->xmlRollUp->calculationType == 2))
        calculation->addItem(field->xmlFormula && field->xmlFormula->enabled ? "XML 계산식으로 요약 계산"
                                                                             : "외부 계산식 · 비활성/명시 값 보존",
                             2);
    calculation->setCurrentIndex(calculation->findData(field->xmlRollUp ? field->xmlRollUp->calculationType : 0));
    form->addRow("요약 계산", calculation);
    auto *method = new QComboBox;
    method->setObjectName("rollUpMethod");
    for (int i = 0; i < 8; ++i)
        if (rollUpSupported(field->type, i) || (field->xmlRollUp && field->xmlRollUp->rollupType == i))
            method->addItem(QString::fromStdString(rollUpMethodName(i)), i);
    if (!method->count())
        method->addItem("최대 / OR (사용 안 함)", 0);
    method->setCurrentIndex(std::max(0, method->findData(field->xmlRollUp ? field->xmlRollUp->rollupType : 0)));
    form->addRow("계산 방식", method);
    auto *help = new QLabel(
        "작업 필드는 WBS·프로젝트 요약과 현재 분류 그룹에, 자원 필드는 자원 그룹에 적용합니다.\n평균은 말단 전체 또는 "
        "직접 하위 그룹·행에 같은 가중치를 줍니다. 전체 행 개수는 하위 요약 행도 포함합니다.\n원본 명시 요약 값은 "
        "보존합니다. 자동 계산을 끄면 그 값을 표시합니다. XML 외부 계산식은 해당 설정에서 지원식을 활성화하면 "
        "실행합니다.");
    help->setWordWrap(true);
    body->addWidget(help);
    auto *preview = new QPushButton("영향 미리보기");
    preview->setObjectName("previewRollUp");
    body->addWidget(preview);
    auto *summary = new QLabel;
    summary->setObjectName("rollUpSummary");
    summary->setWordWrap(true);
    body->addWidget(summary);
    auto *table = new QTableWidget(0, 4);
    table->setObjectName("rollUpPreview");
    table->setHorizontalHeaderLabels({"요약 대상", "값", "출처", "상태"});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    body->addWidget(table, 1);
    auto *error = new QLabel;
    error->setObjectName("rollUpError");
    error->setTextFormat(Qt::PlainText);
    error->setWordWrap(true);
    body->addWidget(error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText("저장");
    buttons->button(QDialogButtonBox::Cancel)->setText("취소");
    body->addWidget(buttons);
    auto probe = [&] {
        guard();
        auto p = *input;
        auto &fields = customFields(p, scope);
        auto f = std::find_if(fields.begin(), fields.end(), [&](const auto &v) { return "field:" + v.id == key; });
        f->xmlRollUp = XmlRollUp{calculation->currentData().toInt(), method->currentData().toInt()};
        validateXmlRollUp(p);
        return p;
    };
    auto render = [&] {
        try {
            auto p = probe();
            ScheduleFormulaScope formulaSchedule(p, doc.calculation() ? &*doc.calculation() : nullptr,
                                                 ScheduleFormulaMode::Active, false);
            const RollUpValues values(p, scope);
            table->setRowCount(0);
            auto row = [&](const std::string &id, const std::string &name) {
                if (table->rowCount() >= 20)
                    return;
                const auto &c = values.cell(id, key);
                int n = table->rowCount();
                table->insertRow(n);
                QStringList cells{QString::fromStdString(name),
                                  c.value ? QString::fromStdString(attributeText(p, key, *c.value, scope)) : "미설정",
                                  QString::fromStdString(c.origin), QString::fromStdString(c.issue)};
                for (int i = 0; i < 4; ++i) {
                    auto *item = new QTableWidgetItem(cells[i]);
                    item->setToolTip(cells[i]);
                    table->setItem(n, i, item);
                }
            };
            row("", "프로젝트 전체");
            if (scope == FieldScope::Task)
                for (const auto &g : p.wbsNodes)
                    row(g.id, g.name);
            summary->setText(
                QString("요약 %1개 · 최대 20행 표시 · 단위 %2")
                    .arg(scope == FieldScope::Task ? p.wbsNodes.size() + 1 : 1)
                    .arg(QString::fromStdString(
                        (calculation->currentData() == 1 &&
                         (method->currentData() == 2 || method->currentData() == 6 || method->currentData() == 7))
                            ? "개"
                            : attributeUnit(p, key, scope))));
            error->clear();
        } catch (const std::exception &e) {
            showEditorError(*error, e);
        }
    };
    QObject::connect(preview, &QPushButton::clicked, &dialog, render);
    QObject::connect(calculation, &QComboBox::currentIndexChanged, &dialog, [&] {
        method->setEnabled(calculation->currentData() == 1);
        render();
    });
    QObject::connect(method, &QComboBox::currentIndexChanged, &dialog, render);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            auto p = probe();
            ScheduleFormulaScope formulaSchedule(p, doc.calculation() ? &*doc.calculation() : nullptr,
                                                 ScheduleFormulaMode::Active, false);
            (void)RollUpValues(p, scope);
            doc.setXmlRollUp(scope, key, XmlRollUp{calculation->currentData().toInt(), method->currentData().toInt()});
            dialog.accept();
        } catch (const std::exception &e) {
            showEditorError(*error, e);
        }
    });
    method->setEnabled(calculation->currentData() == 1);
    dialog.resize(850, 600);
    prepareEditorDialog(dialog);
    render();
    dialog.exec();
}
} // namespace ppm
