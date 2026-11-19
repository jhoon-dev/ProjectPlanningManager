#pragma once
#include <QSettings>
#include <QString>

namespace ppm {
struct ApplicationStorage {
    QString packageFamilyName;
    QString stateDirectory;
    QString recoveryDirectory;
    QString settingsFile;
    QString legacyStateDirectory;
    bool packaged() const {
        return !packageFamilyName.isEmpty();
    }
};
// No directory creation, settings migration or fallback to another package's state.
ApplicationStorage applicationStorage();
QSettings applicationSettings();
// Copies missing settings and validated recovery generations once; legacy state is retained.
void migrateApplicationStorage();
void migrateApplicationSettings(QSettings &legacy, QSettings &current);
} // namespace ppm
