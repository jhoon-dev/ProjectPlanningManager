#include "ui/SandboxAccess.h"
#include "formatting/QtDiagnosticText.h"
#include "ui/MainWindow.h"
#include "ui/EditorUi.h"
#include "ui/ExchangeOperation.h"
#include "persistence/ProjectXml.h"
#include "ui/XmlProgressDialog.h"
#include "ui/XmlBaselineDialog.h"
#include "ui/XmlPeriodActualDialog.h"
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QVBoxLayout>
#include <algorithm>
#include <stdexcept>

namespace ppm {
namespace {
QString qs(const std::string &s) {
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}
QString preview(QWidget *parent, const ProjectXmlExchange &e, const QString &proposed, bool *includeProgress = nullptr,
                bool *includeBaselines = nullptr) {
    QDialog dialog(parent);
    dialog.setObjectName("projectXmlPreview");
    dialog.setWindowTitle(e.importing          ? "Project XML 가져오기 · 변환 확인"
                          : e.exportingActuals ? "표준 실적 포함 XML 내보내기 · 변환 확인"
                                               : "Project XML 내보내기 · 변환 확인");
    dialog.resize(800, 580);
    auto *layout = new QVBoxLayout(&dialog);
    auto *detail = new QPlainTextEdit(projectXmlPreview(e), &dialog);
    detail->setObjectName("xmlDiagnostics");
    detail->setReadOnly(true);
    layout->addWidget(detail);
    QCheckBox *progress = nullptr;
    if (e.importing && includeProgress) {
        progress = new QCheckBox("XML 작업 진척을 별도 검토한 뒤 새 사본에 채택", &dialog);
        progress->setObjectName("xmlIncludeProgress");
        progress->setEnabled(std::any_of(e.progressCandidates.begin(), e.progressCandidates.end(),
                                         [](const auto &row) { return row.present; }));
        layout->addWidget(progress);
    }
    QCheckBox *baselines = nullptr;
    if (e.importing && includeBaselines) {
        baselines = new QCheckBox("XML 번호별 기준선을 별도 검토한 뒤 새 사본에 채택", &dialog);
        baselines->setObjectName("xmlIncludeBaselines");
        baselines->setEnabled(!e.baselineCandidates.empty());
        layout->addWidget(baselines);
    }
    auto *note = new QLabel("새 교환 폴더 전체 경로 · 기존 폴더는 덮어쓰지 않습니다", &dialog);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *destination = new QLineEdit(proposed, &dialog);
    destination->setObjectName("xmlDestination");
    destination->setAccessibleName("새 교환 폴더 경로");
    layout->addWidget(destination);
    auto *choose = new QPushButton("상위 폴더 선택…", &dialog);
    layout->addWidget(choose);
    QObject::connect(choose, &QPushButton::clicked, &dialog, [&] {
        auto path = QFileDialog::getExistingDirectory(&dialog, "교환 폴더를 만들 위치",
                                                      QFileInfo(destination->text()).absolutePath());
        if (!path.isEmpty())
            destination->setText(QDir(path).filePath(QFileInfo(destination->text()).fileName()));
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(e.importing ? "변환 확인 · 새 사본 열기" : "변환 확인 · 내보내기");
    layout->addWidget(buttons);
    auto *error = new QLabel(&dialog);
    error->setWordWrap(true);
    layout->addWidget(error);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (destination->text().trimmed().isEmpty() || !authorizeSandboxFile(&dialog, destination->text().trimmed()))
            return;
        auto path = QFileInfo(destination->text().trimmed());
        if (destination->text().trimmed().isEmpty() || path.exists() || path.isSymLink() || !path.dir().exists()) {
            error->setText("존재하는 상위 폴더 아래의 새 폴더 경로를 입력하세요.");
            return;
        }
        dialog.accept();
    });
    prepareEditorDialog(dialog);
    if (dialog.exec() != QDialog::Accepted)
        return {};
    if (includeProgress)
        *includeProgress = progress && progress->isChecked();
    if (includeBaselines)
        *includeBaselines = baselines && baselines->isChecked();
    return destination->text().trimmed();
}
} // namespace
void MainWindow::importXml() {
    const auto path = QFileDialog::getOpenFileName(this, "Project XML 가져오기", {}, "Project XML (*.xml)");
    if (path.isEmpty())
        return;
    try {
        const auto generation = document_.sessionGeneration();
        const auto revision = document_.project() ? document_.project()->revision : 0;
        auto prepared = prepareExchange<ProjectXmlExchange>(this, document_, "XML 읽기·변환·검증 중…", [path] {
            return importProjectXml(readProjectXml(path), QFileInfo(path).fileName());
        });
        if (!prepared)
            return;
        auto exchange = std::move(*prepared);
        auto proposed =
            QDir(QFileInfo(path).absolutePath()).filePath(QFileInfo(path).completeBaseName() + "-ppm-import");
        bool includeProgress = false, includeBaselines = false;
        auto destination = preview(this, exchange, proposed, &includeProgress, &includeBaselines);
        if (destination.isEmpty())
            return;
        if (includeProgress) {
            auto adopted = reviewXmlProgress(this, document_, exchange);
            if (!adopted)
                return;
            exchange = std::move(*adopted);
        }
        if (includeBaselines) {
            auto adopted = reviewXmlBaselines(this, document_, exchange);
            if (!adopted)
                return;
            exchange = std::move(*adopted);
        }
        if (generation != document_.sessionGeneration() ||
            revision != (document_.project() ? document_.project()->revision : 0))
            throw AppError(ErrorCode::MAINWINDOWXER_001, {});
        if (!confirmChanges())
            return;
        const auto publishSession = document_.sessionGeneration();
        const auto publishRevision = document_.project() ? document_.project()->revision : 0;
        QString folder;
        if (!runExchangeOperation(
                this, document_, "XML 사본 게시·검증 중…", [&] { folder = publishProjectXml(exchange, destination); },
                true))
            return;
        if (document_.sessionGeneration() != publishSession ||
            (document_.project() ? document_.project()->revision : 0) != publishRevision)
            throw AppError(ErrorCode::EXCHANGE_SOURCE_CHANGED);
        document_.open(QDir(folder).filePath("project.ppm").toUtf8().toStdString());
        refresh();
        statusBar()->showMessage(
            includeProgress
                ? QString("XML 작업 진척 %1건 채택 · 새 사본/원본/진단: ").arg(exchange.project.progress.size()) +
                      folder
                : "XML 계획 변환 완료 · 원본/진단: " + folder);
    } catch (const std::exception &e) {
        report(e);
    }
}
void MainWindow::mergeXmlProgress() {
    if (!document_.project() || document_.readOnly())
        return;
    try {
        if (!document_.project()->statusAt)
            throw AppError(ErrorCode::STATUS_DATE_REQUIRED, {});
        const auto path =
            QFileDialog::getOpenFileName(this, "현재 문서에 병합할 Project XML", {}, "Project XML (*.xml)");
        if (path.isEmpty())
            return;
        auto input = prepareExchange<ProjectXmlExchange>(this, document_, "XML 진척 읽기·검증 중…", [path] {
            return importProjectXml(readProjectXml(path), QFileInfo(path).fileName());
        });
        if (input && reviewXmlProgressMerge(this, document_, *input)) {
            refresh();
            statusBar()->showMessage("XML 진척 병합 검토 완료 · 새 보고는 문서 저장 시 보존됩니다.");
        }
    } catch (const std::exception &e) {
        report(e);
    }
}
void MainWindow::mergeXmlBaselines() {
    if (!document_.project() || document_.readOnly())
        return;
    try {
        const auto path =
            QFileDialog::getOpenFileName(this, "현재 문서에 병합할 기준선 XML", {}, "Project XML (*.xml)");
        if (path.isEmpty())
            return;
        auto input = prepareExchange<ProjectXmlExchange>(this, document_, "XML 기준선 읽기·검증 중…", [path] {
            return importProjectXml(readProjectXml(path), QFileInfo(path).fileName());
        });
        if (input && reviewXmlBaselineMerge(this, document_, *input)) {
            refresh();
            statusBar()->showMessage("XML 기준선 병합 완료 · 일반 저장으로 값·연결·전후 이력을 보존합니다.");
        }
    } catch (const std::exception &e) {
        report(e);
    }
}
void MainWindow::mergeXmlPeriodActuals() {
    if (!document_.project() || document_.readOnly())
        return;
    try {
        if (!document_.project()->statusAt)
            throw AppError(ErrorCode::STATUS_DATE_REQUIRED);
        const auto path =
            QFileDialog::getOpenFileName(this, "일별 실제 공수·원가가 있는 Project XML", {}, "Project XML (*.xml)");
        if (path.isEmpty())
            return;
        auto input = prepareExchange<ProjectXmlExchange>(this, document_, "XML 배정 실적 읽기·검증 중…", [path] {
            return importProjectXml(readProjectXml(path), QFileInfo(path).fileName());
        });
        if (input && reviewXmlPeriodActuals(this, document_, *input)) {
            refresh();
            statusBar()->showMessage("XML 일별 실적 검토 완료 · 일반 저장으로 원장과 출처를 보존합니다.");
        }
    } catch (const std::exception &e) {
        report(e);
    }
}
void MainWindow::exportXml(bool includeActuals) {
    if (!document_.project())
        return;
    try {
        const auto generation = document_.sessionGeneration(), revision = document_.project()->revision;
        auto snapshot = document_.snapshot();
        auto prepared = prepareExchange<ProjectXmlExchange>(
            this, document_, "XML 내보내기 준비·검증 중…", [snapshot, includeActuals] {
                return includeActuals ? exportProjectXmlActuals(*snapshot) : exportProjectXml(*snapshot);
            });
        snapshot.reset();
        if (!prepared)
            return;
        auto exchange = std::move(*prepared);
        auto parent = document_.path().empty() ? QDir::homePath() : QFileInfo(qs(document_.path())).absolutePath();
        auto destination = preview(this, exchange,
                                   QDir(parent).filePath(includeActuals ? "ppm-xml-actuals-export" : "ppm-xml-export"));
        if (destination.isEmpty())
            return;
        if (document_.sessionGeneration() != generation || !document_.project() ||
            document_.project()->revision != revision)
            throw AppError(ErrorCode::MAINWINDOWXER_002, {});
        QString folder;
        if (!runExchangeOperation(
                this, document_, "XML 게시·기준선 검증 중…", [&] { folder = publishProjectXml(exchange, destination); },
                true))
            return;
        statusBar()->showMessage(
            (includeActuals ? "표준 실적 XML 내보내기 완료 · 원본/대사/진단: " : "XML 내보내기 완료 · 원본/진단: ") +
            folder);
    } catch (const std::exception &e) {
        report(e);
    }
}
} // namespace ppm
