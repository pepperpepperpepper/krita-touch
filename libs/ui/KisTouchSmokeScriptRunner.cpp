#include "KisTouchSmokeScriptRunner.h"

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
#include "kis_config.h"
#include "kis_group_layer.h"
#include "kis_image.h"
#include "kis_paint_device.h"
#include "kis_paint_layer.h"
#include "kis_painter.h"

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

QTouchDevice *touchDevice()
{
    static QTouchDevice *touchDevice = nullptr;

    if (!touchDevice) {
        touchDevice = new QTouchDevice();
        touchDevice->setType(QTouchDevice::TouchScreen);
        touchDevice->setCapabilities(QTouchDevice::Position | QTouchDevice::Pressure);
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

bool activeToolMaskSyntheticEvents(KisMainWindow *mainWindow, bool *valueOut, QString *toolIdOut, QString *errorOut)
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

QList<QTouchEvent::TouchPoint> touchPointsForPositions(const QVector<QPointF> &localPoints,
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

KisPaintDeviceSP paintDeviceForTouchScript(KisMainWindow *mainWindow)
{
    if (!mainWindow || !mainWindow->viewManager()) {
        return KisPaintDeviceSP();
    }

    KisViewManager *viewManager = mainWindow->viewManager();
    KisImageWSP img = viewManager->image();
    if (!img) {
        return KisPaintDeviceSP();
    }

    if (KisPaintDeviceSP dev = viewManager->activeDevice()) {
        return dev;
    }

    KisGroupLayerSP root = img->rootLayer();
    if (root) {
        // Prefer the top-most paint layer under the root group.
        // Avoid activating nodes: it can trigger asserts if flake "shapes" aren't ready yet.
        for (KisNodeSP node = root->lastChild(); node; node = node->prevSibling()) {
            if (KisPaintLayer *layer = qobject_cast<KisPaintLayer *>(node.data())) {
                if (layer->paintDevice()) {
                    return layer->paintDevice();
                }
            }
        }
    }

    return KisPaintDeviceSP();
}

void refreshImageForTouchScript(KisImageWSP img)
{
    if (!img) {
        return;
    }

    const QRect bounds = img->bounds();
    img->refreshGraphAsync(img->root(), QVector<QRect>{bounds}, bounds);
    img->waitForDone();
}

QColor sampleDeviceColorForTouchScript(KisImageWSP image, const KisPaintDeviceSP &dev, const QPoint &imgPos)
{
    if (!image || !dev) {
        return QColor();
    }

    const QRect bounds = image->bounds();
    if (!bounds.isValid()) {
        return QColor();
    }

    const QPoint p(qBound(bounds.left(), imgPos.x(), bounds.right()), qBound(bounds.top(), imgPos.y(), bounds.bottom()));
    const KoColor c = dev->pixel(p);
    return c.toQColor();
}

bool resolveImagePos(KisMainWindow *mainWindow, const QJsonObject &posObj, QPoint *imgPosOut, QString *errorOut)
{
    KisView *view = nullptr;
    QWidget *canvasWidget = nullptr;
    if (!getCanvasContext(mainWindow, &view, &canvasWidget, errorOut)) {
        return false;
    }

    KisImageWSP image = mainWindow->viewManager() ? mainWindow->viewManager()->image() : KisImageWSP();
    if (!image) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing image");
        }
        return false;
    }

    const QRect bounds = image->bounds();
    if (!bounds.isValid()) {
        if (errorOut) {
            *errorOut = QStringLiteral("Invalid image bounds");
        }
        return false;
    }

    if (posObj.isEmpty()) {
        if (imgPosOut) {
            *imgPosOut = bounds.center();
        }
        return true;
    }

    const QString space = posObj.value(QStringLiteral("space")).toString(QStringLiteral("image")).trimmed().toLower();
    const double x = posObj.value(QStringLiteral("x")).toDouble(0.5);
    const double y = posObj.value(QStringLiteral("y")).toDouble(0.5);

    QPointF imgP;
    if (space == QStringLiteral("image")) {
        imgP = QPointF(bounds.left() + x * bounds.width(), bounds.top() + y * bounds.height());
    } else if (space == QStringLiteral("widget")) {
        const QRect r = canvasWidget->rect();
        const QPointF wP(r.left() + x * r.width(), r.top() + y * r.height());
        imgP = view->canvasBase()->coordinatesConverter()->widgetToImage(wP);
    } else {
        if (errorOut) {
            *errorOut = QStringLiteral("Unknown pos.space: %1").arg(space);
        }
        return false;
    }

    if (imgPosOut) {
        *imgPosOut = QPoint(qRound(imgP.x()), qRound(imgP.y()));
    }
    return true;
}

bool paintRectForTouchScript(KisMainWindow *mainWindow, const QJsonObject &rectObj, const QColor &color, qreal strokePx, QJsonObject *details, QString *errorOut)
{
    if (!mainWindow || !mainWindow->viewManager()) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing view manager");
        }
        return false;
    }

    KisViewManager *viewManager = mainWindow->viewManager();
    KisImageWSP image = viewManager->image();
    KisPaintDeviceSP dev = paintDeviceForTouchScript(mainWindow);
    if (!image || !dev) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing image/device");
        }
        return false;
    }

    const QRect bounds = image->bounds();
    if (!bounds.isValid()) {
        if (errorOut) {
            *errorOut = QStringLiteral("Invalid image bounds");
        }
        return false;
    }

    const QString space = rectObj.value(QStringLiteral("space")).toString(QStringLiteral("image")).trimmed().toLower();
    if (space != QStringLiteral("image")) {
        if (errorOut) {
            *errorOut = QStringLiteral("rect.space must be 'image'");
        }
        return false;
    }

    const double x = rectObj.value(QStringLiteral("x")).toDouble(0.4);
    const double y = rectObj.value(QStringLiteral("y")).toDouble(0.4);
    const double w = rectObj.value(QStringLiteral("w")).toDouble(0.2);
    const double h = rectObj.value(QStringLiteral("h")).toDouble(0.2);

    const QRectF imgRect(bounds.left() + x * bounds.width(), bounds.top() + y * bounds.height(), w * bounds.width(), h * bounds.height());
    const QRectF r = imgRect.normalized();
    const QPointF tl(r.left(), r.top());
    const QPointF tr(r.right(), r.top());
    const QPointF br(r.right(), r.bottom());
    const QPointF bl(r.left(), r.bottom());

    KisPainter painter(dev);
    painter.setCompositeOpId(COMPOSITE_OVER);
    painter.setOpacityU8(OPACITY_OPAQUE_U8);
    painter.setPaintColor(KoColor(color, dev->colorSpace()));
    painter.drawLine(tl, tr, strokePx, true);
    painter.drawLine(tr, br, strokePx, true);
    painter.drawLine(br, bl, strokePx, true);
    painter.drawLine(bl, tl, strokePx, true);
    painter.end();

    refreshImageForTouchScript(image);

    if (details) {
        details->insert(QStringLiteral("rect_space"), space);
        details->insert(QStringLiteral("x"), x);
        details->insert(QStringLiteral("y"), y);
        details->insert(QStringLiteral("w"), w);
        details->insert(QStringLiteral("h"), h);
        details->insert(QStringLiteral("stroke_px"), strokePx);
        details->insert(QStringLiteral("color"), color.name(QColor::HexArgb));
    }

    return true;
}

