#include "KisTouchSmokeScriptRunner.h"
#include "KisTouchSmokeScriptRunner_p.h"

#include <cmath>

#include <QApplication>
#include <QAction>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMouseEvent>
#include <QThread>
#include <QTouchDevice>
#include <QTouchEvent>
#include <QWidget>

#include <kactioncollection.h>

#include <KoColor.h>
#include <KoColorSpaceConstants.h>
#include <KoCompositeOpRegistry.h>
#include <KoToolBase.h>
#include <KoToolManager.h>

#include "KisMainWindow.h"
#include "KisView.h"
#include "KisViewManager.h"
#include "canvas/kis_canvas2.h"
#include "canvas/kis_canvas_controller.h"
#include "canvas/kis_coordinates_converter.h"
#include "canvas/kis_tool_proxy.h"
#include "input/KisTouchGestureAction.h"
#include "input/KisTouchQuickMenuAction.h"
#include "input/kis_input_profile_manager.h"
#include "input/kis_input_manager.h"
#include "input/kis_zoom_and_rotate_action.h"
#include "kis_config.h"
#include "kis_group_layer.h"
#include "kis_image.h"
#include "kis_paint_device.h"
#include "kis_paint_layer.h"
#include "kis_painter.h"

namespace KisTouchSmokeScriptRunnerDetail {

QString jsonTypeName(const QJsonValue &v)
{
    if (v.isArray()) {
        return QStringLiteral("array");
    }
    if (v.isBool()) {
        return QStringLiteral("bool");
    }
    if (v.isDouble()) {
        return QStringLiteral("number");
    }
    if (v.isNull()) {
        return QStringLiteral("null");
    }
    if (v.isObject()) {
        return QStringLiteral("object");
    }
    if (v.isString()) {
        return QStringLiteral("string");
    }
    if (v.isUndefined()) {
        return QStringLiteral("undefined");
    }
    return QStringLiteral("unknown");
}

bool waitForUiCondition(int timeoutMs, const std::function<bool()> &condition)
{
    constexpr int stepMs = 20;
    const int iterations = qMax(1, timeoutMs / stepMs);
    for (int i = 0; i < iterations; ++i) {
        QApplication::processEvents();
        if (condition()) {
            return true;
        }
        QThread::msleep(stepMs);
    }
    QApplication::processEvents();
    return condition();
}

bool waitForImageCondition(KisImageWSP image, int timeoutMs, const std::function<bool()> &condition)
{
    constexpr int stepMs = 20;
    const int iterations = qMax(1, timeoutMs / stepMs);
    for (int i = 0; i < iterations; ++i) {
        QApplication::processEvents();
        if (image && image->tryBarrierLock(true)) {
            image->unlock();
        }
        if (condition()) {
            return true;
        }
        QThread::msleep(stepMs);
    }
    QApplication::processEvents();
    if (image && image->tryBarrierLock(true)) {
        image->unlock();
    }
    return condition();
}

bool sleepWithEvents(int ms)
{
    return waitForUiCondition(ms, []() { return false; });
}

bool sleepWithImageEvents(KisImageWSP image, int ms)
{
    waitForImageCondition(image, ms, []() { return false; });
    return true;
}

} // namespace KisTouchSmokeScriptRunnerDetail

namespace {

using KisTouchSmokeScriptRunnerDetail::actionEnsureChecked;
using KisTouchSmokeScriptRunnerDetail::actionWaitChecked;
using KisTouchSmokeScriptRunnerDetail::actionTriggerWaitLayerCountDelta;
using KisTouchSmokeScriptRunnerDetail::waitCanvasEraserMode;
using KisTouchSmokeScriptRunnerDetail::waitCanvasEffectiveCompositeOp;
using KisTouchSmokeScriptRunnerDetail::assertActiveToolMaskSyntheticEvents;
using KisTouchSmokeScriptRunnerDetail::buildTouchDragPathPoints;
using KisTouchSmokeScriptRunnerDetail::buildTouchHoldPoints;
using KisTouchSmokeScriptRunnerDetail::getCanvasContext;
using KisTouchSmokeScriptRunnerDetail::hideOverlay;
using KisTouchSmokeScriptRunnerDetail::jsonTypeName;
using KisTouchSmokeScriptRunnerDetail::loadJsonObject;
using KisTouchSmokeScriptRunnerDetail::mouseDragPathPixelAlphaNoChange;
using KisTouchSmokeScriptRunnerDetail::mouseDragPathWaitPixelAlphaRange;
using KisTouchSmokeScriptRunnerDetail::overlayVisible;
using KisTouchSmokeScriptRunnerDetail::paintDragPathPixelAlphaNoChange;
using KisTouchSmokeScriptRunnerDetail::paintDragPathWaitPixelAlphaRange;
using KisTouchSmokeScriptRunnerDetail::paintRectForTouchScript;
using KisTouchSmokeScriptRunnerDetail::performTouchDragPathViaGestureAction;
using KisTouchSmokeScriptRunnerDetail::performTouchGestureShortcut;
using KisTouchSmokeScriptRunnerDetail::resolveWidgetPos;
using KisTouchSmokeScriptRunnerDetail::scriptIdFromScenarioSpec;
using KisTouchSmokeScriptRunnerDetail::sendMultiFingerTouchTap;
using KisTouchSmokeScriptRunnerDetail::sendTouchDragPath;
using KisTouchSmokeScriptRunnerDetail::setConfigBool;
using KisTouchSmokeScriptRunnerDetail::setInputProfile;
using KisTouchSmokeScriptRunnerDetail::sleepWithEvents;
using KisTouchSmokeScriptRunnerDetail::sleepWithImageEvents;
using KisTouchSmokeScriptRunnerDetail::TouchDragPathPoints;
using KisTouchSmokeScriptRunnerDetail::toolProxyStrokePathPixelAlphaNoChange;
using KisTouchSmokeScriptRunnerDetail::toolProxyStrokePathWaitPixelAlphaRange;
using KisTouchSmokeScriptRunnerDetail::touchDragPathPixelAlphaNoChange;
using KisTouchSmokeScriptRunnerDetail::touchDragPathOverlayNoChange;
using KisTouchSmokeScriptRunnerDetail::touchDragPathWaitPixelAlphaWithFallback;
using KisTouchSmokeScriptRunnerDetail::touchDragPathWaitOverlayVisibleWithFallback;
using KisTouchSmokeScriptRunnerDetail::touchHoldOverlayNoChange;
using KisTouchSmokeScriptRunnerDetail::touchHoldWaitOverlayVisibleWithFallback;
using KisTouchSmokeScriptRunnerDetail::touchTapCheckableActionNoChange;
using KisTouchSmokeScriptRunnerDetail::touchTapCheckableActionWithFallback;
using KisTouchSmokeScriptRunnerDetail::touchTapLayerCountNoChange;
using KisTouchSmokeScriptRunnerDetail::touchTapLayerCountWithFallback;
using KisTouchSmokeScriptRunnerDetail::touchShortcutFromString;
using KisTouchSmokeScriptRunnerDetail::touchPinchRotateWaitCanvasTransform;
using KisTouchSmokeScriptRunnerDetail::waitForLayerCountDelta;
using KisTouchSmokeScriptRunnerDetail::waitForImageCondition;
using KisTouchSmokeScriptRunnerDetail::waitForPixelAlphaInRange;
using KisTouchSmokeScriptRunnerDetail::waitForUiCondition;
using KisTouchSmokeScriptRunnerDetail::waitOverlayVisible;

struct ActionTriggerCounter
{
    explicit ActionTriggerCounter(KisMainWindow *mainWindow)
        : m_mainWindow(mainWindow)
    {
    }

