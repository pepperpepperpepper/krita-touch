/*
 * This file is part of the Krita touch fork.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisTouchSmokeScriptRunner_p.h"

#include <QApplication>
#include <QAction>
#include <QTouchDevice>
#include <QTouchEvent>
#include <QToolBar>
#include <QWidget>
#include <QWindow>

#include <KoCanvasResourcesIds.h>
#include <KoToolBase.h>
#include <KoToolManager.h>

#include <kactioncollection.h>

#include "KisMainWindow.h"
#include "KisView.h"
#include "KisViewManager.h"
#include "canvas/kis_canvas2.h"
#include "canvas/kis_coordinates_converter.h"
#include "input/KisTouchQuickMenuAction.h"
#include "kis_canvas_resource_provider.h"
#include "kis_group_layer.h"
#include "kis_image.h"

namespace KisTouchSmokeScriptRunnerDetail {

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

static bool activeToolMaskSyntheticEvents(KisMainWindow *mainWindow, bool *valueOut, QString *toolIdOut, QString *errorOut)
{
    if (!mainWindow) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing main window");
        }
        return false;
    }

    KisView *view = mainWindow->activeView();
    if (!view || !view->canvasBase()) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing active view/canvas");
        }
        return false;
    }

    KoToolManager *toolManager = KoToolManager::instance();
    if (!toolManager) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing tool manager");
        }
        return false;
    }

    const QString toolId = toolManager->activeToolId();
    KoToolBase *tool = toolManager->toolById(view->canvasBase(), toolId);
    if (!tool) {
        if (errorOut) {
            *errorOut = QStringLiteral("Active tool not found: %1").arg(toolId);
        }
        return false;
    }

    if (toolIdOut) {
        *toolIdOut = toolId;
    }
    if (valueOut) {
        *valueOut = tool->maskSyntheticEvents();
    }
    return true;
}

bool assertActiveToolMaskSyntheticEvents(KisMainWindow *mainWindow, bool expected, int timeoutMs, QJsonObject *details, QString *errorOut)
{
    QString toolId;
    bool actual = false;

    const bool ok = waitForUiCondition(timeoutMs, [&]() {
        return activeToolMaskSyntheticEvents(mainWindow, &actual, &toolId, nullptr) && actual == expected;
    });

    if (details) {
        details->insert(QStringLiteral("tool_id"), toolId);
        details->insert(QStringLiteral("expected"), expected);
        details->insert(QStringLiteral("actual"), actual);
        details->insert(QStringLiteral("timeout_ms"), timeoutMs);
    }

    if (!ok && errorOut) {
        *errorOut = QStringLiteral("Active tool maskSyntheticEvents mismatch");
    }

    return ok;
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

bool overlayVisible(KisMainWindow *mainWindow, const QString &objectName)
{
    QWidget *overlay = mainWindow ? mainWindow->findChild<QWidget *>(objectName) : nullptr;
    return overlay && overlay->isVisible();
}

bool hideOverlay(KisMainWindow *mainWindow, const QString &objectName, QJsonObject *details)
{
    QWidget *overlay = mainWindow ? mainWindow->findChild<QWidget *>(objectName) : nullptr;
    const bool found = bool(overlay);
    const bool wasVisible = overlay ? overlay->isVisible() : false;
    if (overlay) {
        overlay->hide();
        QApplication::processEvents();
    }
    if (details) {
        details->insert(QStringLiteral("object_name"), objectName);
        details->insert(QStringLiteral("found"), found);
        details->insert(QStringLiteral("was_visible"), wasVisible);
        details->insert(QStringLiteral("is_visible"), overlay ? overlay->isVisible() : false);
    }
    return true;
}

bool waitOverlayVisible(KisMainWindow *mainWindow,
                        const QString &objectName,
                        bool expectedVisible,
                        int timeoutMs,
                        QJsonObject *details,
                        QString *errorOut)
{
    const bool ok = waitForUiCondition(timeoutMs, [&]() { return overlayVisible(mainWindow, objectName) == expectedVisible; });
    if (details) {
        details->insert(QStringLiteral("object_name"), objectName);
        details->insert(QStringLiteral("expected_visible"), expectedVisible);
        details->insert(QStringLiteral("timeout_ms"), timeoutMs);
        details->insert(QStringLiteral("visible"), overlayVisible(mainWindow, objectName));
    }
    if (!ok && errorOut) {
        *errorOut = QStringLiteral("Timed out waiting for overlay '%1' visible=%2").arg(objectName).arg(expectedVisible);
    }
    return ok;
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

bool actionWaitChecked(KisMainWindow *mainWindow, const QString &actionId, bool expectedChecked, int timeoutMs, QJsonObject *details, QString *errorOut)
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

    bool actual = action->isChecked();
    const bool ok = waitForUiCondition(timeoutMs, [&]() {
        actual = action->isChecked();
        return actual == expectedChecked;
    });

    if (details) {
        details->insert(QStringLiteral("action_id"), actionId);
        details->insert(QStringLiteral("expected_checked"), expectedChecked);
        details->insert(QStringLiteral("actual_checked"), actual);
        details->insert(QStringLiteral("timeout_ms"), timeoutMs);
    }

    if (!ok && errorOut) {
        *errorOut = QStringLiteral("Timed out waiting for action '%1' checked=%2").arg(actionId).arg(expectedChecked);
    }

    return ok;
}

static bool getCanvasResourceProvider(KisMainWindow *mainWindow, KisCanvasResourceProvider **providerOut, QString *errorOut)
{
    if (!mainWindow || !mainWindow->viewManager()) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing view manager");
        }
        return false;
    }

    KisCanvasResourceProvider *provider = mainWindow->viewManager()->canvasResourceProvider();
    if (!provider) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing canvas resource provider");
        }
        return false;
    }

    if (!provider->resourceManager()) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing canvas resource manager");
        }
        return false;
    }

    if (providerOut) {
        *providerOut = provider;
    }
    return true;
}

bool waitCanvasEraserMode(KisMainWindow *mainWindow, bool expected, int timeoutMs, QJsonObject *details, QString *errorOut)
{
    KisCanvasResourceProvider *provider = nullptr;
    if (!getCanvasResourceProvider(mainWindow, &provider, errorOut)) {
        return false;
    }

    bool actual = provider->eraserMode();
    const bool ok = waitForUiCondition(timeoutMs, [&]() {
        actual = provider->eraserMode();
        return actual == expected;
    });

    if (details) {
        details->insert(QStringLiteral("expected"), expected);
        details->insert(QStringLiteral("actual"), actual);
        details->insert(QStringLiteral("timeout_ms"), timeoutMs);
    }

    if (!ok && errorOut) {
        *errorOut = QStringLiteral("Timed out waiting for eraser mode %1").arg(expected);
    }

    return ok;
}

bool waitCanvasEffectiveCompositeOp(KisMainWindow *mainWindow,
                                    const QString &expectedId,
                                    bool negate,
                                    int timeoutMs,
                                    QJsonObject *details,
                                    QString *errorOut)
{
    if (expectedId.trimmed().isEmpty()) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing expected composite op id");
        }
        return false;
    }

    KisCanvasResourceProvider *provider = nullptr;
    if (!getCanvasResourceProvider(mainWindow, &provider, errorOut)) {
        return false;
    }

    QString actual = provider->resourceManager()->resource(KoCanvasResource::CurrentEffectiveCompositeOp).toString();
    const bool ok = waitForUiCondition(timeoutMs, [&]() {
        actual = provider->resourceManager()->resource(KoCanvasResource::CurrentEffectiveCompositeOp).toString();
        return negate ? (actual != expectedId) : (actual == expectedId);
    });

    if (details) {
        details->insert(QStringLiteral("expected"), expectedId);
        details->insert(QStringLiteral("negate"), negate);
        details->insert(QStringLiteral("actual"), actual);
        details->insert(QStringLiteral("timeout_ms"), timeoutMs);
    }

    if (!ok && errorOut) {
        *errorOut = QStringLiteral("Timed out waiting for effective composite op (expected '%1', negate=%2)")
                        .arg(expectedId)
                        .arg(negate);
    }

    return ok;
}

static int layerCount(KisImageWSP image)
{
    KisGroupLayerSP root = image ? image->rootLayer() : KisGroupLayerSP();
    return root ? int(root->childCount()) : 0;
}

static QList<QTouchEvent::TouchPoint> touchPointsForPositions(const QVector<QPointF> &localPoints,
                                                              const QVector<QPointF> &globalPoints,
                                                              Qt::TouchPointState state)
{
    QList<QTouchEvent::TouchPoint> points;
    const int count = qMin(localPoints.size(), globalPoints.size());
    points.reserve(count);

    for (int i = 0; i < count; ++i) {
        QTouchEvent::TouchPoint tp(i);
        tp.setState(state);
        tp.setPos(localPoints.at(i));
        tp.setScreenPos(globalPoints.at(i));
        tp.setStartPos(localPoints.at(i));
        tp.setStartScreenPos(globalPoints.at(i));
        tp.setLastPos(localPoints.at(i));
        tp.setLastScreenPos(globalPoints.at(i));
        points.append(tp);
    }

    return points;
}

bool touchTapCheckableActionWithFallback(KisMainWindow *mainWindow,
                                        int fingerCount,
                                        const QJsonObject &posObj,
                                        const QString &actionId,
                                        bool expectedChecked,
                                        int timeoutMs,
                                        int fallbackShortcut,
                                        bool requireInputManager,
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

    if (!toggledViaInputManager && !requireInputManager) {
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
        details->insert(QStringLiteral("require_input_manager"), requireInputManager);
        details->insert(QStringLiteral("input_manager_toggled"), toggledViaInputManager);
        details->insert(QStringLiteral("direct_action_toggled"), toggledViaDirectAction);
        if (!injectError.isEmpty()) {
            details->insert(QStringLiteral("touch_inject_error"), injectError);
        }
    }

    return requireInputManager ? toggledViaInputManager : (toggledViaInputManager || toggledViaDirectAction);
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

bool touchUiTapActionWidget(KisMainWindow *mainWindow,
                            const QString &actionId,
                            bool deliverToWindowHandle,
                            bool useWindowLocalScreenPos,
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

    QWidget *targetWidget = nullptr;

    // Prefer the widget in the Touch Top Bar when available.
    if (QToolBar *touchTopBar = mainWindow->findChild<QToolBar *>(QStringLiteral("touchTopBar"))) {
        if (touchTopBar->actions().contains(action)) {
            targetWidget = touchTopBar->widgetForAction(action);
        }
    }

    if (!targetWidget) {
        const QList<QWidget *> associated = action->associatedWidgets();
        for (QWidget *w : associated) {
            if (!w) {
                continue;
            }

            if (QToolBar *tb = qobject_cast<QToolBar *>(w)) {
                if (QWidget *button = tb->widgetForAction(action)) {
                    targetWidget = button;
                    break;
                }
            }

            targetWidget = w;
            break;
        }
    }

    if (!targetWidget) {
        if (errorOut) {
            *errorOut = QStringLiteral("No associated widget for action: %1").arg(actionId);
        }
        return false;
    }

    if (!targetWidget->isVisible()) {
        if (errorOut) {
            *errorOut = QStringLiteral("Action widget not visible: %1").arg(actionId);
        }
        return false;
    }

    mainWindow->winId();
    QWindow *windowHandle = targetWidget->window() ? targetWidget->window()->windowHandle() : nullptr;
    if (!windowHandle) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing window handle for action widget: %1").arg(actionId);
        }
        return false;
    }

    QTouchDevice *device = touchDevice();
    if (!device) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing touch device");
        }
        return false;
    }

    const QPoint globalCenter = targetWidget->mapToGlobal(targetWidget->rect().center());
    const QPoint windowPos = windowHandle->mapFromGlobal(globalCenter);
    const QPointF windowPosF(windowPos);
    const QPointF screenPosF = useWindowLocalScreenPos ? QPointF(windowPos) : QPointF(globalCenter);

    if (details) {
        details->insert(QStringLiteral("action_id"), actionId);
        details->insert(QStringLiteral("deliver_to_window_handle"), deliverToWindowHandle);
        details->insert(QStringLiteral("use_window_local_screen_pos"), useWindowLocalScreenPos);
        details->insert(QStringLiteral("target_class"), QString::fromLatin1(targetWidget->metaObject()->className()));
        details->insert(QStringLiteral("target_object_name"), targetWidget->objectName());
        details->insert(QStringLiteral("global_x"), globalCenter.x());
        details->insert(QStringLiteral("global_y"), globalCenter.y());
        details->insert(QStringLiteral("window_x"), windowPos.x());
        details->insert(QStringLiteral("window_y"), windowPos.y());
    }

    auto buildPoint = [&](Qt::TouchPointState state, qreal pressure) {
        QTouchEvent::TouchPoint tp(0);
        tp.setState(state);
        tp.setPos(windowPosF);
        tp.setScenePos(windowPosF);
        tp.setScreenPos(screenPosF);
        tp.setStartPos(windowPosF);
        tp.setStartScenePos(windowPosF);
        tp.setStartScreenPos(screenPosF);
        tp.setLastPos(windowPosF);
        tp.setLastScenePos(windowPosF);
        tp.setLastScreenPos(screenPosF);
        tp.setPressure(pressure);
        return tp;
    };

    QList<QTouchEvent::TouchPoint> beginPoints = {buildPoint(Qt::TouchPointPressed, 1.0)};
    QTouchEvent beginEvent(QEvent::TouchBegin, device, Qt::NoModifier, Qt::TouchPointPressed, beginPoints);
    if (deliverToWindowHandle) {
        QApplication::sendEvent(windowHandle, &beginEvent);
    } else {
        QApplication::sendEvent(targetWidget, &beginEvent);
    }
    QApplication::processEvents();

    QList<QTouchEvent::TouchPoint> endPoints = {buildPoint(Qt::TouchPointReleased, 0.0)};
    QTouchEvent endEvent(QEvent::TouchEnd, device, Qt::NoModifier, Qt::TouchPointReleased, endPoints);
    if (deliverToWindowHandle) {
        QApplication::sendEvent(windowHandle, &endEvent);
    } else {
        QApplication::sendEvent(targetWidget, &endEvent);
    }
    QApplication::processEvents();

    return true;
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

static bool touchHoldWaitOverlayVisible(KisMainWindow *mainWindow,
                                       QWidget *canvasWidget,
                                       const QVector<QPointF> &localPoints,
                                       const QVector<QPointF> &globalPoints,
                                       const QString &overlayObjectName,
                                       int timeoutMs,
                                       QJsonObject *details,
                                       QString *errorOut)
{
    if (!mainWindow) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing main window");
        }
        return false;
    }
    if (!canvasWidget) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing canvas widget");
        }
        return false;
    }

    QTouchDevice *device = touchDevice();
    if (!device) {
        if (errorOut) {
            *errorOut = QStringLiteral("touch.hold: could not allocate touch device");
        }
        return false;
    }

    const QList<QTouchEvent::TouchPoint> beginPoints =
        touchPointsForPositions(localPoints, globalPoints, Qt::TouchPointPressed);
    if (beginPoints.isEmpty()) {
        if (errorOut) {
            *errorOut = QStringLiteral("touch.hold: no touch points");
        }
        return false;
    }

    QTouchEvent beginEvent(QEvent::TouchBegin, device, Qt::NoModifier, Qt::TouchPointPressed, beginPoints);
    QApplication::sendEvent(canvasWidget, &beginEvent);
    QApplication::processEvents();

    const bool shown = waitOverlayVisible(mainWindow, overlayObjectName, true, timeoutMs, nullptr, nullptr);

    const QList<QTouchEvent::TouchPoint> endPoints =
        touchPointsForPositions(localPoints, globalPoints, Qt::TouchPointReleased);
    QTouchEvent endEvent(QEvent::TouchEnd, device, Qt::NoModifier, Qt::TouchPointReleased, endPoints);
    QApplication::sendEvent(canvasWidget, &endEvent);
    QApplication::processEvents();

    if (details) {
        details->insert(QStringLiteral("timeout_ms"), timeoutMs);
        details->insert(QStringLiteral("touch_sent"), true);
        details->insert(QStringLiteral("overlay_visible"), shown);
    }

    if (!shown && errorOut) {
        *errorOut = QStringLiteral("Overlay '%1' did not appear during touch hold").arg(overlayObjectName);
    }

    return shown;
}

static bool performQuickMenuHoldWaitOverlayVisible(KisMainWindow *mainWindow,
                                                  const QVector<QPointF> &localPoints,
                                                  const QVector<QPointF> &globalPoints,
                                                  const QString &overlayObjectName,
                                                  int timeoutMs,
                                                  QJsonObject *details,
                                                  QString *errorOut)
{
    if (!mainWindow) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing main window");
        }
        return false;
    }
    if (localPoints.size() != 1 || globalPoints.size() != 1) {
        if (errorOut) {
            *errorOut = QStringLiteral("quickmenu hold expects 1 finger");
        }
        return false;
    }

    QTouchDevice *device = touchDevice();
    if (!device) {
        if (errorOut) {
            *errorOut = QStringLiteral("touch.hold: could not allocate touch device");
        }
        return false;
    }

    const QList<QTouchEvent::TouchPoint> beginPoints =
        touchPointsForPositions(localPoints, globalPoints, Qt::TouchPointPressed);
    QTouchEvent beginEvent(QEvent::TouchBegin, device, Qt::NoModifier, Qt::TouchPointPressed, beginPoints);

    KisTouchQuickMenuAction action;
    action.begin(0, &beginEvent);
    QApplication::processEvents();

    const bool shown = waitOverlayVisible(mainWindow, overlayObjectName, true, timeoutMs, nullptr, nullptr);

    const QList<QTouchEvent::TouchPoint> endPoints =
        touchPointsForPositions(localPoints, globalPoints, Qt::TouchPointReleased);
    QTouchEvent endEvent(QEvent::TouchEnd, device, Qt::NoModifier, Qt::TouchPointReleased, endPoints);
    action.end(&endEvent);
    QApplication::processEvents();

    if (details) {
        details->insert(QStringLiteral("timeout_ms"), timeoutMs);
        details->insert(QStringLiteral("performed"), true);
        details->insert(QStringLiteral("overlay_visible"), shown);
    }

    if (!shown && errorOut) {
        *errorOut = QStringLiteral("Overlay '%1' did not appear during quickmenu hold").arg(overlayObjectName);
    }

    return shown;
}

bool touchDragPathWaitOverlayVisibleWithFallback(KisMainWindow *mainWindow,
                                                int fingerCount,
                                                const QJsonArray &pathArray,
                                                const QString &overlayObjectName,
                                                int timeoutMs,
                                                int fallbackShortcut,
                                                int stepMs,
                                                bool requireInputManager,
                                                QJsonObject *details,
                                                QString *errorOut)
{
    QWidget *canvasWidget = nullptr;
    if (!getCanvasContext(mainWindow, nullptr, &canvasWidget, errorOut)) {
        return false;
    }

    TouchDragPathPoints pathPoints;
    if (!buildTouchDragPathPoints(mainWindow, fingerCount, pathArray, &pathPoints, errorOut)) {
        return false;
    }

    bool shownViaInputManager = false;
    bool shownViaDirectAction = false;

    QJsonObject injectDetails;
    sendTouchDragPath(canvasWidget, pathPoints, stepMs, 0, &injectDetails);
    shownViaInputManager = waitOverlayVisible(mainWindow, overlayObjectName, true, timeoutMs, nullptr, nullptr);

    if (!shownViaInputManager && !requireInputManager) {
        QJsonObject hideDetails;
        hideOverlay(mainWindow, overlayObjectName, &hideDetails);

        QJsonObject directDetails;
        performTouchDragPathViaGestureAction(fallbackShortcut, pathPoints, &directDetails);
        shownViaDirectAction = waitOverlayVisible(mainWindow, overlayObjectName, true, timeoutMs, nullptr, nullptr);

        if (details) {
            details->insert(QStringLiteral("overlay_hide_details"), hideDetails);
            details->insert(QStringLiteral("direct_action_details"), directDetails);
        }
    }

    if (details) {
        details->insert(QStringLiteral("overlay_object_name"), overlayObjectName);
        details->insert(QStringLiteral("timeout_ms"), timeoutMs);
        details->insert(QStringLiteral("step_ms"), stepMs);
        details->insert(QStringLiteral("require_input_manager"), requireInputManager);
        details->insert(QStringLiteral("input_manager_shown"), shownViaInputManager);
        details->insert(QStringLiteral("direct_action_shown"), shownViaDirectAction);
        details->insert(QStringLiteral("touch_inject_details"), injectDetails);
        details->insert(QStringLiteral("visible"), overlayVisible(mainWindow, overlayObjectName));
    }

    const bool ok = requireInputManager ? shownViaInputManager : (shownViaInputManager || shownViaDirectAction);
    if (!ok && errorOut) {
        *errorOut = QStringLiteral("Overlay '%1' did not appear").arg(overlayObjectName);
    }
    return ok;
}

bool touchHoldWaitOverlayVisibleWithFallback(KisMainWindow *mainWindow,
                                            int fingerCount,
                                            const QJsonObject &posObj,
                                            const QString &overlayObjectName,
                                            int timeoutMs,
                                            const QString &fallbackActionName,
                                            bool requireInputManager,
                                            QJsonObject *details,
                                            QString *errorOut)
{
    QWidget *canvasWidget = nullptr;
    QVector<QPointF> localPoints;
    QVector<QPointF> globalPoints;
    if (!buildTouchHoldPoints(mainWindow, fingerCount, posObj, &canvasWidget, &localPoints, &globalPoints, errorOut)) {
        return false;
    }

    bool shownViaInputManager = false;
    bool shownViaDirectAction = false;

    QJsonObject injectDetails;
    {
        QString localError;
        shownViaInputManager = touchHoldWaitOverlayVisible(mainWindow,
                                                          canvasWidget,
                                                          localPoints,
                                                          globalPoints,
                                                          overlayObjectName,
                                                          timeoutMs,
                                                          &injectDetails,
                                                          &localError);
        if (!localError.isEmpty()) {
            injectDetails.insert(QStringLiteral("error"), localError);
        }
    }

    QJsonObject hideDetails;
    if (!shownViaInputManager && !requireInputManager) {
        hideOverlay(mainWindow, overlayObjectName, &hideDetails);

        const QString normalizedFallback = fallbackActionName.trimmed().toLower();
        if (normalizedFallback.isEmpty()) {
            // No fallback path.
        } else if (normalizedFallback == QStringLiteral("quickmenu") || normalizedFallback == QStringLiteral("touch_quickmenu")) {
            QJsonObject directDetails;
            QString localError;
            shownViaDirectAction = performQuickMenuHoldWaitOverlayVisible(mainWindow,
                                                                         localPoints,
                                                                         globalPoints,
                                                                         overlayObjectName,
                                                                         timeoutMs,
                                                                         &directDetails,
                                                                         &localError);
            if (!localError.isEmpty()) {
                directDetails.insert(QStringLiteral("error"), localError);
            }
            if (details) {
                details->insert(QStringLiteral("direct_action_details"), directDetails);
            }
        } else {
            if (errorOut) {
                *errorOut = QStringLiteral("Unknown fallback_action: %1").arg(fallbackActionName);
            }
            return false;
        }
    }

    if (details) {
        details->insert(QStringLiteral("fingers"), fingerCount);
        details->insert(QStringLiteral("overlay_object_name"), overlayObjectName);
        details->insert(QStringLiteral("timeout_ms"), timeoutMs);
        details->insert(QStringLiteral("require_input_manager"), requireInputManager);
        details->insert(QStringLiteral("fallback_action"), fallbackActionName);
        details->insert(QStringLiteral("input_manager_shown"), shownViaInputManager);
        details->insert(QStringLiteral("direct_action_shown"), shownViaDirectAction);
        details->insert(QStringLiteral("touch_inject_details"), injectDetails);
        if (!hideDetails.isEmpty()) {
            details->insert(QStringLiteral("overlay_hide_details"), hideDetails);
        }
        details->insert(QStringLiteral("visible"), overlayVisible(mainWindow, overlayObjectName));
    }

    const bool ok = requireInputManager ? shownViaInputManager : (shownViaInputManager || shownViaDirectAction);
    if (!ok && errorOut) {
        *errorOut = QStringLiteral("Overlay '%1' did not appear").arg(overlayObjectName);
    }
    return ok;
}

bool touchDragPathOverlayNoChange(KisMainWindow *mainWindow,
                                 int fingerCount,
                                 const QJsonArray &pathArray,
                                 const QString &overlayObjectName,
                                 int settleMs,
                                 int directShortcut,
                                 int stepMs,
                                 QJsonObject *details,
                                 QString *errorOut)
{
    QWidget *canvasWidget = nullptr;
    if (!getCanvasContext(mainWindow, nullptr, &canvasWidget, errorOut)) {
        return false;
    }

    TouchDragPathPoints pathPoints;
    if (!buildTouchDragPathPoints(mainWindow, fingerCount, pathArray, &pathPoints, errorOut)) {
        return false;
    }

    QJsonObject injectDetails;
    sendTouchDragPath(canvasWidget, pathPoints, stepMs, 0, &injectDetails);

    // A "no overlay appeared" assertion is only meaningful if the swipe was
    // actually delivered. If injection never fired (no canvas / no touch device /
    // invalid path), the step would otherwise pass vacuously, masking a broken
    // stimulus as a successful negative result.
    if (!injectDetails.value(QStringLiteral("sent")).toBool(false)) {
        if (details) {
            details->insert(QStringLiteral("touch_inject_details"), injectDetails);
        }
        if (errorOut) {
            const QString injectError = injectDetails.value(QStringLiteral("error")).toString();
            *errorOut = injectError.isEmpty()
                ? QStringLiteral("Touch drag path was not delivered (sent=false)")
                : QStringLiteral("Touch drag path was not delivered: %1").arg(injectError);
        }
        return false;
    }

    QJsonObject directDetails;
    performTouchDragPathViaGestureAction(directShortcut, pathPoints, &directDetails);

    sleepWithEvents(settleMs);

    const bool blocked = !overlayVisible(mainWindow, overlayObjectName);

    if (details) {
        details->insert(QStringLiteral("overlay_object_name"), overlayObjectName);
        details->insert(QStringLiteral("settle_ms"), settleMs);
        details->insert(QStringLiteral("step_ms"), stepMs);
        details->insert(QStringLiteral("input_manager_attempted"), true);
        details->insert(QStringLiteral("direct_action_attempted"), true);
        details->insert(QStringLiteral("touch_inject_details"), injectDetails);
        details->insert(QStringLiteral("direct_action_details"), directDetails);
        details->insert(QStringLiteral("blocked"), blocked);
        details->insert(QStringLiteral("visible"), overlayVisible(mainWindow, overlayObjectName));
    }

    if (!blocked && errorOut) {
        *errorOut = QStringLiteral("Overlay '%1' appeared unexpectedly").arg(overlayObjectName);
    }

    return blocked;
}

bool touchHoldOverlayNoChange(KisMainWindow *mainWindow,
                              int fingerCount,
                              const QJsonObject &posObj,
                              const QString &overlayObjectName,
                              int settleMs,
                              const QString &directActionName,
                              QJsonObject *details,
                              QString *errorOut)
{
    QWidget *canvasWidget = nullptr;
    QVector<QPointF> localPoints;
    QVector<QPointF> globalPoints;
    if (!buildTouchHoldPoints(mainWindow, fingerCount, posObj, &canvasWidget, &localPoints, &globalPoints, errorOut)) {
        return false;
    }

    bool visibleViaInputManager = false;
    bool visibleViaDirectAction = false;

    QJsonObject injectDetails;
    {
        QTouchDevice *device = touchDevice();
        if (!device) {
            if (errorOut) {
                *errorOut = QStringLiteral("touch.hold: could not allocate touch device");
            }
            return false;
        }

        const QList<QTouchEvent::TouchPoint> beginPoints =
            touchPointsForPositions(localPoints, globalPoints, Qt::TouchPointPressed);
        QTouchEvent beginEvent(QEvent::TouchBegin, device, Qt::NoModifier, Qt::TouchPointPressed, beginPoints);
        QApplication::sendEvent(canvasWidget, &beginEvent);
        QApplication::processEvents();

        sleepWithEvents(settleMs);
        visibleViaInputManager = overlayVisible(mainWindow, overlayObjectName);

        const QList<QTouchEvent::TouchPoint> endPoints =
            touchPointsForPositions(localPoints, globalPoints, Qt::TouchPointReleased);
        QTouchEvent endEvent(QEvent::TouchEnd, device, Qt::NoModifier, Qt::TouchPointReleased, endPoints);
        QApplication::sendEvent(canvasWidget, &endEvent);
        QApplication::processEvents();

        injectDetails.insert(QStringLiteral("touch_sent"), true);
        injectDetails.insert(QStringLiteral("visible_during_hold"), visibleViaInputManager);
    }

    QJsonObject directDetails;
    {
        hideOverlay(mainWindow, overlayObjectName, nullptr);

        const QString normalized = directActionName.trimmed().toLower();
        if (normalized.isEmpty()) {
            // no direct action attempt
        } else if (normalized == QStringLiteral("quickmenu") || normalized == QStringLiteral("touch_quickmenu")) {
            QString localError;

            QTouchDevice *device = touchDevice();
            if (!device) {
                if (errorOut) {
                    *errorOut = QStringLiteral("touch.hold: could not allocate touch device");
                }
                return false;
            }

            const QList<QTouchEvent::TouchPoint> beginPoints =
                touchPointsForPositions(localPoints, globalPoints, Qt::TouchPointPressed);
            QTouchEvent beginEvent(QEvent::TouchBegin, device, Qt::NoModifier, Qt::TouchPointPressed, beginPoints);

            KisTouchQuickMenuAction action;
            action.begin(0, &beginEvent);
            QApplication::processEvents();

            sleepWithEvents(settleMs);
            visibleViaDirectAction = overlayVisible(mainWindow, overlayObjectName);

            const QList<QTouchEvent::TouchPoint> endPoints =
                touchPointsForPositions(localPoints, globalPoints, Qt::TouchPointReleased);
            QTouchEvent endEvent(QEvent::TouchEnd, device, Qt::NoModifier, Qt::TouchPointReleased, endPoints);
            action.end(&endEvent);
            QApplication::processEvents();

            directDetails.insert(QStringLiteral("performed"), true);
            directDetails.insert(QStringLiteral("visible_during_hold"), visibleViaDirectAction);

            if (!localError.isEmpty()) {
                directDetails.insert(QStringLiteral("error"), localError);
            }
        } else {
            if (errorOut) {
                *errorOut = QStringLiteral("Unknown direct_action: %1").arg(directActionName);
            }
            return false;
        }
    }

    const bool blocked = !visibleViaInputManager && !visibleViaDirectAction;

    if (details) {
        details->insert(QStringLiteral("fingers"), fingerCount);
        details->insert(QStringLiteral("overlay_object_name"), overlayObjectName);
        details->insert(QStringLiteral("settle_ms"), settleMs);
        details->insert(QStringLiteral("input_manager_attempted"), true);
        details->insert(QStringLiteral("direct_action_attempted"), !directActionName.trimmed().isEmpty());
        details->insert(QStringLiteral("direct_action"), directActionName);
        details->insert(QStringLiteral("touch_inject_details"), injectDetails);
        details->insert(QStringLiteral("direct_action_details"), directDetails);
        details->insert(QStringLiteral("blocked"), blocked);
        details->insert(QStringLiteral("visible"), overlayVisible(mainWindow, overlayObjectName));
    }

    if (!blocked && errorOut) {
        *errorOut = QStringLiteral("Overlay '%1' appeared unexpectedly").arg(overlayObjectName);
    }

    return blocked;
}

} // namespace KisTouchSmokeScriptRunnerDetail