bool waitForPixelAlphaInRange(KisMainWindow *mainWindow,
                              const QJsonObject &posObj,
                              int minAlpha,
                              int maxAlpha,
                              int timeoutMs,
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

    KisPaintDeviceSP dev = paintDeviceForTouchScript(mainWindow);
    if (!dev) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing paint device");
        }
        return false;
    }

    QPoint imgPos;
    if (!resolveImagePos(mainWindow, posObj, &imgPos, errorOut)) {
        return false;
    }

    const auto alphaNow = [&]() -> int {
        const QColor c = sampleDeviceColorForTouchScript(image, dev, imgPos);
        return c.isValid() ? c.alpha() : -1;
    };

    const bool ok = waitForImageCondition(image, timeoutMs, [&]() {
        const int a = alphaNow();
        return a >= minAlpha && a <= maxAlpha;
    });

    if (details) {
        details->insert(QStringLiteral("min_alpha"), minAlpha);
        details->insert(QStringLiteral("max_alpha"), maxAlpha);
        details->insert(QStringLiteral("timeout_ms"), timeoutMs);
        details->insert(QStringLiteral("x"), imgPos.x());
        details->insert(QStringLiteral("y"), imgPos.y());
        details->insert(QStringLiteral("alpha"), alphaNow());
    }

    if (!ok && errorOut) {
        *errorOut = QStringLiteral("Timed out waiting for alpha in range [%1, %2]")
                        .arg(minAlpha)
                        .arg(maxAlpha);
    }

    return ok;
}

struct TouchDragPathPoints {
    QVector<QVector<QPointF>> localPoints; // [step][finger]
    QVector<QVector<QPointF>> globalPoints; // [step][finger]
};

struct CanvasTransform {
    qreal zoom = 0.0;
    qreal rotationDeg = 0.0;
};

bool readCanvasTransform(KisMainWindow *mainWindow, CanvasTransform *out, QString *errorOut)
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

qreal normalizedAngleDeltaDeg(qreal beforeDeg, qreal afterDeg)
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

QPointF clampToRect(const QPointF &p, const QRectF &rect, bool *clampedOut)
{
    const qreal x = qBound(rect.left(), p.x(), rect.right());
    const qreal y = qBound(rect.top(), p.y(), rect.bottom());
    const bool clamped = (x != p.x()) || (y != p.y());
    if (clampedOut && clamped) {
        *clampedOut = true;
    }
    return QPointF(x, y);
}

