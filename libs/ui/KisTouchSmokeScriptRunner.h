#ifndef KISTOUCHSMOKESCRIPTRUNNER_H
#define KISTOUCHSMOKESCRIPTRUNNER_H

#include <functional>

#include <QJsonObject>
#include <QString>

class KisMainWindow;

class KisTouchSmokeScriptRunner
{
public:
    using ReportStepFn = std::function<void(const QString &name, bool ok, const QJsonObject &details)>;

    static bool isScriptScenarioSpec(const QString &scenarioSpec);

    static bool loadScriptFromScenarioSpec(const QString &scenarioSpec, QJsonObject *scriptOut, QString *errorOut);

    static bool runScript(const QJsonObject &script, KisMainWindow *mainWindow, const ReportStepFn &reportStep, QString *errorOut);
};

#endif // KISTOUCHSMOKESCRIPTRUNNER_H
