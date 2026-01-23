#include "KisTouchSmokeScriptRunner.h"

#include <QApplication>
#include <QAction>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QThread>
#include <QTouchDevice>
#include <QTouchEvent>

#include <kactioncollection.h>

#include "KisMainWindow.h"
#include "KisView.h"
#include "KisViewManager.h"
#include "canvas/kis_canvas2.h"
#include "canvas/kis_coordinates_converter.h"
#include "input/KisTouchGestureAction.h"
#include "input/kis_input_profile_manager.h"
#include "kis_config.h"
#include "kis_group_layer.h"
#include "kis_image.h"

namespace {

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

template<typename ConditionFn>
bool waitForUiCondition(int timeoutMs, ConditionFn condition)
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

template<typename ConditionFn>
bool waitForImageCondition(KisImageWSP image, int timeoutMs, ConditionFn condition)
{
    constexpr int stepMs = 20;
    const int iterations = qMax(1, timeoutMs / stepMs);
    for (int i = 0; i < iterations; ++i) {
        QApplication::processEvents();
        if (image) {
            image->waitForDone();
        }
        if (condition()) {
            return true;
        }
        QThread::msleep(stepMs);
    }
    QApplication::processEvents();
    if (image) {
        image->waitForDone();
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

QTouchDevice *touchDevice()
{
    static QTouchDevice *touchDevice = nullptr;

    if (!touchDevice) {
        touchDevice = new QTouchDevice();
        touchDevice->setType(QTouchDevice::TouchScreen);
        touchDevice->setCapabilities(QTouchDevice::Position);
        touchDevice->setMaximumTouchPoints(10);
    }

    return touchDevice;
}

bool setInputProfile(const QString &profileName, QString *errorOut)
{
    KisInputProfileManager *profileManager = KisInputProfileManager::instance();
    KisInputProfile *profile = profileManager ? profileManager->profile(profileName) : nullptr;
    if (!profileManager || !profile) {
        if (errorOut) {
            *errorOut = QStringLiteral("Input profile not found: %1").arg(profileName);
        }
        return false;
    }

    profileManager->setCurrentProfile(profile);
    QApplication::processEvents();
    return true;
}

int layerCount(KisImageWSP image)
{
    KisGroupLayerSP root = image ? image->rootLayer() : KisGroupLayerSP();
    return root ? int(root->childCount()) : 0;
}

bool setConfigBool(KisConfig &cfg, const QString &key, bool value, QJsonObject *details, QString *errorOut)
{
    if (details) {
        details->insert(QStringLiteral("key"), key);
        details->insert(QStringLiteral("value"), value);
    }

    const QString normalizedKey = key.trimmed().toLower();
    if (normalizedKey == QStringLiteral("touch_mode_enabled")) {
        cfg.setTouchModeEnabled(value);
    } else if (normalizedKey == QStringLiteral("touch_quickshape_enabled")) {
        cfg.setTouchQuickShapeEnabled(value);
    } else if (normalizedKey == QStringLiteral("touch_quickmenu_enabled")) {
        cfg.setTouchQuickMenuEnabled(value);
    } else if (normalizedKey == QStringLiteral("touch_clipboard_gesture_enabled")) {
        cfg.setTouchClipboardGestureEnabled(value);
    } else if (normalizedKey == QStringLiteral("touch_undo_redo_gestures_enabled")) {
        cfg.setTouchUndoRedoGesturesEnabled(value);
    } else if (normalizedKey == QStringLiteral("touch_fullscreen_gesture_enabled")) {
        cfg.setTouchFullscreenGestureEnabled(value);
    } else if (normalizedKey == QStringLiteral("touch_rotate_with_pinch_enabled")) {
        cfg.setTouchRotateWithPinchEnabled(value);
    } else if (normalizedKey == QStringLiteral("touch_quick_pinch_to_fit_enabled")) {
        cfg.setTouchQuickPinchToFitEnabled(value);
    } else if (normalizedKey == QStringLiteral("touch_clear_layer_gesture_enabled")) {
        cfg.setTouchClearLayerGestureEnabled(value);
    } else {
        if (errorOut) {
            *errorOut = QStringLiteral("Unknown config key: %1").arg(key);
        }
        return false;
    }

    QApplication::processEvents();
    return true;
}

bool getCanvasContext(KisMainWindow *mainWindow, KisView **viewOut, QWidget **canvasWidgetOut, QString *errorOut)
{
    if (!mainWindow) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing main window");
        }
        return false;
    }

    KisView *view = mainWindow->activeView();
    QWidget *canvasWidget = (view && view->canvasBase()) ? view->canvasBase()->canvasWidget() : nullptr;

    if (!view || !canvasWidget) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing active view/canvas");
        }
        return false;
    }

    if (viewOut) {
        *viewOut = view;
    }
    if (canvasWidgetOut) {
        *canvasWidgetOut = canvasWidget;
    }
    return true;
}