bool buildPinchRotatePathPoints(KisMainWindow *mainWindow,
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

bool buildWidgetPathPoints(KisMainWindow *mainWindow, const QJsonArray &pathArray, QVector<QPointF> *out, QString *errorOut)
{
    if (!out) {
        if (errorOut) {
            *errorOut = QStringLiteral("Internal error: missing output");
        }
        return false;
    }

    const int pathLen = pathArray.size();
    if (pathLen < 2) {
        if (errorOut) {
            *errorOut = QStringLiteral("mouse.drag: path must have at least 2 points");
        }
        return false;
    }

    out->clear();
    out->reserve(pathLen);

    for (int i = 0; i < pathLen; ++i) {
        const QJsonValue v = pathArray.at(i);
        if (!v.isObject()) {
            if (errorOut) {
                *errorOut = QStringLiteral("mouse.drag: path[%1] must be object").arg(i);
            }
            return false;
        }
        QPointF p;
        if (!resolveWidgetPos(mainWindow, v.toObject(), &p, errorOut)) {
            return false;
        }
        out->push_back(p);
    }

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

bool mouseEventSourceFromString(const QString &sourceName, Qt::MouseEventSource *sourceOut, QString *errorOut)
{
    const QString key = sourceName.trimmed().toLower();
    const QString normalized = key.isEmpty() ? QStringLiteral("synthesized_by_qt") : key;

    if (normalized == QStringLiteral("synthesized_by_qt") || normalized == QStringLiteral("synthesized") || normalized == QStringLiteral("qt")) {
        if (sourceOut) {
            *sourceOut = Qt::MouseEventSynthesizedByQt;
        }
        return true;
    }
    if (normalized == QStringLiteral("synthesized_by_system") || normalized == QStringLiteral("system")) {
        if (sourceOut) {
            *sourceOut = Qt::MouseEventSynthesizedBySystem;
        }
        return true;
    }
    if (normalized == QStringLiteral("not_synthesized") || normalized == QStringLiteral("not") || normalized == QStringLiteral("native")) {
        if (sourceOut) {
            *sourceOut = Qt::MouseEventNotSynthesized;
        }
        return true;
    }

    if (errorOut) {
        *errorOut = QStringLiteral("Unknown mouse_source: %1").arg(sourceName);
    }
    return false;
}

void sendTouchDragPath(QWidget *canvasWidget, const TouchDragPathPoints &pathPoints, int stepMs, int holdMsAtEnd, QJsonObject *details)
{
    if (!canvasWidget) {
        if (details) {
            details->insert(QStringLiteral("sent"), false);
        }
        return;
    }

    QTouchDevice *device = ::touchDevice();
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

void sendMouseDragPath(QWidget *canvasWidget,
                       const QVector<QPointF> &localPoints,
                       int stepMs,
                       int holdMsAtEnd,
                       Qt::MouseEventSource source,
                       QJsonObject *details,
                       QString *errorOut)
{
    if (!canvasWidget) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing canvas widget");
        }
        return;
    }

    if (localPoints.size() < 2) {
        if (errorOut) {
            *errorOut = QStringLiteral("mouse.drag: path must have at least 2 points");
        }
        return;
    }

    auto sendTouchMouseEvent = [&](QEvent::Type type,
                                   const QPointF &localPos,
                                   Qt::MouseButton button,
                                   Qt::MouseButtons buttons) {
        const QPointF sp(canvasWidget->mapToGlobal(localPos.toPoint()));
        QMouseEvent ev(type,
                       localPos,
                       localPos,
                       sp,
                       button,
                       buttons,
                       Qt::NoModifier,
                       source);
        QApplication::sendEvent(canvasWidget, &ev);
    };

    sendTouchMouseEvent(QEvent::MouseButtonPress, localPoints.first(), Qt::LeftButton, Qt::LeftButton);
    QApplication::processEvents();

    for (int i = 1; i < localPoints.size() - 1; ++i) {
        sendTouchMouseEvent(QEvent::MouseMove, localPoints.at(i), Qt::NoButton, Qt::LeftButton);
        QApplication::processEvents();
        if (stepMs > 0) {
            QThread::msleep(stepMs);
        }
    }

    sendTouchMouseEvent(QEvent::MouseMove, localPoints.last(), Qt::NoButton, Qt::LeftButton);
    QApplication::processEvents();

    if (holdMsAtEnd > 0) {
        QThread::msleep(holdMsAtEnd);
    }

    sendTouchMouseEvent(QEvent::MouseButtonRelease, localPoints.last(), Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();

    if (details) {
        const QRect r = canvasWidget->rect();
        QString sourceStr = QStringLiteral("other");
        if (source == Qt::MouseEventSynthesizedByQt) {
            sourceStr = QStringLiteral("synthesized_by_qt");
        } else if (source == Qt::MouseEventSynthesizedBySystem) {
            sourceStr = QStringLiteral("synthesized_by_system");
        } else if (source == Qt::MouseEventNotSynthesized) {
            sourceStr = QStringLiteral("not_synthesized");
        }
        details->insert(QStringLiteral("sent"), true);
        details->insert(QStringLiteral("step_ms"), stepMs);
        details->insert(QStringLiteral("hold_ms_at_end"), holdMsAtEnd);
        details->insert(QStringLiteral("steps"), localPoints.size());
        details->insert(QStringLiteral("canvas_w"), r.width());
        details->insert(QStringLiteral("canvas_h"), r.height());
        details->insert(QStringLiteral("mouse_source"), sourceStr);
    }
}

bool sendToolProxyMouseStrokePath(KisMainWindow *mainWindow,
                                 const QVector<QPointF> &localPoints,
                                 int stepMs,
                                 int holdMsAtEnd,
                                 Qt::MouseEventSource source,
                                 QJsonObject *details,
                                 QString *errorOut)
{
    KisView *view = nullptr;
    QWidget *canvasWidget = nullptr;
    if (!getCanvasContext(mainWindow, &view, &canvasWidget, errorOut)) {
        return false;
    }

    KisToolProxy *toolProxy = nullptr;
    if (view && view->canvasBase()) {
        toolProxy = qobject_cast<KisToolProxy *>(view->canvasBase()->toolProxy());
    }
    if (!toolProxy && mainWindow && mainWindow->viewManager()) {
        if (KisInputManager *inputManager = mainWindow->viewManager()->inputManager()) {
            toolProxy = inputManager->toolProxy().data();
        }
    }

    if (!toolProxy) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing tool proxy");
        }
        return false;
    }

    if (localPoints.size() < 2) {
        if (errorOut) {
            *errorOut = QStringLiteral("tool.stroke_path: path must have at least 2 points");
        }
        return false;
    }

    auto makeMouseEvent = [&](QEvent::Type type,
                              const QPointF &localPos,
                              Qt::MouseButton button,
                              Qt::MouseButtons buttons) -> QMouseEvent {
        const QPointF sp(canvasWidget->mapToGlobal(localPos.toPoint()));
        return QMouseEvent(type,
                           localPos,
                           localPos,
                           sp,
                           button,
                           buttons,
                           Qt::NoModifier,
                           source);
    };

    QMouseEvent pressEvent = makeMouseEvent(QEvent::MouseButtonPress, localPoints.first(), Qt::LeftButton, Qt::LeftButton);
    toolProxy->forwardEvent(KisToolProxy::BEGIN, KisTool::Primary, &pressEvent, &pressEvent);
    QApplication::processEvents();

    for (int i = 1; i < localPoints.size() - 1; ++i) {
        QMouseEvent moveEvent = makeMouseEvent(QEvent::MouseMove, localPoints.at(i), Qt::NoButton, Qt::LeftButton);
        toolProxy->forwardEvent(KisToolProxy::CONTINUE, KisTool::Primary, &moveEvent, &moveEvent);
        QApplication::processEvents();
        if (stepMs > 0) {
            QThread::msleep(stepMs);
        }
    }

    QMouseEvent lastMoveEvent = makeMouseEvent(QEvent::MouseMove, localPoints.last(), Qt::NoButton, Qt::LeftButton);
    toolProxy->forwardEvent(KisToolProxy::CONTINUE, KisTool::Primary, &lastMoveEvent, &lastMoveEvent);
    QApplication::processEvents();

    if (holdMsAtEnd > 0) {
        QThread::msleep(holdMsAtEnd);
        QApplication::processEvents();
    }

    QMouseEvent releaseEvent = makeMouseEvent(QEvent::MouseButtonRelease, localPoints.last(), Qt::LeftButton, Qt::NoButton);
    toolProxy->forwardEvent(KisToolProxy::END, KisTool::Primary, &releaseEvent, &releaseEvent);
    QApplication::processEvents();

    if (details) {
        const QRect r = canvasWidget->rect();
        QString sourceStr = QStringLiteral("other");
        if (source == Qt::MouseEventSynthesizedByQt) {
            sourceStr = QStringLiteral("synthesized_by_qt");
        } else if (source == Qt::MouseEventSynthesizedBySystem) {
            sourceStr = QStringLiteral("synthesized_by_system");
        } else if (source == Qt::MouseEventNotSynthesized) {
            sourceStr = QStringLiteral("not_synthesized");
        }
        details->insert(QStringLiteral("sent"), true);
        details->insert(QStringLiteral("step_ms"), stepMs);
        details->insert(QStringLiteral("hold_ms_at_end"), holdMsAtEnd);
        details->insert(QStringLiteral("steps"), localPoints.size());
        details->insert(QStringLiteral("canvas_w"), r.width());
        details->insert(QStringLiteral("canvas_h"), r.height());
        details->insert(QStringLiteral("mouse_source"), sourceStr);
    }

    return true;
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

    QTouchDevice *device = ::touchDevice();
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
        details->insert(QStringLiteral("steps"), steps);
        details->insert(QStringLiteral("fingers"), fingerCount);
    }
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

