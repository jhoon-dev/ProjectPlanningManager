#include "ui/XerBatchDialog.h"
#include "ui/EditorUi.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
namespace ppm {
std::optional<std::vector<std::size_t>> chooseXerBatch(QWidget *parent, const std::vector<XerBatchChoice> &choices,
                                                       const QString &note) {
    QDialog dialog(parent);
    dialog.setObjectName("xerBatchSelection");
    dialog.setWindowTitle("XER 일괄 교환 · 프로젝트 선택");
    dialog.resize(850, 520);
    auto *layout = new QVBoxLayout(&dialog);
    auto *label = new QLabel(note, &dialog);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    layout->addWidget(label);
    auto *list = new QListWidget(&dialog);
    list->setObjectName("xerBatchProjects");
    list->setAccessibleName("일괄 교환 프로젝트 선택");
    for (const auto &choice : choices) {
        auto *item = new QListWidgetItem(choice.label, list);
        item->setFlags(choice.blocked ? Qt::NoItemFlags : Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
    }
    layout->addWidget(list);
    auto *all = new QPushButton("가능한 프로젝트 모두 선택", &dialog);
    all->setObjectName("xerBatchSelectAll");
    layout->addWidget(all);
    auto *count = new QLabel(&dialog);
    count->setObjectName("xerBatchCount");
    layout->addWidget(count);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText("선택 프로젝트 변환·검증");
    layout->addWidget(buttons);
    auto selection = [&] {
        std::vector<std::size_t> result;
        for (int i = 0; i < list->count(); ++i)
            if (!choices[std::size_t(i)].blocked && list->item(i)->checkState() == Qt::Checked)
                result.push_back(std::size_t(i));
        return result;
    };
    auto update = [&] {
        const auto selected = selection();
        count->setText(QString("%1 / %2개 선택").arg(selected.size()).arg(choices.size()));
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!selected.empty());
    };
    QObject::connect(list, &QListWidget::itemChanged, &dialog, update);
    QObject::connect(all, &QPushButton::clicked, &dialog, [&] {
        for (int i = 0; i < list->count(); ++i)
            if (!choices[std::size_t(i)].blocked)
                list->item(i)->setCheckState(Qt::Checked);
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    update();
    prepareEditorDialog(dialog);
    if (dialog.exec() != QDialog::Accepted)
        return {};
    return selection();
}
} // namespace ppm
