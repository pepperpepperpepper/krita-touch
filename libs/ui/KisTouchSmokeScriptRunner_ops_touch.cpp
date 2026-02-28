/*
 * This file is part of the Krita touch fork.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisTouchSmokeScriptRunner_p.h"

#include <cmath>

#include <QApplication>
#include <QThread>
#include <QTouchDevice>
#include <QTouchEvent>
#include <QWidget>
#include <QWindow>

#include "input/KisTouchGestureAction.h"
#include "input/kis_input_profile_manager.h"
#include "input/kis_input_manager.h"
#include "input/kis_zoom_and_rotate_action.h"
#include "kis_config.h"
#include "KisView.h"
#include "canvas/kis_canvas2.h"
#include "canvas/kis_canvas_controller.h"

namespace {

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

} // namespace

namespace KisTouchSmokeScriptRunnerDetail {

QTouchDevice *touchDevice()
{
    static QTouchDevice *device = nullptr;

    if (!device) {
        device = new QTouchDevice();
        device->setType(QTouchDevice::TouchScreen);
        device->setCapabilities(QTouchDevice::Position | QTouchDevice::Pressure);
        device->setMaximumTouchPoints(10);
    }

    return device;
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

bool buildTouchHoldPoints(KisMainWindow *mainWindow,
                          int fingerCount,
                          const QJsonObject &posObj,
                          QWidget **canvasWidgetOut,
                          QVector<QPointF> *localPointsOut,
                          QVector<QPointF> *globalPointsOut,
                          QString *errorOut)
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

    const QVector<QPointF> localPoints = multiFingerTapPoints(fingerCount, center);
    if (localPoints.isEmpty()) {
        if (errorOut) {
            *errorOut = QStringLiteral("touch.hold: invalid finger count: %1").arg(fingerCount);
        }
        return false;
    }

    QVector<QPointF> globalPoints;
    globalPoints.reserve(localPoints.size());
    for (const QPointF &p : localPoints) {
        globalPoints.append(QPointF(canvasWidget->mapToGlobal(p.toPoint())));
    }

    if (canvasWidgetOut) {
        *canvasWidgetOut = canvasWidget;
    }
    if (localPointsOut) {
        *localPointsOut = localPoints;
    }
    if (globalPointsOut) {
        *globalPointsOut = globalPoints;
    }
    return true;
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

    QTouchDevice *device = touchDevice();
    if (!device) {
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

    QTouchEvent beginEvent(QEvent::TouchBegin, device, Qt::NoModifier, Qt::TouchPointPressed, beginPoints);
    QApplication::sendEvent(canvasWidget, &beginEvent);
    QApplication::processEvents();

    QTouchEvent endEvent(QEvent::TouchEnd, device, Qt::NoModifier, Qt::TouchPointReleased, endPoints);
    QApplication::sendEvent(canvasWidget, &endEvent);
    QApplication::processEvents();
    return true;
}

bool buildTouchDragPathPoints(KisMainWindow *mainWindow, int fingerCount, const QJsonArray &pathArray, TouchDragPathPoints *out, QString *errorOut)
{
    if (!out) {
        if (errorOut) {
            *errorOut = QStringLiteral("Internal error: missing output");
        }
        return false;
    }

    KisView *view = nullptr;
    QWidget *canvasWidget = nullptr;
    if (!getCanvasContext(mainWindow, &view, &canvasWidget, errorOut)) {
        return false;
    }

    if (fingerCount <= 0) {
        if (errorOut) {
            *errorOut = QStringLiteral("touch.drag: invalid finger count: %1").arg(fingerCount);
        }
        return false;
    }

    const int pathLen = pathArray.size();
    if (pathLen < 2) {
        if (errorOut) {
            *errorOut = QStringLiteral("touch.drag: path must have at least 2 points");
        }
        return false;
    }

    QVector<QPointF> centers;
    centers.reserve(pathLen);
    for (int i = 0; i < pathLen; ++i) {
        const QJsonValue v = pathArray.at(i);
        if (!v.isObject()) {
            if (errorOut) {
                *errorOut = QStringLiteral("touch.drag: path[%1] must be object").arg(i);
            }
            return false;
        }
        QPointF center;
        if (!resolveWidgetPos(mainWindow, v.toObject(), &center, errorOut)) {
            return false;
        }
        centers.push_back(center);
    }

    canvasWidget->setAttribute(Qt::WA_AcceptTouchEvents, true);

    out->localPoints.clear();
    out->globalPoints.clear();
    out->localPoints.reserve(pathLen);
    out->globalPoints.reserve(pathLen);

    for (int i = 0; i < centers.size(); ++i) {
        const QVector<QPointF> points = multiFingerTapPoints(fingerCount, centers.at(i));
        if (points.size() != fingerCount) {
            if (errorOut) {
                *errorOut = QStringLiteral("touch.drag: invalid points generated");
            }
            return false;
        }
        out->localPoints.push_back(points);

        QVector<QPointF> globals;
        globals.reserve(points.size());
        for (const QPointF &p : points) {
            globals.push_back(QPointF(canvasWidget->mapToGlobal(p.toPoint())));
        }
        out->globalPoints.push_back(globals);
    }

    return true;
}

void sendTouchDragPath(QWidget *canvasWidget, const TouchDragPathPoints &pathPoints, int stepMs, int holdMsAtEnd, QJsonObject *details)
{
    if (!canvasWidget) {
        if (details) {
            details->insert(QStringLiteral("sent"), false);
        }
        return;
    }

    QTouchDevice *device = touchDevice();
    if (!device) {
        if (details) {
            details->insert(QStringLiteral("sent"), false);
            details->insert(QStringLiteral("error"), QStringLiteral("missing touch device"));
        }
        return;
    }

    const int steps = pathPoints.localPoints.size();
    if (steps < 2) {
        if (details) {
            details->insert(QStringLiteral("sent"), false);
            details->insert(QStringLiteral("error"), QStringLiteral("invalid path"));
        }
        return;
    }

    const int fingerCount = pathPoints.localPoints.first().size();

    QList<QTouchEvent::TouchPoint> beginPoints;
    beginPoints.reserve(fingerCount);
    for (int i = 0; i < fingerCount; ++i) {
        QTouchEvent::TouchPoint tp(i);
        tp.setState(Qt::TouchPointPressed);
        tp.setPos(pathPoints.localPoints.first().at(i));
        tp.setScreenPos(pathPoints.globalPoints.first().at(i));
        tp.setStartPos(pathPoints.localPoints.first().at(i));
        tp.setStartScreenPos(pathPoints.globalPoints.first().at(i));
        tp.setLastPos(pathPoints.localPoints.first().at(i));
        tp.setLastScreenPos(pathPoints.globalPoints.first().at(i));
        tp.setPressure(1.0);
        beginPoints.append(tp);
    }

    QTouchEvent beginEvent(QEvent::TouchBegin, device, Qt::NoModifier, Qt::TouchPointPressed, beginPoints);
    QApplication::sendEvent(canvasWidget, &beginEvent);
    QApplication::processEvents();

    for (int step = 1; step < steps; ++step) {
        QList<QTouchEvent::TouchPoint> updatePoints;
        updatePoints.reserve(fingerCount);
        for (int i = 0; i < fingerCount; ++i) {
            QTouchEvent::TouchPoint tp(i);
            tp.setState(Qt::TouchPointMoved);
            tp.setPos(pathPoints.localPoints.at(step).at(i));
            tp.setScreenPos(pathPoints.globalPoints.at(step).at(i));
            tp.setStartPos(pathPoints.localPoints.first().at(i));
            tp.setStartScreenPos(pathPoints.globalPoints.first().at(i));
            tp.setLastPos(pathPoints.localPoints.at(step - 1).at(i));
            tp.setLastScreenPos(pathPoints.globalPoints.at(step - 1).at(i));
            tp.setPressure(1.0);
            updatePoints.append(tp);
        }
        QTouchEvent updateEvent(QEvent::TouchUpdate, device, Qt::NoModifier, Qt::TouchPointMoved, updatePoints);
        QApplication::sendEvent(canvasWidget, &updateEvent);
        QApplication::processEvents();
        if (stepMs > 0) {
            QThread::msleep(stepMs);
        }
    }

    if (holdMsAtEnd > 0) {
        QThread::msleep(holdMsAtEnd);
        QApplication::processEvents();
    }

    QApplication::processEvents();

    QList<QTouchEvent::TouchPoint> endPoints;
    endPoints.reserve(fingerCount);
    for (int i = 0; i < fingerCount; ++i) {
        QTouchEvent::TouchPoint tp(i);
        tp.setState(Qt::TouchPointReleased);
        tp.setPos(pathPoints.localPoints.last().at(i));
        tp.setScreenPos(pathPoints.globalPoints.last().at(i));
        tp.setStartPos(pathPoints.localPoints.first().at(i));
        tp.setStartScreenPos(pathPoints.globalPoints.first().at(i));
        tp.setLastPos(pathPoints.localPoints.last().at(i));
        tp.setLastScreenPos(pathPoints.globalPoints.last().at(i));
        tp.setPressure(0.0);
        endPoints.append(tp);
    }

    QTouchEvent endEvent(QEvent::TouchEnd, device, Qt::NoModifier, Qt::TouchPointReleased, endPoints);
    QApplication::sendEvent(canvasWidget, &endEvent);
    QApplication::processEvents();

    if (details) {
        const QRect r = canvasWidget->rect();
        details->insert(QStringLiteral("sent"), true);
        details->insert(QStringLiteral("step_ms"), stepMs);
        details->insert(QStringLiteral("hold_ms_at_end"), holdMsAtEnd);
        details->insert(QStringLiteral("steps"), steps);
        details->insert(QStringLiteral("fingers"), fingerCount);
        details->insert(QStringLiteral("canvas_w"), r.width());
        details->insert(QStringLiteral("canvas_h"), r.height());
    }
}

void sendTouchDragPathToWindowHandle(QWidget *canvasWidget,
                                     const TouchDragPathPoints &pathPoints,
                                     int stepMs,
                                     int holdMsAtEnd,
                                     bool useWindowLocalScreenPos,
                                     QJsonObject *details)
{
    if (!canvasWidget) {
        if (details) {
            details->insert(QStringLiteral("sent"), false);
            details->insert(QStringLiteral("error"), QStringLiteral("missing canvas widget"));
        }
        return;
    }

    QTouchDevice *device = touchDevice();
    if (!device) {
        if (details) {
            details->insert(QStringLiteral("sent"), false);
            details->insert(QStringLiteral("error"), QStringLiteral("missing touch device"));
        }
        return;
    }

    QWidget *windowWidget = canvasWidget->window();
    if (!windowWidget) {
        if (details) {
            details->insert(QStringLiteral("sent"), false);
            details->insert(QStringLiteral("error"), QStringLiteral("missing window widget"));
        }
        return;
    }

    // Ensure the underlying QWindow handle exists.
    windowWidget->winId();
    QWindow *windowHandle = windowWidget->windowHandle();
    if (!windowHandle) {
        if (details) {
            details->insert(QStringLiteral("sent"), false);
            details->insert(QStringLiteral("error"), QStringLiteral("missing window handle"));
        }
        return;
    }

    const int steps = pathPoints.localPoints.size();
    if (steps < 2) {
        if (details) {
            details->insert(QStringLiteral("sent"), false);
            details->insert(QStringLiteral("error"), QStringLiteral("invalid path"));
        }
        return;
    }

    const int fingerCount = pathPoints.localPoints.first().size();

    auto windowPointAt = [&](int stepIndex, int fingerIndex) -> QPointF {
        const QPoint local = pathPoints.localPoints.at(stepIndex).at(fingerIndex).toPoint();
        return QPointF(canvasWidget->mapTo(windowWidget, local));
    };

    auto screenPointAt = [&](int stepIndex, int fingerIndex) -> QPointF {
        if (useWindowLocalScreenPos) {
            // Simulate a Wayland-like failure mode where QTouchEvent::screenPos is effectively
            // window-local (so mapping via mapFromGlobal() would produce incorrect local coords).
            return windowPointAt(stepIndex, fingerIndex);
        }
        return pathPoints.globalPoints.at(stepIndex).at(fingerIndex);
    };

    canvasWidget->setAttribute(Qt::WA_AcceptTouchEvents, true);

    QList<QTouchEvent::TouchPoint> beginPoints;
    beginPoints.reserve(fingerCount);
    for (int i = 0; i < fingerCount; ++i) {
        const QPointF windowPos = windowPointAt(0, i);
        const QPointF screenPos = screenPointAt(0, i);

        QTouchEvent::TouchPoint tp(i);
        tp.setState(Qt::TouchPointPressed);
        tp.setPos(windowPos);
        tp.setScenePos(windowPos);
        tp.setScreenPos(screenPos);
        tp.setStartPos(windowPos);
        tp.setStartScenePos(windowPos);
        tp.setStartScreenPos(screenPos);
        tp.setLastPos(windowPos);
        tp.setLastScenePos(windowPos);
        tp.setLastScreenPos(screenPos);
        tp.setPressure(1.0);
        beginPoints.append(tp);
    }

    QTouchEvent beginEvent(QEvent::TouchBegin, device, Qt::NoModifier, Qt::TouchPointPressed, beginPoints);
    QApplication::sendEvent(windowHandle, &beginEvent);
    QApplication::processEvents();

    for (int step = 1; step < steps; ++step) {
        QList<QTouchEvent::TouchPoint> updatePoints;
        updatePoints.reserve(fingerCount);
        for (int i = 0; i < fingerCount; ++i) {
            const QPointF windowPos = windowPointAt(step, i);
            const QPointF screenPos = screenPointAt(step, i);
            const QPointF startWindowPos = windowPointAt(0, i);
            const QPointF startScreenPos = screenPointAt(0, i);
            const QPointF lastWindowPos = windowPointAt(step - 1, i);
            const QPointF lastScreenPos = screenPointAt(step - 1, i);

            QTouchEvent::TouchPoint tp(i);
            tp.setState(Qt::TouchPointMoved);
            tp.setPos(windowPos);
            tp.setScenePos(windowPos);
            tp.setScreenPos(screenPos);
            tp.setStartPos(startWindowPos);
            tp.setStartScenePos(startWindowPos);
            tp.setStartScreenPos(startScreenPos);
            tp.setLastPos(lastWindowPos);
            tp.setLastScenePos(lastWindowPos);
            tp.setLastScreenPos(lastScreenPos);
            tp.setPressure(1.0);
            updatePoints.append(tp);
        }
        QTouchEvent updateEvent(QEvent::TouchUpdate, device, Qt::NoModifier, Qt::TouchPointMoved, updatePoints);
        QApplication::sendEvent(windowHandle, &updateEvent);
        QApplication::processEvents();
        if (stepMs > 0) {
            QThread::msleep(stepMs);
        }
    }

    if (holdMsAtEnd > 0) {
        QThread::msleep(holdMsAtEnd);
        QApplication::processEvents();
    }

    QApplication::processEvents();

    QList<QTouchEvent::TouchPoint> endPoints;
    endPoints.reserve(fingerCount);
    for (int i = 0; i < fingerCount; ++i) {
        const QPointF windowPos = windowPointAt(steps - 1, i);
        const QPointF screenPos = screenPointAt(steps - 1, i);
        const QPointF startWindowPos = windowPointAt(0, i);
        const QPointF startScreenPos = screenPointAt(0, i);
        const QPointF lastWindowPos = windowPointAt(steps - 1, i);
        const QPointF lastScreenPos = screenPointAt(steps - 1, i);

        QTouchEvent::TouchPoint tp(i);
        tp.setState(Qt::TouchPointReleased);
        tp.setPos(windowPos);
        tp.setScenePos(windowPos);
        tp.setScreenPos(screenPos);
        tp.setStartPos(startWindowPos);
        tp.setStartScenePos(startWindowPos);
        tp.setStartScreenPos(startScreenPos);
        tp.setLastPos(lastWindowPos);
        tp.setLastScenePos(lastWindowPos);
        tp.setLastScreenPos(lastScreenPos);
        tp.setPressure(0.0);
        endPoints.append(tp);
    }

    QTouchEvent endEvent(QEvent::TouchEnd, device, Qt::NoModifier, Qt::TouchPointReleased, endPoints);
    QApplication::sendEvent(windowHandle, &endEvent);
    QApplication::processEvents();

    if (details) {
        const QRect r = canvasWidget->rect();
        details->insert(QStringLiteral("sent"), true);
        details->insert(QStringLiteral("target"), QStringLiteral("window_handle"));
        details->insert(QStringLiteral("use_window_local_screen_pos"), useWindowLocalScreenPos);
        details->insert(QStringLiteral("step_ms"), stepMs);
        details->insert(QStringLiteral("hold_ms_at_end"), holdMsAtEnd);
        details->insert(QStringLiteral("steps"), steps);
        details->insert(QStringLiteral("fingers"), fingerCount);
        details->insert(QStringLiteral("canvas_w"), r.width());
        details->insert(QStringLiteral("canvas_h"), r.height());
    }
}

void performTouchDragPathViaGestureAction(int shortcut, const TouchDragPathPoints &pathPoints, QJsonObject *details)
{
    if (details) {
        details->insert(QStringLiteral("shortcut"), shortcut);
    }

    if (shortcut < 0) {
        if (details) {
            details->insert(QStringLiteral("error"), QStringLiteral("invalid shortcut"));
        }
        return;
    }

    QTouchDevice *device = touchDevice();
    if (!device) {
        if (details) {
            details->insert(QStringLiteral("error"), QStringLiteral("missing touch device"));
        }
        return;
    }

    const int steps = pathPoints.localPoints.size();
    if (steps < 2) {
        if (details) {
            details->insert(QStringLiteral("error"), QStringLiteral("invalid path"));
        }
        return;
    }

    const int fingerCount = pathPoints.localPoints.first().size();

    QList<QTouchEvent::TouchPoint> beginPoints;
    beginPoints.reserve(fingerCount);
    for (int i = 0; i < fingerCount; ++i) {
        QTouchEvent::TouchPoint tp(i);
        tp.setState(Qt::TouchPointPressed);
        tp.setPos(pathPoints.localPoints.first().at(i));
        tp.setScreenPos(pathPoints.globalPoints.first().at(i));
        tp.setStartPos(pathPoints.localPoints.first().at(i));
        tp.setStartScreenPos(pathPoints.globalPoints.first().at(i));
        tp.setLastPos(pathPoints.localPoints.first().at(i));
        tp.setLastScreenPos(pathPoints.globalPoints.first().at(i));
        beginPoints.append(tp);
    }

    QTouchEvent beginEvent(QEvent::TouchBegin, device, Qt::NoModifier, Qt::TouchPointPressed, beginPoints);

    KisTouchGestureAction gesture;
    gesture.begin(shortcut, &beginEvent);

    for (int step = 1; step < steps; ++step) {
        QList<QTouchEvent::TouchPoint> updatePoints;
        updatePoints.reserve(fingerCount);
        for (int i = 0; i < fingerCount; ++i) {
            QTouchEvent::TouchPoint tp(i);
            tp.setState(Qt::TouchPointMoved);
            tp.setPos(pathPoints.localPoints.at(step).at(i));
            tp.setScreenPos(pathPoints.globalPoints.at(step).at(i));
            tp.setStartPos(pathPoints.localPoints.first().at(i));
            tp.setStartScreenPos(pathPoints.globalPoints.first().at(i));
            tp.setLastPos(pathPoints.localPoints.at(step - 1).at(i));
            tp.setLastScreenPos(pathPoints.globalPoints.at(step - 1).at(i));
            updatePoints.append(tp);
        }
        QTouchEvent updateEvent(QEvent::TouchUpdate, device, Qt::NoModifier, Qt::TouchPointMoved, updatePoints);
        gesture.inputEvent(&updateEvent);
    }

    QList<QTouchEvent::TouchPoint> endPoints;
    endPoints.reserve(fingerCount);
    for (int i = 0; i < fingerCount; ++i) {
        QTouchEvent::TouchPoint tp(i);
        tp.setState(Qt::TouchPointReleased);
        tp.setPos(pathPoints.localPoints.last().at(i));
        tp.setScreenPos(pathPoints.globalPoints.last().at(i));
        tp.setStartPos(pathPoints.localPoints.first().at(i));
        tp.setStartScreenPos(pathPoints.globalPoints.first().at(i));
        tp.setLastPos(pathPoints.localPoints.last().at(i));
        tp.setLastScreenPos(pathPoints.globalPoints.last().at(i));
        endPoints.append(tp);
    }

    QTouchEvent endEvent(QEvent::TouchEnd, device, Qt::NoModifier, Qt::TouchPointReleased, endPoints);
    gesture.end(&endEvent);

    QApplication::processEvents();

    if (details) {
        QVector<QPointF> avg;
        avg.reserve(pathPoints.localPoints.size());
        for (const QVector<QPointF> &stepPoints : pathPoints.localPoints) {
            QPointF sum;
            for (const QPointF &p : stepPoints) {
                sum += p;
            }
            avg.append(stepPoints.isEmpty() ? QPointF() : (sum / qreal(stepPoints.size())));
        }

        qreal accumAbsDx = 0.0;
        qreal accumAbsDy = 0.0;
        int xDirectionChanges = 0;
        int lastXSign = 0;
        for (int i = 1; i < avg.size(); ++i) {
            const QPointF d = avg.at(i) - avg.at(i - 1);
            accumAbsDx += std::abs(d.x());
            accumAbsDy += std::abs(d.y());

            constexpr qreal kMinTrackStepPx = 3.0;
            if (std::abs(d.x()) > std::abs(d.y()) && std::abs(d.x()) >= kMinTrackStepPx) {
                const int sign = d.x() > 0.0 ? 1 : -1;
                if (lastXSign != 0 && sign != lastXSign) {
                    xDirectionChanges += 1;
                }
                lastXSign = sign;
            }
        }

        const QPointF netDelta = (avg.size() >= 2) ? (avg.last() - avg.first()) : QPointF();
        details->insert(QStringLiteral("net_dx"), netDelta.x());
        details->insert(QStringLiteral("net_dy"), netDelta.y());
        details->insert(QStringLiteral("accum_abs_dx"), accumAbsDx);
        details->insert(QStringLiteral("accum_abs_dy"), accumAbsDy);
        details->insert(QStringLiteral("x_direction_changes"), xDirectionChanges);

        constexpr qreal kDominance = 1.2;
        constexpr qreal kMinScrubAbsPx = 160.0;
        const qreal absDx = std::abs(netDelta.x());
        const qreal absDy = std::abs(netDelta.y());
        details->insert(QStringLiteral("mostly_vertical"), absDy >= absDx * kDominance);
        details->insert(QStringLiteral("mostly_horizontal"), absDx >= absDy * kDominance);
        details->insert(QStringLiteral("scrub_distance_ok"), accumAbsDx >= kMinScrubAbsPx && accumAbsDx >= accumAbsDy * kDominance);
        details->insert(QStringLiteral("scrub_direction_ok"), xDirectionChanges >= 1);
    }
    if (details) {
        details->insert(QStringLiteral("performed"), true);
        details->insert(QStringLiteral("steps"), pathPoints.localPoints.size());
        details->insert(QStringLiteral("fingers"), pathPoints.localPoints.isEmpty() ? 0 : pathPoints.localPoints.first().size());
    }
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

struct CanvasTransform {
    qreal zoom = 0.0;
    qreal rotationDeg = 0.0;
};

static bool readCanvasTransform(KisMainWindow *mainWindow, CanvasTransform *out, QString *errorOut)
{
    if (!out) {
        if (errorOut) {
            *errorOut = QStringLiteral("Internal error: missing output");
        }
        return false;
    }

    KisView *view = nullptr;
    QWidget *canvasWidget = nullptr;
    if (!getCanvasContext(mainWindow, &view, &canvasWidget, errorOut)) {
        return false;
    }

    KisCanvas2 *canvas = view ? view->canvasBase() : nullptr;
    KisCanvasController *controller = canvas ? static_cast<KisCanvasController *>(canvas->canvasController()) : nullptr;
    if (!canvas || !controller) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing canvas/controller");
        }
        return false;
    }

    out->zoom = canvas->viewConverter() ? canvas->viewConverter()->zoom() : 0.0;
    out->rotationDeg = controller->rotation();
    return true;
}

static qreal normalizedAngleDeltaDeg(qreal beforeDeg, qreal afterDeg)
{
    qreal d = afterDeg - beforeDeg;
    while (d > 180.0) {
        d -= 360.0;
    }
    while (d < -180.0) {
        d += 360.0;
    }
    return d;
}

static QPointF clampToRect(const QPointF &p, const QRectF &rect, bool *clampedOut)
{
    const qreal x = qBound(rect.left(), p.x(), rect.right());
    const qreal y = qBound(rect.top(), p.y(), rect.bottom());
    const bool clamped = (x != p.x()) || (y != p.y());
    if (clampedOut && clamped) {
        *clampedOut = true;
    }
    return QPointF(x, y);
}

static bool buildPinchRotatePathPoints(KisMainWindow *mainWindow,
                                      const QJsonObject &centerObj,
                                      qreal radiusStartFrac,
                                      qreal radiusEndFrac,
                                      qreal rotationDeg,
                                      qreal startAngleDeg,
                                      int steps,
                                      TouchDragPathPoints *out,
                                      QJsonObject *details,
                                      QString *errorOut)
{
    if (!out) {
        if (errorOut) {
            *errorOut = QStringLiteral("Internal error: missing output");
        }
        return false;
    }

    KisView *view = nullptr;
    QWidget *canvasWidget = nullptr;
    if (!getCanvasContext(mainWindow, &view, &canvasWidget, errorOut)) {
        return false;
    }

    QPointF center;
    if (!resolveWidgetPos(mainWindow, centerObj, &center, errorOut)) {
        return false;
    }

    const QRect canvasRect = canvasWidget->rect();
    const qreal minDim = qMax(1.0, qMin<qreal>(canvasRect.width(), canvasRect.height()));
    const qreal r0 = radiusStartFrac * minDim;
    const qreal r1 = radiusEndFrac * minDim;

    if (steps < 2) {
        if (errorOut) {
            *errorOut = QStringLiteral("touch.pinch_rotate: steps must be >= 2");
        }
        return false;
    }

    canvasWidget->setAttribute(Qt::WA_AcceptTouchEvents, true);

    out->localPoints.clear();
    out->globalPoints.clear();
    out->localPoints.reserve(steps);
    out->globalPoints.reserve(steps);

    const qreal rotRad = (rotationDeg * M_PI) / 180.0;
    const qreal startRad = (startAngleDeg * M_PI) / 180.0;

    bool clamped = false;
    const QRectF safe = canvasRect.adjusted(1, 1, -2, -2);
    for (int i = 0; i < steps; ++i) {
        const qreal t = steps <= 1 ? 0.0 : (qreal(i) / qreal(steps - 1));
        const qreal r = r0 + (r1 - r0) * t;
        const qreal a = startRad + rotRad * t;
        const QPointF v(std::cos(a) * r, std::sin(a) * r);

        QPointF p0 = clampToRect(center + v, safe, &clamped);
        QPointF p1 = clampToRect(center - v, safe, &clamped);

        QVector<QPointF> locals;
        locals.reserve(2);
        locals.append(p0);
        locals.append(p1);

        QVector<QPointF> globals;
        globals.reserve(2);
        const QPoint g0 = canvasWidget->mapToGlobal(QPoint(int(std::lround(p0.x())), int(std::lround(p0.y()))));
        const QPoint g1 = canvasWidget->mapToGlobal(QPoint(int(std::lround(p1.x())), int(std::lround(p1.y()))));
        globals.append(QPointF(g0));
        globals.append(QPointF(g1));

        out->localPoints.append(locals);
        out->globalPoints.append(globals);
    }

    if (details) {
        details->insert(QStringLiteral("center_x"), center.x());
        details->insert(QStringLiteral("center_y"), center.y());
        details->insert(QStringLiteral("radius_start_frac"), radiusStartFrac);
        details->insert(QStringLiteral("radius_end_frac"), radiusEndFrac);
        details->insert(QStringLiteral("rotation_deg"), rotationDeg);
        details->insert(QStringLiteral("start_angle_deg"), startAngleDeg);
        details->insert(QStringLiteral("steps"), steps);
        details->insert(QStringLiteral("canvas_w"), canvasRect.width());
        details->insert(QStringLiteral("canvas_h"), canvasRect.height());
        details->insert(QStringLiteral("clamped"), clamped);
    }

    return true;
}

class ZoomAndRotateActionProbe : public KisZoomAndRotateAction
{
public:
    KisInputManager *peekInputManager() const
    {
        return inputManager();
    }
};

bool touchPinchRotateWaitCanvasTransform(KisMainWindow *mainWindow,
                                        const QJsonObject &centerObj,
                                        qreal radiusStartFrac,
                                        qreal radiusEndFrac,
                                        qreal rotationDeg,
                                        qreal startAngleDeg,
                                        int steps,
                                        int timeoutMs,
                                        int stepMs,
                                        bool expectRotation,
                                        qreal minZoomRatioChange,
                                        qreal minAbsRotationDeg,
                                        qreal maxAbsRotationDeg,
                                        bool forceDirectAction,
                                        QJsonObject *details,
                                        QString *errorOut)
{
    KisView *view = nullptr;
    QWidget *canvasWidget = nullptr;
    if (!getCanvasContext(mainWindow, &view, &canvasWidget, errorOut)) {
        return false;
    }

    CanvasTransform before;
    if (!readCanvasTransform(mainWindow, &before, errorOut)) {
        return false;
    }

    TouchDragPathPoints pathPoints;
    QJsonObject pathDetails;
    if (!buildPinchRotatePathPoints(mainWindow,
                                    centerObj,
                                    radiusStartFrac,
                                    radiusEndFrac,
                                    rotationDeg,
                                    startAngleDeg,
                                    steps,
                                    &pathPoints,
                                    &pathDetails,
                                    errorOut)) {
        return false;
    }

    QJsonObject injectDetails;
    if (!forceDirectAction) {
        sendTouchDragPath(canvasWidget, pathPoints, stepMs, 0, &injectDetails);
    } else {
        injectDetails.insert(QStringLiteral("sent"), false);
        injectDetails.insert(QStringLiteral("note"), QStringLiteral("skipped due to force_direct_action"));
    }

    auto matchesExpectation = [&](const CanvasTransform &cur) -> bool {
        const qreal zoomRatioChangeNow =
            before.zoom > 0.0 ? std::abs(cur.zoom / before.zoom - 1.0) : std::abs(cur.zoom - before.zoom);
        const qreal absRotDeltaNow = std::abs(normalizedAngleDeltaDeg(before.rotationDeg, cur.rotationDeg));

        const bool zoomOk = zoomRatioChangeNow >= minZoomRatioChange;
        const bool rotOk = expectRotation ? (absRotDeltaNow >= minAbsRotationDeg) : (absRotDeltaNow <= maxAbsRotationDeg);
        return zoomOk && rotOk;
    };

    CanvasTransform afterInjected = before;
    const bool okInjected = forceDirectAction
        ? false
        : waitForUiCondition(timeoutMs, [&]() {
              CanvasTransform cur;
              if (!readCanvasTransform(mainWindow, &cur, nullptr)) {
                  return false;
              }
              return matchesExpectation(cur);
          });

    readCanvasTransform(mainWindow, &afterInjected, nullptr);

    CanvasTransform after = afterInjected;
    bool ok = okInjected;
    bool usedDirectActionFallback = false;
    bool okDirect = false;
    QJsonObject directActionDetails;

    if (!okInjected || forceDirectAction) {
        KisInputManager *inputManager = view ? view->globalInputManager() : nullptr;
        directActionDetails.insert(QStringLiteral("has_input_manager"), bool(inputManager));

        // Prime the global KisAbstractInputAction input manager pointer by routing a tiny
        // 1-finger tap through the real input manager. This makes calling a KisAbstractInputAction
        // directly safe even when multi-touch injection is flaky on some platforms (notably Android).
        bool primed = false;
        if (inputManager) {
            QTouchDevice *device = touchDevice();
            if (device) {
                const QPointF center(canvasWidget->rect().center());
                const QPointF centerGlobal(canvasWidget->mapToGlobal(center.toPoint()));

                QTouchEvent::TouchPoint tp0(0);
                tp0.setState(Qt::TouchPointPressed);
                tp0.setPos(center);
                tp0.setScreenPos(centerGlobal);
                tp0.setStartPos(center);
                tp0.setStartScreenPos(centerGlobal);
                tp0.setLastPos(center);
                tp0.setLastScreenPos(centerGlobal);
                tp0.setPressure(1.0);

                QTouchEvent beginEvent(QEvent::TouchBegin,
                                       device,
                                       Qt::NoModifier,
                                       Qt::TouchPointPressed,
                                       QList<QTouchEvent::TouchPoint>{tp0});
                inputManager->eventFilter(canvasWidget, &beginEvent);

                tp0.setState(Qt::TouchPointReleased);
                tp0.setPressure(0.0);
                QTouchEvent endEvent(QEvent::TouchEnd,
                                     device,
                                     Qt::NoModifier,
                                     Qt::TouchPointReleased,
                                     QList<QTouchEvent::TouchPoint>{tp0});
                inputManager->eventFilter(canvasWidget, &endEvent);

                QApplication::processEvents();
                primed = true;
            }
        }
        directActionDetails.insert(QStringLiteral("primed_input_manager"), primed);

        ZoomAndRotateActionProbe actionProbe;
        KisInputManager *actionInputManager = actionProbe.peekInputManager();
        directActionDetails.insert(QStringLiteral("action_has_input_manager"), bool(actionInputManager));
        directActionDetails.insert(QStringLiteral("action_has_canvas"),
                                   bool(actionInputManager && actionInputManager->canvas()));

        if (actionInputManager && actionInputManager->canvas()) {
            const int actualSteps = pathPoints.localPoints.size();
            if (actualSteps >= 2) {
                QTouchDevice *device = touchDevice();
                if (!device) {
                    directActionDetails.insert(QStringLiteral("error"), QStringLiteral("missing touch device"));
                } else {
                    usedDirectActionFallback = true;
                    const int fingerCount = pathPoints.localPoints.first().size();
                    directActionDetails.insert(QStringLiteral("fingers"), fingerCount);
                    directActionDetails.insert(QStringLiteral("steps"), actualSteps);

                    auto makePoints = [&](int stepIndex, Qt::TouchPointState state, int lastIndex) {
                        QList<QTouchEvent::TouchPoint> points;
                        points.reserve(fingerCount);
                        for (int i = 0; i < fingerCount; ++i) {
                            QTouchEvent::TouchPoint tp(i);
                            tp.setState(state);
                            tp.setPos(pathPoints.localPoints.at(stepIndex).at(i));
                            tp.setScreenPos(pathPoints.globalPoints.at(stepIndex).at(i));
                            tp.setStartPos(pathPoints.localPoints.first().at(i));
                            tp.setStartScreenPos(pathPoints.globalPoints.first().at(i));
                            tp.setLastPos(pathPoints.localPoints.at(lastIndex).at(i));
                            tp.setLastScreenPos(pathPoints.globalPoints.at(lastIndex).at(i));
                            tp.setPressure(state == Qt::TouchPointReleased ? 0.0 : 1.0);
                            points.append(tp);
                        }
                        return points;
                    };

                    QTouchEvent beginEvent(QEvent::TouchBegin,
                                           device,
                                           Qt::NoModifier,
                                           Qt::TouchPointPressed,
                                           makePoints(0, Qt::TouchPointPressed, 0));

                    KisZoomAndRotateAction action;
                    action.begin(KisZoomAndRotateAction::ContinuousRotateMode, &beginEvent);
                    QApplication::processEvents();

                    for (int step = 1; step < actualSteps; ++step) {
                        const int lastIndex = qMax(0, step - 1);
                        QTouchEvent updateEvent(QEvent::TouchUpdate,
                                                device,
                                                Qt::NoModifier,
                                                Qt::TouchPointMoved,
                                                makePoints(step, Qt::TouchPointMoved, lastIndex));
                        action.inputEvent(&updateEvent);
                        QApplication::processEvents();
                        if (stepMs > 0) {
                            QThread::msleep(stepMs);
                        }
                    }

                    QTouchEvent endEvent(QEvent::TouchEnd,
                                         device,
                                         Qt::NoModifier,
                                         Qt::TouchPointReleased,
                                         makePoints(actualSteps - 1, Qt::TouchPointReleased, actualSteps - 1));
                    action.end(&endEvent);
                    QApplication::processEvents();

                    okDirect = waitForUiCondition(timeoutMs, [&]() {
                        CanvasTransform cur;
                        if (!readCanvasTransform(mainWindow, &cur, nullptr)) {
                            return false;
                        }
                        return matchesExpectation(cur);
                    });

                    readCanvasTransform(mainWindow, &after, nullptr);
                    ok = okDirect;
                }
            } else {
                directActionDetails.insert(QStringLiteral("error"), QStringLiteral("invalid path"));
            }
        }
    }

    const qreal rotDelta = normalizedAngleDeltaDeg(before.rotationDeg, after.rotationDeg);
    const qreal absRotDelta = std::abs(rotDelta);
    const qreal zoomRatioChange = before.zoom > 0.0 ? std::abs(after.zoom / before.zoom - 1.0) : std::abs(after.zoom - before.zoom);

    if (details) {
        details->insert(QStringLiteral("timeout_ms"), timeoutMs);
        details->insert(QStringLiteral("step_ms"), stepMs);
        details->insert(QStringLiteral("expect_rotation"), expectRotation);
        details->insert(QStringLiteral("min_zoom_ratio_change"), minZoomRatioChange);
        details->insert(QStringLiteral("min_abs_rotation_deg"), minAbsRotationDeg);
        details->insert(QStringLiteral("max_abs_rotation_deg"), maxAbsRotationDeg);
        details->insert(QStringLiteral("force_direct_action"), forceDirectAction);
        details->insert(QStringLiteral("before_zoom"), before.zoom);
        details->insert(QStringLiteral("after_zoom"), after.zoom);
        details->insert(QStringLiteral("zoom_ratio_change"), zoomRatioChange);
        details->insert(QStringLiteral("before_rotation_deg"), before.rotationDeg);
        details->insert(QStringLiteral("after_rotation_deg"), after.rotationDeg);
        details->insert(QStringLiteral("rotation_delta_deg"), rotDelta);
        details->insert(QStringLiteral("abs_rotation_delta_deg"), absRotDelta);
        details->insert(QStringLiteral("path_details"), pathDetails);
        details->insert(QStringLiteral("touch_inject_details"), injectDetails);
        details->insert(QStringLiteral("touch_inject_ok"), okInjected);
        details->insert(QStringLiteral("used_direct_action_fallback"), usedDirectActionFallback);
        details->insert(QStringLiteral("direct_action_ok"), okDirect);
        if (!directActionDetails.isEmpty()) {
            details->insert(QStringLiteral("direct_action_details"), directActionDetails);
        }
    }

    if (!ok && errorOut) {
        *errorOut = QStringLiteral("Canvas transform did not match expectation (zoom_change=%1 rot_delta=%2)")
                        .arg(zoomRatioChange, 0, 'f', 4)
                        .arg(rotDelta, 0, 'f', 2);
    }

    return ok;
}

} // namespace KisTouchSmokeScriptRunnerDetail