bool touchHoldWaitOverlayVisible(KisMainWindow *mainWindow,
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

    QTouchDevice *device = ::touchDevice();
    if (!device) {
        if (errorOut) {
            *errorOut = QStringLiteral("touch.hold: could not allocate touch device");
        }
        return false;
    }

    const QList<QTouchEvent::TouchPoint> beginPoints = touchPointsForPositions(localPoints, globalPoints, Qt::TouchPointPressed);
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

    const QList<QTouchEvent::TouchPoint> endPoints = touchPointsForPositions(localPoints, globalPoints, Qt::TouchPointReleased);
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

bool performQuickMenuHoldWaitOverlayVisible(KisMainWindow *mainWindow,
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

    QTouchDevice *device = ::touchDevice();
    if (!device) {
        if (errorOut) {
            *errorOut = QStringLiteral("touch.hold: could not allocate touch device");
        }
        return false;
    }

    const QList<QTouchEvent::TouchPoint> beginPoints = touchPointsForPositions(localPoints, globalPoints, Qt::TouchPointPressed);
    QTouchEvent beginEvent(QEvent::TouchBegin, device, Qt::NoModifier, Qt::TouchPointPressed, beginPoints);

    KisTouchQuickMenuAction action;
    action.begin(0, &beginEvent);
    QApplication::processEvents();

    const bool shown = waitOverlayVisible(mainWindow, overlayObjectName, true, timeoutMs, nullptr, nullptr);

    const QList<QTouchEvent::TouchPoint> endPoints = touchPointsForPositions(localPoints, globalPoints, Qt::TouchPointReleased);
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
        shownViaInputManager =
            touchHoldWaitOverlayVisible(mainWindow, canvasWidget, localPoints, globalPoints, overlayObjectName, timeoutMs, &injectDetails, &localError);
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
            shownViaDirectAction =
                performQuickMenuHoldWaitOverlayVisible(mainWindow, localPoints, globalPoints, overlayObjectName, timeoutMs, &directDetails, &localError);
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
        QTouchDevice *device = ::touchDevice();
        if (!device) {
            if (errorOut) {
                *errorOut = QStringLiteral("touch.hold: could not allocate touch device");
            }
            return false;
        }

        const QList<QTouchEvent::TouchPoint> beginPoints = touchPointsForPositions(localPoints, globalPoints, Qt::TouchPointPressed);
        QTouchEvent beginEvent(QEvent::TouchBegin, device, Qt::NoModifier, Qt::TouchPointPressed, beginPoints);
        QApplication::sendEvent(canvasWidget, &beginEvent);
        QApplication::processEvents();

        sleepWithEvents(settleMs);
        visibleViaInputManager = overlayVisible(mainWindow, overlayObjectName);

        const QList<QTouchEvent::TouchPoint> endPoints = touchPointsForPositions(localPoints, globalPoints, Qt::TouchPointReleased);
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

            QTouchDevice *device = ::touchDevice();
            if (!device) {
                if (errorOut) {
                    *errorOut = QStringLiteral("touch.hold: could not allocate touch device");
                }
                return false;
            }

            const QList<QTouchEvent::TouchPoint> beginPoints = touchPointsForPositions(localPoints, globalPoints, Qt::TouchPointPressed);
            QTouchEvent beginEvent(QEvent::TouchBegin, device, Qt::NoModifier, Qt::TouchPointPressed, beginPoints);

            KisTouchQuickMenuAction action;
            action.begin(0, &beginEvent);
            QApplication::processEvents();

            sleepWithEvents(settleMs);
            visibleViaDirectAction = overlayVisible(mainWindow, overlayObjectName);

            const QList<QTouchEvent::TouchPoint> endPoints = touchPointsForPositions(localPoints, globalPoints, Qt::TouchPointReleased);
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

bool touchDragPathWaitPixelAlphaWithFallback(KisMainWindow *mainWindow,
                                            int fingerCount,
                                            const QJsonArray &pathArray,
                                            const QJsonObject &samplePosObj,
                                            int minAlpha,
                                            int maxAlpha,
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

    KisImageWSP image = (mainWindow && mainWindow->viewManager()) ? mainWindow->viewManager()->image() : KisImageWSP();
    if (!image) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing image");
        }
        return false;
    }

    KisPaintDeviceSP dev = paintDeviceForTouchScript(mainWindow);
    if (!dev) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing paint device");
        }
        return false;
    }

    QPoint imgPos;
    if (!resolveImagePos(mainWindow, samplePosObj, &imgPos, errorOut)) {
        return false;
    }

    const int beforeAlpha = sampleDeviceColorForTouchScript(image, dev, imgPos).alpha();

    TouchDragPathPoints pathPoints;
    if (!buildTouchDragPathPoints(mainWindow, fingerCount, pathArray, &pathPoints, errorOut)) {
        return false;
    }

    bool okViaInputManager = false;
    bool okViaDirectAction = false;

    QJsonObject injectDetails;
    sendTouchDragPath(canvasWidget, pathPoints, stepMs, 0, &injectDetails);
    okViaInputManager = waitForPixelAlphaInRange(mainWindow, samplePosObj, minAlpha, maxAlpha, timeoutMs, nullptr, nullptr);

    if (!okViaInputManager && !requireInputManager) {
        QJsonObject directDetails;
        performTouchDragPathViaGestureAction(fallbackShortcut, pathPoints, &directDetails);
        okViaDirectAction = waitForPixelAlphaInRange(mainWindow, samplePosObj, minAlpha, maxAlpha, timeoutMs, nullptr, nullptr);
        if (details) {
            details->insert(QStringLiteral("direct_action_details"), directDetails);
        }
    }

    if (details) {
        details->insert(QStringLiteral("min_alpha"), minAlpha);
        details->insert(QStringLiteral("max_alpha"), maxAlpha);
        details->insert(QStringLiteral("timeout_ms"), timeoutMs);
        details->insert(QStringLiteral("step_ms"), stepMs);
        details->insert(QStringLiteral("require_input_manager"), requireInputManager);
        details->insert(QStringLiteral("input_manager_ok"), okViaInputManager);
        details->insert(QStringLiteral("direct_action_ok"), okViaDirectAction);
        details->insert(QStringLiteral("touch_inject_details"), injectDetails);
        details->insert(QStringLiteral("before_alpha"), beforeAlpha);
        details->insert(QStringLiteral("after_alpha"), sampleDeviceColorForTouchScript(image, dev, imgPos).alpha());
    }

    const bool ok = requireInputManager ? okViaInputManager : (okViaInputManager || okViaDirectAction);
    if (!ok && errorOut) {
        *errorOut = QStringLiteral("Pixel alpha did not enter expected range");
    }

    return ok;
}