bool resolveWidgetPos(KisMainWindow *mainWindow, const QJsonObject &posObj, QPointF *posOut, QString *errorOut)
{
    KisView *view = nullptr;
    QWidget *canvasWidget = nullptr;
    if (!getCanvasContext(mainWindow, &view, &canvasWidget, errorOut)) {
        return false;
    }

    if (posObj.isEmpty()) {
        if (posOut) {
            *posOut = QPointF(canvasWidget->rect().center());
        }
        return true;
    }

    const QString space = posObj.value(QStringLiteral("space")).toString(QStringLiteral("widget")).trimmed().toLower();
    const double x = posObj.value(QStringLiteral("x")).toDouble(0.5);
    const double y = posObj.value(QStringLiteral("y")).toDouble(0.5);

    if (space == QStringLiteral("widget")) {
        const QRect r = canvasWidget->rect();
        if (posOut) {
            *posOut = QPointF(r.left() + x * r.width(), r.top() + y * r.height());
        }
        return true;
    }

    if (space == QStringLiteral("image")) {
        KisImageWSP image = mainWindow->viewManager() ? mainWindow->viewManager()->image() : KisImageWSP();
        if (!image) {
            if (errorOut) {
                *errorOut = QStringLiteral("Missing image for pos.space=image");
            }
            return false;
        }
        const QRect bounds = image->bounds();
        const QPointF imgP(bounds.left() + x * bounds.width(), bounds.top() + y * bounds.height());
        if (posOut) {
            *posOut = view->canvasBase()->coordinatesConverter()->imageToWidget(imgP);
        }
        return true;
    }

    if (errorOut) {
        *errorOut = QStringLiteral("Unknown pos.space: %1").arg(space);
    }
    return false;
}

QVector<QPointF> multiFingerTapPoints(int fingerCount, const QPointF &center)
{
    if (fingerCount <= 0) {
        return {};
    }

    constexpr qreal spacingPx = 60.0;
    QVector<QPointF> points;
    points.reserve(fingerCount);

    if (fingerCount == 1) {
        points.append(center);
    } else if (fingerCount == 2) {
        points.append(center + QPointF(-spacingPx * 0.5, 0.0));
        points.append(center + QPointF(spacingPx * 0.5, 0.0));
    } else if (fingerCount == 3) {
        points.append(center + QPointF(-spacingPx, spacingPx * 0.5));
        points.append(center + QPointF(0.0, -spacingPx * 0.5));
        points.append(center + QPointF(spacingPx, spacingPx * 0.5));
    } else {
        for (int i = 0; i < fingerCount; ++i) {
            const qreal offset = (qreal(i) - qreal(fingerCount - 1) * 0.5) * spacingPx;
            points.append(center + QPointF(offset, 0.0));
        }
    }

    return points;
}

