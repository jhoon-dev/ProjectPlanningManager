#include "ui/SandboxAccess.h"
#include "formatting/QtDiagnosticText.h"
#include "persistence/PrimaveraXer.h"
#include "ui/MainWindow.h"
#include "ui/EditorUi.h"
#include "ui/ExchangeOperation.h"
#include "ui/XerProjectDialog.h"
#include "ui/XerBatchDialog.h"
#include "ui/DesktopWorkspace.h"
#include "persistence/XerBatch.h"
#include <QMetaObject>
#include <algorithm>
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
#include <QStatusBar>
#include <QVBoxLayout>
#include <stdexcept>
namespace jobs {
namespace {
QString qs(const std::string &s) {
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}
QString exchangePreview(QWidget *parent, bool importing, const QString &text, const QString &proposed,
                        const QString &objectName) {
    QDialog dialog(parent);
    dialog.setObjectName(objectName);
    dialog.setWindowTitle(importing ? "Primavera XER 가져오기 · 변환 확인" : "Primavera XER 내보내기 · 변환 확인");
    dialog.resize(800, 580);
    auto *layout = new QVBoxLayout(&dialog);
    auto *detail = new QPlainTextEdit(text, &dialog);
    detail->setObjectName("xerDiagnostics");
    detail->setReadOnly(true);
    layout->addWidget(detail);
    auto *note = new QLabel("새 교환 폴더 전체 경로 · 기존 폴더는 덮어쓰지 않습니다", &dialog);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *destination = new QLineEdit(proposed, &dialog);
    destination->setObjectName("xerDestination");
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
    buttons->button(QDialogButtonBox::Ok)->setText(importing ? "변환 확인 · 새 사본 열기" : "변환 확인 · 내보내기");
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
    return dialog.exec() == QDialog::Accepted ? destination->text().trimmed() : QString{};
}
QString preview(QWidget *parent, const PrimaveraXerExchange &e, const QString &proposed) {
    return exchangePreview(parent, e.importing, primaveraXerPreview(e), proposed, "primaveraXerPreview");
}
std::optional<XerEncoding> encoding(QWidget *parent) {
    QDialog dialog(parent);
    dialog.setObjectName("xerEncodingDialog");
    dialog.setWindowTitle("XER 원본 문자 인코딩");
    dialog.resize(580, 240);
    auto *layout = new QVBoxLayout(&dialog);
    auto *label = new QLabel(
        "원본 인코딩을 선택하세요. 자동 추측하지 않습니다.\nCP949 등은 원본을 보관하고 UTF-8 사본으로 변환해야 합니다.",
        &dialog);
    label->setWordWrap(true);
    layout->addWidget(label);
    auto *choice = new QComboBox(&dialog);
    choice->setObjectName("xerEncoding");
    choice->addItems({"UTF-8 (BOM 허용 · PPM 내보내기)", "Windows-1252 (서유럽)"});
    layout->addWidget(choice);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    prepareEditorDialog(dialog);
    if (dialog.exec() != QDialog::Accepted)
        return {};
    return choice->currentIndex() == 0 ? XerEncoding::Utf8 : XerEncoding::Windows1252;
}
} // namespace
void MainWindow::importXer() {
    const auto path = QFileDialog::getOpenFileName(this, "Primavera XER 가져오기", {}, "Primavera XER (*.xer)");
    if (path.isEmpty())
        return;
    auto charset = encoding(this);
    if (!charset)
        return;
    try {
        const auto generation = document_.sessionGeneration(),
                   revision = document_.project() ? document_.project()->revision : 0;
        struct Source {
            QByteArray bytes;
            std::vector<XerProjectInfo> projects;
        };
        auto source = prepareExchange<Source>(this, document_, "XER 읽기·프로젝트 목록 확인 중…", [path, charset] {
            Source input;
            input.bytes = readPrimaveraXer(path);
            input.projects = listPrimaveraXerProjects(input.bytes, *charset);
            return input;
        });
        if (!source)
            return;
        QString selected = source->projects.front().id;
        if (source->projects.size() > 1) {
            auto choice = chooseXerProject(this, source->projects);
            if (!choice)
                return;
            selected = *choice;
        }
        if (generation != document_.sessionGeneration() ||
            revision != (document_.project() ? document_.project()->revision : 0))
            throw AppError(ErrorCode::MAINWINDOWXER_001, {});
        auto prepared = prepareExchange<PrimaveraXerExchange>(
            this, document_, "선택 XER 프로젝트 변환·검증 중…", [&source, path, charset, selected] {
                return importPrimaveraXer(source->bytes, QFileInfo(path).fileName(), *charset, selected);
            });
        if (!prepared)
            return;
        auto exchange = std::move(*prepared);
        source.reset();
        auto destination =
            preview(this, exchange,
                    QDir(QFileInfo(path).absolutePath()).filePath(QFileInfo(path).completeBaseName() + "-xer-import"));
        if (destination.isEmpty())
            return;
        if (generation != document_.sessionGeneration() ||
            revision != (document_.project() ? document_.project()->revision : 0))
            throw AppError(ErrorCode::MAINWINDOWXER_001, {});
        if (!confirmChanges())
            return;
        const auto publishSession = document_.sessionGeneration();
        const auto publishRevision = document_.project() ? document_.project()->revision : 0;
        QString folder;
        if (!runExchangeOperation(
                this, document_, "XER 사본 게시·검증 중…", [&] { folder = publishPrimaveraXer(exchange, destination); },
                true))
            return;
        if (document_.sessionGeneration() != publishSession ||
            (document_.project() ? document_.project()->revision : 0) != publishRevision)
            throw AppError(ErrorCode::EXCHANGE_SOURCE_CHANGED);
        document_.open(QDir(folder).filePath("project.ppm").toUtf8().toStdString());
        refresh();
        statusBar()->showMessage("XER 계획 변환 완료 · 원본/진단: " + folder);
    } catch (const std::exception &e) {
        report(e);
    }
}
void MainWindow::exportXer() {
    if (!document_.project())
        return;
    try {
        const auto generation = document_.sessionGeneration(), revision = document_.project()->revision;
        auto snapshot = document_.snapshot();
        auto prepared = prepareExchange<PrimaveraXerExchange>(this, document_, "XER 내보내기 준비·검증 중…",
                                                              [snapshot] { return exportPrimaveraXer(*snapshot); });
        snapshot.reset();
        if (!prepared)
            return;
        auto exchange = std::move(*prepared);
        auto parent = document_.path().empty() ? QDir::homePath() : QFileInfo(qs(document_.path())).absolutePath();
        auto destination = preview(this, exchange, QDir(parent).filePath("jobs-xer-export"));
        if (destination.isEmpty())
            return;
        if (document_.sessionGeneration() != generation || !document_.project() ||
            document_.project()->revision != revision)
            throw AppError(ErrorCode::MAINWINDOWXER_002, {});
        QString folder;
        if (!runExchangeOperation(
                this, document_, "XER 게시·기준선 검증 중…",
                [&] { folder = publishPrimaveraXer(exchange, destination); }, true))
            return;
        statusBar()->showMessage("XER 내보내기 완료 · 원본/진단: " + folder);
    } catch (const std::exception &e) {
        report(e);
    }
}
void MainWindow::batchXer(bool importing) {
    if (!workspace_)
        return;
    try {
        std::vector<XerBatchSource> inputs;
        std::vector<Project> outputs;
        std::vector<XerBatchChoice> choices;
        std::vector<std::pair<std::size_t, QString>> sourceIds;
        QString parent = QDir::homePath();
        const auto workspaceGeneration = workspace_->generation();
        auto configuration = workspace_->configuration();
        auto snapshots = workspace_->snapshots();
        if (importing) {
            const auto files =
                QFileDialog::getOpenFileNames(this, "XER 일괄 작업공간 가져오기", {}, "Primavera XER (*.xer)");
            if (files.empty())
                return;
            auto charset = encoding(this);
            if (!charset)
                return;
            parent = QFileInfo(files.front()).absolutePath();
            struct ReadBatch {
                std::vector<XerBatchSource> inputs;
                std::vector<std::vector<XerProjectInfo>> inventories;
            };
            auto read =
                prepareExchange<ReadBatch>(this, document_, "XER 파일·프로젝트 목록 읽는 중…", [files, charset] {
                    ReadBatch result;
                    if (files.size() > 100)
                        throw AppError(ErrorCode::WORKSPACE_LIMIT);
                    qint64 bytes = 0;
                    for (const auto &file : files) {
                        XerBatchSource input{readPrimaveraXer(file), QFileInfo(file).fileName(), *charset, {}};
                        bytes += input.bytes.size();
                        if (bytes > 64 * 1024 * 1024)
                            throw AppError(ErrorCode::WORKSPACE_LIMIT);
                        result.inventories.push_back(listPrimaveraXerProjects(input.bytes, *charset));
                        result.inputs.push_back(std::move(input));
                    }
                    return result;
                });
            if (!read)
                return;
            inputs = std::move(read->inputs);
            for (std::size_t i = 0; i < read->inventories.size(); ++i)
                for (const auto &project : read->inventories[i]) {
                    const bool blocked = project.externalIncoming || project.externalOutgoing;
                    choices.push_back({QString("%1 · [%2] %3 · %4 · 작업 %5%6")
                                           .arg(inputs[i].name, project.id, project.code, project.name)
                                           .arg(project.tasks)
                                           .arg(blocked ? " · 외부 관계: 변환 차단" : ""),
                                       blocked});
                    sourceIds.emplace_back(i, project.id);
                }
        } else {
            for (const auto &project : snapshots)
                choices.push_back({qs(project->name) + " · " + qs(project->documentId) +
                                       QString(" · 작업 %1").arg(project->tasks.size()),
                                   false});
        }
        auto selected = chooseXerBatch(this, choices,
                                       importing ? "가져올 프로젝트를 선택하세요. 외부 선후행 관계가 있는 프로젝트는 "
                                                   "차단합니다. 모든 선택을 검증한 후 새 작업공간을 만듭니다."
                                                 : "현재 열려 있는 프로젝트만 표시합니다. 닫힌 프로젝트는 제외됩니다. "
                                                   "선택 프로젝트의 현재 입력과 공유 연결을 새 사본으로 저장합니다.");
        if (!selected)
            return;
        if (workspace_->generation() != workspaceGeneration)
            throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
        struct SourceVersion {
            std::string id;
            std::uint64_t session, revision;
        };
        std::vector<SourceVersion> versions;
        if (importing) {
            for (auto index : *selected)
                inputs[sourceIds[index].first].projectIds.append(sourceIds[index].second);
            std::erase_if(inputs, [](const auto &input) { return input.projectIds.empty(); });
        } else {
            for (auto index : *selected) {
                const auto &snapshot = snapshots[index];
                auto *doc = workspace_->document(snapshot->documentId);
                if (!doc || !doc->project() || doc->project()->revision != snapshot->revision)
                    throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
                versions.push_back({snapshot->documentId, doc->sessionGeneration(), snapshot->revision});
                outputs.push_back(*snapshot);
            }
        }
        auto prepared = prepareExchange<XerBatchExchange>(this, document_, "XER 일괄 변환·참조 검증 중…", [&] {
            return importing ? jobs::importXerBatch(inputs) : jobs::exportXerBatch(outputs, configuration);
        });
        if (!prepared)
            return;
        inputs.clear();
        outputs.clear();
        snapshots.clear();
        const auto destination = exchangePreview(
            this, importing, xerBatchPreview(*prepared),
            QDir(parent).filePath(importing ? "jobs-xer-workspace-import" : "jobs-xer-workspace-export"),
            "xerBatchPreview");
        if (destination.isEmpty())
            return;
        auto current = [&] {
            if (workspace_->generation() != workspaceGeneration)
                return false;
            for (const auto &version : versions) {
                auto *doc = workspace_->document(version.id);
                if (!doc || doc->sessionGeneration() != version.session || !doc->project() ||
                    doc->project()->revision != version.revision)
                    return false;
            }
            return true;
        };
        if (!current())
            throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
        QString folder;
        if (!runExchangeOperation(
                this, document_, "XER 일괄 폴더 저장·검증 중…",
                [&] {
                    folder = publishXerBatch(*prepared, destination, [&] {
                        bool valid = false;
                        QMetaObject::invokeMethod(this, [&] { valid = current(); }, Qt::BlockingQueuedConnection);
                        if (!valid)
                            throw AppError(ErrorCode::WORKSPACE_SOURCE_CHANGED);
                    });
                },
                true))
            return;
        statusBar()->showMessage("XER 일괄 저장 완료 · " + folder, 30000);
        if (importing)
            workspace_->openWorkspace(QDir(folder).filePath("workspace.ppmworkspace").toUtf8().toStdString(), this);
    } catch (const std::exception &error) {
        report(error);
    }
}
} // namespace jobs