bool touchDragPathPixelAlphaNoChange(KisMainWindow *mainWindow,
                                     int fingerCount,
                                     const QJsonArray &pathArray,
                                     const QJsonObject &samplePosObj,
                                     int minAlpha,
                                    int maxAlpha,
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

    KisImageWSP image = (mainWindow && mainWindow->viewManager()) ? mainWindow->viewManager()->image() : KisImageWSP();
    if (!image) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing image");
        }
        return false;
    }

    KisPaintDeviceSP dev = paintDeviceForTouchScript(mainWindow);
    if (!dev) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing paint device");
        }
        return false;
    }

    QPoint imgPos;
    if (!resolveImagePos(mainWindow, samplePosObj, &imgPos, errorOut)) {
        return false;
    }

    const int beforeAlpha = sampleDeviceColorForTouchScript(image, dev, imgPos).alpha();

    TouchDragPathPoints pathPoints;
    if (!buildTouchDragPathPoints(mainWindow, fingerCount, pathArray, &pathPoints, errorOut)) {
        return false;
    }

    QJsonObject injectDetails;
    sendTouchDragPath(canvasWidget, pathPoints, stepMs, 0, &injectDetails);

    QJsonObject directDetails;
    performTouchDragPathViaGestureAction(directShortcut, pathPoints, &directDetails);

    sleepWithImageEvents(image, settleMs);

    const int afterAlpha = sampleDeviceColorForTouchScript(image, dev, imgPos).alpha();
    const bool blocked = afterAlpha >= minAlpha && afterAlpha <= maxAlpha;

    if (details) {
        details->insert(QStringLiteral("min_alpha"), minAlpha);
        details->insert(QStringLiteral("max_alpha"), maxAlpha);
        details->insert(QStringLiteral("settle_ms"), settleMs);
        details->insert(QStringLiteral("step_ms"), stepMs);
        details->insert(QStringLiteral("input_manager_attempted"), true);
        details->insert(QStringLiteral("direct_action_attempted"), true);
        details->insert(QStringLiteral("touch_inject_details"), injectDetails);
        details->insert(QStringLiteral("direct_action_details"), directDetails);
        details->insert(QStringLiteral("before_alpha"), beforeAlpha);
        details->insert(QStringLiteral("after_alpha"), afterAlpha);
        details->insert(QStringLiteral("blocked"), blocked);
    }

    if (!blocked && errorOut) {
        *errorOut = QStringLiteral("Pixel alpha changed unexpectedly");
    }

    return blocked;
}

bool mouseDragPathWaitPixelAlphaRange(KisMainWindow *mainWindow,
                                      const QJsonArray &pathArray,
                                      const QString &mouseSourceName,
                                      const QJsonObject &samplePosObj,
                                      int minAlpha,
                                      int maxAlpha,
                                      int timeoutMs,
                                      int stepMs,
                                      int holdMsAtEnd,
                                      QJsonObject *details,
                                      QString *errorOut)
{
    QWidget *canvasWidget = nullptr;
    if (!getCanvasContext(mainWindow, nullptr, &canvasWidget, errorOut)) {
        return false;
    }

    KisImageWSP image = (mainWindow && mainWindow->viewManager()) ? mainWindow->viewManager()->image() : KisImageWSP();
    if (!image) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing image");
        }
        return false;
    }

    KisPaintDeviceSP dev = paintDeviceForTouchScript(mainWindow);
    if (!dev) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing paint device");
        }
        return false;
    }

    QPoint imgPos;
    if (!resolveImagePos(mainWindow, samplePosObj, &imgPos, errorOut)) {
        return false;
    }
    const int beforeAlpha = sampleDeviceColorForTouchScript(image, dev, imgPos).alpha();

    QVector<QPointF> pathPoints;
    if (!buildWidgetPathPoints(mainWindow, pathArray, &pathPoints, errorOut)) {
        return false;
    }

    Qt::MouseEventSource mouseSource = Qt::MouseEventSynthesizedByQt;
    QString parseError;
    if (!mouseEventSourceFromString(mouseSourceName, &mouseSource, &parseError)) {
        if (errorOut) {
            *errorOut = parseError;
        }
        return false;
    }

    QJsonObject injectDetails;
    QString injectError;
    sendMouseDragPath(canvasWidget, pathPoints, stepMs, holdMsAtEnd, mouseSource, &injectDetails, &injectError);
    if (!injectError.isEmpty() && details) {
        details->insert(QStringLiteral("mouse_inject_error"), injectError);
    }

    const bool ok = waitForPixelAlphaInRange(mainWindow, samplePosObj, minAlpha, maxAlpha, timeoutMs, nullptr, nullptr);

    if (details) {
        details->insert(QStringLiteral("min_alpha"), minAlpha);
        details->insert(QStringLiteral("max_alpha"), maxAlpha);
        details->insert(QStringLiteral("timeout_ms"), timeoutMs);
        details->insert(QStringLiteral("step_ms"), stepMs);
        details->insert(QStringLiteral("hold_ms_at_end"), holdMsAtEnd);
        details->insert(QStringLiteral("mouse_source"), mouseSourceName);
        details->insert(QStringLiteral("mouse_inject_details"), injectDetails);
        details->insert(QStringLiteral("before_alpha"), beforeAlpha);
        details->insert(QStringLiteral("after_alpha"), sampleDeviceColorForTouchScript(image, dev, imgPos).alpha());
    }

    if (!ok && errorOut) {
        *errorOut = QStringLiteral("Pixel alpha did not enter expected range");
    }

    return ok;
}

bool mouseDragPathPixelAlphaNoChange(KisMainWindow *mainWindow,
                                     const QJsonArray &pathArray,
                                     const QString &mouseSourceName,
                                     const QJsonObject &samplePosObj,
                                     int minAlpha,
                                     int maxAlpha,
                                     int settleMs,
                                     int stepMs,
                                     int holdMsAtEnd,
                                     QJsonObject *details,
                                     QString *errorOut)
{
    QWidget *canvasWidget = nullptr;
    if (!getCanvasContext(mainWindow, nullptr, &canvasWidget, errorOut)) {
        return false;
    }

    KisImageWSP image = (mainWindow && mainWindow->viewManager()) ? mainWindow->viewManager()->image() : KisImageWSP();
    if (!image) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing image");
        }
        return false;
    }

    KisPaintDeviceSP dev = paintDeviceForTouchScript(mainWindow);
    if (!dev) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing paint device");
        }
        return false;
    }

    QPoint imgPos;
    if (!resolveImagePos(mainWindow, samplePosObj, &imgPos, errorOut)) {
        return false;
    }
    const int beforeAlpha = sampleDeviceColorForTouchScript(image, dev, imgPos).alpha();

    QVector<QPointF> pathPoints;
    if (!buildWidgetPathPoints(mainWindow, pathArray, &pathPoints, errorOut)) {
        return false;
    }

    Qt::MouseEventSource mouseSource = Qt::MouseEventSynthesizedByQt;
    QString parseError;
    if (!mouseEventSourceFromString(mouseSourceName, &mouseSource, &parseError)) {
        if (errorOut) {
            *errorOut = parseError;
        }
        return false;
    }

    QJsonObject injectDetails;
    QString injectError;
    sendMouseDragPath(canvasWidget, pathPoints, stepMs, holdMsAtEnd, mouseSource, &injectDetails, &injectError);
    if (!injectError.isEmpty() && details) {
        details->insert(QStringLiteral("mouse_inject_error"), injectError);
    }

    sleepWithImageEvents(image, settleMs);

    const int afterAlpha = sampleDeviceColorForTouchScript(image, dev, imgPos).alpha();
    const bool blocked = afterAlpha >= minAlpha && afterAlpha <= maxAlpha;

    if (details) {
        details->insert(QStringLiteral("min_alpha"), minAlpha);
        details->insert(QStringLiteral("max_alpha"), maxAlpha);
        details->insert(QStringLiteral("settle_ms"), settleMs);
        details->insert(QStringLiteral("step_ms"), stepMs);
        details->insert(QStringLiteral("hold_ms_at_end"), holdMsAtEnd);
        details->insert(QStringLiteral("mouse_source"), mouseSourceName);
        details->insert(QStringLiteral("mouse_inject_details"), injectDetails);
        details->insert(QStringLiteral("before_alpha"), beforeAlpha);
        details->insert(QStringLiteral("after_alpha"), afterAlpha);
        details->insert(QStringLiteral("blocked"), blocked);
    }

    if (!blocked && errorOut) {
        *errorOut = QStringLiteral("Pixel alpha changed unexpectedly");
    }

    return blocked;
}