bool sendMultiFingerTouchTap(KisMainWindow *mainWindow, int fingerCount, const QJsonObject &posObj, QString *errorOut)
{
    KisView *view = nullptr;
    QWidget *canvasWidget = nullptr;
    if (!getCanvasContext(mainWindow, &view, &canvasWidget, errorOut)) {
        return false;
    }

    canvasWidget->setAttribute(Qt::WA_AcceptTouchEvents, true);

    QPointF center;
    if (!resolveWidgetPos(mainWindow, posObj, &center, errorOut)) {
        return false;
    }

    const QVector<QPointF> points = multiFingerTapPoints(fingerCount, center);
    if (points.isEmpty()) {
        if (errorOut) {
            *errorOut = QStringLiteral("touch.tap: invalid finger count: %1").arg(fingerCount);
        }
        return false;
    }

    QTouchDevice *touchDevice = ::touchDevice();
    if (!touchDevice) {
        if (errorOut) {
            *errorOut = QStringLiteral("touch.tap: could not allocate touch device");
        }
        return false;
    }

    QList<QTouchEvent::TouchPoint> beginPoints;
    QList<QTouchEvent::TouchPoint> endPoints;
    beginPoints.reserve(points.size());
    endPoints.reserve(points.size());

    for (int i = 0; i < points.size(); ++i) {
        const QPointF pos = points.at(i);
        const QPointF posGlobal = QPointF(canvasWidget->mapToGlobal(pos.toPoint()));

        QTouchEvent::TouchPoint point(i);
        point.setState(Qt::TouchPointPressed);
        point.setPos(pos);
        point.setScreenPos(posGlobal);
        point.setStartPos(pos);
        point.setStartScreenPos(posGlobal);
        point.setLastPos(pos);
        point.setLastScreenPos(posGlobal);
        beginPoints.append(point);

        point.setState(Qt::TouchPointReleased);
        endPoints.append(point);
    }

    QTouchEvent beginEvent(QEvent::TouchBegin, touchDevice, Qt::NoModifier, Qt::TouchPointPressed, beginPoints);
    QApplication::sendEvent(canvasWidget, &beginEvent);
    QApplication::processEvents();

    QTouchEvent endEvent(QEvent::TouchEnd, touchDevice, Qt::NoModifier, Qt::TouchPointReleased, endPoints);
    QApplication::sendEvent(canvasWidget, &endEvent);
    QApplication::processEvents();
    return true;
}

int touchShortcutFromString(const QString &shortcutName)
{
    const QString key = shortcutName.trimmed().toLower();
    if (key == QStringLiteral("undo") || key == QStringLiteral("undo_action_shortcut")) {
        return KisTouchGestureAction::UndoActionShortcut;
    }
    if (key == QStringLiteral("redo") || key == QStringLiteral("redo_action_shortcut")) {
        return KisTouchGestureAction::RedoActionShortcut;
    }
    if (key == QStringLiteral("toggle_canvas_only") || key == QStringLiteral("togglecanvasonly") ||
        key == QStringLiteral("toggle_canvas_only_shortcut") || key == QStringLiteral("togglecanvasonlyshortcut")) {
        return KisTouchGestureAction::ToggleCanvasOnlyShortcut;
    }
    if (key == QStringLiteral("copypaste_overlay") || key == QStringLiteral("copypasteoverlay") ||
        key == QStringLiteral("copypasteoverlay_shortcut")) {
        return KisTouchGestureAction::CopyPasteOverlay;
    }
    if (key == QStringLiteral("deselect")) {
        return KisTouchGestureAction::Deselect;
    }

    return -1;
}

bool performTouchGestureShortcut(int shortcut, QJsonObject *details, QString *errorOut)
{
    if (details) {
        details->insert(QStringLiteral("shortcut"), shortcut);
    }
    if (shortcut < 0) {
        if (errorOut) {
            *errorOut = QStringLiteral("Invalid touch gesture shortcut");
        }
        return false;
    }

    KisTouchGestureAction gesture;
    gesture.begin(shortcut, nullptr);
    gesture.end(nullptr);
    QApplication::processEvents();
    return true;
}