    ~ActionTriggerCounter()
    {
        for (const QMetaObject::Connection &c : m_connections) {
            QObject::disconnect(c);
        }
    }

    bool reset(const QString &actionId, QJsonObject *details, QString *errorOut)
    {
        QStringList foundIn;
        const QVector<QAction *> actions = findActions(actionId, &foundIn, errorOut);
        if (actions.isEmpty()) {
            return false;
        }
        ensureHook(actionId, actions);
        m_counts[actionId] = 0;
        if (details) {
            QJsonArray foundInArray;
            for (const QString &s : foundIn) {
                foundInArray.append(s);
            }
            details->insert(QStringLiteral("action_id"), actionId);
            details->insert(QStringLiteral("found_in"), foundInArray);
            details->insert(QStringLiteral("actions_found"), actions.size());
            details->insert(QStringLiteral("count"), 0);
        }
        return true;
    }

    bool waitDelta(const QString &actionId, int delta, int timeoutMs, QJsonObject *details, QString *errorOut)
    {
        QStringList foundIn;
        const QVector<QAction *> actions = findActions(actionId, &foundIn, errorOut);
        if (actions.isEmpty()) {
            return false;
        }
        ensureHook(actionId, actions);

        const int before = m_counts.value(actionId, 0);
        const int expectedMin = qMax(0, delta);

        const bool ok = waitForUiCondition(timeoutMs, [&]() {
            return m_counts.value(actionId, 0) >= expectedMin;
        });

        if (details) {
            QJsonArray foundInArray;
            for (const QString &s : foundIn) {
                foundInArray.append(s);
            }
            details->insert(QStringLiteral("action_id"), actionId);
            details->insert(QStringLiteral("found_in"), foundInArray);
            details->insert(QStringLiteral("actions_found"), actions.size());
            details->insert(QStringLiteral("delta"), delta);
            details->insert(QStringLiteral("expected_min_count"), expectedMin);
            details->insert(QStringLiteral("timeout_ms"), timeoutMs);
            details->insert(QStringLiteral("before_count"), before);
            details->insert(QStringLiteral("after_count"), m_counts.value(actionId, 0));
        }

        if (!ok && errorOut) {
            *errorOut = QStringLiteral("Action did not trigger enough times");
        }

        return ok;
    }

    bool waitNoChange(const QString &actionId, int settleMs, QJsonObject *details, QString *errorOut)
    {
        QStringList foundIn;
        const QVector<QAction *> actions = findActions(actionId, &foundIn, errorOut);
        if (actions.isEmpty()) {
            return false;
        }
        ensureHook(actionId, actions);

        const int before = m_counts.value(actionId, 0);
        sleepWithEvents(settleMs);
        const int after = m_counts.value(actionId, 0);
        const bool ok = before == after;

        if (details) {
            QJsonArray foundInArray;
            for (const QString &s : foundIn) {
                foundInArray.append(s);
            }
            details->insert(QStringLiteral("action_id"), actionId);
            details->insert(QStringLiteral("found_in"), foundInArray);
            details->insert(QStringLiteral("actions_found"), actions.size());
            details->insert(QStringLiteral("settle_ms"), settleMs);
            details->insert(QStringLiteral("before_count"), before);
            details->insert(QStringLiteral("after_count"), after);
        }

        if (!ok && errorOut) {
            *errorOut = QStringLiteral("Action triggered unexpectedly");
        }

        return ok;
    }

private:
    QVector<QAction *> findActions(const QString &actionId, QStringList *foundInOut, QString *errorOut) const
    {
        QVector<QAction *> actions;

        if (!m_mainWindow) {
            if (errorOut) {
                *errorOut = QStringLiteral("Missing main window");
            }
            return actions;
        }

        if (KisKActionCollection *actionCollection = m_mainWindow->actionCollection()) {
            if (QAction *action = actionCollection->action(actionId)) {
                if (foundInOut) {
                    foundInOut->append(QStringLiteral("main_window"));
                }
                actions.append(action);
            }
        }

        if (KisViewManager *viewManager = m_mainWindow->viewManager()) {
            if (KisKActionCollection *actionCollection = viewManager->actionCollection()) {
                if (QAction *action = actionCollection->action(actionId)) {
                    if (foundInOut) {
                        foundInOut->append(QStringLiteral("view_manager"));
                    }
                    if (!actions.contains(action)) {
                        actions.append(action);
                    }
                }
            }
        }

        if (actions.isEmpty() && errorOut) {
            *errorOut = QStringLiteral("Action not found: %1").arg(actionId);
        }
        return actions;
    }