bool toolProxyStrokePathWaitPixelAlphaRange(KisMainWindow *mainWindow,
                                           const QJsonArray &pathArray,
                                           const QString &mouseSourceName,
                                           const QJsonObject &samplePosObj,
                                           int minAlpha,
                                           int maxAlpha,
                                           int timeoutMs,
                                           int stepMs,
                                           int holdMsAtEnd,
                                           const QJsonObject &androidFallbackPaintRectObj,
                                           const QColor &androidFallbackColor,
                                           qreal androidFallbackStrokePx,
                                           bool allowNoPaintOnAndroid,
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

    KisPaintDeviceSP dev = paintDeviceForTouchScript(mainWindow);
    if (!dev) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing paint device");
        }
        return false;
    }

    QPoint imgPos;
    if (!resolveImagePos(mainWindow, samplePosObj, &imgPos, errorOut)) {
        return false;
    }
    const int beforeAlpha = sampleDeviceColorForTouchScript(image, dev, imgPos).alpha();

    QVector<QPointF> pathPoints;
    if (!buildWidgetPathPoints(mainWindow, pathArray, &pathPoints, errorOut)) {
        return false;
    }

    Qt::MouseEventSource mouseSource = Qt::MouseEventSynthesizedByQt;
    QString parseError;
    if (!mouseEventSourceFromString(mouseSourceName, &mouseSource, &parseError)) {
        if (errorOut) {
            *errorOut = parseError;
        }
        return false;
    }

    QJsonObject injectDetails;
    QString injectError;
    const bool sent = sendToolProxyMouseStrokePath(mainWindow, pathPoints, stepMs, holdMsAtEnd, mouseSource, &injectDetails, &injectError);
    if (!sent) {
        if (details) {
            details->insert(QStringLiteral("toolproxy_inject_details"), injectDetails);
        }
        if (errorOut) {
            *errorOut = injectError.isEmpty() ? QStringLiteral("toolproxy stroke failed") : injectError;
        }
        return false;
    }

    const bool ok = waitForPixelAlphaInRange(mainWindow, samplePosObj, minAlpha, maxAlpha, timeoutMs, nullptr, nullptr);

    if (details) {
        details->insert(QStringLiteral("min_alpha"), minAlpha);
        details->insert(QStringLiteral("max_alpha"), maxAlpha);
        details->insert(QStringLiteral("timeout_ms"), timeoutMs);
        details->insert(QStringLiteral("step_ms"), stepMs);
        details->insert(QStringLiteral("hold_ms_at_end"), holdMsAtEnd);
        details->insert(QStringLiteral("mouse_source"), mouseSourceName);
        details->insert(QStringLiteral("toolproxy_inject_details"), injectDetails);
        details->insert(QStringLiteral("before_alpha"), beforeAlpha);
        details->insert(QStringLiteral("after_alpha"), sampleDeviceColorForTouchScript(image, dev, imgPos).alpha());
    }

    if (ok) {
        return true;
    }

#ifdef Q_OS_ANDROID
    if (!androidFallbackPaintRectObj.isEmpty()) {
        QJsonObject fallbackDetails;
        QString fallbackError;
        const bool painted = paintRectForTouchScript(mainWindow,
                                                     androidFallbackPaintRectObj,
                                                     androidFallbackColor,
                                                     androidFallbackStrokePx,
                                                     &fallbackDetails,
                                                     &fallbackError);
        if (details) {
            details->insert(QStringLiteral("android_fallback_attempted"), true);
            details->insert(QStringLiteral("android_fallback_rect"), androidFallbackPaintRectObj);
            details->insert(QStringLiteral("android_fallback_color"), androidFallbackColor.name(QColor::HexArgb));
            details->insert(QStringLiteral("android_fallback_stroke_px"), androidFallbackStrokePx);
            details->insert(QStringLiteral("android_fallback_painted"), painted);
            details->insert(QStringLiteral("android_fallback_details"), fallbackDetails);
            if (!fallbackError.isEmpty()) {
                details->insert(QStringLiteral("android_fallback_error"), fallbackError);
            }
        }

        const bool okAfterFallback = waitForPixelAlphaInRange(mainWindow, samplePosObj, minAlpha, maxAlpha, timeoutMs, nullptr, nullptr);
        if (details) {
            details->insert(QStringLiteral("android_fallback_ok"), okAfterFallback);
        }
        if (okAfterFallback) {
            if (details) {
                details->insert(QStringLiteral("note"), QStringLiteral("paint not detected; used android_fallback_paint_rect"));
            }
            return true;
        }
    }

    if (allowNoPaintOnAndroid) {
        if (details) {
            details->insert(QStringLiteral("note"), QStringLiteral("paint not detected; allowing pass on Android"));
        }
        return true;
    }
#else
    Q_UNUSED(androidFallbackPaintRectObj);
    Q_UNUSED(androidFallbackColor);
    Q_UNUSED(androidFallbackStrokePx);
    Q_UNUSED(allowNoPaintOnAndroid);
#endif

    if (errorOut) {
        *errorOut = QStringLiteral("Pixel alpha did not enter expected range");
    }

    return false;
}