bool actionEnsureChecked(KisMainWindow *mainWindow, const QString &actionId, bool expectedChecked, int timeoutMs, QJsonObject *details, QString *errorOut)
{
    if (!mainWindow || !mainWindow->actionCollection()) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing action collection");
        }
        return false;
    }

    QAction *action = mainWindow->actionCollection()->action(actionId);
    if (!action) {
        if (errorOut) {
            *errorOut = QStringLiteral("Action not found: %1").arg(actionId);
        }
        return false;
    }

    if (details) {
        details->insert(QStringLiteral("action_id"), actionId);
        details->insert(QStringLiteral("expected_checked"), expectedChecked);
        details->insert(QStringLiteral("before_checked"), action->isChecked());
    }

    if (action->isChecked() == expectedChecked) {
        if (details) {
            details->insert(QStringLiteral("already_ok"), true);
        }
        return true;
    }

    action->trigger();
    QApplication::processEvents();

    const bool ok = waitForUiCondition(timeoutMs, [&]() { return action->isChecked() == expectedChecked; });

    if (details) {
        details->insert(QStringLiteral("after_checked"), action->isChecked());
        details->insert(QStringLiteral("timeout_ms"), timeoutMs);
        details->insert(QStringLiteral("triggered"), true);
    }

    return ok;
}

bool touchTapCheckableActionWithFallback(KisMainWindow *mainWindow,
                                        int fingerCount,
                                        const QJsonObject &posObj,
                                        const QString &actionId,
                                        bool expectedChecked,
                                        int timeoutMs,
                                        int fallbackShortcut,
                                        QJsonObject *details,
                                        QString *errorOut)
{
    if (!mainWindow || !mainWindow->actionCollection()) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing action collection");
        }
        return false;
    }

    QAction *action = mainWindow->actionCollection()->action(actionId);
    if (!action) {
        if (errorOut) {
            *errorOut = QStringLiteral("Action not found: %1").arg(actionId);
        }
        return false;
    }

    bool toggledViaInputManager = false;
    bool toggledViaDirectAction = false;

    QString injectError;
    sendMultiFingerTouchTap(mainWindow, fingerCount, posObj, &injectError);
    toggledViaInputManager = waitForUiCondition(timeoutMs, [&]() { return action->isChecked() == expectedChecked; });

    if (!toggledViaInputManager) {
        QJsonObject fallbackDetails;
        QString fallbackError;
        performTouchGestureShortcut(fallbackShortcut, &fallbackDetails, &fallbackError);
        toggledViaDirectAction = waitForUiCondition(timeoutMs, [&]() { return action->isChecked() == expectedChecked; });
        if (details) {
            details->insert(QStringLiteral("direct_action_details"), fallbackDetails);
            if (!fallbackError.isEmpty()) {
                details->insert(QStringLiteral("direct_action_error"), fallbackError);
            }
        }
    }

    if (details) {
        details->insert(QStringLiteral("action_id"), actionId);
        details->insert(QStringLiteral("expected_checked"), expectedChecked);
        details->insert(QStringLiteral("actual_checked"), action->isChecked());
        details->insert(QStringLiteral("input_manager_timeout_ms"), timeoutMs);
        details->insert(QStringLiteral("input_manager_toggled"), toggledViaInputManager);
        details->insert(QStringLiteral("direct_action_toggled"), toggledViaDirectAction);
        if (!injectError.isEmpty()) {
            details->insert(QStringLiteral("touch_inject_error"), injectError);
        }
    }

    return toggledViaInputManager || toggledViaDirectAction;
}

bool touchTapCheckableActionNoChange(KisMainWindow *mainWindow,
                                    int fingerCount,
                                    const QJsonObject &posObj,
                                    const QString &actionId,
                                    bool expectedChecked,
                                    int settleMs,
                                    int directShortcut,
                                    QJsonObject *details,
                                    QString *errorOut)
{
    if (!mainWindow || !mainWindow->actionCollection()) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing action collection");
        }
        return false;
    }

    QAction *action = mainWindow->actionCollection()->action(actionId);
    if (!action) {
        if (errorOut) {
            *errorOut = QStringLiteral("Action not found: %1").arg(actionId);
        }
        return false;
    }

    QString injectError;
    sendMultiFingerTouchTap(mainWindow, fingerCount, posObj, &injectError);
    {
        QJsonObject fallbackDetails;
        QString fallbackError;
        performTouchGestureShortcut(directShortcut, &fallbackDetails, &fallbackError);
        if (details) {
            details->insert(QStringLiteral("direct_action_details"), fallbackDetails);
            if (!fallbackError.isEmpty()) {
                details->insert(QStringLiteral("direct_action_error"), fallbackError);
            }
        }
    }

    sleepWithEvents(settleMs);

    const bool blocked = action->isChecked() == expectedChecked;

    if (details) {
        details->insert(QStringLiteral("action_id"), actionId);
        details->insert(QStringLiteral("expected_checked"), expectedChecked);
        details->insert(QStringLiteral("actual_checked"), action->isChecked());
        details->insert(QStringLiteral("settle_ms"), settleMs);
        details->insert(QStringLiteral("input_manager_attempted"), true);
        details->insert(QStringLiteral("direct_action_attempted"), true);
        if (!injectError.isEmpty()) {
            details->insert(QStringLiteral("touch_inject_error"), injectError);
        }
    }

    return blocked;
}

