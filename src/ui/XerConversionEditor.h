#pragma once
#include "domain/XerSourceRelink.h"
#include <QGroupBox>
#include <functional>
class QCheckBox;
class QLineEdit;
class QSpinBox;
class QComboBox;
namespace ppm {
class XerConversionEditor final : public QGroupBox {
    QCheckBox *enabled_;
    QLineEdit *date_, *reference_;
    QSpinBox *numerator_, *denominator_;
    QComboBox *types_[2];
    QSpinBox *workNumerator_[2], *workDenominator_[2];
    std::string targetCurrency_;

public:
    XerConversionEditor(const QString &title, const QString &prefix, const Project &, QWidget *parent);
    std::function<void()> changed;
    void source(const XerFinancialSnapshot &);
    std::optional<XerSourceConversion> conversion() const;
};
void previewXerConversions(QWidget *parent, const XerFinancialSnapshot &, const XerFinancialSnapshot &,
                           const XerRelinkAudit &);
} // namespace ppm
