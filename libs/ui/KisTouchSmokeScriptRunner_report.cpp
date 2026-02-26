/*
 * This file is part of the Krita touch fork.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisTouchSmokeScriptRunner_p.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QWidget>

#include <KoColor.h>
#include <KoColorSpaceConstants.h>
#include <KoCompositeOpRegistry.h>

#include "KisMainWindow.h"
#include "KisView.h"
#include "KisViewManager.h"
#include "canvas/kis_canvas2.h"
#include "canvas/kis_coordinates_converter.h"
#include "kis_group_layer.h"
#include "kis_image.h"
#include "kis_paint_device.h"
#include "kis_paint_layer.h"
#include "kis_painter.h"

namespace KisTouchSmokeScriptRunnerDetail {

namespace {

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

} // namespace

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

bool touchDragPathWaitPixelAlphaWithFallback(KisMainWindow *mainWindow,
                                            int fingerCount,
                                            const QJsonArray &pathArray,
                                            const QJsonObject &samplePosObj,
                                            int minAlpha,
                                            int maxAlpha,
                                            int timeoutMs,
                                            int fallbackShortcut,
                                            int stepMs,
                                            int holdMsAtEnd,
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
    sendTouchDragPath(canvasWidget, pathPoints, stepMs, holdMsAtEnd, &injectDetails);
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
        details->insert(QStringLiteral("hold_ms_at_end"), holdMsAtEnd);
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

} // namespace KisTouchSmokeScriptRunnerDetail
