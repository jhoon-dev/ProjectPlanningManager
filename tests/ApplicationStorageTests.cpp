#include "persistence/ApplicationStorage.h"
#include "persistence/JsonRecoveryStore.h"
#include "application/ProductIdentity.h"
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("ToolMoa");
    QCoreApplication::setApplicationName("PPM-Storage-Contract");
    try {
        const auto storage = ppm::applicationStorage();
        if (storage.packaged())
            throw std::logic_error("unpackaged test unexpectedly has package identity");
        if (storage.stateDirectory != QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) ||
            storage.recoveryDirectory != QDir(storage.stateDirectory).filePath("recovery") ||
            !storage.settingsFile.isEmpty())
            throw std::logic_error("standalone data contract changed");
        const auto settings = ppm::applicationSettings();
        const QSettings legacy;
        if (settings.format() != legacy.format() || settings.fileName() != legacy.fileName() ||
            ppm::JsonRecoveryStore::defaultDirectory() != storage.recoveryDirectory)
            throw std::logic_error("standalone settings/recovery migrated unexpectedly");
        QCoreApplication::setApplicationName("JOBS");
        const auto legacyData = ppm::applicationStorage();
        const auto legacySettings = ppm::applicationSettings();
        QCoreApplication::setApplicationName(ppm::productShortName);
        const auto branded = ppm::applicationStorage();
        const auto brandedSettings = ppm::applicationSettings();
        if (branded.stateDirectory == legacyData.stateDirectory ||
            branded.legacyStateDirectory != legacyData.stateDirectory ||
            brandedSettings.fileName() == legacySettings.fileName())
            throw std::logic_error("PPM storage does not expose the legacy migration source");
        QTemporaryDir folder;
        QSettings oldSettings(folder.filePath("legacy.ini"), QSettings::IniFormat);
        QSettings newSettings(folder.filePath("ppm.ini"), QSettings::IniFormat);
        oldSettings.setValue("existing", "old");
        oldSettings.setValue("missing", QByteArray("binary bookmark"));
        newSettings.setValue("existing", "new");
        oldSettings.sync();
        ppm::migrateApplicationSettings(oldSettings, newSettings);
        oldSettings.setValue("missing", "changed after migration");
        ppm::migrateApplicationSettings(oldSettings, newSettings);
        if (newSettings.value("existing") != "new" || newSettings.value("missing").toByteArray() != "binary bookmark" ||
            oldSettings.value("existing") != "old")
            throw std::logic_error("Settings migration overwrote current state, old state, or repeated an import");
        std::cout << "PASS: PPM paths, legacy discovery, non-overwriting and idempotent settings migration\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