    void ensureHook(const QString &actionId, const QVector<QAction *> &actions)
    {
        if (!m_counts.contains(actionId)) {
            m_counts.insert(actionId, 0);
        }

        QSet<QAction *> &hooked = m_hookedActions[actionId];
        for (QAction *action : actions) {
            if (!action || hooked.contains(action)) {
                continue;
            }

            hooked.insert(action);
            m_connections.append(QObject::connect(action, &QAction::triggered, action, [this, actionId](bool) {
                m_counts[actionId] = m_counts.value(actionId, 0) + 1;
            }));
        }
    }

    KisMainWindow *m_mainWindow {nullptr};
    QHash<QString, int> m_counts;
    QHash<QString, QSet<QAction *>> m_hookedActions;
    QVector<QMetaObject::Connection> m_connections;
};

} // namespace

bool KisTouchSmokeScriptRunner::isScriptScenarioSpec(const QString &scenarioSpec)
{
    const QString trimmed = scenarioSpec.trimmed();
    if (trimmed.startsWith(QLatin1Char('@'))) {
        return true;
    }

    const QString lowered = trimmed.toLower();
    return lowered.startsWith(QStringLiteral("script:")) || lowered.startsWith(QStringLiteral("script="));
}

bool KisTouchSmokeScriptRunner::loadScriptFromScenarioSpec(const QString &scenarioSpec, QJsonObject *scriptOut, QString *errorOut)
{
    const QString trimmed = scenarioSpec.trimmed();

    if (trimmed.startsWith(QLatin1Char('@'))) {
        const QString filePath = trimmed.mid(1).trimmed();
        return loadJsonObject(filePath, scriptOut, errorOut);
    }

    const QString scriptId = scriptIdFromScenarioSpec(trimmed);
    if (scriptId.isEmpty()) {
        if (errorOut) {
            *errorOut = QStringLiteral("Not a script scenario spec: %1").arg(trimmed);
        }
        return false;
    }

    const QString idTrimmed = scriptId.trimmed();
    QString fileName = idTrimmed;
    if (!fileName.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
        fileName.append(QStringLiteral(".json"));
    }

    const QStringList candidates = {
        idTrimmed.startsWith(QStringLiteral(":/")) ? idTrimmed : QString(),
        QStringLiteral(":/touchsmoke/scripts/%1").arg(fileName),
        QFileInfo(idTrimmed).isAbsolute() ? idTrimmed : QDir::current().absoluteFilePath(idTrimmed),
    };

    QString lastError;
    for (const QString &candidate : candidates) {
        if (candidate.isEmpty()) {
            continue;
        }
        QString err;
        if (loadJsonObject(candidate, scriptOut, &err)) {
            return true;
        }
        lastError = err;
    }

    if (errorOut) {
        *errorOut = lastError.isEmpty()
            ? QStringLiteral("Could not load script: %1").arg(trimmed)
            : lastError;
    }
    return false;
}

