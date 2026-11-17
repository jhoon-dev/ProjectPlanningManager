#include "ui/ResourceViewDialog.h"
#include "ui/MainWindow.h"
#include "persistence/JsonProjectStore.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <QTimer>
#include <QTreeWidget>
#include <iostream>
namespace {
using namespace jobs;
void check(bool x, const char *s) {
    if (!x)
        throw std::runtime_error(s);
}
template <class F> void until(F f, const char *s) {
    QElapsedTimer t;
    t.start();
    while (!f() && t.elapsed() < 30000)
        QTest::qWait(10);
    check(f(), s);
}
QPushButton *button(QDialog &d, const char *s) {
    auto *b = d.findChild<QPushButton *>(s);
    check(b, "button");
    return b;
}
void ready(QDialog &d) {
    until([&] { return button(d, "exportResourceViewXlsx")->isEnabled(); }, "view not ready");
}
void run() {
    QTemporaryDir temp;
    JsonProjectStore store;
    Document doc(store);
    doc.create("자원 보기 화면", "2026-11-02", {"설계"});
    doc.setDuration(0, 480);
    CustomField f;
    f.name = "지역";
    f.type = CustomFieldType::Choice;
    f.options = {{"seoul", "서울"}, {"busan", "부산"}};
    auto fid = doc.putCustomField(f, FieldScope::Resource);
    Resource r;
    r.name = "설계 인력";
    auto r1 = doc.putResource(r);
    r.name = "현장 인력";
    auto r2 = doc.putResource(r);
    doc.setResourceAttribute({r1}, "field:" + fid, "seoul");
    doc.setResourceAttribute({r2}, "field:" + fid, "busan");
    const auto output = QDir::currentPath() + "/professional-fields-qa";
    QDir().mkpath(output);
    {
        ResourceViewDialog d(doc);
        d.show();
        ready(d);
        const auto before = *doc.project();
        auto *tree = d.findChild<QTreeWidget *>("resourceViewTree");
        check(tree->topLevelItemCount() == 2, "initial resources");
        auto *group = d.findChild<QComboBox *>("resourceGroup0");
        group->setCurrentIndex(group->findData(QString::fromStdString("field:" + fid)));
        ready(d);
        check(tree->topLevelItemCount() == 2 && tree->topLevelItem(0)->childCount() == 1, "group tree");
        button(d, "addResourceFilter")->click();
        auto *filters = d.findChild<QTableWidget *>("resourceViewFilters");
        auto *key = static_cast<QComboBox *>(filters->cellWidget(0, 0));
        key->setCurrentIndex(key->findData(QString::fromStdString("field:" + fid)));
        auto *op = static_cast<QComboBox *>(filters->cellWidget(0, 1));
        op->setCurrentIndex(op->findData("equals"));
        static_cast<QLineEdit *>(filters->cellWidget(0, 2))->setText("서울");
        ready(d);
        check(tree->topLevelItemCount() == 1 &&
                  tree->topLevelItem(0)->child(0)->data(0, Qt::UserRole).toString() == QString::fromStdString(r1),
              "choice label filter");
        check(*doc.project() == before, "local filters committed input");
        d.findChild<QLineEdit *>("resourceViewName")->setText("서울 인력");
        button(d, "saveResourceView")->click();
        ready(d);
        check(doc.project()->savedResourceViews.size() == 1 &&
                  doc.project()->savedResourceViews[0].settings.filters[0].value == "seoul",
              "saved stable option ID");
        check(d.grab().save(output + "/resource-view.png"), "screen");
        d.resize(800, 600);
        QTest::qWait(20);
        auto *save = button(d, "exportResourceViewXlsx");
        check(d.rect().contains(QRect(save->mapTo(&d, QPoint{}), save->size())), "small footer");
        check(d.grab().save(output + "/resource-view-small.png"), "small screen");
        auto *saved = d.findChild<QComboBox *>("savedResourceViews");
        saved->setCurrentIndex(0);
        d.findChild<QLineEdit *>("resourceViewQuery")->setText("없음");
        ready(d);
        check(tree->topLevelItemCount() == 0, "query result");
        saved->setCurrentIndex(1);
        ready(d);
        check(tree->topLevelItemCount() == 1, "restore saved view");
        doc.renameTask(0, "변경");
        check(!button(d, "saveResourceView")->isEnabled() && !button(d, "exportResourceViewXlsx")->isEnabled(),
              "stale action");
        button(d, "reloadResourceView")->click();
        ready(d);
        check(tree->topLevelItemCount() == 2, "reload source");
        doc.setWriteAuthorization([] { return false; });
        button(d, "reloadResourceView")->click();
        ready(d);
        check(!button(d, "saveResourceView")->isEnabled() && button(d, "exportResourceViewXlsx")->isEnabled(),
              "role readonly view");
        d.reject();
        doc.setWriteAuthorization({});
    }
    const auto path = temp.filePath("views.ppm").toStdString();
    doc.saveAs(path);
    doc.open(path, true);
    {
        ResourceViewDialog d(doc);
        d.show();
        ready(d);
        check(!button(d, "saveResourceView")->isEnabled(), "readonly save");
        d.reject();
    }
    doc.open(path);
    {
        MainWindow w(doc);
        w.show();
        auto *a = w.findChild<QAction *>("resourceViews");
        check(a, "menu action");
        QTimer::singleShot(0, [] {
            auto *d = dynamic_cast<ResourceViewDialog *>(QApplication::activeModalWidget());
            if (!d)
                std::exit(2);
            d->reject();
        });
        a->trigger();
    }
    {
        ResourceViewDialog d(doc);
        d.reject();
    }
    QThreadPool::globalInstance()->waitForDone();
    std::cout << "PASS resource view UI: groups, typed filters, local draft, save/restore, stale, role/readonly, "
                 "close, menu\n";
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    auto font = QFontDatabase::addApplicationFont(QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/malgun.ttf"));
    if (font >= 0)
        app.setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 9));
#endif
    try {
        run();
    } catch (const jobs::AppError &e) {
        std::cerr << e.code();
        for (const auto &a : e.diagnostic().arguments)
            std::cerr << " " << a;
        return 1;
    } catch (const std::exception &e) {
        std::cerr << e.what();
        return 1;
    }
}
