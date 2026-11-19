#include "application/Document.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/PrimaveraXer.h"
#include "ui/MainWindow.h"
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <functional>
#include <iostream>
using namespace ppm;
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void require(bool value, const char *message) {
    if (!value) {
        std::cerr << message << '\n';
        std::_Exit(2);
    }
}
QByteArray read(const QString &path) {
    QFile f(path);
    check(f.open(QIODevice::ReadOnly), "read");
    return f.readAll();
}
void write(const QString &path, const QByteArray &bytes) {
    QFile f(path);
    check(f.open(QIODevice::WriteOnly), "write");
    check(f.write(bytes) == bytes.size(), "short write");
}
struct Drive {
    QTimer timer;
    QString input, destination, selected = "200";
    bool cancelSelection = false, cancelPreview = false, expectError = false, blocked = false;
    bool seenSelection = false, seenPreview = false, seenError = false;
    std::function<void()> onSelect;
    Drive(QString in, QString out) : input(std::move(in)), destination(std::move(out)) {
        timer.setInterval(10);
        QObject::connect(&timer, &QTimer::timeout, [this] {
            auto *modal = QApplication::activeModalWidget();
            if (!modal)
                return;
            if (auto *file = qobject_cast<QFileDialog *>(modal)) {
                auto *edit = file->findChild<QLineEdit *>("fileNameEdit");
                require(edit, "file picker input");
                edit->setText(input);
                QMetaObject::invokeMethod(file, "accept");
                return;
            }
            if (modal->objectName() == "xerEncodingDialog") {
                modal->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
                return;
            }
            if (modal->objectName() == "xerProjectDialog") {
                if (seenSelection)
                    return;
                seenSelection = true;
                auto *table = modal->findChild<QTableView *>("xerProjects");
                auto *buttons = modal->findChild<QDialogButtonBox *>();
                auto *search = modal->findChild<QLineEdit *>("xerProjectSearch");
                require(table && buttons && search, "project picker controls missing");
                require(table->model()->rowCount() == 2 && !buttons->button(QDialogButtonBox::Ok)->isEnabled(),
                        "multi picker automatically accepted first project");
                if (cancelSelection) {
                    buttons->button(QDialogButtonBox::Cancel)->click();
                    return;
                }
                search->setText("2026-10-26");
                require(table->model()->rowCount() == 0, "project search matched date outside ID/code/name");
                search->setText(selected);
                require(table->model()->rowCount() == 1, "project ID filter failed");
                table->selectRow(0);
                require(table->selectionModel()->selectedRows().front().data(Qt::UserRole).toString() == selected,
                        "filtered row changed project ID");
                require(buttons->button(QDialogButtonBox::Ok)->isEnabled() != blocked,
                        "external relation blocking wrong");
                if (blocked) {
                    require(modal->findChild<QLabel *>("xerProjectStatus")->text().contains("외부"),
                            "missing blocked explanation");
                    buttons->button(QDialogButtonBox::Cancel)->click();
                    return;
                }
                search->clear();
                table->selectRow(selected == "100" ? 0 : 1);
                require(table->columnViewportPosition(6) + table->columnWidth(6) <= table->viewport()->width(),
                        "external relation counts require horizontal scrolling");
                modal->grab().save("xer-selection-qa/project-picker.png");
                if (onSelect)
                    onSelect();
                buttons->button(QDialogButtonBox::Ok)->click();
                return;
            }
            if (modal->objectName() == "primaveraXerPreview") {
                if (seenPreview)
                    return;
                seenPreview = true;
                auto *detail = modal->findChild<QPlainTextEdit *>("xerDiagnostics");
                require(detail && detail->toPlainText().contains("선택하지 않은 프로젝트") &&
                            detail->toPlainText().contains("선택 프로젝트 ID " + selected),
                        "preview missing selection and original scope");
                modal->findChild<QLineEdit *>("xerDestination")->setText(destination);
                modal->grab().save("xer-selection-qa/selection-preview.png");
                modal->findChild<QDialogButtonBox *>()
                    ->button(cancelPreview ? QDialogButtonBox::Cancel : QDialogButtonBox::Ok)
                    ->click();
                return;
            }
            if (auto *box = qobject_cast<QMessageBox *>(modal)) {
                if (box->standardButtons().testFlag(QMessageBox::Discard)) {
                    box->button(QMessageBox::Discard)->click();
                    return;
                }
                if (!expectError)
                    std::cerr << box->text().toStdString() << '\n';
                require(expectError, "unexpected error");
                seenError = true;
                box->accept();
            }
        });
        timer.start();
    }
};
void trigger(MainWindow &window) {
    auto *a = window.findChild<QAction *>("importPrimaveraXer");
    check(a && a->isEnabled(), "import action");
    a->trigger();
}
void workflow() {
    QTemporaryDir temp;
    check(temp.isValid(), "temporary dir");
    const auto source = read(QString::fromUtf8(PPM_XER_MULTI_FIXTURE));
    const auto input = temp.filePath("multi.xer");
    write(input, source);
    JsonProjectStore store;
    Document doc(store);
    doc.create("기존 일정", "2026-10-26", {"보존 작업"});
    doc.saveAs(temp.filePath("before.ppm").toStdString());
    MainWindow window(doc);
    window.resize(1380, 850);
    window.show();
    QTest::qWait(30);
    QDir().mkpath("xer-selection-qa");
    const auto before = *doc.project();
    const auto initialSession = doc.sessionGeneration();
    {
        Drive d(input, temp.filePath("cancel-selection"));
        d.cancelSelection = true;
        trigger(window);
        check(d.seenSelection && !d.seenPreview && *doc.project() == before &&
                  doc.sessionGeneration() == initialSession && !QFileInfo::exists(d.destination),
              "selection cancel changed source");
    }
    {
        Drive d(input, temp.filePath("cancel-preview"));
        d.cancelPreview = true;
        trigger(window);
        check(d.seenSelection && d.seenPreview && *doc.project() == before && !QFileInfo::exists(d.destination),
              "preview cancel changed source");
    }
    {
        Drive d(input, temp.filePath("selected-b"));
        trigger(window);
        check(d.seenSelection && d.seenPreview && doc.project()->name == "현장 B" &&
                  doc.project()->resources.size() == 2 && !doc.dirty() && doc.sessionGeneration() > initialSession,
              "B import scope/session wrong");
        check(read(QDir(d.destination).filePath("original.xer")) == source, "whole original not preserved");
    }
    const auto b = *doc.project();
    {
        Drive d(input, temp.filePath("changed-selection"));
        d.expectError = true;
        d.onSelect = [&] { doc.renameTask(0, "선택 중 변경"); };
        trigger(window);
        check(d.seenError && !d.seenPreview && !QFileInfo::exists(d.destination),
              "selection revision changed accepted stale result");
        doc.undo();
    }
    {
        Drive d(input, temp.filePath("reopened-selection"));
        d.expectError = true;
        d.onSelect = [&] {
            const auto path = doc.path();
            doc.open(path);
        };
        trigger(window);
        check(d.seenError && !d.seenPreview && !QFileInfo::exists(d.destination),
              "same ID reopened session accepted stale result");
    }
    // One read only: replacing the disk file after inventory cannot change the approved source bytes.
    {
        Drive d(input, temp.filePath("selected-a"));
        d.selected = "100";
        d.onSelect = [&] { write(input, "changed on disk"); };
        trigger(window);
        check(d.seenPreview && doc.project()->name == "현장 A <원문>" && doc.project()->resources.size() == 1 &&
                  read(QDir(d.destination).filePath("original.xer")) == source,
              "source was reread after selection");
    }
    auto crossed = source;
    const auto marker = crossed.indexOf("%T\tRSRC");
    check(marker >= 0, "link insertion point");
    crossed.insert(marker, "%R\t9000\t30\t1020\t100\t200\tPR_FS\t0\r\n");
    write(input, crossed);
    const auto a = *doc.project();
    {
        Drive d(input, temp.filePath("blocked"));
        d.blocked = true;
        trigger(window);
        check(d.seenSelection && !d.seenPreview && *doc.project() == a && !QFileInfo::exists(d.destination),
              "blocked external project was imported");
    }
    check(a.documentId != b.documentId, "separate imports reused native document ID");
    write("xer-selection-qa/ui-contract.json", QJsonDocument(QJsonObject{{"explicit_selection", true},
                                                                         {"stable_id_after_filter", true},
                                                                         {"selection_cancel", true},
                                                                         {"preview_cancel", true},
                                                                         {"revision_stale", true},
                                                                         {"same_id_session_stale", true},
                                                                         {"source_read_once", true},
                                                                         {"external_relation_block", true},
                                                                         {"A_resources", 1},
                                                                         {"B_resources", 2}})
                                                   .toJson());
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
#ifdef Q_OS_WIN
    const auto id =
        QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    if (id >= 0)
        QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(id).front(), 9));
#endif
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [] {
        std::cerr << "XER selection UI timeout\n";
        std::_Exit(2);
    });
    watchdog.start(60000);
    try {
        workflow();
        std::cout << "XER selection UI: explicit "
                     "selection/filter/cancel/preview/publication/stale/session/one-read/block passed\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