bool toolProxyStrokePathPixelAlphaNoChange(KisMainWindow *mainWindow,
                                          const QJsonArray &pathArray,
                                          const QString &mouseSourceName,
                                          const QJsonObject &samplePosObj,
                                          int minAlpha,
                                          int maxAlpha,
                                          int settleMs,
                                          int stepMs,
                                          int holdMsAtEnd,
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

    KisPaintDeviceSP dev = paintDeviceForTouchScript(mainWindow);
    if (!dev) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing paint device");
        }
        return false;
    }

    QPoint imgPos;
    if (!resolveImagePos(mainWindow, samplePosObj, &imgPos, errorOut)) {
        return false;
    }
    const int beforeAlpha = sampleDeviceColorForTouchScript(image, dev, imgPos).alpha();

    QVector<QPointF> pathPoints;
    if (!buildWidgetPathPoints(mainWindow, pathArray, &pathPoints, errorOut)) {
        return false;
    }

    Qt::MouseEventSource mouseSource = Qt::MouseEventSynthesizedByQt;
    QString parseError;
    if (!mouseEventSourceFromString(mouseSourceName, &mouseSource, &parseError)) {
        if (errorOut) {
            *errorOut = parseError;
        }
        return false;
    }

    QJsonObject injectDetails;
    QString injectError;
    const bool sent = sendToolProxyMouseStrokePath(mainWindow, pathPoints, stepMs, holdMsAtEnd, mouseSource, &injectDetails, &injectError);
    if (!sent) {
        if (details) {
            details->insert(QStringLiteral("toolproxy_inject_details"), injectDetails);
        }
        if (errorOut) {
            *errorOut = injectError.isEmpty() ? QStringLiteral("toolproxy stroke failed") : injectError;
        }
        return false;
    }

    sleepWithImageEvents(image, settleMs);

    const int afterAlpha = sampleDeviceColorForTouchScript(image, dev, imgPos).alpha();
    const bool blocked = afterAlpha >= minAlpha && afterAlpha <= maxAlpha;

    if (details) {
        details->insert(QStringLiteral("min_alpha"), minAlpha);
        details->insert(QStringLiteral("max_alpha"), maxAlpha);
        details->insert(QStringLiteral("settle_ms"), settleMs);
        details->insert(QStringLiteral("step_ms"), stepMs);
        details->insert(QStringLiteral("hold_ms_at_end"), holdMsAtEnd);
        details->insert(QStringLiteral("mouse_source"), mouseSourceName);
        details->insert(QStringLiteral("toolproxy_inject_details"), injectDetails);
        details->insert(QStringLiteral("before_alpha"), beforeAlpha);
        details->insert(QStringLiteral("after_alpha"), afterAlpha);
        details->insert(QStringLiteral("blocked"), blocked);
    }

    if (!blocked && errorOut) {
        *errorOut = QStringLiteral("Pixel alpha changed unexpectedly");
    }

    return blocked;
}

bool paintDragPathWaitPixelAlphaRange(KisMainWindow *mainWindow,
                                      const QJsonArray &pathArray,
                                      const QString &mouseSourceName,
                                      const QJsonObject &samplePosObj,
                                      int minAlpha,
                                      int maxAlpha,
                                      int timeoutMs,
                                      int stepMs,
                                      int holdMsAtEnd,
                                      bool allowNoPaintOnAndroid,
                                      QJsonObject *details,
                                      QString *errorOut)
{
    QWidget *canvasWidget = nullptr;
    if (!getCanvasContext(mainWindow, nullptr, &canvasWidget, errorOut)) {
        return false;
    }

    KisImageWSP image = (mainWindow && mainWindow->viewManager()) ? mainWindow->viewManager()->image() : KisImageWSP();
    if (!image) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing image");
        }
        return false;
    }

    KisPaintDeviceSP dev = paintDeviceForTouchScript(mainWindow);
    if (!dev) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing paint device");
        }
        return false;
    }

    QPoint imgPos;
    if (!resolveImagePos(mainWindow, samplePosObj, &imgPos, errorOut)) {
        return false;
    }
    const int beforeAlpha = sampleDeviceColorForTouchScript(image, dev, imgPos).alpha();

    bool okViaTouch = false;
    bool okViaMouse = false;

    QJsonObject touchDetails;
    QString touchError;
    {
        TouchDragPathPoints touchPath;
        if (!buildTouchDragPathPoints(mainWindow, 1, pathArray, &touchPath, &touchError)) {
            touchDetails.insert(QStringLiteral("error"), touchError);
        } else {
            QJsonObject injectDetails;
            sendTouchDragPath(canvasWidget, touchPath, stepMs, holdMsAtEnd, &injectDetails);
            touchDetails.insert(QStringLiteral("touch_inject_details"), injectDetails);
            okViaTouch = waitForPixelAlphaInRange(mainWindow, samplePosObj, minAlpha, maxAlpha, timeoutMs, nullptr, nullptr);
            touchDetails.insert(QStringLiteral("ok"), okViaTouch);
        }
    }

    QJsonObject mouseDetails;
    QString mouseError;
    if (!okViaTouch) {
        QVector<QPointF> mousePath;
        if (!buildWidgetPathPoints(mainWindow, pathArray, &mousePath, &mouseError)) {
            mouseDetails.insert(QStringLiteral("error"), mouseError);
        } else {
            Qt::MouseEventSource mouseSource = Qt::MouseEventSynthesizedByQt;
            QString parseError;
            if (!mouseEventSourceFromString(mouseSourceName, &mouseSource, &parseError)) {
                mouseDetails.insert(QStringLiteral("error"), parseError);
            } else {
                QJsonObject injectDetails;
                QString injectError;
                sendMouseDragPath(canvasWidget, mousePath, stepMs, holdMsAtEnd, mouseSource, &injectDetails, &injectError);
                if (!injectError.isEmpty()) {
                    mouseDetails.insert(QStringLiteral("inject_error"), injectError);
                }
                mouseDetails.insert(QStringLiteral("mouse_inject_details"), injectDetails);
                okViaMouse = waitForPixelAlphaInRange(mainWindow, samplePosObj, minAlpha, maxAlpha, timeoutMs, nullptr, nullptr);
                mouseDetails.insert(QStringLiteral("ok"), okViaMouse);
            }
        }
    }

    if (details) {
        details->insert(QStringLiteral("min_alpha"), minAlpha);
        details->insert(QStringLiteral("max_alpha"), maxAlpha);
        details->insert(QStringLiteral("timeout_ms"), timeoutMs);
        details->insert(QStringLiteral("step_ms"), stepMs);
        details->insert(QStringLiteral("hold_ms_at_end"), holdMsAtEnd);
        details->insert(QStringLiteral("mouse_source"), mouseSourceName);
        details->insert(QStringLiteral("allow_no_paint_on_android"), allowNoPaintOnAndroid);
        details->insert(QStringLiteral("touch_ok"), okViaTouch);
        details->insert(QStringLiteral("mouse_ok"), okViaMouse);
        details->insert(QStringLiteral("via"), okViaTouch ? QStringLiteral("touch") : (okViaMouse ? QStringLiteral("mouse") : QStringLiteral("none")));
        details->insert(QStringLiteral("touch_details"), touchDetails);
        details->insert(QStringLiteral("mouse_details"), mouseDetails);
        details->insert(QStringLiteral("before_alpha"), beforeAlpha);
        details->insert(QStringLiteral("after_alpha"), sampleDeviceColorForTouchScript(image, dev, imgPos).alpha());
    }

    const bool ok = okViaTouch || okViaMouse;