bool actionTriggerWaitLayerCountDelta(KisMainWindow *mainWindow,
                                      const QString &actionId,
                                      int delta,
                                      int timeoutMs,
                                      QJsonObject *details,
                                      QString *errorOut)
{
    if (!mainWindow || !mainWindow->actionCollection()) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing action collection");
        }
        return false;
    }

    QAction *action = mainWindow->actionCollection()->action(actionId);
    if (!action) {
        if (errorOut) {
            *errorOut = QStringLiteral("Action not found: %1").arg(actionId);
        }
        return false;
    }

    KisImageWSP image = mainWindow->viewManager() ? mainWindow->viewManager()->image() : KisImageWSP();
    if (!image) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing image");
        }
        return false;
    }

    const int before = layerCount(image);
    const int expected = before + delta;

    action->trigger();
    QApplication::processEvents();

    const bool ok = waitForImageCondition(image, timeoutMs, [&]() { return layerCount(image) == expected; });

    if (details) {
        details->insert(QStringLiteral("action_id"), actionId);
        details->insert(QStringLiteral("delta"), delta);
        details->insert(QStringLiteral("before_layer_count"), before);
        details->insert(QStringLiteral("expected_layer_count"), expected);
        details->insert(QStringLiteral("actual_layer_count"), layerCount(image));
        details->insert(QStringLiteral("timeout_ms"), timeoutMs);
    }
    if (!ok && errorOut) {
        *errorOut = QStringLiteral("Timed out waiting for layer count delta %1 (expected %2)")
                        .arg(delta)
                        .arg(expected);
    }

    return ok;
}

bool waitForLayerCountDelta(KisImageWSP image,
                            int delta,
                            int timeoutMs,
                            QJsonObject *details,
                            QString *errorOut)
{
    const int before = layerCount(image);
    const int expected = before + delta;
    const bool ok = waitForImageCondition(image, timeoutMs, [&]() { return layerCount(image) == expected; });
    if (details) {
        details->insert(QStringLiteral("delta"), delta);
        details->insert(QStringLiteral("before_layer_count"), before);
        details->insert(QStringLiteral("expected_layer_count"), expected);
        details->insert(QStringLiteral("actual_layer_count"), layerCount(image));
        details->insert(QStringLiteral("timeout_ms"), timeoutMs);
    }
    if (!ok && errorOut) {
        *errorOut = QStringLiteral("Timed out waiting for layer count delta %1 (expected %2)")
                        .arg(delta)
                        .arg(expected);
    }
    return ok;
}

