#include "ui/MppImport.h"
#include "ui/EditorUi.h"
#include "ui/ExchangeOperation.h"
#include "ui/SandboxAccess.h"
#include "persistence/ProjectMpp.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
namespace ppm {
namespace {
QString currency(QWidget *parent) {
    QDialog d(parent);
    d.setObjectName("mppImportOptions");
    d.setWindowTitle("MPP 읽기 · 계획 사본 설정");
    auto *l = new QVBoxLayout(&d);
    auto *note =
        new QLabel("기간 기반 계획의 지원 부분을 읽습니다. 원본의 실적·원가/단가·기준선은 MPP에 보존합니다.\n새 계획 "
                   "사본의 통화를 선택하세요. 원본 금액을 환산하거나 이 통화의 금액으로 재해석하지 않습니다.",
                   &d);
    note->setWordWrap(true);
    l->addWidget(note);
    auto *c = new QComboBox(&d);
    c->setObjectName("mppPlanCurrency");
    c->addItems({"KRW", "USD", "EUR", "JPY"});
    l->addWidget(c);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    l->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    d.resize(620, 220);
    prepareEditorDialog(d);
    return d.exec() == QDialog::Accepted ? c->currentText() : QString{};
}
QString preview(QWidget *parent, const ProjectMppExchange &e, const QString &proposed) {
    QDialog d(parent);
    d.setObjectName("projectMppPreview");
    d.setWindowTitle("MPP 읽기 · 원본 보존과 변환 확인");
    d.resize(900, 650);
    auto *l = new QVBoxLayout(&d);
    auto *text = new QPlainTextEdit(projectMppPreview(e), &d);
    text->setObjectName("mppDiagnostics");
    text->setReadOnly(true);
    l->addWidget(text, 1);
    auto *note = new QLabel(
        "새 교환 폴더에 original.mpp · project.ppm · conversion.json을 보관합니다. 기존 폴더는 덮어쓰지 않습니다.", &d);
    note->setWordWrap(true);
    l->addWidget(note);
    auto *destination = new QLineEdit(proposed, &d);
    destination->setObjectName("mppDestination");
    destination->setAccessibleName("새 MPP 교환 폴더 경로");
    l->addWidget(destination);
    auto *choose = new QPushButton("상위 폴더 선택…", &d);
    l->addWidget(choose);
    QObject::connect(choose, &QPushButton::clicked, &d, [&] {
        auto p =
            QFileDialog::getExistingDirectory(&d, "MPP 교환 폴더 위치", QFileInfo(destination->text()).absolutePath());
        if (!p.isEmpty())
            destination->setText(QDir(p).filePath(QFileInfo(destination->text()).fileName()));
    });
    auto *confirm = new QCheckBox(
        "지원 부분과 변환 진단을 검토했고, 실적·원가·기준선을 새 계획으로 오인하지 않음을 확인했습니다.", &d);
    confirm->setObjectName("mppConfirm");
    l->addWidget(confirm);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    buttons->button(QDialogButtonBox::Ok)->setText("변환 확인 · 새 사본 열기");
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    l->addWidget(buttons);
    auto *error = new QLabel(&d);
    error->setWordWrap(true);
    l->addWidget(error);
    QObject::connect(confirm, &QCheckBox::toggled, buttons->button(QDialogButtonBox::Ok), &QPushButton::setEnabled);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &d, [&] {
        if (!confirm->isChecked())
            return;
        const auto path = destination->text().trimmed();
        QFileInfo info(path);
        if (path.isEmpty() || info.exists() || info.isSymLink() || !info.dir().exists()) {
            error->setText("존재하는 상위 폴더 아래의 새 폴더 경로를 입력하세요.");
            return;
        }
        if (authorizeSandboxFile(&d, path))
            d.accept();
    });
    prepareEditorDialog(d);
    return d.exec() == QDialog::Accepted ? destination->text().trimmed() : QString{};
}
} // namespace
QString importMppPlan(QWidget *parent, Document &document, const std::function<bool()> &beforeReplace) {
    const auto path =
        QFileDialog::getOpenFileName(parent, "Microsoft Project MPP 읽기", {}, "Microsoft Project (*.mpp)");
    if (path.isEmpty())
        return {};
    const auto session = document.sessionGeneration(), revision = document.project() ? document.project()->revision : 0;
    const auto selected = currency(parent);
    if (selected.isEmpty())
        return {};
    MppReaderOptions options;
    options.planCurrency = selected;
    auto e = prepareExchange<ProjectMppExchange>(parent, document, "MPP 읽기·계획 변환·검증 중…", [&] {
        return importProjectMpp(readProjectMpp(path), QFileInfo(path).fileName(), options);
    });
    if (!e)
        return {};
    const auto proposed =
        QDir(QFileInfo(path).absolutePath()).filePath(QFileInfo(path).completeBaseName() + "-ppm-import");
    const auto destination = preview(parent, *e, proposed);
    if (destination.isEmpty())
        return {};
    if (session != document.sessionGeneration() || revision != (document.project() ? document.project()->revision : 0))
        throw AppError(ErrorCode::EXCHANGE_SOURCE_CHANGED);
    if (!beforeReplace())
        return {};
    const auto publishSession = document.sessionGeneration(),
               publishRevision = document.project() ? document.project()->revision : 0;
    QString folder;
    if (!runExchangeOperation(
            parent, document, "MPP 원본·계획 사본 게시·검증 중…", [&] { folder = publishProjectMpp(*e, destination); },
            true))
        return {};
    if (publishSession != document.sessionGeneration() ||
        publishRevision != (document.project() ? document.project()->revision : 0))
        throw AppError(ErrorCode::EXCHANGE_SOURCE_CHANGED);
    document.open(QDir(folder).filePath("project.ppm").toStdString());
    return folder;
}
} // namespace ppm