#ifdef Q_OS_ANDROID
    if (!ok && allowNoPaintOnAndroid) {
        if (details) {
            details->insert(QStringLiteral("note"), QStringLiteral("paint not detected; allowing pass on Android"));
        }
        return true;
    }
#endif

    if (!ok && errorOut) {
        *errorOut = QStringLiteral("Pixel alpha did not enter expected range");
    }

    return ok;
}

bool paintDragPathPixelAlphaNoChange(KisMainWindow *mainWindow,
                                     const QJsonArray &pathArray,
                                     const QString &mouseSourceName,
                                     const QJsonObject &samplePosObj,
                                     int minAlpha,
                                     int maxAlpha,
                                     int settleMs,
                                     int stepMs,
                                     int holdMsAtEnd,
                                     QJsonObject *details,
                                     QString *errorOut)
{
    QWidget *canvasWidget = nullptr;
    if (!getCanvasContext(mainWindow, nullptr, &canvasWidget, errorOut)) {
        return false;
    }

    KisImageWSP image = (mainWindow && mainWindow->viewManager()) ? mainWindow->viewManager()->image() : KisImageWSP();
    if (!image) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing image");
        }
        return false;
    }

    KisPaintDeviceSP dev = paintDeviceForTouchScript(mainWindow);
    if (!dev) {
        if (errorOut) {
            *errorOut = QStringLiteral("Missing paint device");
        }
        return false;
    }

    QPoint imgPos;
    if (!resolveImagePos(mainWindow, samplePosObj, &imgPos, errorOut)) {
        return false;
    }
    const int beforeAlpha = sampleDeviceColorForTouchScript(image, dev, imgPos).alpha();

    QJsonObject touchDetails;
    QString touchError;
    {
        TouchDragPathPoints touchPath;
        if (!buildTouchDragPathPoints(mainWindow, 1, pathArray, &touchPath, &touchError)) {
            touchDetails.insert(QStringLiteral("error"), touchError);
        } else {
            QJsonObject injectDetails;
            sendTouchDragPath(canvasWidget, touchPath, stepMs, holdMsAtEnd, &injectDetails);
            touchDetails.insert(QStringLiteral("touch_inject_details"), injectDetails);
        }
    }

    QJsonObject mouseDetails;
    QString mouseError;
    {
        QVector<QPointF> mousePath;
        if (!buildWidgetPathPoints(mainWindow, pathArray, &mousePath, &mouseError)) {
            mouseDetails.insert(QStringLiteral("error"), mouseError);
        } else {
            Qt::MouseEventSource mouseSource = Qt::MouseEventSynthesizedByQt;
            QString parseError;
            if (!mouseEventSourceFromString(mouseSourceName, &mouseSource, &parseError)) {
                mouseDetails.insert(QStringLiteral("error"), parseError);
            } else {
                QJsonObject injectDetails;
                QString injectError;
                sendMouseDragPath(canvasWidget, mousePath, stepMs, holdMsAtEnd, mouseSource, &injectDetails, &injectError);
                if (!injectError.isEmpty()) {
                    mouseDetails.insert(QStringLiteral("inject_error"), injectError);
                }
                mouseDetails.insert(QStringLiteral("mouse_inject_details"), injectDetails);
            }
        }
    }

    sleepWithImageEvents(image, settleMs);

    const int afterAlpha = sampleDeviceColorForTouchScript(image, dev, imgPos).alpha();
    const bool blocked = afterAlpha >= minAlpha && afterAlpha <= maxAlpha;

    if (details) {
        details->insert(QStringLiteral("min_alpha"), minAlpha);
        details->insert(QStringLiteral("max_alpha"), maxAlpha);
        details->insert(QStringLiteral("settle_ms"), settleMs);
        details->insert(QStringLiteral("step_ms"), stepMs);
        details->insert(QStringLiteral("hold_ms_at_end"), holdMsAtEnd);
        details->insert(QStringLiteral("mouse_source"), mouseSourceName);
        details->insert(QStringLiteral("touch_details"), touchDetails);
        details->insert(QStringLiteral("mouse_details"), mouseDetails);
        details->insert(QStringLiteral("before_alpha"), beforeAlpha);
        details->insert(QStringLiteral("after_alpha"), afterAlpha);
        details->insert(QStringLiteral("blocked"), blocked);
    }

    if (!blocked && errorOut) {
        *errorOut = QStringLiteral("Pixel alpha changed unexpectedly");
    }

    return blocked;
}

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
    sendTouchDragPath(canvasWidget, pathPoints, stepMs, 0, &injectDetails);

    CanvasTransform after;
    const bool ok = waitForUiCondition(timeoutMs, [&]() {
        CanvasTransform cur;
        if (!readCanvasTransform(mainWindow, &cur, nullptr)) {
            return false;
        }

        const qreal zoomRatioChangeNow = before.zoom > 0.0 ? std::abs(cur.zoom / before.zoom - 1.0) : std::abs(cur.zoom - before.zoom);
        const qreal absRotDeltaNow = std::abs(normalizedAngleDeltaDeg(before.rotationDeg, cur.rotationDeg));

        const bool zoomOk = zoomRatioChangeNow >= minZoomRatioChange;
        const bool rotOk = expectRotation ? (absRotDeltaNow >= minAbsRotationDeg) : (absRotDeltaNow <= maxAbsRotationDeg);
        return zoomOk && rotOk;
    });

    readCanvasTransform(mainWindow, &after, nullptr);
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
        details->insert(QStringLiteral("before_zoom"), before.zoom);
        details->insert(QStringLiteral("after_zoom"), after.zoom);
        details->insert(QStringLiteral("zoom_ratio_change"), zoomRatioChange);
        details->insert(QStringLiteral("before_rotation_deg"), before.rotationDeg);
        details->insert(QStringLiteral("after_rotation_deg"), after.rotationDeg);
        details->insert(QStringLiteral("rotation_delta_deg"), rotDelta);
        details->insert(QStringLiteral("abs_rotation_delta_deg"), absRotDelta);
        details->insert(QStringLiteral("path_details"), pathDetails);
        details->insert(QStringLiteral("touch_inject_details"), injectDetails);
    }

    if (!ok && errorOut) {
        *errorOut = QStringLiteral("Canvas transform did not match expectation (zoom_change=%1 rot_delta=%2)")
                        .arg(zoomRatioChange, 0, 'f', 4)
                        .arg(rotDelta, 0, 'f', 2);
    }

    return ok;
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
