#include "ui/UiTranslations.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QTranslator>

// Deliberately link the production installer without its resource bundle to
// exercise a missing/corrupt catalog without a production-only test switch.
int qInitResources_ppm_brand() {
    return 1;
}

class UiTranslationsFallbackTests : public QObject {
    Q_OBJECT
private slots:
    void missingCatalogKeepsApplicationUsable() {
        QTest::ignoreMessage(QtWarningMsg,
                             "Cannot load the Korean Qt translation catalog; continuing with Qt's default language");
        ppm::installUiTranslations(*qApp);
        QDialogButtonBox buttons(QDialogButtonBox::Close | QDialogButtonBox::Cancel);
        QCOMPARE(buttons.button(QDialogButtonBox::Close)->text(), QString("Close"));
        QSignalSpy rejected(&buttons, &QDialogButtonBox::rejected);
        buttons.button(QDialogButtonBox::Close)->click();
        QCOMPARE(rejected.count(), 1);
        QCOMPARE(QCoreApplication::translate("QPlatformTheme", "Discard"), QString("저장 안 함"));
        ppm::installUiTranslations(*qApp);
        QCOMPARE(qApp->findChildren<QTranslator *>().size(), 2);
    }
};
QTEST_MAIN(UiTranslationsFallbackTests)
#include "UiTranslationsFallbackTests.moc"
