#pragma once
#include "application/Document.h"
#include <QByteArray>
#include <QJsonObject>
namespace jobs {
struct AutomationPreview {
    std::shared_ptr<Document::PreparedEdit> edit;
    QJsonObject receipt;
};
AutomationPreview previewAutomation(Document &, const QByteArray &, std::shared_ptr<CalculationControl> = {});
QJsonObject applyAutomation(Document &, AutomationPreview &);
QJsonObject describeAutomationProject(const Project &);
} // namespace jobs