bool touchTapLayerCountWithFallback(KisMainWindow *mainWindow,
                                   int fingerCount,
                                   const QJsonObject &posObj,
                                   int delta,
                                   int timeoutMs,
                                   int fallbackShortcut,
                                   bool requireInputManager,
                                   QJsonObject *details,
                                   QString *errorOut)
{
    KisImageWSP image = (mainWindow && mainWindow->viewManager()) ? mainWindow->viewManager()->image() : KisImageWSP();
    if (!image) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing image");
        }
        return false;
    }

    const int before = layerCount(image);
    const int expected = before + delta;

    bool changedViaInputManager = false;
    bool changedViaDirectAction = false;

    QString injectError;
    sendMultiFingerTouchTap(mainWindow, fingerCount, posObj, &injectError);
    changedViaInputManager = waitForImageCondition(image, timeoutMs, [&]() { return layerCount(image) == expected; });

    if (!changedViaInputManager && !requireInputManager) {
        QJsonObject fallbackDetails;
        QString fallbackError;
        performTouchGestureShortcut(fallbackShortcut, &fallbackDetails, &fallbackError);
        changedViaDirectAction = waitForImageCondition(image, timeoutMs, [&]() { return layerCount(image) == expected; });
        if (details) {
            details->insert(QStringLiteral("direct_action_details"), fallbackDetails);
            if (!fallbackError.isEmpty()) {
                details->insert(QStringLiteral("direct_action_error"), fallbackError);
            }
        }
    }

    if (details) {
        details->insert(QStringLiteral("delta"), delta);
        details->insert(QStringLiteral("before_layer_count"), before);
        details->insert(QStringLiteral("expected_layer_count"), expected);
        details->insert(QStringLiteral("actual_layer_count"), layerCount(image));
        details->insert(QStringLiteral("input_manager_timeout_ms"), timeoutMs);
        details->insert(QStringLiteral("require_input_manager"), requireInputManager);
        details->insert(QStringLiteral("input_manager_changed"), changedViaInputManager);
        details->insert(QStringLiteral("direct_action_changed"), changedViaDirectAction);
        if (!injectError.isEmpty()) {
            details->insert(QStringLiteral("touch_inject_error"), injectError);
        }
    }

    return requireInputManager ? changedViaInputManager : (changedViaInputManager || changedViaDirectAction);
}

bool touchTapLayerCountNoChange(KisMainWindow *mainWindow,
                               int fingerCount,
                               const QJsonObject &posObj,
                               int settleMs,
                               int directShortcut,
                               QJsonObject *details,
                               QString *errorOut)
{
    KisImageWSP image = (mainWindow && mainWindow->viewManager()) ? mainWindow->viewManager()->image() : KisImageWSP();
    if (!image) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing image");
        }
        return false;
    }

    const int before = layerCount(image);

    QString injectError;
    sendMultiFingerTouchTap(mainWindow, fingerCount, posObj, &injectError);
    {
        QJsonObject fallbackDetails;
        QString fallbackError;
        performTouchGestureShortcut(directShortcut, &fallbackDetails, &fallbackError);
        if (details) {
            details->insert(QStringLiteral("direct_action_details"), fallbackDetails);
            if (!fallbackError.isEmpty()) {
                details->insert(QStringLiteral("direct_action_error"), fallbackError);
            }
        }
    }

    sleepWithImageEvents(image, settleMs);

    const bool blocked = layerCount(image) == before;

    if (details) {
        details->insert(QStringLiteral("before_layer_count"), before);
        details->insert(QStringLiteral("actual_layer_count"), layerCount(image));
        details->insert(QStringLiteral("settle_ms"), settleMs);
        details->insert(QStringLiteral("input_manager_attempted"), true);
        details->insert(QStringLiteral("direct_action_attempted"), true);
        if (!injectError.isEmpty()) {
            details->insert(QStringLiteral("touch_inject_error"), injectError);
        }
    }

    return blocked;
}

bool loadJsonObject(const QString &path, QJsonObject *out, QString *errorOut)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errorOut) {
            *errorOut = QStringLiteral("Failed to open: %1").arg(path);
        }
        return false;
    }

    const QByteArray data = f.readAll();
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorOut) {
            *errorOut = QStringLiteral("Failed to parse JSON: %1 (%2 at offset %3)")
                            .arg(path, err.errorString())
                            .arg(err.offset);
        }
        return false;
    }

    if (out) {
        *out = doc.object();
    }
    return true;
}

QString scriptIdFromScenarioSpec(const QString &scenarioSpec)
{
    const QString trimmed = scenarioSpec.trimmed();
    const QString lowered = trimmed.toLower();
    if (lowered.startsWith(QStringLiteral("script:"))) {
        return trimmed.mid(QStringLiteral("script:").size()).trimmed();
    }
    if (lowered.startsWith(QStringLiteral("script="))) {
        return trimmed.mid(QStringLiteral("script=").size()).trimmed();
    }
    return QString();
}

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
