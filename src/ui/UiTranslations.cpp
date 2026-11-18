#include "ui/UiTranslations.h"
#include <QCoreApplication>
#include <QTranslator>
#include <string_view>
#include <utility>

namespace {
// Keep standard buttons localized even in packages built with --no-translations.
// Only Qt's standard-button contexts are handled; custom application labels are untouched.
class KoreanButtons final : public QTranslator {
public:
    using QTranslator::QTranslator;
    bool isEmpty() const override {
        return false;
    }
    QString translate(const char *context, const char *source, const char *, int) const override {
        if (!context || !source)
            return {};
        if (std::string_view(context) == "QCocoaTheme" && std::string_view(source) == "Don't Save")
            return QString::fromUtf8("저장 안 함");
        if (std::string_view(context) != "QPlatformTheme")
            return {};
        static const std::pair<std::string_view, const char *> buttons[]{{"OK", "확인"},
                                                                         {"Save", "저장"},
                                                                         {"Save All", "모두 저장"},
                                                                         {"Open", "열기"},
                                                                         {"&Yes", "예(&Y)"},
                                                                         {"Yes to &All", "모두 예(&A)"},
                                                                         {"&No", "아니요(&N)"},
                                                                         {"N&o to All", "모두 아니요(&O)"},
                                                                         {"Abort", "중단"},
                                                                         {"Retry", "다시 시도"},
                                                                         {"Ignore", "무시"},
                                                                         {"Close", "닫기"},
                                                                         {"Cancel", "취소"},
                                                                         {"Discard", "저장 안 함"},
                                                                         {"Help", "도움말"},
                                                                         {"Apply", "적용"},
                                                                         {"Reset", "초기화"},
                                                                         {"Restore Defaults", "기본값 복원"}};
        for (const auto &[original, korean] : buttons)
            if (source == original)
                return QString::fromUtf8(korean);
        return {};
    }
};
} // namespace
namespace jobs {
void installUiTranslations(QCoreApplication &app) {
    constexpr auto name = "ppmKoreanButtons";
    if (app.findChild<QTranslator *>(name))
        return;
    auto *translator = new KoreanButtons(&app);
    translator->setObjectName(name);
    app.installTranslator(translator);
}
} // namespace jobs
