#include "ui/XerBatchDialog.h"
#include "ui/DesktopWorkspace.h"
#include "ui/MainWindow.h"
#include "persistence/XerBatch.h"
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <functional>
#include <iostream>
using namespace ppm;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void require(bool ok, const char *message) {
    if (!ok) {
        std::cerr << message << '\n';
        std::_Exit(2);
    }
}
struct Drive {
    QTimer timer;
    QString destination;
    bool cancelSelection = false, cancelPreview = false, seenSelection = false, seenPreview = false, seenError = false;
    std::function<void()> onPreview;
    explicit Drive(QString path) : destination(std::move(path)) {
        timer.setInterval(10);
        QObject::connect(&timer, &QTimer::timeout, [this] {
            auto *modal = QApplication::activeModalWidget();
            if (!modal)
                return;
            if (auto *file = qobject_cast<QFileDialog *>(modal)) {
                file->findChild<QLineEdit *>("fileNameEdit")->setText(PPM_XER_MULTI_FIXTURE);
                QMetaObject::invokeMethod(file, "accept");
            } else if (modal->objectName() == "xerEncodingDialog") {
                modal->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
            } else if (modal->objectName() == "xerBatchSelection" && !seenSelection) {
                seenSelection = true;
                auto *buttons = modal->findChild<QDialogButtonBox *>();
                require(!buttons->button(QDialogButtonBox::Ok)->isEnabled(), "batch silently preselected projects");
                if (cancelSelection)
                    buttons->button(QDialogButtonBox::Cancel)->click();
                else {
                    modal->findChild<QPushButton *>("xerBatchSelectAll")->click();
                    modal->grab().save("xer-batch-qa/selection.png");
                    buttons->button(QDialogButtonBox::Ok)->click();
                }
            } else if (modal->objectName() == "xerBatchPreview" && !seenPreview) {
                seenPreview = true;
                auto *detail = modal->findChild<QPlainTextEdit *>("xerDiagnostics");
                require(detail && detail->isReadOnly() && detail->toPlainText().contains("프로젝트 2개"),
                        "batch preview missing counts");
                modal->findChild<QLineEdit *>("xerDestination")->setText(destination);
                modal->grab().save("xer-batch-qa/preview.png");
                if (onPreview)
                    onPreview();
                modal->findChild<QDialogButtonBox *>()
                    ->button(cancelPreview ? QDialogButtonBox::Cancel : QDialogButtonBox::Ok)
                    ->click();
            } else if (auto *message = qobject_cast<QMessageBox *>(modal)) {
                seenError = true;
                if (message->standardButtons() & QMessageBox::Discard)
                    message->button(QMessageBox::Discard)->click();
                else
                    message->accept();
            }
        });
        timer.start();
    }
};
void trigger(MainWindow &window, const char *name) {
    auto *action = window.findChild<QAction *>(name);
    check(action && action->isEnabled(), "batch menu missing or disabled");
    action->trigger();
    QTest::qWait(30);
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    app.setAttribute(Qt::AA_DontUseNativeDialogs);
    if (QFileInfo::exists("C:/Windows/Fonts/malgun.ttf")) {
        const int font = QFontDatabase::addApplicationFont("C:/Windows/Fonts/malgun.ttf");
        if (font >= 0)
            app.setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 10));
    }
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [] {
        std::cerr << "batch UI timeout\n";
        std::_Exit(2);
    });
    watchdog.start(120000);
    try {
        QDir().mkpath("xer-batch-qa");
        {
            QTimer timer;
            timer.setInterval(10);
            QObject::connect(&timer, &QTimer::timeout, [] {
                auto *modal = QApplication::activeModalWidget();
                if (!modal || modal->objectName() != "xerBatchSelection")
                    return;
                auto *list = modal->findChild<QListWidget *>("xerBatchProjects");
                auto *buttons = modal->findChild<QDialogButtonBox *>();
                require(list->count() == 3 && !buttons->button(QDialogButtonBox::Ok)->isEnabled(),
                        "selection defaults");
                modal->findChild<QPushButton *>("xerBatchSelectAll")->click();
                require(list->item(1)->checkState() == Qt::Unchecked && !(list->item(1)->flags() & Qt::ItemIsEnabled),
                        "blocked external project selected");
                require(modal->findChild<QLabel *>("xerBatchCount")->text().contains("2 / 3"), "selection count");
                buttons->button(QDialogButtonBox::Ok)->click();
            });
            timer.start();
            auto result = chooseXerBatch(nullptr, {{"A", false}, {"외부 관계", true}, {"C", false}}, "계약 검사");
            check(result && *result == std::vector<std::size_t>{0, 2}, "stable batch selection IDs");
        }
        QTemporaryDir temp;
        auto files = std::make_shared<JsonProjectStore>();
        std::vector<std::string> paths, ids;
        for (int i = 0; i < 2; ++i) {
            Document doc(*files);
            doc.create(i == 0 ? "현장 A" : "현장 B", "2026-10-26", {"기존 작업"});
            doc.setDuration(0, 60);
            paths.push_back(temp.filePath(QString("source%1.ppm").arg(i)).toStdString());
            ids.push_back(doc.project()->documentId);
            doc.saveAs(paths.back());
        }
        DesktopWorkspace workspace(files, {});
        auto *first = workspace.openProject(paths[0]);
        workspace.openProject(paths[1]);
        QTest::qWait(30);
        auto before = *workspace.document(ids[0])->project();
        {
            Drive drive(temp.filePath("cancel-selection"));
            drive.cancelSelection = true;
            trigger(*first, "exportXerBatch");
            check(drive.seenSelection && !drive.seenPreview && !QFileInfo::exists(drive.destination),
                  "selection cancel published");
        }
        {
            Drive drive(temp.filePath("cancel-preview"));
            drive.cancelPreview = true;
            trigger(*first, "exportXerBatch");
            check(drive.seenPreview && !QFileInfo::exists(drive.destination), "preview cancel published");
        }
        {
            Drive drive(temp.filePath("stale"));
            drive.onPreview = [&] { workspace.document(ids[1])->renameTask(0, "다른 창에서 변경"); };
            trigger(*first, "exportXerBatch");
            check(drive.seenError && !QFileInfo::exists(drive.destination), "other document stale snapshot published");
            workspace.document(ids[1])->undo();
        }
        {
            Drive drive(temp.filePath("export"));
            trigger(*first, "exportXerBatch");
            check(drive.seenPreview && !drive.seenError &&
                      QFileInfo::exists(QDir(drive.destination).filePath("workspace.xer")),
                  "UI batch export failed");
        }
        check(*workspace.document(ids[0])->project() == before && !workspace.document(ids[0])->dirty(),
              "batch export changed source");
        {
            Drive drive(temp.filePath("import"));
            trigger(*first, "importXerBatch");
            check(drive.seenPreview && !drive.seenError && workspace.snapshots().size() == 4,
                  "batch import did not adopt all projects");
            check(*workspace.document(ids[0])->project() == before && !workspace.document(ids[0])->dirty(),
                  "batch import replaced existing document");
        }
        std::cout << "PASS: batch selection, blocked links, menus, preview/cancel/stale, publish, additive workspace "
                     "adoption\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
