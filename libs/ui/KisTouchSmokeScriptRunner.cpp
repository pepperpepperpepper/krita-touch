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
#include <QThread>
#include <QTouchDevice>
#include <QTouchEvent>
#include <QWidget>

#include <kactioncollection.h>

#include <KoColor.h>
#include <KoColorSpaceConstants.h>
#include <KoCompositeOpRegistry.h>

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

void sendTouchDragPath(QWidget *canvasWidget, const TouchDragPathPoints &pathPoints, int stepMs, QJsonObject *details)
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
            updatePoints.append(tp);
        }
        QTouchEvent updateEvent(QEvent::TouchUpdate, device, Qt::NoModifier, Qt::TouchPointMoved, updatePoints);
        QApplication::sendEvent(canvasWidget, &updateEvent);
        QApplication::processEvents();
        if (stepMs > 0) {
            QThread::msleep(stepMs);
        }
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
    QApplication::sendEvent(canvasWidget, &endEvent);
    QApplication::processEvents();

    if (details) {
        const QRect r = canvasWidget->rect();
        details->insert(QStringLiteral("sent"), true);
        details->insert(QStringLiteral("step_ms"), stepMs);
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
    sendTouchDragPath(canvasWidget, pathPoints, stepMs, &injectDetails);
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
    sendTouchDragPath(canvasWidget, pathPoints, stepMs, &injectDetails);

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
    sendTouchDragPath(canvasWidget, pathPoints, stepMs, &injectDetails);
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
    sendTouchDragPath(canvasWidget, pathPoints, stepMs, &injectDetails);

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
