#pragma once
#include "domain/ExternalEvmTypes.h"
#include <QJsonArray>
namespace jobs {
QJsonArray encodeExternalEvmAdoptions(const std::vector<ExternalEvmAdoption> &);
std::vector<ExternalEvmAdoption> decodeExternalEvmAdoptions(const QJsonValue &);
} // namespace jobs
