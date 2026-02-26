/*
 * This file is part of the Krita touch fork.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisTouchSmokeScriptRunner_p.h"

#include <QApplication>
#include <QMouseEvent>
#include <QThread>
#include <QWidget>

#include <KoColor.h>
#include <KoToolBase.h>

#include "KisMainWindow.h"
#include "KisView.h"
#include "KisViewManager.h"
#include "canvas/kis_canvas2.h"
#include "canvas/kis_coordinates_converter.h"
#include "canvas/kis_tool_proxy.h"
#include "input/kis_input_manager.h"
#include "kis_group_layer.h"
#include "kis_image.h"
#include "kis_paint_device.h"
#include "kis_paint_layer.h"

namespace KisTouchSmokeScriptRunnerDetail {

namespace {

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
        QApplication::processEvents();
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

} // namespace

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

} // namespace KisTouchSmokeScriptRunnerDetail