bool KisTouchSmokeScriptRunner::runScript(const QJsonObject &script,
                                         KisMainWindow *mainWindow,
                                         const ReportStepFn &reportStep,
                                         QString *errorOut)
{
    if (!mainWindow) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing main window");
        }
        return false;
    }

    const bool failFast = script.value(QStringLiteral("fail_fast")).toBool(false);

    const QJsonValue stepsValue = script.value(QStringLiteral("steps"));
    if (!stepsValue.isArray()) {
        if (errorOut) {
            *errorOut = QStringLiteral("touch script: expected 'steps' array, got %1").arg(jsonTypeName(stepsValue));
        }
        return false;
    }

    bool allOk = true;

    // Ensure deterministic touch shortcut mapping for smoke.
    {
        QJsonObject details;
        details.insert(QStringLiteral("profile"), QStringLiteral("Touch Gestures Only"));
        QString profileError;
        const bool profileOk = setInputProfile(QStringLiteral("Touch Gestures Only"), &profileError);
        if (!profileError.isEmpty()) {
            details.insert(QStringLiteral("error"), profileError);
        }
        reportStep(QStringLiteral("touch_script.set_input_profile"), profileOk, details);
        if (!profileOk) {
            allOk = false;
            if (failFast) {
                if (errorOut) {
                    *errorOut = profileError;
                }
                return false;
            }
        }
    }

    KisConfig cfg(true);
    ActionTriggerCounter actionTriggerCounter(mainWindow);

    const QJsonArray steps = stepsValue.toArray();
    for (int i = 0; i < steps.size(); ++i) {
        const QJsonValue stepValue = steps.at(i);
        const QJsonObject step = stepValue.toObject();
        const QString stepName = step.value(QStringLiteral("name")).toString(QStringLiteral("step_%1").arg(i));
        const QString op = step.value(QStringLiteral("op")).toString().trimmed();

        bool ok = false;
        QJsonObject details;
        QString localError;

        if (!stepValue.isObject()) {
            localError = QStringLiteral("Step %1: expected object, got %2").arg(i).arg(jsonTypeName(stepValue));
        } else if (op == QStringLiteral("wait")) {
            const int ms = step.value(QStringLiteral("ms")).toInt(0);
            details.insert(QStringLiteral("ms"), ms);
            ok = sleepWithEvents(ms);
        } else if (op == QStringLiteral("input_profile.set")) {
            const QString profile = step.value(QStringLiteral("profile")).toString();
            details.insert(QStringLiteral("profile"), profile);
            ok = setInputProfile(profile, &localError);
        } else if (op == QStringLiteral("config.set_bool")) {
            const QString key = step.value(QStringLiteral("key")).toString();
            const QJsonValue v = step.value(QStringLiteral("value"));
            if (!v.isBool()) {
                localError = QStringLiteral("config.set_bool: expected boolean 'value', got %1").arg(jsonTypeName(v));
            } else {
                ok = setConfigBool(cfg, key, v.toBool(), &details, &localError);
            }
        } else if (op == QStringLiteral("canvas.wait_eraser_mode")) {
            const bool expected = step.value(QStringLiteral("expected")).toBool(false);
            const int timeoutMs = step.value(QStringLiteral("timeout_ms")).toInt(900);
            ok = waitCanvasEraserMode(mainWindow, expected, timeoutMs, &details, &localError);
        } else if (op == QStringLiteral("canvas.wait_effective_composite_op")) {
            const QString expectedId = step.value(QStringLiteral("expected")).toString();
            const bool negate = step.value(QStringLiteral("not")).toBool(false);
            const int timeoutMs = step.value(QStringLiteral("timeout_ms")).toInt(900);
            ok = waitCanvasEffectiveCompositeOp(mainWindow, expectedId, negate, timeoutMs, &details, &localError);
        } else if (op == QStringLiteral("ui.overlay.hide")) {
            const QString objectName = step.value(QStringLiteral("object_name")).toString();
            ok = hideOverlay(mainWindow, objectName, &details);
        } else if (op == QStringLiteral("ui.wait_overlay_visible")) {
            const QString objectName = step.value(QStringLiteral("object_name")).toString();
            const bool expected = step.value(QStringLiteral("expected")).toBool(true);
            const int timeoutMs = step.value(QStringLiteral("timeout_ms")).toInt(900);
            ok = waitOverlayVisible(mainWindow, objectName, expected, timeoutMs, &details, &localError);
        } else if (op == QStringLiteral("action.trigger")) {
            const QString actionId = step.value(QStringLiteral("id")).toString();
            details.insert(QStringLiteral("action_id"), actionId);
            if (mainWindow->actionCollection()) {
                if (QAction *action = mainWindow->actionCollection()->action(actionId)) {
                    action->trigger();
                    QApplication::processEvents();
                    ok = true;
                } else {
                    localError = QStringLiteral("Action not found: %1").arg(actionId);
                }
            } else {
                localError = QStringLiteral("Missing action collection");
            }
        } else if (op == QStringLiteral("action.ensure_checked")) {
            const QString actionId = step.value(QStringLiteral("id")).toString();
            const bool expected = step.value(QStringLiteral("expected")).toBool(false);
            const int timeoutMs = step.value(QStringLiteral("timeout_ms")).toInt(500);
            ok = actionEnsureChecked(mainWindow, actionId, expected, timeoutMs, &details, &localError);
        } else if (op == QStringLiteral("action.wait_checked")) {
            const QString actionId = step.value(QStringLiteral("id")).toString();
            const bool expected = step.value(QStringLiteral("expected")).toBool(false);
            const int timeoutMs = step.value(QStringLiteral("timeout_ms")).toInt(500);
            ok = actionWaitChecked(mainWindow, actionId, expected, timeoutMs, &details, &localError);
        } else if (op == QStringLiteral("action.reset_trigger_count")) {
            const QString actionId = step.value(QStringLiteral("id")).toString();
            ok = actionTriggerCounter.reset(actionId, &details, &localError);
        } else if (op == QStringLiteral("action.wait_trigger_count_delta")) {
            const QString actionId = step.value(QStringLiteral("id")).toString();
            const int delta = step.value(QStringLiteral("delta")).toInt(1);
            const int timeoutMs = step.value(QStringLiteral("timeout_ms")).toInt(900);
            ok = actionTriggerCounter.waitDelta(actionId, delta, timeoutMs, &details, &localError);
        } else if (op == QStringLiteral("action.wait_trigger_count_no_change")) {
            const QString actionId = step.value(QStringLiteral("id")).toString();
            const int settleMs = step.value(QStringLiteral("settle_ms")).toInt(250);
            ok = actionTriggerCounter.waitNoChange(actionId, settleMs, &details, &localError);
        } else if (op == QStringLiteral("tool.assert_mask_synthetic_events")) {
            const bool expected = step.value(QStringLiteral("expected")).toBool(false);
            const int timeoutMs = step.value(QStringLiteral("timeout_ms")).toInt(500);
            ok = assertActiveToolMaskSyntheticEvents(mainWindow, expected, timeoutMs, &details, &localError);
        } else if (op == QStringLiteral("touch.tap_checkable_action_with_fallback")) {
            const int fingers = step.value(QStringLiteral("fingers")).toInt(1);
            const QJsonObject posObj = step.value(QStringLiteral("pos")).toObject();
            const QString actionId = step.value(QStringLiteral("action_id")).toString();
            const bool expected = step.value(QStringLiteral("expected_checked")).toBool(false);
            const int timeoutMs = step.value(QStringLiteral("timeout_ms")).toInt(900);
            const QString shortcutName = step.value(QStringLiteral("fallback_shortcut")).toString();
            const int shortcut = touchShortcutFromString(shortcutName);
            details.insert(QStringLiteral("fingers"), fingers);
            details.insert(QStringLiteral("fallback_shortcut"), shortcutName);
            ok = touchTapCheckableActionWithFallback(mainWindow, fingers, posObj, actionId, expected, timeoutMs, shortcut, &details, &localError);
        } else if (op == QStringLiteral("touch.tap_checkable_action_no_change")) {
            const int fingers = step.value(QStringLiteral("fingers")).toInt(1);
            const QJsonObject posObj = step.value(QStringLiteral("pos")).toObject();
            const QString actionId = step.value(QStringLiteral("action_id")).toString();
            const bool expected = step.value(QStringLiteral("expected_checked")).toBool(false);
            const int settleMs = step.value(QStringLiteral("settle_ms")).toInt(200);
            const QString shortcutName = step.value(QStringLiteral("direct_shortcut")).toString();
            const int shortcut = touchShortcutFromString(shortcutName);
            details.insert(QStringLiteral("fingers"), fingers);
            details.insert(QStringLiteral("direct_shortcut"), shortcutName);
            ok = touchTapCheckableActionNoChange(mainWindow, fingers, posObj, actionId, expected, settleMs, shortcut, &details, &localError);
        } else if (op == QStringLiteral("action.trigger_wait_layer_count_delta")) {
            const QString actionId = step.value(QStringLiteral("id")).toString();
            const int delta = step.value(QStringLiteral("delta")).toInt(0);
            const int timeoutMs = step.value(QStringLiteral("timeout_ms")).toInt(1500);
            ok = actionTriggerWaitLayerCountDelta(mainWindow, actionId, delta, timeoutMs, &details, &localError);
        } else if (op == QStringLiteral("image.wait_layer_count_delta")) {
            const int delta = step.value(QStringLiteral("delta")).toInt(0);
            const int timeoutMs = step.value(QStringLiteral("timeout_ms")).toInt(1500);
            KisImageWSP image = mainWindow->viewManager() ? mainWindow->viewManager()->image() : KisImageWSP();
            if (!image) {
                localError = QStringLiteral("Missing image");
            } else {
                ok = waitForLayerCountDelta(image, delta, timeoutMs, &details, &localError);
            }
        } else if (op == QStringLiteral("image.paint_rect")) {
            const QJsonObject rectObj = step.value(QStringLiteral("rect")).toObject();
            const QJsonObject colorObj = step.value(QStringLiteral("color")).toObject();
            const qreal strokePx = step.value(QStringLiteral("stroke_px")).toDouble(48.0);

            QColor color(0, 0, 0, 255);
            if (!colorObj.isEmpty()) {
                const int r = colorObj.value(QStringLiteral("r")).toInt(0);
                const int g = colorObj.value(QStringLiteral("g")).toInt(0);
                const int b = colorObj.value(QStringLiteral("b")).toInt(0);
                const int a = colorObj.value(QStringLiteral("a")).toInt(255);
                color = QColor(qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255), qBound(0, a, 255));
            }

            ok = paintRectForTouchScript(mainWindow, rectObj, color, strokePx, &details, &localError);
        } else if (op == QStringLiteral("image.wait_pixel_alpha_range")) {
            const QJsonObject posObj = step.value(QStringLiteral("pos")).toObject();
            const int minAlpha = step.value(QStringLiteral("min_alpha")).toInt(0);
            const int maxAlpha = step.value(QStringLiteral("max_alpha")).toInt(255);
            const int timeoutMs = step.value(QStringLiteral("timeout_ms")).toInt(900);
            ok = waitForPixelAlphaInRange(mainWindow, posObj, minAlpha, maxAlpha, timeoutMs, &details, &localError);
        } else if (op == QStringLiteral("mouse.drag_path_wait_pixel_alpha_range")) {
            const QJsonArray path = step.value(QStringLiteral("path")).toArray();
            const QString mouseSource = step.value(QStringLiteral("mouse_source")).toString();
            const QJsonObject samplePos = step.value(QStringLiteral("sample_pos")).toObject();
            const int minAlpha = step.value(QStringLiteral("min_alpha")).toInt(0);
            const int maxAlpha = step.value(QStringLiteral("max_alpha")).toInt(255);
            const int timeoutMs = step.value(QStringLiteral("timeout_ms")).toInt(900);
            const int stepMs = step.value(QStringLiteral("step_ms")).toInt(20);
            const int holdMsAtEnd = step.value(QStringLiteral("hold_ms_at_end")).toInt(0);

            ok = mouseDragPathWaitPixelAlphaRange(mainWindow, path, mouseSource, samplePos, minAlpha, maxAlpha, timeoutMs, stepMs, holdMsAtEnd, &details, &localError);
        } else if (op == QStringLiteral("mouse.drag_path_pixel_alpha_no_change")) {
            const QJsonArray path = step.value(QStringLiteral("path")).toArray();
            const QString mouseSource = step.value(QStringLiteral("mouse_source")).toString();
            const QJsonObject samplePos = step.value(QStringLiteral("sample_pos")).toObject();
            const int minAlpha = step.value(QStringLiteral("min_alpha")).toInt(0);
            const int maxAlpha = step.value(QStringLiteral("max_alpha")).toInt(255);
            const int settleMs = step.value(QStringLiteral("settle_ms")).toInt(250);
            const int stepMs = step.value(QStringLiteral("step_ms")).toInt(20);
            const int holdMsAtEnd = step.value(QStringLiteral("hold_ms_at_end")).toInt(0);

            ok = mouseDragPathPixelAlphaNoChange(mainWindow, path, mouseSource, samplePos, minAlpha, maxAlpha, settleMs, stepMs, holdMsAtEnd, &details, &localError);
        } else if (op == QStringLiteral("tool.stroke_path_wait_pixel_alpha_range")) {
            const QJsonArray path = step.value(QStringLiteral("path")).toArray();
            const QString mouseSource = step.value(QStringLiteral("mouse_source")).toString();
            const QJsonObject samplePos = step.value(QStringLiteral("sample_pos")).toObject();
            const int minAlpha = step.value(QStringLiteral("min_alpha")).toInt(0);
            const int maxAlpha = step.value(QStringLiteral("max_alpha")).toInt(255);
            const int timeoutMs = step.value(QStringLiteral("timeout_ms")).toInt(900);
            const int stepMs = step.value(QStringLiteral("step_ms")).toInt(20);
            const int holdMsAtEnd = step.value(QStringLiteral("hold_ms_at_end")).toInt(0);
            const QJsonObject androidFallbackRect = step.value(QStringLiteral("android_fallback_paint_rect")).toObject();
            const QJsonObject androidFallbackColorObj = step.value(QStringLiteral("android_fallback_color")).toObject();
            const qreal androidFallbackStrokePx = step.value(QStringLiteral("android_fallback_stroke_px")).toDouble(48.0);
            const bool allowNoPaintOnAndroid = step.value(QStringLiteral("allow_no_paint_on_android")).toBool(false);

            QColor androidFallbackColor(0, 0, 0, 255);
            if (!androidFallbackColorObj.isEmpty()) {
                const int r = androidFallbackColorObj.value(QStringLiteral("r")).toInt(0);
                const int g = androidFallbackColorObj.value(QStringLiteral("g")).toInt(0);
                const int b = androidFallbackColorObj.value(QStringLiteral("b")).toInt(0);
                const int a = androidFallbackColorObj.value(QStringLiteral("a")).toInt(255);
                androidFallbackColor = QColor(qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255), qBound(0, a, 255));
            }

            ok = toolProxyStrokePathWaitPixelAlphaRange(mainWindow,
                                                       path,
                                                       mouseSource,
                                                       samplePos,
                                                       minAlpha,
                                                       maxAlpha,
                                                       timeoutMs,
                                                       stepMs,
                                                       holdMsAtEnd,
                                                       androidFallbackRect,
                                                       androidFallbackColor,
                                                       androidFallbackStrokePx,
                                                       allowNoPaintOnAndroid,
                                                       &details,
                                                       &localError);
        } else if (op == QStringLiteral("tool.stroke_path_pixel_alpha_no_change")) {
            const QJsonArray path = step.value(QStringLiteral("path")).toArray();
            const QString mouseSource = step.value(QStringLiteral("mouse_source")).toString();
            const QJsonObject samplePos = step.value(QStringLiteral("sample_pos")).toObject();
            const int minAlpha = step.value(QStringLiteral("min_alpha")).toInt(0);
            const int maxAlpha = step.value(QStringLiteral("max_alpha")).toInt(255);
            const int settleMs = step.value(QStringLiteral("settle_ms")).toInt(250);
            const int stepMs = step.value(QStringLiteral("step_ms")).toInt(20);
            const int holdMsAtEnd = step.value(QStringLiteral("hold_ms_at_end")).toInt(0);

            ok = toolProxyStrokePathPixelAlphaNoChange(mainWindow,
                                                       path,
                                                       mouseSource,
                                                       samplePos,
                                                       minAlpha,
                                                       maxAlpha,
                                                       settleMs,
                                                       stepMs,
                                                       holdMsAtEnd,
                                                       &details,
                                                       &localError);
        } else if (op == QStringLiteral("paint.drag_path_wait_pixel_alpha_range")) {
            const QJsonArray path = step.value(QStringLiteral("path")).toArray();
            const QString mouseSource = step.value(QStringLiteral("mouse_source")).toString();
            const QJsonObject samplePos = step.value(QStringLiteral("sample_pos")).toObject();
            const int minAlpha = step.value(QStringLiteral("min_alpha")).toInt(0);
            const int maxAlpha = step.value(QStringLiteral("max_alpha")).toInt(255);
            const int timeoutMs = step.value(QStringLiteral("timeout_ms")).toInt(900);
            const int stepMs = step.value(QStringLiteral("step_ms")).toInt(20);
            const int holdMsAtEnd = step.value(QStringLiteral("hold_ms_at_end")).toInt(0);
            const bool allowNoPaintOnAndroid = step.value(QStringLiteral("allow_no_paint_on_android")).toBool(false);

            ok = paintDragPathWaitPixelAlphaRange(mainWindow,
                                                  path,
                                                  mouseSource,
                                                  samplePos,
                                                  minAlpha,
                                                  maxAlpha,
                                                  timeoutMs,
                                                  stepMs,
                                                  holdMsAtEnd,
                                                  allowNoPaintOnAndroid,
                                                  &details,
                                                  &localError);
        } else if (op == QStringLiteral("paint.drag_path_pixel_alpha_no_change")) {
            const QJsonArray path = step.value(QStringLiteral("path")).toArray();
            const QString mouseSource = step.value(QStringLiteral("mouse_source")).toString();
            const QJsonObject samplePos = step.value(QStringLiteral("sample_pos")).toObject();
            const int minAlpha = step.value(QStringLiteral("min_alpha")).toInt(0);
            const int maxAlpha = step.value(QStringLiteral("max_alpha")).toInt(255);
            const int settleMs = step.value(QStringLiteral("settle_ms")).toInt(250);
            const int stepMs = step.value(QStringLiteral("step_ms")).toInt(20);
            const int holdMsAtEnd = step.value(QStringLiteral("hold_ms_at_end")).toInt(0);

            ok = paintDragPathPixelAlphaNoChange(mainWindow, path, mouseSource, samplePos, minAlpha, maxAlpha, settleMs, stepMs, holdMsAtEnd, &details, &localError);
        } else if (op == QStringLiteral("touch.tap_layer_count_with_fallback")) {
            const int fingers = step.value(QStringLiteral("fingers")).toInt(1);
            const QJsonObject posObj = step.value(QStringLiteral("pos")).toObject();
            const int delta = step.value(QStringLiteral("layer_count_delta")).toInt(0);
            const int timeoutMs = step.value(QStringLiteral("timeout_ms")).toInt(1500);
            const QString shortcutName = step.value(QStringLiteral("fallback_shortcut")).toString();
            const int shortcut = touchShortcutFromString(shortcutName);
            const bool requireInputManager = step.value(QStringLiteral("require_input_manager")).toBool(false);
            details.insert(QStringLiteral("fingers"), fingers);
            details.insert(QStringLiteral("fallback_shortcut"), shortcutName);
            ok = touchTapLayerCountWithFallback(mainWindow,
                                                fingers,
                                                posObj,
                                                delta,
                                                timeoutMs,
                                                shortcut,
                                                requireInputManager,
                                                &details,
                                                &localError);
        } else if (op == QStringLiteral("touch.tap_layer_count_no_change")) {
            const int fingers = step.value(QStringLiteral("fingers")).toInt(1);
            const QJsonObject posObj = step.value(QStringLiteral("pos")).toObject();
            const int settleMs = step.value(QStringLiteral("settle_ms")).toInt(250);
            const QString shortcutName = step.value(QStringLiteral("direct_shortcut")).toString();
            const int shortcut = touchShortcutFromString(shortcutName);
            details.insert(QStringLiteral("fingers"), fingers);
            details.insert(QStringLiteral("direct_shortcut"), shortcutName);
            ok = touchTapLayerCountNoChange(mainWindow, fingers, posObj, settleMs, shortcut, &details, &localError);
        } else if (op == QStringLiteral("touch.drag_path_wait_overlay_visible_with_fallback")) {
            const int fingers = step.value(QStringLiteral("fingers")).toInt(3);
            const QJsonArray path = step.value(QStringLiteral("path")).toArray();
            const QString overlayName = step.value(QStringLiteral("overlay_object_name")).toString();
            const int timeoutMs = step.value(QStringLiteral("timeout_ms")).toInt(900);
            const int stepMs = step.value(QStringLiteral("step_ms")).toInt(20);
            const bool requireInputManager = step.value(QStringLiteral("require_input_manager")).toBool(false);
            const QString shortcutName = step.value(QStringLiteral("fallback_shortcut")).toString();
            const int shortcut = touchShortcutFromString(shortcutName);

            details.insert(QStringLiteral("fingers"), fingers);
            details.insert(QStringLiteral("fallback_shortcut"), shortcutName);
            ok = touchDragPathWaitOverlayVisibleWithFallback(mainWindow,
                                                            fingers,
                                                            path,
                                                            overlayName,
                                                            timeoutMs,
                                                            shortcut,
                                                            stepMs,
                                                            requireInputManager,
                                                            &details,
                                                            &localError);
        } else if (op == QStringLiteral("touch.hold_wait_overlay_visible_with_fallback")) {
            const int fingers = step.value(QStringLiteral("fingers")).toInt(1);
            const QJsonObject posObj = step.value(QStringLiteral("pos")).toObject();
            const QString overlayName = step.value(QStringLiteral("overlay_object_name")).toString();
            const int timeoutMs = step.value(QStringLiteral("timeout_ms")).toInt(900);
            const bool requireInputManager = step.value(QStringLiteral("require_input_manager")).toBool(false);
            const QString fallbackAction = step.value(QStringLiteral("fallback_action")).toString();

            details.insert(QStringLiteral("fingers"), fingers);
            details.insert(QStringLiteral("fallback_action"), fallbackAction);
            ok = touchHoldWaitOverlayVisibleWithFallback(mainWindow,
                                                        fingers,
                                                        posObj,
                                                        overlayName,
                                                        timeoutMs,
                                                        fallbackAction,
                                                        requireInputManager,
                                                        &details,
                                                        &localError);
        } else if (op == QStringLiteral("touch.drag_path_overlay_no_change")) {
            const int fingers = step.value(QStringLiteral("fingers")).toInt(3);
            const QJsonArray path = step.value(QStringLiteral("path")).toArray();
            const QString overlayName = step.value(QStringLiteral("overlay_object_name")).toString();
            const int settleMs = step.value(QStringLiteral("settle_ms")).toInt(250);
            const int stepMs = step.value(QStringLiteral("step_ms")).toInt(20);
            const QString shortcutName = step.value(QStringLiteral("direct_shortcut")).toString();
            const int shortcut = touchShortcutFromString(shortcutName);

            details.insert(QStringLiteral("fingers"), fingers);
            details.insert(QStringLiteral("direct_shortcut"), shortcutName);
            ok = touchDragPathOverlayNoChange(mainWindow, fingers, path, overlayName, settleMs, shortcut, stepMs, &details, &localError);
        } else if (op == QStringLiteral("touch.hold_overlay_no_change")) {
            const int fingers = step.value(QStringLiteral("fingers")).toInt(1);
            const QJsonObject posObj = step.value(QStringLiteral("pos")).toObject();
            const QString overlayName = step.value(QStringLiteral("overlay_object_name")).toString();
            const int settleMs = step.value(QStringLiteral("settle_ms")).toInt(250);
            const QString directAction = step.value(QStringLiteral("direct_action")).toString();

            details.insert(QStringLiteral("fingers"), fingers);
            details.insert(QStringLiteral("direct_action"), directAction);
            ok = touchHoldOverlayNoChange(mainWindow, fingers, posObj, overlayName, settleMs, directAction, &details, &localError);
        } else if (op == QStringLiteral("touch.drag_path_wait_pixel_alpha_with_fallback")) {
            const int fingers = step.value(QStringLiteral("fingers")).toInt(3);
            const QJsonArray path = step.value(QStringLiteral("path")).toArray();
            const QJsonObject samplePos = step.value(QStringLiteral("sample_pos")).toObject();
            const int minAlpha = step.value(QStringLiteral("min_alpha")).toInt(0);
            const int maxAlpha = step.value(QStringLiteral("max_alpha")).toInt(255);
            const int timeoutMs = step.value(QStringLiteral("timeout_ms")).toInt(1500);
            const int stepMs = step.value(QStringLiteral("step_ms")).toInt(20);
            const int holdMsAtEnd = step.value(QStringLiteral("hold_ms_at_end")).toInt(0);
            const bool requireInputManager = step.value(QStringLiteral("require_input_manager")).toBool(false);
            const QString shortcutName = step.value(QStringLiteral("fallback_shortcut")).toString();
            const int shortcut = touchShortcutFromString(shortcutName);

            details.insert(QStringLiteral("fingers"), fingers);
            details.insert(QStringLiteral("fallback_shortcut"), shortcutName);
            ok = touchDragPathWaitPixelAlphaWithFallback(mainWindow,
                                                        fingers,
                                                        path,
                                                        samplePos,
                                                        minAlpha,
                                                        maxAlpha,
                                                        timeoutMs,
                                                        shortcut,
                                                        stepMs,
                                                        holdMsAtEnd,
                                                        requireInputManager,
                                                        &details,
                                                        &localError);
        } else if (op == QStringLiteral("touch.drag_path_pixel_alpha_no_change")) {
            const int fingers = step.value(QStringLiteral("fingers")).toInt(3);
            const QJsonArray path = step.value(QStringLiteral("path")).toArray();
            const QJsonObject samplePos = step.value(QStringLiteral("sample_pos")).toObject();
            const int minAlpha = step.value(QStringLiteral("min_alpha")).toInt(0);
            const int maxAlpha = step.value(QStringLiteral("max_alpha")).toInt(255);
            const int settleMs = step.value(QStringLiteral("settle_ms")).toInt(250);
            const int stepMs = step.value(QStringLiteral("step_ms")).toInt(20);
            const QString shortcutName = step.value(QStringLiteral("direct_shortcut")).toString();
            const int shortcut = touchShortcutFromString(shortcutName);

            details.insert(QStringLiteral("fingers"), fingers);
            details.insert(QStringLiteral("direct_shortcut"), shortcutName);
            ok = touchDragPathPixelAlphaNoChange(mainWindow,
                                                 fingers,
                                                 path,
                                                 samplePos,
                                                 minAlpha,
                                                 maxAlpha,
                                                 settleMs,
                                                 shortcut,
                                                 stepMs,
                                                 &details,
                                                 &localError);
        } else if (op == QStringLiteral("touch.pinch_rotate_wait_canvas_transform")) {
            const QJsonObject centerObj = step.value(QStringLiteral("center")).toObject();
            const qreal radiusStartFrac = step.value(QStringLiteral("radius_start_frac")).toDouble(0.18);
            const qreal radiusEndFrac = step.value(QStringLiteral("radius_end_frac")).toDouble(0.22);
            const qreal rotationDeg = step.value(QStringLiteral("rotation_deg")).toDouble(20.0);
            const qreal startAngleDeg = step.value(QStringLiteral("start_angle_deg")).toDouble(0.0);
            const int steps = step.value(QStringLiteral("steps")).toInt(12);
            const int timeoutMs = step.value(QStringLiteral("timeout_ms")).toInt(1200);
            const int stepMs = step.value(QStringLiteral("step_ms")).toInt(20);
            const bool expectRotation = step.value(QStringLiteral("expect_rotation")).toBool(true);
            const qreal minZoomRatioChange = step.value(QStringLiteral("min_zoom_ratio_change")).toDouble(0.03);
            const qreal minAbsRotationDeg = step.value(QStringLiteral("min_abs_rotation_deg")).toDouble(5.0);
            const qreal maxAbsRotationDeg = step.value(QStringLiteral("max_abs_rotation_deg")).toDouble(2.0);
            const bool forceDirectAction = step.value(QStringLiteral("force_direct_action")).toBool(false);

            ok = touchPinchRotateWaitCanvasTransform(mainWindow,
                                                    centerObj,
                                                    radiusStartFrac,
                                                    radiusEndFrac,
                                                    rotationDeg,
                                                    startAngleDeg,
                                                    steps,
                                                    timeoutMs,
                                                    stepMs,
                                                    expectRotation,
                                                    minZoomRatioChange,
                                                    minAbsRotationDeg,
                                                    maxAbsRotationDeg,
                                                    forceDirectAction,
                                                    &details,
                                                    &localError);
        } else {
            localError = QStringLiteral("Unknown op: %1").arg(op);
        }

        if (!localError.isEmpty()) {
            details.insert(QStringLiteral("error"), localError);
        }

        reportStep(stepName, ok, details);

        if (!ok) {
            allOk = false;
            if (failFast) {
                break;
            }
        }
    }

    if (!allOk && errorOut && errorOut->isEmpty()) {
        *errorOut = QStringLiteral("touch script failed");
    }

    return allOk;
}

// NOTE: This file is part of the Krita touch fork (added on top of upstream).
// For maintainability we split the implementation across multiple smaller files,
// but include them here so we don't have to modify upstream build wiring.
#include "KisTouchSmokeScriptRunner_ops_touch.cpp"
#include "KisTouchSmokeScriptRunner_ops_ui.cpp"
#include "KisTouchSmokeScriptRunner_report.cpp"
#include "KisTouchSmokeScriptRunner_ops_mouse.cpp"
