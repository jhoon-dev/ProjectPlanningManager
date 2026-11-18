#include "ui/UiTranslations.h"
#include <QCoreApplication>
#include <QDir>
#include <QResource>
#include <QTranslator>
#include <string_view>

static void initializeTranslationResources() {
    Q_INIT_RESOURCE(jobs_brand);
}

namespace {
// Preserve the explicit save decision wording on top of Qt's complete catalog.
class SaveDecisionTranslation final : public QTranslator {
public:
    using QTranslator::QTranslator;
    bool isEmpty() const override {
        return false;
    }
    QString translate(const char *context, const char *source, const char *, int) const override {
        if (!context || !source)
            return {};
        if ((std::string_view(context) == "QCocoaTheme" && std::string_view(source) == "Don't Save") ||
            (std::string_view(context) == "QPlatformTheme" && std::string_view(source) == "Discard"))
            return QString::fromUtf8("저장 안 함");
        return {};
    }
};
} // namespace
namespace jobs {
void installUiTranslations(QCoreApplication &app) {
    constexpr auto name = "ppmQtKoreanTranslation";
    if (app.findChild<QTranslator *>(name))
        return;
    initializeTranslationResources();
    auto *translator = new QTranslator(&app);
    translator->setObjectName(name);
    // A compatible replacement catalog next to the executable takes precedence.
    const auto directory = QDir(QCoreApplication::applicationDirPath()).filePath("translations");
    if (!translator->load("qtbase_ko", directory) && !translator->load(":/jobs/i18n/qtbase_ko.qm"))
        qFatal("Cannot load the bundled Korean Qt translation catalog");
    app.installTranslator(translator);
    app.installTranslator(new SaveDecisionTranslation(&app));
}
} // namespace jobs
