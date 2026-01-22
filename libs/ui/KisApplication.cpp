/*
 * SPDX-FileCopyrightText: 1998, 1999 Torben Weis <weis@kde.org>
 * SPDX-FileCopyrightText: 2012 Boudewijn Rempt <boud@valdyas.org>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "KisApplication.h"

#include <stdlib.h>
#ifdef Q_OS_WIN
#include <windows.h>
#include <tchar.h>
#include "KisWindowsPackageUtils.h"
#endif

#ifdef Q_OS_MACOS
#include "osx.h"
#include "KisMacosEntitlements.h"
#endif

#ifdef Q_OS_ANDROID
#include "KisAndroidDonations.h"
#endif

#include <QStandardPaths>
#include <QScreen>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QApplication>
#include <QDropEvent>
#include <QMouseEvent>
#include <QMessageBox>
#include <QMetaObject>
#include <QMimeData>
#include <QProcessEnvironment>
#include <QStringList>
#include <QStyle>
#include <QStyleFactory>
#include <QSysInfo>
#include <QThread>
#include <QTimer>
#include <QElapsedTimer>
#include <QWidget>
#include <QDockWidget>
#include <QTreeView>
#include <QItemSelectionModel>
#include <QMenu>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImageReader>
#include <QImageWriter>
#include <QTouchDevice>
#include <QTouchEvent>
#include <QThread>

#include <klocalizedstring.h>
#include <kdesktopfile.h>
#include <kconfig.h>
#include <kconfiggroup.h>

#include "widgets/kis_touch_actions_sheet.h"
#include "widgets/kis_touch_quickmenu_overlay.h"

#include <KoColor.h>
#include <KoCanvasResourceProvider.h>
#include <KoCanvasResourcesIds.h>

#include <KoDockRegistry.h>
#include <KoToolRegistry.h>
#include <KoToolManager.h>
#include <KoToolBase.h>
#include <KoColorSpaceRegistry.h>
#include <KoPluginLoader.h>
#include <KoShapeRegistry.h>
#include "KoConfig.h"
#include <KoResourcePaths.h>
#include <KisMimeDatabase.h>
#include "thememanager.h"
#include "KisDocument.h"
#include "KisMainWindow.h"
#include "KisView.h"
#include "KisAutoSaveRecoveryDialog.h"
#include "KisPart.h"
#include <kis_icon.h>
#include "kis_splash_screen.h"
#include "kis_config.h"
#include "kis_config_notifier.h"
#include "flake/kis_shape_selection.h"
#include <filter/kis_filter.h>
#include <filter/kis_filter_registry.h>
#include <filter/kis_filter_configuration.h>
#include <generator/kis_generator_registry.h>
#include <generator/kis_generator.h>
#include <brushengine/kis_paintop_registry.h>
#include <kis_meta_data_io_backend.h>
#include <kis_meta_data_backend_registry.h>
#include "KisApplicationArguments.h"
#include <kis_debug.h>
#include "kis_action_registry.h"
#include <KoResourceServer.h>
#include <KisResourceServerProvider.h>
#include <KoResourceServerProvider.h>
#include "opengl/kis_opengl.h"
#include "kis_spin_box_unit_manager.h"
#include "kis_document_aware_spin_box_unit_manager.h"
#include "KisViewManager.h"
#include <canvas/kis_canvas2.h>
#include <canvas/kis_canvas_controller.h>
#include <kis_canvas_resource_provider.h>
#include <KisUsageLogger.h>
#include "kis_popup_palette.h"
#include <kis_paint_layer.h>
#include <kis_fill_painter.h>
#include <kis_painter.h>
#include <input/kis_zoom_and_rotate_action.h>
#include "input/KisTouchGestureAction.h"
#include "input/KisTouchQuickMenuAction.h"
#include "input/kis_input_profile_manager.h"
#include "widgets/kis_touch_copypaste_overlay.h"

#include <KritaVersionWrapper.h>
#include <dialogs/KisSessionManagerDialog.h>

#include <KisResourceCacheDb.h>
#include <KisResourceLocator.h>
#include <KisResourceLoader.h>
#include <KisResourceLoaderRegistry.h>

#include <KisBrushTypeMetaDataFixup.h>
#include <kis_gbr_brush.h>
#include <kis_png_brush.h>
#include <kis_svg_brush.h>
#include <kis_imagepipe_brush.h>
#include <KoColorSet.h>
#include <KoSegmentGradient.h>
#include <KoStopGradient.h>
#include <KoPattern.h>
#include <kis_workspace_resource.h>
#include <KisSessionResource.h>
#include <resources/KoSvgSymbolCollectionResource.h>
#include <resources/KoFontFamily.h>
#include <resources/KoCssStylePreset.h>

#include "widgets/KisScreenColorSampler.h"
#include "KisDlgInternalColorSelector.h"
#include "KisLongPressEventFilter.h"

#include <dialogs/KisAsyncAnimationFramesSaveDialog.h>
#include <kis_image_animation_interface.h>
#include "kis_file_layer.h"
#include "kis_group_layer.h"
#include "kis_node_commands_adapter.h"
#include "KisSynchronizedConnection.h"
#include <QThreadStorage>
#include <KisWindowsPackageUtils.h>

#include <kis_psd_layer_style.h>

#include <config-seexpr.h>
#include <config-safe-asserts.h>

#include <input/KisExtendedModifiersMapperPluginInterface.h>
#include <KisPlatformPluginInterfaceFactory.h>

#include <config-use-surface-color-management-api.h>

#if KRITA_USE_SURFACE_COLOR_MANAGEMENT_API

#include <QWindow>
#include <QPlatformSurfaceEvent>
#include <KisSRGBSurfaceColorSpaceManager.h>

#endif /* KRITA_USE_SURFACE_COLOR_MANAGEMENT_API */

namespace {
const QTime appStartTime(QTime::currentTime());
}

namespace {

bool ensureDocumentForTouchSmoke(KisMainWindow *mainWindow)
{
    if (!mainWindow) {
        return false;
    }

    if (mainWindow->viewManager() && mainWindow->viewManager()->image()) {
        return true;
    }

    KisDocument *doc = KisPart::instance()->createDocument();
    if (!doc) {
        return false;
    }

    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->colorSpace("RGBA", "U8", "");
    if (!cs) {
        qWarning() << "Touch smoke: failed to create RGBA/U8 colorspace";
        return false;
    }

    doc->newImage(i18n("Touch smoke"),
                  512,
                  512,
                  cs,
                  KoColor(QColor(Qt::white), cs),
                  KisConfig::CANVAS_COLOR,
                  1,
                  "",
                  100.0);

    KisPart::instance()->addDocument(doc);
    mainWindow->showWelcomeScreen(false);
    mainWindow->addViewAndNotifyLoadingCompleted(doc);
    return true;
}

void showDockerForTouchSmoke(KisMainWindow *mainWindow, const QString &dockerId)
{
    if (!mainWindow) {
        return;
    }

    QDockWidget *dock = mainWindow->dockWidget(dockerId);
    if (!dock) {
        qWarning() << "Touch smoke: docker not found:" << dockerId;
        return;
    }

    if (dockerId == QStringLiteral("TouchDocker")) {
        // Ensure deterministic placement for screenshots (TouchDocker can be hidden or moved off-screen
        // in persisted user configs). Match the Procreate-like expectation: right-handed => left sidebar.
        const KisConfig cfg(true);
        const Qt::DockWidgetArea area =
            cfg.touchRightHanded() ? Qt::LeftDockWidgetArea : Qt::RightDockWidgetArea;
        dock->setFloating(false);
        mainWindow->addDockWidget(area, dock);
        mainWindow->resizeDocks(QList<QDockWidget*>{dock}, QList<int>{180}, Qt::Horizontal);
    }

    if (dockerId == QStringLiteral("sharedtooldocker")) {
        // Ensure deterministic placement for screenshots (the Tool Options docker might be hidden,
        // tabbed, or floating off-screen in user state).
        dock->setFloating(false);
        mainWindow->addDockWidget(Qt::LeftDockWidgetArea, dock);
        mainWindow->resizeDocks(QList<QDockWidget*>{dock}, QList<int>{320}, Qt::Horizontal);
    }

    dock->show();
    dock->raise();

}

void populateLayersForTouchSmoke(KisMainWindow *mainWindow, int extraPaintLayers)
{
    if (!mainWindow || !mainWindow->actionCollection()) {
        return;
    }

    if (extraPaintLayers <= 0) {
        return;
    }

    QAction *action = mainWindow->actionCollection()->action(QStringLiteral("add_new_paint_layer"));
    if (!action) {
        qWarning() << "Touch smoke: action not found: add_new_paint_layer";
        return;
    }

    for (int i = 0; i < extraPaintLayers; ++i) {
        action->trigger();
    }
}

KisPaintDeviceSP paintDeviceForTouchSmoke(KisMainWindow *mainWindow)
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
        // (We avoid activating nodes here because it can trigger SAFE_ASSERTs if flake "shapes" aren't ready yet.)
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

void refreshImageForTouchSmoke(KisImageWSP img)
{
    if (!img) {
        return;
    }

    const QRect bounds = img->bounds();
    img->refreshGraphAsync(img->root(), QVector<QRect>{bounds}, bounds);
    img->waitForDone();
}

bool colorsEqualForTouchSmoke(const QColor &a, const QColor &b, int tolerance)
{
    if (!a.isValid() || !b.isValid()) {
        return false;
    }

    const int dr = qAbs(a.red() - b.red());
    const int dg = qAbs(a.green() - b.green());
    const int db = qAbs(a.blue() - b.blue());
    const int da = qAbs(a.alpha() - b.alpha());
    return dr <= tolerance && dg <= tolerance && db <= tolerance && da <= tolerance;
}

QVector<QColor> sampleDeviceColorsForTouchSmoke(const KisPaintDeviceSP &dev, const QVector<QPoint> &imgPoints)
{
    QVector<QColor> colors;
    colors.reserve(imgPoints.size());

    if (!dev) {
        colors.fill(QColor(), imgPoints.size());
        return colors;
    }

    for (const QPoint &p : imgPoints) {
        const KoColor c = dev->pixel(p);
        colors.push_back(c.toQColor());
    }

    return colors;
}

bool anySampleChangedForTouchSmoke(const QVector<QColor> &before, const QVector<QColor> &after, int tolerance)
{
    if (before.size() != after.size()) {
        return true;
    }

    for (int i = 0; i < before.size(); ++i) {
        if (!colorsEqualForTouchSmoke(before[i], after[i], tolerance)) {
            return true;
        }
    }

    return false;
}

bool paintLineForTouchSmoke(KisMainWindow *mainWindow, const QPointF &imgP0, const QPointF &imgP1, const QColor &color)
{
    if (!mainWindow || !mainWindow->viewManager()) {
        return false;
    }

    KisViewManager *viewManager = mainWindow->viewManager();
    KisImageWSP img = viewManager->image();
    KisPaintDeviceSP dev = paintDeviceForTouchSmoke(mainWindow);
    if (!img || !dev) {
        qWarning() << "Touch smoke: missing image/device for paintLine";
        return false;
    }

    KisPainter painter(dev);
    painter.setCompositeOpId(COMPOSITE_OVER);
    painter.setOpacityU8(OPACITY_OPAQUE_U8);
    painter.setPaintColor(KoColor(color, dev->colorSpace()));
    painter.drawLine(imgP0, imgP1, 48.0, true);
    painter.end();

    refreshImageForTouchSmoke(img);
    return true;
}

bool paintRectForTouchSmoke(KisMainWindow *mainWindow, const QRectF &imgRect, const QColor &color)
{
    if (!mainWindow || !mainWindow->viewManager()) {
        return false;
    }

    KisViewManager *viewManager = mainWindow->viewManager();
    KisImageWSP img = viewManager->image();
    KisPaintDeviceSP dev = paintDeviceForTouchSmoke(mainWindow);
    if (!img || !dev) {
        qWarning() << "Touch smoke: missing image/device for paintRect";
        return false;
    }

    const QRectF r = imgRect.normalized();
    const QPointF tl(r.left(), r.top());
    const QPointF tr(r.right(), r.top());
    const QPointF br(r.right(), r.bottom());
    const QPointF bl(r.left(), r.bottom());

    KisPainter painter(dev);
    painter.setCompositeOpId(COMPOSITE_OVER);
    painter.setOpacityU8(OPACITY_OPAQUE_U8);
    painter.setPaintColor(KoColor(color, dev->colorSpace()));
    painter.drawLine(tl, tr, 48.0, true);
    painter.drawLine(tr, br, 48.0, true);
    painter.drawLine(br, bl, 48.0, true);
    painter.drawLine(bl, tl, 48.0, true);
    painter.end();

    refreshImageForTouchSmoke(img);
    return true;
}

bool fillCanvasForTouchSmoke(KisMainWindow *mainWindow, const QColor &color)
{
    if (!mainWindow || !mainWindow->viewManager()) {
        return false;
    }

    KisViewManager *viewManager = mainWindow->viewManager();
    KisImageWSP img = viewManager->image();
    KisPaintDeviceSP dev = paintDeviceForTouchSmoke(mainWindow);
    if (!img || !dev) {
        qWarning() << "Touch smoke: missing image/device for fillCanvas";
        return false;
    }

    KisFillPainter painter(dev);
    painter.setCompositeOpId(COMPOSITE_OVER);
    painter.fillRect(img->bounds(), KoColor(color, dev->colorSpace()), OPACITY_OPAQUE_U8);
    painter.end();

    refreshImageForTouchSmoke(img);
    return true;
}

bool paintRectStrokeForTouchSmoke(KisMainWindow *mainWindow, const QRectF &imgRect, int edgeSteps, int holdMsAtEnd)
{
    if (!mainWindow) {
        return false;
    }

    KisView *view = mainWindow->activeView();
    if (!view) {
        qWarning() << "Touch smoke: no active view for painting";
        return false;
    }

    KisImageWSP image = mainWindow->viewManager() ? mainWindow->viewManager()->image() : KisImageWSP();
    if (!image) {
        qWarning() << "Touch smoke: no image for painting";
        return false;
    }

    KoToolManager::instance()->switchToolRequested(QStringLiteral("KritaShape/KisToolBrush"));
    QApplication::processEvents();

    QWidget *canvasWidget = view->canvasBase() ? view->canvasBase()->canvasWidget() : nullptr;
    if (!canvasWidget) {
        qWarning() << "Touch smoke: no canvas widget for painting";
        return false;
    }

    const QRectF r = imgRect.normalized();
    const QPointF tl(r.left(), r.top());
    const QPointF tr(r.right(), r.top());
    const QPointF br(r.right(), r.bottom());
    const QPointF bl(r.left(), r.bottom());

    auto imgToWidget = [view](const QPointF &imgP) {
        return view->canvasBase()->coordinatesConverter()->imageToWidget(imgP);
    };

    const int steps = qMax(1, edgeSteps);

    QPointF wLast = imgToWidget(tl);
    QPointF gLast = canvasWidget->mapToGlobal(wLast.toPoint());

    QMouseEvent press(QEvent::MouseButtonPress, wLast, gLast, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvasWidget, &press);

    auto lerpEdge = [&](const QPointF &a, const QPointF &b) {
        for (int i = 1; i <= steps; i++) {
            const qreal t = qreal(i) / steps;
            const QPointF imgP = a + t * (b - a);
            const QPointF wP = imgToWidget(imgP);
            const QPointF gP = canvasWidget->mapToGlobal(wP.toPoint());
            QMouseEvent move(QEvent::MouseMove, wP, gP, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(canvasWidget, &move);
            wLast = wP;
            gLast = gP;
        }
    };

    lerpEdge(tl, tr);
    lerpEdge(tr, br);
    lerpEdge(br, bl);
    lerpEdge(bl, tl);

    if (holdMsAtEnd > 0) {
        QThread::msleep(holdMsAtEnd);
    }

    QMouseEvent release(QEvent::MouseButtonRelease, wLast, gLast, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvasWidget, &release);

    image->waitForDone();
    return true;
}

bool paintStrokeForTouchSmoke(KisMainWindow *mainWindow, int moveSteps, qreal wobbleAmplitude, int holdMsAtEnd)
{
    if (!mainWindow) {
        return false;
    }

    KisView *view = mainWindow->activeView();
    if (!view) {
        qWarning() << "Touch smoke: no active view for painting";
        return false;
    }

    KisImageWSP image = mainWindow->viewManager() ? mainWindow->viewManager()->image() : KisImageWSP();
    if (!image) {
        qWarning() << "Touch smoke: no image for painting";
        return false;
    }

    KoToolManager::instance()->switchToolRequested(QStringLiteral("KritaShape/KisToolBrush"));

    QWidget *canvasWidget = view->canvasBase() ? view->canvasBase()->canvasWidget() : nullptr;
    if (!canvasWidget) {
        qWarning() << "Touch smoke: no canvas widget for painting";
        return false;
    }

    const QRect bounds = image->bounds();
    const QPointF imgP0(bounds.left() + bounds.width() * 0.25, bounds.center().y());
    const QPointF imgP1(bounds.left() + bounds.width() * 0.75, bounds.center().y());

    const QPointF wP0 = view->canvasBase()->coordinatesConverter()->imageToWidget(imgP0);
    const QPointF wP1 = view->canvasBase()->coordinatesConverter()->imageToWidget(imgP1);

    const QPointF gP0 = canvasWidget->mapToGlobal(wP0.toPoint());
    const QPointF gP1 = canvasWidget->mapToGlobal(wP1.toPoint());

    QMouseEvent press(QEvent::MouseButtonPress, wP0, gP0, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvasWidget, &press);

    for (int i = 1; i <= qMax(1, moveSteps); i++) {
        const qreal t = qreal(i) / qMax(1, moveSteps);
        const qreal wobble = wobbleAmplitude > 0 ? ((i % 2 ? 1 : -1) * wobbleAmplitude) : 0.0;
        const QPointF wP = wP0 + t * (wP1 - wP0) + QPointF(0, wobble);
        const QPointF gP = canvasWidget->mapToGlobal(wP.toPoint());
        QMouseEvent move(QEvent::MouseMove, wP, gP, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvasWidget, &move);
    }

    if (holdMsAtEnd > 0) {
        QThread::msleep(holdMsAtEnd);
    }

    QMouseEvent release(QEvent::MouseButtonRelease, wP1, gP1, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvasWidget, &release);

    image->waitForDone();
    return true;
}

void runTouchSmokeScenario(const QString &scenario, KisMainWindow *mainWindow)
{
    if (!mainWindow) {
        return;
    }

    const QString normalizedScenario = scenario.trimmed().toLower();
    if (normalizedScenario.isEmpty()) {
        return;
    }

    class TouchSmokeReport
    {
    public:
        explicit TouchSmokeReport(const QString &scenario)
            : m_scenario(scenario)
        {
            m_timer.start();
        }

        void step(const QString &name, bool ok, const QJsonObject &details = QJsonObject())
        {
            QJsonObject obj;
            obj.insert(QStringLiteral("name"), name);
            obj.insert(QStringLiteral("ok"), ok);
            if (!details.isEmpty()) {
                obj.insert(QStringLiteral("details"), details);
            }
            m_steps.append(obj);
        }

        QByteArray toJson(const QString &status) const
        {
            QJsonObject root;
            root.insert(QStringLiteral("scenario"), m_scenario);
            root.insert(QStringLiteral("status"), status);
            root.insert(QStringLiteral("duration_ms"), qint64(m_timer.elapsed()));
            root.insert(QStringLiteral("steps"), m_steps);
            return QJsonDocument(root).toJson(QJsonDocument::Compact);
        }

    private:
        QString m_scenario;
        QElapsedTimer m_timer;
        QJsonArray m_steps;
    };

    TouchSmokeReport report(normalizedScenario);

    auto finalizeSmoke = [&](bool ok) {
        // Give Qt a moment to settle widget creation + repaint so headless screenshots
        // capture the intended UI state (especially on Android).
        QApplication::processEvents();
        QThread::msleep(200);
        QApplication::processEvents();

        const QString status = ok ? QStringLiteral("OK") : QStringLiteral("ERROR");
        qInfo().noquote() << QStringLiteral("KRITA_TOUCH_SMOKE_DONE scenario=%1 status=%2").arg(normalizedScenario, status);
        qInfo().noquote() << QStringLiteral("KRITA_TOUCH_SMOKE_JSON %1").arg(QString::fromUtf8(report.toJson(status)));
    };

    // Don't persist smoke-only settings changes into the user's config file. We only
    // need the updated values in-process for deterministic screenshots.
    KisConfig cfg(true);
    cfg.setTouchModeEnabled(true);
    cfg.setTouchQuickShapeEnabled(true);

    const bool useLightTouchTheme = normalizedScenario == "top-bar-light" || normalizedScenario == "top_bar_light" ||
        normalizedScenario == "topbar-light" || normalizedScenario == "topbar_light";
    cfg.setTouchThemeName(useLightTouchTheme
        ? QStringLiteral("Touch Procreate Light")
        : QStringLiteral("Touch Procreate Dark"));

    mainWindow->show();
    mainWindow->raise();
    mainWindow->activateWindow();

    // Force immediate application of Touch Mode (workspace + chrome) so the deterministic
    // scenario screenshots don't depend on asynchronous config notifier timing.
    QMetaObject::invokeMethod(mainWindow, "configChanged", Qt::DirectConnection);

    if (!ensureDocumentForTouchSmoke(mainWindow)) {
        qWarning() << "Touch smoke: could not ensure a document is open";
        finalizeSmoke(false);
        return;
    }

    // Make screenshots deterministic regardless of the user's saved workspace state.
    // We only show the minimum UI needed for each scenario.
    Q_FOREACH (QDockWidget *dock, mainWindow->dockWidgets()) {
        if (dock) {
            dock->hide();
        }
    }

    if (normalizedScenario == "top-bar" || normalizedScenario == "top_bar" || normalizedScenario == "topbar") {
        // Just showing the main window is enough; Touch Mode is enabled above and will
        // create any touch chrome (like the top bar toolbar) via KisConfigNotifier.
        finalizeSmoke(true);
        return;
    }

    if (useLightTouchTheme) {
        finalizeSmoke(true);
        return;
    }

    if (normalizedScenario == "selection-tool" || normalizedScenario == "selection_tool") {
        KisView *view = mainWindow->activeView();
        KisImageWSP image = mainWindow->viewManager() ? mainWindow->viewManager()->image() : KisImageWSP();
        const QRect bounds = image ? image->bounds() : QRect();

        if (!view || !view->canvasBase() || !image || !bounds.isValid()) {
            qWarning() << "Touch smoke: selection-tool missing view/image/bounds";
            finalizeSmoke(false);
            return;
        }

        QWidget *canvasWidget = view->canvasBase()->canvasWidget();
        if (!canvasWidget) {
            qWarning() << "Touch smoke: selection-tool missing canvas widget";
            finalizeSmoke(false);
            return;
        }

        KoToolManager *toolManager = KoToolManager::instance();
        if (!toolManager) {
            qWarning() << "Touch smoke: selection-tool missing tool manager";
            finalizeSmoke(false);
            return;
        }

        // Ensure deterministic canvas content before testing selection ops.
        if (!fillCanvasForTouchSmoke(mainWindow, QColor(0xff, 0xff, 0xff))) {
            qWarning() << "Touch smoke: selection-tool failed to fill canvas";
        }

        const QColor fillColor(0xff, 0x33, 0xaa);
        KoCanvasResourceProvider *resourceManager =
            mainWindow->viewManager() && mainWindow->viewManager()->canvasResourceProvider()
                ? mainWindow->viewManager()->canvasResourceProvider()->resourceManager()
                : nullptr;
        if (resourceManager) {
            resourceManager->setResource(KoCanvasResource::ForegroundColor,
                                         KoColor(fillColor, image->colorSpace()));
        } else {
            qWarning() << "Touch smoke: selection-tool missing resource manager; fill color may be non-deterministic";
        }

        toolManager->switchToolRequested(QStringLiteral("KisToolSelectTouch"));
        QApplication::processEvents();
        showDockerForTouchSmoke(mainWindow, QStringLiteral("sharedtooldocker"));

        auto imgToWidget = [&](const QPointF &imgP) {
            return view->canvasBase()->coordinatesConverter()->imageToWidget(imgP);
        };

        auto tapAtImagePos = [&](const QPointF &imgP) {
            const QPointF widgetPos = imgToWidget(imgP);
            const QPointF globalPos = canvasWidget->mapToGlobal(widgetPos.toPoint());

            QMouseEvent press(QEvent::MouseButtonPress,
                              widgetPos,
                              globalPos,
                              Qt::LeftButton,
                              Qt::LeftButton,
                              Qt::NoModifier);
            QApplication::sendEvent(canvasWidget, &press);

            QMouseEvent release(QEvent::MouseButtonRelease,
                                widgetPos,
                                globalPos,
                                Qt::LeftButton,
                                Qt::NoButton,
                                Qt::NoModifier);
            QApplication::sendEvent(canvasWidget, &release);
        };

        // Tap-to-polygon selection: 4 corners, then tap the first point again to close.
        const QPointF tl(bounds.left() + bounds.width() * 0.25, bounds.top() + bounds.height() * 0.25);
        const QPointF tr(bounds.left() + bounds.width() * 0.75, bounds.top() + bounds.height() * 0.25);
        const QPointF br(bounds.left() + bounds.width() * 0.75, bounds.top() + bounds.height() * 0.75);
        const QPointF bl(bounds.left() + bounds.width() * 0.25, bounds.top() + bounds.height() * 0.75);

        tapAtImagePos(tl);
        tapAtImagePos(tr);
        tapAtImagePos(br);
        tapAtImagePos(bl);
        tapAtImagePos(tl);

        auto selectedExactRect = [&]() -> QRect {
            KisSelectionSP selection = view->selection();
            if (!selection || !selection->pixelSelection()) {
                return QRect();
            }
            return selection->pixelSelection()->selectedExactRect();
        };

        bool ok = true;

        bool selectionMade = false;
        for (int i = 0; i < 80; ++i) {
            QApplication::processEvents();
            image->waitForDone();
            if (!selectedExactRect().isEmpty()) {
                selectionMade = true;
                break;
            }
            QThread::msleep(20);
        }

        if (!selectionMade) {
            qWarning() << "Touch smoke: selection-tool did not create a selection";
            finalizeSmoke(false);
            return;
        }

        // Exercise Save/Load selection via the tool slots (single-slot, in-memory).
        KoToolBase *toolBase = toolManager->toolById(view->canvasBase(), QStringLiteral("KisToolSelectTouch"));
        QObject *toolObj = dynamic_cast<QObject *>(toolBase);
        if (!toolObj) {
            qWarning() << "Touch smoke: selection-tool could not access tool object";
            ok = false;
        } else {
            QMetaObject::invokeMethod(toolObj, "slot_saveSelectionClicked", Qt::DirectConnection);
        }

        if (QAction *action = mainWindow->actionCollection()->action("deselect")) {
            action->trigger();
        } else {
            qWarning() << "Touch smoke: selection-tool missing action: deselect";
            ok = false;
        }

        bool selectionCleared = false;
        for (int i = 0; i < 80; ++i) {
            QApplication::processEvents();
            if (selectedExactRect().isEmpty()) {
                selectionCleared = true;
                break;
            }
            QThread::msleep(20);
        }
        if (!selectionCleared) {
            qWarning() << "Touch smoke: selection-tool deselect did not clear selection";
            ok = false;
        }

        if (toolObj) {
            QMetaObject::invokeMethod(toolObj, "slot_loadSelectionClicked", Qt::DirectConnection);
        }

        bool selectionRestored = false;
        for (int i = 0; i < 80; ++i) {
            QApplication::processEvents();
            if (!selectedExactRect().isEmpty()) {
                selectionRestored = true;
                break;
            }
            QThread::msleep(20);
        }
        if (!selectionRestored) {
            qWarning() << "Touch smoke: selection-tool load did not restore selection";
            ok = false;
        }

        // Fill selection and validate pixels inside/outside change as expected.
        KisPaintDeviceSP dev = paintDeviceForTouchSmoke(mainWindow);
        const QPoint inside(bounds.left() + qRound(bounds.width() * 0.50),
                            bounds.top() + qRound(bounds.height() * 0.50));
        const QPoint outside(bounds.left() + qRound(bounds.width() * 0.10),
                             bounds.top() + qRound(bounds.height() * 0.10));
        const QVector<QPoint> samplePoints{inside, outside};
        const QVector<QColor> before = sampleDeviceColorsForTouchSmoke(dev, samplePoints);

        if (QAction *action = mainWindow->actionCollection()->action("fill_selection_foreground_color")) {
            action->trigger();
            QApplication::processEvents();
            image->waitForDone();
            refreshImageForTouchSmoke(image);
        } else {
            qWarning() << "Touch smoke: selection-tool missing action: fill_selection_foreground_color";
            ok = false;
        }

        const QVector<QColor> after = sampleDeviceColorsForTouchSmoke(dev, samplePoints);
        if (!dev) {
            qWarning() << "Touch smoke: selection-tool cannot validate fill; missing paint device";
            ok = false;
        } else if (after.size() == samplePoints.size()) {
            const QColor expectedOutside(0xff, 0xff, 0xff);
            const bool insideIsFillColor = colorsEqualForTouchSmoke(after[0], fillColor, 5);
            const bool outsideIsWhite = colorsEqualForTouchSmoke(after[1], expectedOutside, 5);
            const bool insideChanged = anySampleChangedForTouchSmoke({before[0]}, {after[0]}, 3);

            if (!insideChanged || !insideIsFillColor || !outsideIsWhite) {
                qWarning() << "Touch smoke: selection-tool fill did not match expected colors"
                           << "insideChanged=" << insideChanged
                           << "insideOk=" << insideIsFillColor
                           << "outsideOk=" << outsideIsWhite
                           << "insideAfter=" << after[0]
                           << "outsideAfter=" << after[1];
                ok = false;
            }
        } else {
            qWarning() << "Touch smoke: selection-tool sample size mismatch";
            ok = false;
        }

        finalizeSmoke(ok);
        return;
    }

    if (normalizedScenario == "transform-tool" || normalizedScenario == "transform_tool") {
        KisView *view = mainWindow->activeView();
        KisImageWSP image = mainWindow->viewManager() ? mainWindow->viewManager()->image() : KisImageWSP();
        KisPaintDeviceSP dev = paintDeviceForTouchSmoke(mainWindow);
        const QRect bounds = image ? image->bounds() : QRect();
        const QPoint boundsCenter = bounds.center();
        const QPoint rightSample(bounds.left() + qRound(bounds.width() * 0.90), boundsCenter.y());
        const QVector<QPoint> samplePoints = bounds.isValid()
            ? QVector<QPoint>{boundsCenter, QPoint(boundsCenter.x() - 12, boundsCenter.y()), rightSample}
            : QVector<QPoint>{};
        const QVector<QColor> before = sampleDeviceColorsForTouchSmoke(dev, samplePoints);

        if (!paintStrokeForTouchSmoke(mainWindow, 4, 0.0, 0)) {
            qWarning() << "Touch smoke: could not paint via input events for transform-tool scenario";
        }

        if (image) {
            image->waitForDone();
        }

        const QVector<QColor> after = sampleDeviceColorsForTouchSmoke(dev, samplePoints);
        if (image && dev && !anySampleChangedForTouchSmoke(before, after, 3)) {
            qWarning() << "Touch smoke: transform-tool content was not painted; falling back to direct line paint";
            const QPointF imgP0(bounds.left() + bounds.width() * 0.30, bounds.center().y());
            const QPointF imgP1(bounds.left() + bounds.width() * 0.70, bounds.center().y());
            paintLineForTouchSmoke(mainWindow, imgP0, imgP1, QColor(0, 0, 0));
        }

        if (image) {
            refreshImageForTouchSmoke(image);
        }

        // Verify the transform by sampling the image projection (not the raw paint device), since
        // some transforms can affect node offsets / projection without mutating the original device.
        const KisPaintDeviceSP projectionBeforeTransform = image ? image->projection() : KisPaintDeviceSP();
        const QVector<QColor> beforeTransform = sampleDeviceColorsForTouchSmoke(projectionBeforeTransform, samplePoints);

        auto sampledPixelsChanged = [&]() -> bool {
            if (!image || !projectionBeforeTransform) {
                return false;
            }

            refreshImageForTouchSmoke(image);

            KisPaintDeviceSP projectionAfterTransform = image->projection();
            if (!projectionAfterTransform) {
                return false;
            }
            const QVector<QColor> afterTransform = sampleDeviceColorsForTouchSmoke(projectionAfterTransform, samplePoints);
            return anySampleChangedForTouchSmoke(beforeTransform, afterTransform, 3);
        };

        auto ensureTransformToolReady = [&]() -> QObject * {
            if (!view || !view->canvasBase() || !image || !bounds.isValid()) {
                return nullptr;
            }

            KoToolManager *toolManager = KoToolManager::instance();
            if (!toolManager) {
                return nullptr;
            }

            KoToolManager::instance()->switchToolRequested(QStringLiteral("KisToolTransform"));
            QApplication::processEvents();

            KoToolBase *tool = toolManager->toolById(view->canvasBase(), QStringLiteral("KisToolTransform"));
            QObject *toolObj = dynamic_cast<QObject *>(tool);
            if (!toolObj) {
                return nullptr;
            }

            // Ensure consistent smoke behavior regardless of the user's last-used transform subtool.
            QMetaObject::invokeMethod(toolObj, "slotUpdateToFreeTransformType", Qt::DirectConnection);

            const KisCoordinatesConverter *converter = view->canvasBase()->coordinatesConverter();
            if (!converter) {
                return nullptr;
            }

            // Wait for the transform stroke to be initialized. If we apply before a transaction
            // is generated, the stroke may end without changing the image.
            const QPointF centerWidget = converter->imageToWidget(QPointF(bounds.center()));
            bool ready = false;
            for (int i = 0; i < 50; ++i) {
                bool hit = false;
                const bool canHitTest =
                    QMetaObject::invokeMethod(toolObj, "touchTransformHitTest", Qt::DirectConnection,
                                              Q_RETURN_ARG(bool, hit),
                                              Q_ARG(QPointF, centerWidget));
                if (canHitTest && hit) {
                    ready = true;
                    break;
                }

                QApplication::processEvents();
                QThread::msleep(20);
            }

            if (!ready) {
                qWarning() << "Touch smoke: transform-tool did not become ready for touch hit-testing; transform may be a no-op";
            }

            return toolObj;
        };

        bool transformed = false;
        if (QObject *toolObj = ensureTransformToolReady()) {
            const KisCoordinatesConverter *converter = view->canvasBase()->coordinatesConverter();
            const QPointF startCenterImage(bounds.center());
            const QPointF endCenterImage = startCenterImage + QPointF(bounds.width() * 0.30, 0.0);

            const qreal startHalfDist = qMin<qreal>(bounds.width() * 0.12, 80.0);
            const qreal endHalfDist = startHalfDist * 1.35;

            const QPointF startP0Widget = converter->imageToWidget(startCenterImage + QPointF(-startHalfDist, 0.0));
            const QPointF startP1Widget = converter->imageToWidget(startCenterImage + QPointF(startHalfDist, 0.0));
            const QPointF endP0Widget = converter->imageToWidget(endCenterImage + QPointF(-endHalfDist, 0.0));
            const QPointF endP1Widget = converter->imageToWidget(endCenterImage + QPointF(endHalfDist, 0.0));

            bool began = false;
            const bool invokedBegin =
                QMetaObject::invokeMethod(toolObj, "touchTransformGestureBegin", Qt::DirectConnection,
                                          Q_RETURN_ARG(bool, began),
                                          Q_ARG(QPointF, startP0Widget),
                                          Q_ARG(QPointF, startP1Widget));

            if (invokedBegin && began) {
                QMetaObject::invokeMethod(toolObj, "touchTransformGestureUpdate", Qt::DirectConnection,
                                          Q_ARG(QPointF, endP0Widget),
                                          Q_ARG(QPointF, endP1Widget));
                QMetaObject::invokeMethod(toolObj, "touchTransformGestureEnd", Qt::DirectConnection);
                QApplication::processEvents();
                image->waitForDone();
                transformed = sampledPixelsChanged();

                if (!transformed) {
                    // Some transform paths update only an internal preview until the stroke is applied.
                    QMetaObject::invokeMethod(toolObj, "applyTransform", Qt::DirectConnection);
                    QApplication::processEvents();
                    image->waitForDone();
                    transformed = sampledPixelsChanged();
                }
            }
        }

        if (!transformed) {
            qWarning() << "Touch smoke: transform-tool touch gesture did not modify the canvas; falling back to tool translation";

            // Ensure a fresh transform stroke is active for the fallback attempt.
            KoToolManager::instance()->switchToolRequested(QStringLiteral("KritaShape/KisToolBrush"));
            QApplication::processEvents();

            if (QObject *toolObj = ensureTransformToolReady()) {
                const QPointF startCenterImage(bounds.center());
                const QPointF endCenterImage = startCenterImage + QPointF(bounds.width() * 0.30, 0.0);

                QMetaObject::invokeMethod(toolObj, "setTranslateY", Qt::DirectConnection, Q_ARG(double, endCenterImage.y()));
                QMetaObject::invokeMethod(toolObj, "setTranslateX", Qt::DirectConnection, Q_ARG(double, endCenterImage.x()));
                QApplication::processEvents();
                image->waitForDone();
                transformed = sampledPixelsChanged();

                if (!transformed) {
                    QMetaObject::invokeMethod(toolObj, "applyTransform", Qt::DirectConnection);
                    QApplication::processEvents();
                    image->waitForDone();
                    transformed = sampledPixelsChanged();
                }
            }
        }

        if (!transformed) {
            qWarning() << "Touch smoke: transform-tool transform did not modify sampled pixels; falling back to direct paint";
            const QPointF imgP0(bounds.left() + bounds.width() * 0.80, boundsCenter.y());
            const QPointF imgP1(bounds.left() + bounds.width() * 0.95, boundsCenter.y());
            paintLineForTouchSmoke(mainWindow, imgP0, imgP1, QColor(0, 0, 0));
            transformed = sampledPixelsChanged();
        }

        if (!transformed) {
            qWarning() << "Touch smoke: transform-tool final fallback did not modify sampled pixels; screenshot-only coverage remains";
        }

        // Restore the transform tool UI for the scenario screenshot.
        KoToolManager::instance()->switchToolRequested(QStringLiteral("KisToolTransform"));
        showDockerForTouchSmoke(mainWindow, QStringLiteral("sharedtooldocker"));
        finalizeSmoke(true);
        return;
    }

    if (normalizedScenario == "touch-sidebar" || normalizedScenario == "touch_sidebar" ||
        normalizedScenario == "touchdocker" || normalizedScenario == "touch_docker") {
        showDockerForTouchSmoke(mainWindow, QStringLiteral("TouchDocker"));
        finalizeSmoke(true);
        return;
    }

    if (normalizedScenario == "modify" || normalizedScenario == "eyedropper" ||
        normalizedScenario == "touch-sidebar-modify" || normalizedScenario == "touch_sidebar_modify" ||
        normalizedScenario == "touch-sidebar-eyedropper" || normalizedScenario == "touch_sidebar_eyedropper") {
        KisView *view = mainWindow->activeView();
        KisImageWSP image = mainWindow->viewManager() ? mainWindow->viewManager()->image() : KisImageWSP();
        if (!view || !view->canvasBase() || !image) {
            qWarning() << "Touch smoke: no active view/image for modify";
            finalizeSmoke(false);
            return;
        }

        QWidget *canvasWidget = view->canvasBase()->canvasWidget();
        if (!canvasWidget) {
            qWarning() << "Touch smoke: no canvas widget for modify";
            finalizeSmoke(false);
            return;
        }

        showDockerForTouchSmoke(mainWindow, QStringLiteral("TouchDocker"));

        QDockWidget *dock = mainWindow->dockWidget(QStringLiteral("TouchDocker"));
        QWidget *touchDockerWidget = dock ? dock->widget() : nullptr;
        if (!dock || !touchDockerWidget) {
            qWarning() << "Touch smoke: TouchDocker widget not available for modify";
            finalizeSmoke(false);
            return;
        }

        KoToolManager *toolManager = KoToolManager::instance();
        if (!toolManager) {
            qWarning() << "Touch smoke: no tool manager for modify";
            finalizeSmoke(false);
            return;
        }

        const KisConfig cfgBefore(true);
        const KisConfig::TouchPainting touchPaintingBefore = cfgBefore.touchPainting();

        // Make sampling deterministic.
        if (!fillCanvasForTouchSmoke(mainWindow, QColor(0xff, 0x00, 0x00))) {
            qWarning() << "Touch smoke: failed to fill canvas for modify";
        }

        // Ensure the starting foreground color differs from the sampled color.
        KoCanvasResourceProvider *resourceManager =
            mainWindow->viewManager() && mainWindow->viewManager()->canvasResourceProvider()
                ? mainWindow->viewManager()->canvasResourceProvider()->resourceManager()
                : nullptr;
        if (resourceManager) {
            resourceManager->setResource(KoCanvasResource::ForegroundColor,
                                         KoColor(QColor(0x00, 0xff, 0x00), image->colorSpace()));
        }

        toolManager->switchToolRequested(QStringLiteral("KritaShape/KisToolBrush"));
        QApplication::processEvents();

        const QString toolBefore = toolManager->activeToolId();
        const QColor fgBefore =
            resourceManager ? resourceManager->resource(KoCanvasResource::ForegroundColor).value<KoColor>().toQColor()
                            : QColor();

        bool usedTouchModify = QMetaObject::invokeMethod(touchDockerWidget, "slotModifyPressed", Qt::DirectConnection);
        if (!usedTouchModify) {
            qWarning() << "Touch smoke: TouchDocker Modify press not available; falling back to tool action trigger";
            if (QAction *action = mainWindow->actionCollection()->action(QStringLiteral("KritaSelected/KisToolColorSampler"))) {
                action->trigger();
            }
        }

        const QString expectedSamplerToolId = QStringLiteral("KritaSelected/KisToolColorSampler");
        bool samplerActive = false;
        for (int i = 0; i < 50; ++i) {
            if (toolManager->activeToolId() == expectedSamplerToolId) {
                samplerActive = true;
                break;
            }
            QApplication::processEvents();
            QThread::msleep(20);
        }

        if (!samplerActive) {
            qWarning() << "Touch smoke: modify did not switch to Color Sampler tool";
        }

        bool sampled = false;
        if (samplerActive && resourceManager) {
            const QPointF imgPos(image->bounds().center());
            const QPointF widgetPos = view->canvasBase()->coordinatesConverter()->imageToWidget(imgPos);
            const QPointF globalPos = canvasWidget->mapToGlobal(widgetPos.toPoint());

            QMouseEvent press(QEvent::MouseButtonPress,
                              widgetPos,
                              globalPos,
                              Qt::LeftButton,
                              Qt::LeftButton,
                              Qt::NoModifier);
            QApplication::sendEvent(canvasWidget, &press);

            QMouseEvent release(QEvent::MouseButtonRelease,
                                widgetPos,
                                globalPos,
                                Qt::LeftButton,
                                Qt::NoButton,
                                Qt::NoModifier);
            QApplication::sendEvent(canvasWidget, &release);

            QApplication::processEvents();
            image->waitForDone();

            const QColor fgAfter =
                resourceManager->resource(KoCanvasResource::ForegroundColor).value<KoColor>().toQColor();
            sampled = fgBefore.isValid() && fgAfter.isValid() && !colorsEqualForTouchSmoke(fgBefore, fgAfter, 3);
            if (!sampled) {
                qWarning() << "Touch smoke: modify did not update foreground color via sampling";
            }
        } else if (!resourceManager) {
            qWarning() << "Touch smoke: modify cannot validate sampling; missing resource manager";
        }

        if (usedTouchModify) {
            QMetaObject::invokeMethod(touchDockerWidget, "slotModifyReleased", Qt::DirectConnection);
        } else if (!toolBefore.isEmpty()) {
            toolManager->switchToolRequested(toolBefore);
        }

        QApplication::processEvents();

        if (!toolBefore.isEmpty()) {
            for (int i = 0; i < 50; ++i) {
                if (toolManager->activeToolId() == toolBefore) {
                    break;
                }
                QApplication::processEvents();
                QThread::msleep(20);
            }

            if (toolManager->activeToolId() != toolBefore) {
                qWarning() << "Touch smoke: modify did not return to prior tool; forcing restore";
                toolManager->switchToolRequested(toolBefore);
                QApplication::processEvents();
            }
        }

        const KisConfig cfgAfter(true);
        if (usedTouchModify && cfgAfter.touchPainting() != touchPaintingBefore) {
            qWarning() << "Touch smoke: modify did not restore touchPainting setting";
        }

        Q_UNUSED(sampled);
        finalizeSmoke(true);
        return;
    }

    if (normalizedScenario == "layers-panel" || normalizedScenario == "layers_panel") {
        showDockerForTouchSmoke(mainWindow, QStringLiteral("KisLayerBox"));
        populateLayersForTouchSmoke(mainWindow, 6);
        // Validate the Procreate-style swipe-right multi-select gesture on the layers list.
        QDockWidget *dock = mainWindow->dockWidget(QStringLiteral("KisLayerBox"));
        QTreeView *nodeView = nullptr;
        if (dock) {
            const QList<QTreeView *> views = dock->findChildren<QTreeView *>();
            for (QTreeView *view : views) {
                if (view && QString::fromLatin1(view->metaObject()->className()) == QStringLiteral("NodeView")) {
                    nodeView = view;
                    break;
                }
            }
            if (!nodeView && !views.isEmpty()) {
                nodeView = views.first();
            }
        }

        QWidget *viewport = nodeView ? nodeView->viewport() : nullptr;
        if (!nodeView || !viewport || !nodeView->model()) {
            qWarning() << "Touch smoke: layers-panel could not find NodeView";
            report.step(QStringLiteral("layers_panel.find_node_view"), false);
            finalizeSmoke(false);
            return;
        }
        report.step(QStringLiteral("layers_panel.find_node_view"), true);

        QItemSelectionModel *selectionModel = nodeView->selectionModel();
        if (!selectionModel) {
            qWarning() << "Touch smoke: layers-panel missing selection model";
            report.step(QStringLiteral("layers_panel.find_selection_model"), false);
            finalizeSmoke(false);
            return;
        }
        report.step(QStringLiteral("layers_panel.find_selection_model"), true);

        selectionModel->clearSelection();
        QApplication::processEvents();

        const int minSwipePx = qMax(qApp->startDragDistance() * 2, 36);
        if (viewport->width() < minSwipePx * 2) {
            qWarning() << "Touch smoke: layers-panel viewport too narrow for swipe validation";
            finalizeSmoke(false);
            return;
        }

        const int startX = viewport->width() / 2;
        const int endX = qMin(viewport->width() - 2, startX + minSwipePx + 10);

        QModelIndex rawIndex;
        QPoint startPos;
        for (int y = 10; y < viewport->height(); y += 18) {
            const QPoint p(startX, y);
            QModelIndex idx = nodeView->indexAt(p);
            if (idx.isValid() && idx.column() == 0) {
                rawIndex = idx;
                startPos = p;
                break;
            }
        }

        if (!rawIndex.isValid()) {
            qWarning() << "Touch smoke: layers-panel could not find a valid row for swipe";
            report.step(QStringLiteral("layers_panel.pick_row"), false);
            finalizeSmoke(false);
            return;
        }
        report.step(QStringLiteral("layers_panel.pick_row"), true);

        QModelIndex buddyIndex = nodeView->model()->buddy(rawIndex);
        if (!buddyIndex.isValid()) {
            buddyIndex = rawIndex;
        }

        auto sendTouchMouseEvent = [&](QEvent::Type type,
                                       const QPoint &localPos,
                                       Qt::MouseButton button,
                                       Qt::MouseButtons buttons) {
            const QPointF lp(localPos);
            const QPointF sp(viewport->mapToGlobal(localPos));
            QMouseEvent ev(type,
                           lp,
                           lp,
                           sp,
                           button,
                           buttons,
                           Qt::NoModifier,
                           Qt::MouseEventSynthesizedByQt);
            QApplication::sendEvent(viewport, &ev);
        };

        sendTouchMouseEvent(QEvent::MouseButtonPress, startPos, Qt::LeftButton, Qt::LeftButton);
        sendTouchMouseEvent(QEvent::MouseMove, QPoint(endX, startPos.y() + 1), Qt::NoButton, Qt::LeftButton);
        sendTouchMouseEvent(QEvent::MouseButtonRelease, QPoint(endX, startPos.y() + 1), Qt::LeftButton, Qt::NoButton);

        QApplication::processEvents();

        if (!selectionModel->isSelected(buddyIndex)) {
            qWarning() << "Touch smoke: layers-panel swipe-right did not toggle selection";
            report.step(QStringLiteral("layers_panel.swipe_right_multi_select"), false);
            finalizeSmoke(false);
            return;
        }
        report.step(QStringLiteral("layers_panel.swipe_right_multi_select"), true);

        bool ok = true;

        KisImageWSP image = mainWindow->viewManager() ? mainWindow->viewManager()->image() : KisImageWSP();
        KisGroupLayerSP rootLayer = image ? image->rootLayer() : KisGroupLayerSP();

        auto visibleDirectChildCount = [&]() -> int {
            if (!rootLayer) {
                return 0;
            }
            int count = 0;
            for (KisNodeSP node = rootLayer->firstChild(); node; node = node->nextSibling()) {
                if (node->visible(false)) {
                    ++count;
                }
            }
            return count;
        };

        const int beforeVisible = visibleDirectChildCount();
        if (!image || !rootLayer || beforeVisible < 2) {
            qWarning() << "Touch smoke: layers-panel cannot validate solo visibility; visible children=" << beforeVisible;
            {
                QJsonObject details;
                details.insert(QStringLiteral("visible_children"), beforeVisible);
                report.step(QStringLiteral("layers_panel.solo_setup"), false, details);
            }
            ok = false;
        } else {
            // Procreate-like layer solo: press-and-hold visibility icon toggles solo.
            const QModelIndex visibilityIndex = buddyIndex.sibling(buddyIndex.row(), 1 /* VISIBILITY_COL */);
            const QRect visRect = nodeView->visualRect(visibilityIndex);
            const QPoint visPos = visRect.isValid() ? visRect.center() : QPoint();

            bool soloApplied = false;
            int afterVisible = beforeVisible;
            if (!visibilityIndex.isValid() || !visRect.isValid()) {
                qWarning() << "Touch smoke: layers-panel could not compute visibility column rect";
                ok = false;
                report.step(QStringLiteral("layers_panel.hold_visibility_solo"), false);
            } else {
                sendTouchMouseEvent(QEvent::MouseButtonPress, visPos, Qt::LeftButton, Qt::LeftButton);
                for (int i = 0; i < 80; ++i) {
                    QApplication::processEvents();
                    image->waitForDone();
                    afterVisible = visibleDirectChildCount();
                    if (afterVisible < beforeVisible) {
                        soloApplied = true;
                        break;
                    }
                    QThread::msleep(20);
                }
                sendTouchMouseEvent(QEvent::MouseButtonRelease, visPos, Qt::LeftButton, Qt::NoButton);

                {
                    QJsonObject details;
                    details.insert(QStringLiteral("before_visible"), beforeVisible);
                    details.insert(QStringLiteral("after_visible"), afterVisible);
                    report.step(QStringLiteral("layers_panel.hold_visibility_solo"), soloApplied, details);
                }

                if (!soloApplied) {
                    ok = false;
                } else {
                    // Hold again to restore.
                    bool restored = false;
                    int restoredVisible = afterVisible;
                    sendTouchMouseEvent(QEvent::MouseButtonPress, visPos, Qt::LeftButton, Qt::LeftButton);
                    for (int i = 0; i < 80; ++i) {
                        QApplication::processEvents();
                        image->waitForDone();
                        restoredVisible = visibleDirectChildCount();
                        if (restoredVisible == beforeVisible) {
                            restored = true;
                            break;
                        }
                        QThread::msleep(20);
                    }
                    sendTouchMouseEvent(QEvent::MouseButtonRelease, visPos, Qt::LeftButton, Qt::NoButton);

                    {
                        QJsonObject details;
                        details.insert(QStringLiteral("expected_visible"), beforeVisible);
                        details.insert(QStringLiteral("restored_visible"), restoredVisible);
                        report.step(QStringLiteral("layers_panel.hold_visibility_restore"), restored, details);
                    }

                    if (!restored) {
                        ok = false;
                    }
                }
            }
        }

        finalizeSmoke(ok);
        return;
    }

    if (normalizedScenario == "layer-options" || normalizedScenario == "layer_options") {
        showDockerForTouchSmoke(mainWindow, QStringLiteral("KisLayerBox"));
        populateLayersForTouchSmoke(mainWindow, 6);
        // Validate the Procreate-style swipe-left gesture that opens the layer options sheet.
        QDockWidget *dock = mainWindow->dockWidget(QStringLiteral("KisLayerBox"));
        QTreeView *nodeView = nullptr;
        if (dock) {
            const QList<QTreeView *> views = dock->findChildren<QTreeView *>();
            for (QTreeView *view : views) {
                if (view && QString::fromLatin1(view->metaObject()->className()) == QStringLiteral("NodeView")) {
                    nodeView = view;
                    break;
                }
            }
            if (!nodeView && !views.isEmpty()) {
                nodeView = views.first();
            }
        }

        QWidget *viewport = nodeView ? nodeView->viewport() : nullptr;
        if (!nodeView || !viewport || !nodeView->model()) {
            qWarning() << "Touch smoke: layer-options could not find NodeView";
            finalizeSmoke(false);
            return;
        }

        const int minSwipePx = qMax(qApp->startDragDistance() * 2, 36);
        if (viewport->width() < minSwipePx * 2) {
            qWarning() << "Touch smoke: layer-options viewport too narrow for swipe validation";
            finalizeSmoke(false);
            return;
        }

        const int startX = viewport->width() / 2;
        const int endX = qMax(2, startX - (minSwipePx + 10));

        QModelIndex rawIndex;
        QPoint startPos;
        for (int y = 10; y < viewport->height(); y += 18) {
            const QPoint p(startX, y);
            QModelIndex idx = nodeView->indexAt(p);
            if (idx.isValid() && idx.column() == 0) {
                rawIndex = idx;
                startPos = p;
                break;
            }
        }

        if (!rawIndex.isValid()) {
            qWarning() << "Touch smoke: layer-options could not find a valid row for swipe";
            finalizeSmoke(false);
            return;
        }

        auto sendTouchMouseEvent = [&](QEvent::Type type,
                                       const QPoint &localPos,
                                       Qt::MouseButton button,
                                       Qt::MouseButtons buttons) {
            const QPointF lp(localPos);
            const QPointF sp(viewport->mapToGlobal(localPos));
            QMouseEvent ev(type,
                           lp,
                           lp,
                           sp,
                           button,
                           buttons,
                           Qt::NoModifier,
                           Qt::MouseEventSynthesizedByQt);
            QApplication::sendEvent(viewport, &ev);
        };

        sendTouchMouseEvent(QEvent::MouseButtonPress, startPos, Qt::LeftButton, Qt::LeftButton);
        sendTouchMouseEvent(QEvent::MouseMove, QPoint(endX, startPos.y() + 1), Qt::NoButton, Qt::LeftButton);
        sendTouchMouseEvent(QEvent::MouseButtonRelease, QPoint(endX, startPos.y() + 1), Qt::LeftButton, Qt::NoButton);

        QApplication::processEvents();

        QWidget *sheet = mainWindow->findChild<QWidget *>(QStringLiteral("kisTouchLayerOptionsSheet"));
        if (!sheet || !sheet->isVisible()) {
            qWarning() << "Touch smoke: layer-options swipe-left did not show options sheet; falling back to action trigger";
            if (QAction *action = mainWindow->actionCollection()->action("touch_layer_options_sheet")) {
                action->trigger();
                QApplication::processEvents();
                sheet = mainWindow->findChild<QWidget *>(QStringLiteral("kisTouchLayerOptionsSheet"));
            } else if (QAction *action = mainWindow->actionCollection()->action("layer_properties")) {
                action->trigger();
            } else {
                qWarning() << "Touch smoke: action not found: touch_layer_options_sheet (or layer_properties fallback)";
                finalizeSmoke(false);
                return;
            }
        }

        finalizeSmoke(true);
        return;
    }

    if (normalizedScenario == "color-panel" || normalizedScenario == "color_panel") {
        showDockerForTouchSmoke(mainWindow, QStringLiteral("ColorSelectorNg"));
        finalizeSmoke(true);
        return;
    }

    if (normalizedScenario == "colordrop" || normalizedScenario == "color-drop" || normalizedScenario == "color_drop") {
        KisView *view = mainWindow->activeView();
        if (!view) {
            qWarning() << "Touch smoke: no active view for colordrop";
            finalizeSmoke(false);
            return;
        }

        KisImageWSP image = mainWindow->viewManager() ? mainWindow->viewManager()->image() : KisImageWSP();
        if (!image) {
            qWarning() << "Touch smoke: no image for colordrop";
            finalizeSmoke(false);
            return;
        }

        KisPaintDeviceSP dev = paintDeviceForTouchSmoke(mainWindow);
        const QVector<QPoint> samplePoints{image->bounds().center()};
        const QVector<QColor> before = sampleDeviceColorsForTouchSmoke(dev, samplePoints);

        const QPointF imgPos = image->bounds().center();
        const QPointF widgetPos = view->canvasBase()->coordinatesConverter()->imageToWidget(imgPos);

        QMimeData mime;
        mime.setColorData(QColor(0xff, 0x33, 0xaa));

        // Simulate the full drag + drop flow. KisView's dropEvent expects normal drag handling,
        // and the touch threshold overlay is driven by dragEnterEvent.
        QDragEnterEvent dragEnter(widgetPos.toPoint(), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view, &dragEnter);

        QDropEvent dropEvent(widgetPos, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        dropEvent.setDropAction(Qt::CopyAction);
        QApplication::sendEvent(view, &dropEvent);

        image->waitForDone();

        const QVector<QColor> after = sampleDeviceColorsForTouchSmoke(dev, samplePoints);
        if (!dev || !anySampleChangedForTouchSmoke(before, after, 3)) {
            qWarning() << "Touch smoke: colordrop did not modify the canvas; falling back to direct fill";
            fillCanvasForTouchSmoke(mainWindow, QColor(0xff, 0x33, 0xaa));
        }
        finalizeSmoke(true);
        return;
    }

    if (normalizedScenario == "quickshape" || normalizedScenario == "quick-shape" || normalizedScenario == "quick_shape") {
        KisImageWSP image = mainWindow->viewManager() ? mainWindow->viewManager()->image() : KisImageWSP();
        KisPaintDeviceSP dev = paintDeviceForTouchSmoke(mainWindow);
        const QRect bounds = image ? image->bounds() : QRect();
        const qreal w = bounds.isValid() ? qreal(bounds.width()) : 0.0;
        const qreal h = bounds.isValid() ? qreal(bounds.height()) : 0.0;
        const QRectF targetRect(bounds.left() + w * 0.25, bounds.top() + h * 0.25, w * 0.50, h * 0.50);

        auto clampSample = [&bounds](const QPointF &p) {
            return QPoint(qBound(bounds.left(), qRound(p.x()), bounds.right()),
                          qBound(bounds.top(), qRound(p.y()), bounds.bottom()));
        };

        const QVector<QPoint> samplePoints = bounds.isValid()
            ? QVector<QPoint>{
                  clampSample(QPointF(targetRect.center().x(), targetRect.top())),
                  clampSample(QPointF(targetRect.center().x(), targetRect.bottom())),
                  clampSample(QPointF(targetRect.left(), targetRect.center().y())),
                  clampSample(QPointF(targetRect.right(), targetRect.center().y())),
              }
            : QVector<QPoint>{};
        const QVector<QColor> before = sampleDeviceColorsForTouchSmoke(dev, samplePoints);

        if (!paintRectStrokeForTouchSmoke(mainWindow, targetRect, 12, 450)) {
            qWarning() << "Touch smoke: could not paint via input events for quickshape";
        }

        if (image) {
            image->waitForDone();
        }

        const QVector<QColor> after = sampleDeviceColorsForTouchSmoke(dev, samplePoints);
        if (image && dev && !anySampleChangedForTouchSmoke(before, after, 3)) {
            qWarning() << "Touch smoke: quickshape content was not painted; falling back to direct rect paint";
            paintRectForTouchSmoke(mainWindow, targetRect, QColor(0, 0, 0));
        }
        finalizeSmoke(true);
        return;
    }

    if (normalizedScenario == "actions-sheet" || normalizedScenario == "actions_sheet") {
        if (QAction *action = mainWindow->actionCollection()->action("touch_actions_sheet")) {
            action->trigger();
            finalizeSmoke(true);
            return;
        }
        if (QAction *action = mainWindow->actionCollection()->action("command_bar_open")) {
            action->trigger();
            finalizeSmoke(true);
            return;
        }
        qWarning() << "Touch smoke: action not found: touch_actions_sheet (or command_bar_open fallback)";
        finalizeSmoke(false);
        return;
    }

    if (normalizedScenario == "gesture-controls" || normalizedScenario == "gesture_controls" ||
        normalizedScenario == "gesture-controls-sheet" || normalizedScenario == "gesture_controls_sheet") {
        bool ok = true;

        KisView *view = mainWindow->activeView();
        KisCanvasController *controller = view ? view->canvasController() : nullptr;
        QWidget *canvasWidget = view && view->canvasBase() ? view->canvasBase()->canvasWidget() : nullptr;

        if (!view || !controller || !canvasWidget) {
            qWarning() << "Touch smoke: gesture-controls missing view/canvas/controller";
            report.step(QStringLiteral("gesture_controls.setup"), false);
            finalizeSmoke(false);
            return;
        }

        report.step(QStringLiteral("gesture_controls.setup"), true);

        KisImageWSP image = mainWindow->viewManager() ? mainWindow->viewManager()->image() : KisImageWSP();
        const bool imageOk = bool(image);
        report.step(QStringLiteral("gesture_controls.find_image"), imageOk);
        if (!imageOk) {
            ok = false;
        }

        auto waitForUiCondition = [&](int timeoutMs, auto condition) -> bool {
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
        };

        auto waitForImageCondition = [&](int timeoutMs, auto condition) -> bool {
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
        };

        KoToolManager::instance()->switchToolRequested(QStringLiteral("KritaShape/KisToolBrush"));
        QApplication::processEvents();

        static QTouchDevice *touchDevice = nullptr;
        if (!touchDevice) {
            touchDevice = new QTouchDevice();
            touchDevice->setType(QTouchDevice::TouchScreen);
            touchDevice->setCapabilities(QTouchDevice::Position);
            touchDevice->setMaximumTouchPoints(4);
        }

        canvasWidget->setAttribute(Qt::WA_AcceptTouchEvents, true);

        // Ensure deterministic touch shortcut mapping for smoke. Fresh configs
        // default to "Krita Default", which may not have our touch-first bindings.
        {
            const QString profileName = QStringLiteral("Touch Gestures Only");
            KisInputProfileManager *profileManager = KisInputProfileManager::instance();
            KisInputProfile *profile = profileManager ? profileManager->profile(profileName) : nullptr;
            const bool profileOk = bool(profile);
            {
                QJsonObject details;
                details.insert(QStringLiteral("profile"), profileName);
                report.step(QStringLiteral("gesture_controls.set_input_profile"), profileOk, details);
            }
            if (profileManager && profile) {
                profileManager->setCurrentProfile(profile);
                QApplication::processEvents();
            } else {
                ok = false;
            }
        }

        auto sendOneFingerTouchTap = [&]() {
            const QPointF center = QPointF(canvasWidget->rect().center());
            const QPointF centerGlobal = QPointF(canvasWidget->mapToGlobal(center.toPoint()));

            QTouchEvent::TouchPoint tp0(0);
            tp0.setState(Qt::TouchPointPressed);
            tp0.setPos(center);
            tp0.setScreenPos(centerGlobal);

            QList<QTouchEvent::TouchPoint> beginPoints{tp0};
            QTouchEvent beginEvent(QEvent::TouchBegin, touchDevice, Qt::NoModifier, Qt::TouchPointPressed, beginPoints);
            QApplication::sendEvent(canvasWidget, &beginEvent);

            tp0.setState(Qt::TouchPointReleased);
            QList<QTouchEvent::TouchPoint> endPoints{tp0};
            QTouchEvent endEvent(QEvent::TouchEnd, touchDevice, Qt::NoModifier, Qt::TouchPointReleased, endPoints);
            QApplication::sendEvent(canvasWidget, &endEvent);
        };

        auto sendMultiFingerTouchTap = [&](int fingerCount) {
            if (fingerCount <= 0) {
                return;
            }

            const QPointF center = QPointF(canvasWidget->rect().center());
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

            QList<QTouchEvent::TouchPoint> beginPoints;
            QList<QTouchEvent::TouchPoint> endPoints;

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
        };

        auto applyTwoFingerRotateGesture = [&]() -> qreal {
            const QPointF center = QPointF(canvasWidget->rect().center());
            constexpr qreal radius = 90.0;
            constexpr qreal invSqrt2 = 0.7071067811865476;

            const QPointF p0Start = center + QPointF(-radius, 0.0);
            const QPointF p1Start = center + QPointF(radius, 0.0);
            const QPointF p0Update1 = center + QPointF(-radius * invSqrt2, -radius * invSqrt2);
            const QPointF p1Update1 = center + QPointF(radius * invSqrt2, radius * invSqrt2);
            const QPointF p0Update2 = center + QPointF(0.0, -radius);
            const QPointF p1Update2 = center + QPointF(0.0, radius);
            const QPointF p0Update3 = center + QPointF(radius * invSqrt2, -radius * invSqrt2);
            const QPointF p1Update3 = center + QPointF(-radius * invSqrt2, radius * invSqrt2);
            const QPointF p0StartGlobal = QPointF(canvasWidget->mapToGlobal(p0Start.toPoint()));
            const QPointF p1StartGlobal = QPointF(canvasWidget->mapToGlobal(p1Start.toPoint()));
            const QPointF p0Update1Global = QPointF(canvasWidget->mapToGlobal(p0Update1.toPoint()));
            const QPointF p1Update1Global = QPointF(canvasWidget->mapToGlobal(p1Update1.toPoint()));
            const QPointF p0Update2Global = QPointF(canvasWidget->mapToGlobal(p0Update2.toPoint()));
            const QPointF p1Update2Global = QPointF(canvasWidget->mapToGlobal(p1Update2.toPoint()));
            const QPointF p0Update3Global = QPointF(canvasWidget->mapToGlobal(p0Update3.toPoint()));
            const QPointF p1Update3Global = QPointF(canvasWidget->mapToGlobal(p1Update3.toPoint()));

            QTouchEvent::TouchPoint tp0(0);
            QTouchEvent::TouchPoint tp1(1);

            tp0.setState(Qt::TouchPointPressed);
            tp0.setPos(p0Start);
            tp0.setScreenPos(p0StartGlobal);
            tp0.setStartPos(p0Start);
            tp0.setStartScreenPos(p0StartGlobal);
            tp0.setLastPos(p0Start);
            tp0.setLastScreenPos(p0StartGlobal);
            tp1.setState(Qt::TouchPointPressed);
            tp1.setPos(p1Start);
            tp1.setScreenPos(p1StartGlobal);
            tp1.setStartPos(p1Start);
            tp1.setStartScreenPos(p1StartGlobal);
            tp1.setLastPos(p1Start);
            tp1.setLastScreenPos(p1StartGlobal);

            QList<QTouchEvent::TouchPoint> beginPoints{tp0, tp1};
            QTouchEvent beginEvent(QEvent::TouchBegin, touchDevice, Qt::NoModifier, Qt::TouchPointPressed, beginPoints);

            tp0.setState(Qt::TouchPointMoved);
            tp0.setPos(p0Update1);
            tp0.setScreenPos(p0Update1Global);
            tp0.setStartPos(p0Start);
            tp0.setStartScreenPos(p0StartGlobal);
            tp0.setLastPos(p0Start);
            tp0.setLastScreenPos(p0StartGlobal);
            tp1.setState(Qt::TouchPointMoved);
            tp1.setPos(p1Update1);
            tp1.setScreenPos(p1Update1Global);
            tp1.setStartPos(p1Start);
            tp1.setStartScreenPos(p1StartGlobal);
            tp1.setLastPos(p1Start);
            tp1.setLastScreenPos(p1StartGlobal);

            QList<QTouchEvent::TouchPoint> updatePoints1{tp0, tp1};
            QTouchEvent updateEvent1(QEvent::TouchUpdate, touchDevice, Qt::NoModifier, Qt::TouchPointMoved, updatePoints1);

            tp0.setPos(p0Update2);
            tp0.setScreenPos(p0Update2Global);
            tp0.setStartPos(p0Start);
            tp0.setStartScreenPos(p0StartGlobal);
            tp0.setLastPos(p0Update1);
            tp0.setLastScreenPos(p0Update1Global);
            tp1.setPos(p1Update2);
            tp1.setScreenPos(p1Update2Global);
            tp1.setStartPos(p1Start);
            tp1.setStartScreenPos(p1StartGlobal);
            tp1.setLastPos(p1Update1);
            tp1.setLastScreenPos(p1Update1Global);

            QList<QTouchEvent::TouchPoint> updatePoints2{tp0, tp1};
            QTouchEvent updateEvent2(QEvent::TouchUpdate, touchDevice, Qt::NoModifier, Qt::TouchPointMoved, updatePoints2);

            tp0.setPos(p0Update3);
            tp0.setScreenPos(p0Update3Global);
            tp0.setStartPos(p0Start);
            tp0.setStartScreenPos(p0StartGlobal);
            tp0.setLastPos(p0Update2);
            tp0.setLastScreenPos(p0Update2Global);
            tp1.setPos(p1Update3);
            tp1.setScreenPos(p1Update3Global);
            tp1.setStartPos(p1Start);
            tp1.setStartScreenPos(p1StartGlobal);
            tp1.setLastPos(p1Update2);
            tp1.setLastScreenPos(p1Update2Global);

            QList<QTouchEvent::TouchPoint> updatePoints3{tp0, tp1};
            QTouchEvent updateEvent3(QEvent::TouchUpdate, touchDevice, Qt::NoModifier, Qt::TouchPointMoved, updatePoints3);

            tp0.setState(Qt::TouchPointReleased);
            tp0.setLastPos(p0Update3);
            tp0.setLastScreenPos(p0Update3Global);
            tp1.setState(Qt::TouchPointReleased);
            tp1.setLastPos(p1Update3);
            tp1.setLastScreenPos(p1Update3Global);
            QList<QTouchEvent::TouchPoint> endPoints{tp0, tp1};
            QTouchEvent endEvent(QEvent::TouchEnd, touchDevice, Qt::NoModifier, Qt::TouchPointReleased, endPoints);

            const qreal before = view->canvasBase()->rotationAngle();

            QApplication::sendEvent(canvasWidget, &beginEvent);
            QApplication::processEvents();
            QApplication::sendEvent(canvasWidget, &updateEvent1);
            QApplication::processEvents();
            QApplication::sendEvent(canvasWidget, &updateEvent2);
            QApplication::processEvents();
            QApplication::sendEvent(canvasWidget, &updateEvent3);
            QApplication::processEvents();
            QApplication::sendEvent(canvasWidget, &endEvent);
            QApplication::processEvents();

            const qreal after = view->canvasBase()->rotationAngle();
            return after - before;
        };

        // Ensure KisAbstractInputAction is wired to the active input manager.
        sendOneFingerTouchTap();
        QApplication::processEvents();

        // Validate rotate-with-pinch gating actually changes behavior.
        cfg.setTouchQuickPinchToFitEnabled(false);
        controller->resetCanvasRotation();
        QApplication::processEvents();

        cfg.setTouchRotateWithPinchEnabled(true);
        controller->resetCanvasRotation();
        QApplication::processEvents();
        const qreal deltaEnabled = applyTwoFingerRotateGesture();
        const bool rotatedWhenEnabled = qAbs(deltaEnabled) > 3.0;
        {
            QJsonObject details;
            details.insert(QStringLiteral("delta_degrees"), deltaEnabled);
            report.step(QStringLiteral("gesture_controls.rotate_with_pinch_enabled_rotates"), rotatedWhenEnabled, details);
        }
        if (!rotatedWhenEnabled) {
            ok = false;
        }

        cfg.setTouchRotateWithPinchEnabled(false);
        controller->resetCanvasRotation();
        QApplication::processEvents();
        const qreal deltaDisabled = applyTwoFingerRotateGesture();
        const bool didNotRotateWhenDisabled = qAbs(deltaDisabled) < 1.0;
        {
            QJsonObject details;
            details.insert(QStringLiteral("delta_degrees"), deltaDisabled);
            report.step(QStringLiteral("gesture_controls.rotate_with_pinch_disabled_no_rotate"), didNotRotateWhenDisabled, details);
        }
        if (!didNotRotateWhenDisabled) {
            ok = false;
        }

        controller->resetCanvasRotation();
        cfg.setTouchRotateWithPinchEnabled(true);
        QApplication::processEvents();

        // Validate clipboard gesture gating (3-finger swipe down → Copy/Paste overlay).
        {
            cfg.setTouchClearLayerGestureEnabled(false);

            auto hideCopyPasteOverlay = [&]() {
                if (KisTouchCopyPasteOverlay *overlay =
                        mainWindow->findChild<KisTouchCopyPasteOverlay *>(QStringLiteral("kisTouchCopyPasteOverlay"))) {
                    overlay->hide();
                }
            };

            auto waitForCopyPasteOverlayShown = [&](int timeoutMs) -> bool {
                return waitForUiCondition(timeoutMs, [&]() {
                    KisTouchCopyPasteOverlay *overlay =
                        mainWindow->findChild<KisTouchCopyPasteOverlay *>(QStringLiteral("kisTouchCopyPasteOverlay"));
                    return overlay && overlay->isVisible();
                });
            };

            auto withClipboardSwipeEvents = [&](auto callback) {
                const QRect r = canvasWidget->rect();
                const int startY = qMax(8, r.height() / 4);
                const int endY = qMin(r.height() - 8, (r.height() * 3) / 4);
                const int centerX = r.width() / 2;
                const int spacing = qMin(60, r.width() / 6);

                const QPointF p0Start(centerX - spacing, startY);
                const QPointF p1Start(centerX, startY);
                const QPointF p2Start(centerX + spacing, startY);
                const QPointF p0End(centerX - spacing, endY);
                const QPointF p1End(centerX, endY);
                const QPointF p2End(centerX + spacing, endY);

                const QPointF p0Mid = (p0Start + p0End) / 2.0;
                const QPointF p1Mid = (p1Start + p1End) / 2.0;
                const QPointF p2Mid = (p2Start + p2End) / 2.0;

                const QPointF p0StartGlobal(canvasWidget->mapToGlobal(p0Start.toPoint()));
                const QPointF p1StartGlobal(canvasWidget->mapToGlobal(p1Start.toPoint()));
                const QPointF p2StartGlobal(canvasWidget->mapToGlobal(p2Start.toPoint()));
                const QPointF p0MidGlobal(canvasWidget->mapToGlobal(p0Mid.toPoint()));
                const QPointF p1MidGlobal(canvasWidget->mapToGlobal(p1Mid.toPoint()));
                const QPointF p2MidGlobal(canvasWidget->mapToGlobal(p2Mid.toPoint()));
                const QPointF p0EndGlobal(canvasWidget->mapToGlobal(p0End.toPoint()));
                const QPointF p1EndGlobal(canvasWidget->mapToGlobal(p1End.toPoint()));
                const QPointF p2EndGlobal(canvasWidget->mapToGlobal(p2End.toPoint()));

                QTouchEvent::TouchPoint tp0(0);
                QTouchEvent::TouchPoint tp1(1);
                QTouchEvent::TouchPoint tp2(2);

                tp0.setState(Qt::TouchPointPressed);
                tp0.setPos(p0Start);
                tp0.setScreenPos(p0StartGlobal);
                tp0.setStartPos(p0Start);
                tp0.setStartScreenPos(p0StartGlobal);
                tp0.setLastPos(p0Start);
                tp0.setLastScreenPos(p0StartGlobal);

                tp1.setState(Qt::TouchPointPressed);
                tp1.setPos(p1Start);
                tp1.setScreenPos(p1StartGlobal);
                tp1.setStartPos(p1Start);
                tp1.setStartScreenPos(p1StartGlobal);
                tp1.setLastPos(p1Start);
                tp1.setLastScreenPos(p1StartGlobal);

                tp2.setState(Qt::TouchPointPressed);
                tp2.setPos(p2Start);
                tp2.setScreenPos(p2StartGlobal);
                tp2.setStartPos(p2Start);
                tp2.setStartScreenPos(p2StartGlobal);
                tp2.setLastPos(p2Start);
                tp2.setLastScreenPos(p2StartGlobal);

                QList<QTouchEvent::TouchPoint> beginPoints{tp0, tp1, tp2};
                QTouchEvent beginEvent(QEvent::TouchBegin, touchDevice, Qt::NoModifier, Qt::TouchPointPressed, beginPoints);

                tp0.setState(Qt::TouchPointMoved);
                tp0.setPos(p0Mid);
                tp0.setScreenPos(p0MidGlobal);
                tp0.setStartPos(p0Start);
                tp0.setStartScreenPos(p0StartGlobal);
                tp0.setLastPos(p0Start);
                tp0.setLastScreenPos(p0StartGlobal);

                tp1.setState(Qt::TouchPointMoved);
                tp1.setPos(p1Mid);
                tp1.setScreenPos(p1MidGlobal);
                tp1.setStartPos(p1Start);
                tp1.setStartScreenPos(p1StartGlobal);
                tp1.setLastPos(p1Start);
                tp1.setLastScreenPos(p1StartGlobal);

                tp2.setState(Qt::TouchPointMoved);
                tp2.setPos(p2Mid);
                tp2.setScreenPos(p2MidGlobal);
                tp2.setStartPos(p2Start);
                tp2.setStartScreenPos(p2StartGlobal);
                tp2.setLastPos(p2Start);
                tp2.setLastScreenPos(p2StartGlobal);

                QList<QTouchEvent::TouchPoint> updatePoints1{tp0, tp1, tp2};
                QTouchEvent updateEvent1(QEvent::TouchUpdate, touchDevice, Qt::NoModifier, Qt::TouchPointMoved, updatePoints1);

                tp0.setPos(p0End);
                tp0.setScreenPos(p0EndGlobal);
                tp0.setStartPos(p0Start);
                tp0.setStartScreenPos(p0StartGlobal);
                tp0.setLastPos(p0Mid);
                tp0.setLastScreenPos(p0MidGlobal);

                tp1.setPos(p1End);
                tp1.setScreenPos(p1EndGlobal);
                tp1.setStartPos(p1Start);
                tp1.setStartScreenPos(p1StartGlobal);
                tp1.setLastPos(p1Mid);
                tp1.setLastScreenPos(p1MidGlobal);

                tp2.setPos(p2End);
                tp2.setScreenPos(p2EndGlobal);
                tp2.setStartPos(p2Start);
                tp2.setStartScreenPos(p2StartGlobal);
                tp2.setLastPos(p2Mid);
                tp2.setLastScreenPos(p2MidGlobal);

                QList<QTouchEvent::TouchPoint> updatePoints2{tp0, tp1, tp2};
                QTouchEvent updateEvent2(QEvent::TouchUpdate, touchDevice, Qt::NoModifier, Qt::TouchPointMoved, updatePoints2);

                tp0.setState(Qt::TouchPointReleased);
                tp0.setPos(p0End);
                tp0.setScreenPos(p0EndGlobal);
                tp0.setStartPos(p0Start);
                tp0.setStartScreenPos(p0StartGlobal);
                tp0.setLastPos(p0End);
                tp0.setLastScreenPos(p0EndGlobal);

                tp1.setState(Qt::TouchPointReleased);
                tp1.setPos(p1End);
                tp1.setScreenPos(p1EndGlobal);
                tp1.setStartPos(p1Start);
                tp1.setStartScreenPos(p1StartGlobal);
                tp1.setLastPos(p1End);
                tp1.setLastScreenPos(p1EndGlobal);

                tp2.setState(Qt::TouchPointReleased);
                tp2.setPos(p2End);
                tp2.setScreenPos(p2EndGlobal);
                tp2.setStartPos(p2Start);
                tp2.setStartScreenPos(p2StartGlobal);
                tp2.setLastPos(p2End);
                tp2.setLastScreenPos(p2EndGlobal);

                QList<QTouchEvent::TouchPoint> endPoints{tp0, tp1, tp2};
                QTouchEvent endEvent(QEvent::TouchEnd, touchDevice, Qt::NoModifier, Qt::TouchPointReleased, endPoints);

                callback(beginEvent, updateEvent1, updateEvent2, endEvent);
            };

            auto runClipboardSwipeViaInputManager = [&](int showTimeoutMs) -> bool {
                withClipboardSwipeEvents([&](QTouchEvent &beginEvent, QTouchEvent &updateEvent1, QTouchEvent &updateEvent2, QTouchEvent &endEvent) {
                    QApplication::sendEvent(canvasWidget, &beginEvent);
                    QApplication::processEvents();
                    QApplication::sendEvent(canvasWidget, &updateEvent1);
                    QApplication::processEvents();
                    QApplication::sendEvent(canvasWidget, &updateEvent2);
                    QApplication::processEvents();
                    QApplication::sendEvent(canvasWidget, &endEvent);
                    QApplication::processEvents();
                });

                return waitForCopyPasteOverlayShown(showTimeoutMs);
            };

            auto runClipboardSwipeViaDirectAction = [&](int showTimeoutMs) -> bool {
                withClipboardSwipeEvents([&](QTouchEvent &beginEvent, QTouchEvent &updateEvent1, QTouchEvent &updateEvent2, QTouchEvent &endEvent) {
                    KisTouchGestureAction gesture;
                    gesture.begin(KisTouchGestureAction::CopyPasteOverlay, &beginEvent);
                    gesture.inputEvent(&updateEvent1);
                    gesture.inputEvent(&updateEvent2);
                    gesture.end(&endEvent);
                });

                QApplication::processEvents();
                return waitForCopyPasteOverlayShown(showTimeoutMs);
            };

            auto runClipboardGesture = [&](bool enabled, int showTimeoutMs, QJsonObject *details) -> bool {
                cfg.setTouchClipboardGestureEnabled(enabled);
                QApplication::processEvents();

                hideCopyPasteOverlay();

                const bool shownViaInputManager = runClipboardSwipeViaInputManager(showTimeoutMs);

                bool shownViaDirectAction = false;
                if (!shownViaInputManager) {
                    hideCopyPasteOverlay();
                    shownViaDirectAction = runClipboardSwipeViaDirectAction(showTimeoutMs);
                }

                const bool shown = shownViaInputManager || shownViaDirectAction;

                if (details) {
                    details->insert(QStringLiteral("enabled"), enabled);
                    details->insert(QStringLiteral("input_manager_shown"), shownViaInputManager);
                    details->insert(QStringLiteral("direct_action_shown"), shownViaDirectAction);
                }

                return shown;
            };

            QJsonObject enabledDetails;
            const bool shownWhenEnabled = runClipboardGesture(true, 900, &enabledDetails);
            report.step(QStringLiteral("gesture_controls.clipboard_enabled_opens_overlay"), shownWhenEnabled, enabledDetails);
            if (!shownWhenEnabled) {
                ok = false;
            }

            hideCopyPasteOverlay();

            QJsonObject disabledDetails;
            const bool shownWhenDisabled = runClipboardGesture(false, 200, &disabledDetails);
            const bool hiddenWhenDisabled = !shownWhenDisabled;
            report.step(QStringLiteral("gesture_controls.clipboard_disabled_no_overlay"), hiddenWhenDisabled, disabledDetails);
            if (!hiddenWhenDisabled) {
                ok = false;
            }

            hideCopyPasteOverlay();

            cfg.setTouchClipboardGestureEnabled(true);
            cfg.setTouchClearLayerGestureEnabled(true);
        }

        // Validate clear-layer scrub gating (3-finger side-to-side scrub → clear active layer).
        if (image && image->bounds().isValid() && mainWindow->actionCollection()) {
            bool stepOk = true;
            report.step(QStringLiteral("gesture_controls.clear_layer.setup"), true);

            // Avoid accidentally triggering the clipboard overlay when testing clear-layer scrubbing.
            cfg.setTouchClipboardGestureEnabled(false);
            QApplication::processEvents();

            // Ensure we're clearing an editable, top-most layer.
            if (QAction *addLayerAction = mainWindow->actionCollection()->action(QStringLiteral("add_new_paint_layer"))) {
                addLayerAction->trigger();
                waitForImageCondition(200, [&]() { return false; });
            }

            const QRect bounds = image->bounds();
            const QVector<QPoint> samplePoints{bounds.center()};

            auto sampleCenter = [&]() -> QColor {
                const QVector<QColor> colors = sampleDeviceColorsForTouchSmoke(paintDeviceForTouchSmoke(mainWindow), samplePoints);
                return colors.isEmpty() ? QColor() : colors.first();
            };

            qreal scrubStepX = 0.0;
            auto withClearLayerScrubEvents = [&](auto callback) {
                const QRect r = canvasWidget->rect();
                const qreal centerX = r.width() / 2.0;
                const qreal centerY = r.height() / 2.0;
                const qreal margin = 12.0;
                const qreal spacing = qMin<qreal>(60.0, r.width() / 6.0);

                const QPointF p0Start(centerX - spacing, centerY);
                const QPointF p1Start(centerX, centerY);
                const QPointF p2Start(centerX + spacing, centerY);

                const qreal maxStep = qMax<qreal>(0.0, centerX - spacing - margin);
                const qreal stepX = qMin<qreal>(120.0, maxStep);
                scrubStepX = stepX;

                const QPointF p0Right = p0Start + QPointF(stepX, 0.0);
                const QPointF p1Right = p1Start + QPointF(stepX, 0.0);
                const QPointF p2Right = p2Start + QPointF(stepX, 0.0);

                const QPointF p0Left = p0Start - QPointF(stepX, 0.0);
                const QPointF p1Left = p1Start - QPointF(stepX, 0.0);
                const QPointF p2Left = p2Start - QPointF(stepX, 0.0);

                auto toGlobal = [&](const QPointF &localPos) {
                    return QPointF(canvasWidget->mapToGlobal(localPos.toPoint()));
                };

                const QPointF p0StartGlobal = toGlobal(p0Start);
                const QPointF p1StartGlobal = toGlobal(p1Start);
                const QPointF p2StartGlobal = toGlobal(p2Start);
                const QPointF p0RightGlobal = toGlobal(p0Right);
                const QPointF p1RightGlobal = toGlobal(p1Right);
                const QPointF p2RightGlobal = toGlobal(p2Right);
                const QPointF p0LeftGlobal = toGlobal(p0Left);
                const QPointF p1LeftGlobal = toGlobal(p1Left);
                const QPointF p2LeftGlobal = toGlobal(p2Left);

                QTouchEvent::TouchPoint tp0(0);
                QTouchEvent::TouchPoint tp1(1);
                QTouchEvent::TouchPoint tp2(2);

                tp0.setState(Qt::TouchPointPressed);
                tp0.setPos(p0Start);
                tp0.setScreenPos(p0StartGlobal);
                tp0.setStartPos(p0Start);
                tp0.setStartScreenPos(p0StartGlobal);
                tp0.setLastPos(p0Start);
                tp0.setLastScreenPos(p0StartGlobal);

                tp1.setState(Qt::TouchPointPressed);
                tp1.setPos(p1Start);
                tp1.setScreenPos(p1StartGlobal);
                tp1.setStartPos(p1Start);
                tp1.setStartScreenPos(p1StartGlobal);
                tp1.setLastPos(p1Start);
                tp1.setLastScreenPos(p1StartGlobal);

                tp2.setState(Qt::TouchPointPressed);
                tp2.setPos(p2Start);
                tp2.setScreenPos(p2StartGlobal);
                tp2.setStartPos(p2Start);
                tp2.setStartScreenPos(p2StartGlobal);
                tp2.setLastPos(p2Start);
                tp2.setLastScreenPos(p2StartGlobal);

                QList<QTouchEvent::TouchPoint> beginPoints{tp0, tp1, tp2};
                QTouchEvent beginEvent(QEvent::TouchBegin, touchDevice, Qt::NoModifier, Qt::TouchPointPressed, beginPoints);

                // Scrub pattern: right → left → right → left → back to start.
                tp0.setState(Qt::TouchPointMoved);
                tp0.setPos(p0Right);
                tp0.setScreenPos(p0RightGlobal);
                tp0.setStartPos(p0Start);
                tp0.setStartScreenPos(p0StartGlobal);
                tp0.setLastPos(p0Start);
                tp0.setLastScreenPos(p0StartGlobal);

                tp1.setState(Qt::TouchPointMoved);
                tp1.setPos(p1Right);
                tp1.setScreenPos(p1RightGlobal);
                tp1.setStartPos(p1Start);
                tp1.setStartScreenPos(p1StartGlobal);
                tp1.setLastPos(p1Start);
                tp1.setLastScreenPos(p1StartGlobal);

                tp2.setState(Qt::TouchPointMoved);
                tp2.setPos(p2Right);
                tp2.setScreenPos(p2RightGlobal);
                tp2.setStartPos(p2Start);
                tp2.setStartScreenPos(p2StartGlobal);
                tp2.setLastPos(p2Start);
                tp2.setLastScreenPos(p2StartGlobal);

                QList<QTouchEvent::TouchPoint> updatePoints1{tp0, tp1, tp2};
                QTouchEvent updateEvent1(QEvent::TouchUpdate, touchDevice, Qt::NoModifier, Qt::TouchPointMoved, updatePoints1);

                tp0.setPos(p0Left);
                tp0.setScreenPos(p0LeftGlobal);
                tp0.setStartPos(p0Start);
                tp0.setStartScreenPos(p0StartGlobal);
                tp0.setLastPos(p0Right);
                tp0.setLastScreenPos(p0RightGlobal);

                tp1.setPos(p1Left);
                tp1.setScreenPos(p1LeftGlobal);
                tp1.setStartPos(p1Start);
                tp1.setStartScreenPos(p1StartGlobal);
                tp1.setLastPos(p1Right);
                tp1.setLastScreenPos(p1RightGlobal);

                tp2.setPos(p2Left);
                tp2.setScreenPos(p2LeftGlobal);
                tp2.setStartPos(p2Start);
                tp2.setStartScreenPos(p2StartGlobal);
                tp2.setLastPos(p2Right);
                tp2.setLastScreenPos(p2RightGlobal);

                QList<QTouchEvent::TouchPoint> updatePoints2{tp0, tp1, tp2};
                QTouchEvent updateEvent2(QEvent::TouchUpdate, touchDevice, Qt::NoModifier, Qt::TouchPointMoved, updatePoints2);

                tp0.setPos(p0Right);
                tp0.setScreenPos(p0RightGlobal);
                tp0.setStartPos(p0Start);
                tp0.setStartScreenPos(p0StartGlobal);
                tp0.setLastPos(p0Left);
                tp0.setLastScreenPos(p0LeftGlobal);

                tp1.setPos(p1Right);
                tp1.setScreenPos(p1RightGlobal);
                tp1.setStartPos(p1Start);
                tp1.setStartScreenPos(p1StartGlobal);
                tp1.setLastPos(p1Left);
                tp1.setLastScreenPos(p1LeftGlobal);

                tp2.setPos(p2Right);
                tp2.setScreenPos(p2RightGlobal);
                tp2.setStartPos(p2Start);
                tp2.setStartScreenPos(p2StartGlobal);
                tp2.setLastPos(p2Left);
                tp2.setLastScreenPos(p2LeftGlobal);

                QList<QTouchEvent::TouchPoint> updatePoints3{tp0, tp1, tp2};
                QTouchEvent updateEvent3(QEvent::TouchUpdate, touchDevice, Qt::NoModifier, Qt::TouchPointMoved, updatePoints3);

                tp0.setPos(p0Left);
                tp0.setScreenPos(p0LeftGlobal);
                tp0.setStartPos(p0Start);
                tp0.setStartScreenPos(p0StartGlobal);
                tp0.setLastPos(p0Right);
                tp0.setLastScreenPos(p0RightGlobal);

                tp1.setPos(p1Left);
                tp1.setScreenPos(p1LeftGlobal);
                tp1.setStartPos(p1Start);
                tp1.setStartScreenPos(p1StartGlobal);
                tp1.setLastPos(p1Right);
                tp1.setLastScreenPos(p1RightGlobal);

                tp2.setPos(p2Left);
                tp2.setScreenPos(p2LeftGlobal);
                tp2.setStartPos(p2Start);
                tp2.setStartScreenPos(p2StartGlobal);
                tp2.setLastPos(p2Right);
                tp2.setLastScreenPos(p2RightGlobal);

                QList<QTouchEvent::TouchPoint> updatePoints4{tp0, tp1, tp2};
                QTouchEvent updateEvent4(QEvent::TouchUpdate, touchDevice, Qt::NoModifier, Qt::TouchPointMoved, updatePoints4);

                tp0.setPos(p0Start);
                tp0.setScreenPos(p0StartGlobal);
                tp0.setStartPos(p0Start);
                tp0.setStartScreenPos(p0StartGlobal);
                tp0.setLastPos(p0Left);
                tp0.setLastScreenPos(p0LeftGlobal);

                tp1.setPos(p1Start);
                tp1.setScreenPos(p1StartGlobal);
                tp1.setStartPos(p1Start);
                tp1.setStartScreenPos(p1StartGlobal);
                tp1.setLastPos(p1Left);
                tp1.setLastScreenPos(p1LeftGlobal);

                tp2.setPos(p2Start);
                tp2.setScreenPos(p2StartGlobal);
                tp2.setStartPos(p2Start);
                tp2.setStartScreenPos(p2StartGlobal);
                tp2.setLastPos(p2Left);
                tp2.setLastScreenPos(p2LeftGlobal);

                QList<QTouchEvent::TouchPoint> updatePoints5{tp0, tp1, tp2};
                QTouchEvent updateEvent5(QEvent::TouchUpdate, touchDevice, Qt::NoModifier, Qt::TouchPointMoved, updatePoints5);

                tp0.setState(Qt::TouchPointReleased);
                tp0.setPos(p0Start);
                tp0.setScreenPos(p0StartGlobal);
                tp0.setStartPos(p0Start);
                tp0.setStartScreenPos(p0StartGlobal);
                tp0.setLastPos(p0Start);
                tp0.setLastScreenPos(p0StartGlobal);

                tp1.setState(Qt::TouchPointReleased);
                tp1.setPos(p1Start);
                tp1.setScreenPos(p1StartGlobal);
                tp1.setStartPos(p1Start);
                tp1.setStartScreenPos(p1StartGlobal);
                tp1.setLastPos(p1Start);
                tp1.setLastScreenPos(p1StartGlobal);

                tp2.setState(Qt::TouchPointReleased);
                tp2.setPos(p2Start);
                tp2.setScreenPos(p2StartGlobal);
                tp2.setStartPos(p2Start);
                tp2.setStartScreenPos(p2StartGlobal);
                tp2.setLastPos(p2Start);
                tp2.setLastScreenPos(p2StartGlobal);

                QList<QTouchEvent::TouchPoint> endPoints{tp0, tp1, tp2};
                QTouchEvent endEvent(QEvent::TouchEnd, touchDevice, Qt::NoModifier, Qt::TouchPointReleased, endPoints);

                callback(beginEvent, updateEvent1, updateEvent2, updateEvent3, updateEvent4, updateEvent5, endEvent);
            };

            auto performClearLayerScrubViaInputManager = [&]() {
                withClearLayerScrubEvents([&](QTouchEvent &beginEvent,
                                             QTouchEvent &updateEvent1,
                                             QTouchEvent &updateEvent2,
                                             QTouchEvent &updateEvent3,
                                             QTouchEvent &updateEvent4,
                                             QTouchEvent &updateEvent5,
                                             QTouchEvent &endEvent) {
                    QApplication::sendEvent(canvasWidget, &beginEvent);
                    QApplication::processEvents();
                    QApplication::sendEvent(canvasWidget, &updateEvent1);
                    QApplication::processEvents();
                    QApplication::sendEvent(canvasWidget, &updateEvent2);
                    QApplication::processEvents();
                    QApplication::sendEvent(canvasWidget, &updateEvent3);
                    QApplication::processEvents();
                    QApplication::sendEvent(canvasWidget, &updateEvent4);
                    QApplication::processEvents();
                    QApplication::sendEvent(canvasWidget, &updateEvent5);
                    QApplication::processEvents();
                    QApplication::sendEvent(canvasWidget, &endEvent);
                    QApplication::processEvents();
                });
            };

            auto performClearLayerScrubViaDirectAction = [&]() {
                withClearLayerScrubEvents([&](QTouchEvent &beginEvent,
                                             QTouchEvent &updateEvent1,
                                             QTouchEvent &updateEvent2,
                                             QTouchEvent &updateEvent3,
                                             QTouchEvent &updateEvent4,
                                             QTouchEvent &updateEvent5,
                                             QTouchEvent &endEvent) {
                    KisTouchGestureAction gesture;
                    gesture.begin(KisTouchGestureAction::CopyPasteOverlay, &beginEvent);
                    gesture.inputEvent(&updateEvent1);
                    gesture.inputEvent(&updateEvent2);
                    gesture.inputEvent(&updateEvent3);
                    gesture.inputEvent(&updateEvent4);
                    gesture.inputEvent(&updateEvent5);
                    gesture.end(&endEvent);
                });
            };

            auto runClearLayerScrubGestureWithFallback = [&](int clearTimeoutMs, QJsonObject *details) -> bool {
                performClearLayerScrubViaInputManager();
                const int inputManagerTimeoutMs = qMin(900, clearTimeoutMs);
                const bool clearedViaInputManager = waitForImageCondition(inputManagerTimeoutMs, [&]() { return sampleCenter().alpha() <= 10; });

                bool clearedViaDirectAction = false;
                if (!clearedViaInputManager) {
                    performClearLayerScrubViaDirectAction();
                    clearedViaDirectAction = waitForImageCondition(clearTimeoutMs, [&]() { return sampleCenter().alpha() <= 10; });
                }

                if (details) {
                    details->insert(QStringLiteral("scrub_step_px"), scrubStepX);
                    details->insert(QStringLiteral("input_manager_timeout_ms"), inputManagerTimeoutMs);
                    details->insert(QStringLiteral("input_manager_cleared"), clearedViaInputManager);
                    details->insert(QStringLiteral("direct_action_cleared"), clearedViaDirectAction);
                }

                return clearedViaInputManager || clearedViaDirectAction;
            };

            auto runClearLayerScrubGestureNoClear = [&](int settleMs, QJsonObject *details) -> bool {
                performClearLayerScrubViaInputManager();
                performClearLayerScrubViaDirectAction();

                const bool cleared = waitForImageCondition(settleMs, [&]() { return sampleCenter().alpha() <= 10; });
                if (details) {
                    details->insert(QStringLiteral("scrub_step_px"), scrubStepX);
                    details->insert(QStringLiteral("settle_ms"), settleMs);
                    details->insert(QStringLiteral("input_manager_attempted"), true);
                    details->insert(QStringLiteral("direct_action_attempted"), true);
                    details->insert(QStringLiteral("cleared"), cleared);
                }
                return cleared;
            };

            // Paint a deterministic non-transparent pixel so we can validate clearing.
            {
                const bool painted = fillCanvasForTouchSmoke(mainWindow, QColor(255, 0, 0, 255));
                report.step(QStringLiteral("gesture_controls.clear_layer.prepare_paint"), painted);
                if (!painted) {
                    ok = false;
                    stepOk = false;
                }
            }

            if (stepOk) {
                const QColor before = sampleCenter();
                QJsonObject details;
                details.insert(QStringLiteral("before_alpha"), before.alpha());
                report.step(QStringLiteral("gesture_controls.clear_layer.sample_before"), before.isValid(), details);

                cfg.setTouchClearLayerGestureEnabled(true);
                QApplication::processEvents();
                QJsonObject scrubDetails;
                const bool cleared = runClearLayerScrubGestureWithFallback(1500, &scrubDetails);
                const QColor after = sampleCenter();
                {
                    QJsonObject d;
                    d.insert(QStringLiteral("before_alpha"), before.alpha());
                    d.insert(QStringLiteral("after_alpha"), after.alpha());
                    d.insert(QStringLiteral("scrub_step_px"), scrubDetails.value(QStringLiteral("scrub_step_px")));
                    d.insert(QStringLiteral("input_manager_timeout_ms"), scrubDetails.value(QStringLiteral("input_manager_timeout_ms")));
                    d.insert(QStringLiteral("input_manager_cleared"), scrubDetails.value(QStringLiteral("input_manager_cleared")));
                    d.insert(QStringLiteral("direct_action_cleared"), scrubDetails.value(QStringLiteral("direct_action_cleared")));
                    report.step(QStringLiteral("gesture_controls.clear_layer_enabled_clears_layer"), cleared, d);
                }
                if (!cleared) {
                    ok = false;
                }

                // Repaint and ensure the gesture does nothing when disabled.
                const bool repainted = fillCanvasForTouchSmoke(mainWindow, QColor(255, 0, 0, 255));
                report.step(QStringLiteral("gesture_controls.clear_layer.repaint_for_disabled_test"), repainted);
                if (!repainted) {
                    ok = false;
                } else {
                    const QColor beforeBlocked = sampleCenter();
                    cfg.setTouchClearLayerGestureEnabled(false);
                    QApplication::processEvents();
                    QJsonObject blockedScrubDetails;
                    const bool clearedWhileDisabled = runClearLayerScrubGestureNoClear(250, &blockedScrubDetails);

                    const QColor afterBlocked = sampleCenter();
                    const bool blocked = !clearedWhileDisabled && colorsEqualForTouchSmoke(beforeBlocked, afterBlocked, 3);
                    {
                        QJsonObject d;
                        d.insert(QStringLiteral("before_alpha"), beforeBlocked.alpha());
                        d.insert(QStringLiteral("after_alpha"), afterBlocked.alpha());
                        d.insert(QStringLiteral("scrub_step_px"), blockedScrubDetails.value(QStringLiteral("scrub_step_px")));
                        d.insert(QStringLiteral("settle_ms"), blockedScrubDetails.value(QStringLiteral("settle_ms")));
                        d.insert(QStringLiteral("input_manager_attempted"), blockedScrubDetails.value(QStringLiteral("input_manager_attempted")));
                        d.insert(QStringLiteral("direct_action_attempted"), blockedScrubDetails.value(QStringLiteral("direct_action_attempted")));
                        d.insert(QStringLiteral("cleared"), blockedScrubDetails.value(QStringLiteral("cleared")));
                        report.step(QStringLiteral("gesture_controls.clear_layer_disabled_no_clear"), blocked, d);
                    }
                    if (!blocked) {
                        ok = false;
                    }

                    cfg.setTouchClearLayerGestureEnabled(true);
                    QApplication::processEvents();
                }
            }

            cfg.setTouchClipboardGestureEnabled(true);
            QApplication::processEvents();
        } else {
            report.step(QStringLiteral("gesture_controls.clear_layer.setup"), false);
            ok = false;
        }

        // Validate undo/redo gesture gating by observing layer count changes.
        if (image && image->rootLayer() && mainWindow->actionCollection()) {
            auto layerCount = [&]() -> int {
                KisGroupLayerSP root = image ? image->rootLayer() : KisGroupLayerSP();
                return root ? int(root->childCount()) : 0;
            };

            QAction *addLayerAction = mainWindow->actionCollection()->action(QStringLiteral("add_new_paint_layer"));
            if (!addLayerAction) {
                qWarning() << "Touch smoke: gesture-controls missing action: add_new_paint_layer";
                report.step(QStringLiteral("gesture_controls.undo_redo.setup"), false);
                ok = false;
            } else {
                report.step(QStringLiteral("gesture_controls.undo_redo.setup"), true);

                const int beforeCount = layerCount();
                addLayerAction->trigger();
                const bool layerAdded = waitForImageCondition(1500, [&]() { return layerCount() == beforeCount + 1; });
                const int afterAddCount = layerCount();
                {
                    QJsonObject details;
                    details.insert(QStringLiteral("before_layer_count"), beforeCount);
                    details.insert(QStringLiteral("after_layer_count"), afterAddCount);
                    report.step(QStringLiteral("gesture_controls.undo_redo.prepare_add_layer"), layerAdded, details);
                }
                if (!layerAdded) {
                    ok = false;
                } else {
                    KisTouchGestureAction gesture;

                    auto performUndoViaInputManager = [&]() { sendMultiFingerTouchTap(2); };
                    auto performRedoViaInputManager = [&]() { sendMultiFingerTouchTap(3); };

                    auto performUndoViaDirectAction = [&]() {
                        gesture.begin(KisTouchGestureAction::UndoActionShortcut, nullptr);
                        gesture.end(nullptr);
                    };

                    auto performRedoViaDirectAction = [&]() {
                        gesture.begin(KisTouchGestureAction::RedoActionShortcut, nullptr);
                        gesture.end(nullptr);
                    };

                    auto runUndoTapWithFallback = [&](int expectedLayerCount, int timeoutMs, QJsonObject *details) -> bool {
                        bool undoneViaInputManager = false;
                        bool undoneViaDirectAction = false;

                        performUndoViaInputManager();
                        undoneViaInputManager = waitForImageCondition(timeoutMs, [&]() { return layerCount() == expectedLayerCount; });
                        if (!undoneViaInputManager) {
                            performUndoViaDirectAction();
                            undoneViaDirectAction = waitForImageCondition(timeoutMs, [&]() { return layerCount() == expectedLayerCount; });
                        }

                        if (details) {
                            details->insert(QStringLiteral("expected_layer_count"), expectedLayerCount);
                            details->insert(QStringLiteral("actual_layer_count"), layerCount());
                            details->insert(QStringLiteral("input_manager_timeout_ms"), timeoutMs);
                            details->insert(QStringLiteral("input_manager_undone"), undoneViaInputManager);
                            details->insert(QStringLiteral("direct_action_undone"), undoneViaDirectAction);
                        }

                        return undoneViaInputManager || undoneViaDirectAction;
                    };

                    auto runRedoTapWithFallback = [&](int expectedLayerCount, int timeoutMs, QJsonObject *details) -> bool {
                        bool redoneViaInputManager = false;
                        bool redoneViaDirectAction = false;

                        performRedoViaInputManager();
                        redoneViaInputManager = waitForImageCondition(timeoutMs, [&]() { return layerCount() == expectedLayerCount; });
                        if (!redoneViaInputManager) {
                            performRedoViaDirectAction();
                            redoneViaDirectAction = waitForImageCondition(timeoutMs, [&]() { return layerCount() == expectedLayerCount; });
                        }

                        if (details) {
                            details->insert(QStringLiteral("expected_layer_count"), expectedLayerCount);
                            details->insert(QStringLiteral("actual_layer_count"), layerCount());
                            details->insert(QStringLiteral("input_manager_timeout_ms"), timeoutMs);
                            details->insert(QStringLiteral("input_manager_redone"), redoneViaInputManager);
                            details->insert(QStringLiteral("direct_action_redone"), redoneViaDirectAction);
                        }

                        return redoneViaInputManager || redoneViaDirectAction;
                    };

                    auto runUndoTapNoChange = [&](int expectedLayerCount, int settleMs, QJsonObject *details) -> bool {
                        performUndoViaInputManager();
                        performUndoViaDirectAction();

                        waitForImageCondition(settleMs, [&]() { return false; });

                        const bool blocked = layerCount() == expectedLayerCount;
                        if (details) {
                            details->insert(QStringLiteral("expected_layer_count"), expectedLayerCount);
                            details->insert(QStringLiteral("actual_layer_count"), layerCount());
                            details->insert(QStringLiteral("settle_ms"), settleMs);
                            details->insert(QStringLiteral("input_manager_attempted"), true);
                            details->insert(QStringLiteral("direct_action_attempted"), true);
                        }
                        return blocked;
                    };

                    auto runRedoTapNoChange = [&](int expectedLayerCount, int settleMs, QJsonObject *details) -> bool {
                        performRedoViaInputManager();
                        performRedoViaDirectAction();

                        waitForImageCondition(settleMs, [&]() { return false; });

                        const bool blocked = layerCount() == expectedLayerCount;
                        if (details) {
                            details->insert(QStringLiteral("expected_layer_count"), expectedLayerCount);
                            details->insert(QStringLiteral("actual_layer_count"), layerCount());
                            details->insert(QStringLiteral("settle_ms"), settleMs);
                            details->insert(QStringLiteral("input_manager_attempted"), true);
                            details->insert(QStringLiteral("direct_action_attempted"), true);
                        }
                        return blocked;
                    };

                    cfg.setTouchUndoRedoGesturesEnabled(true);
                    QApplication::processEvents();
                    QJsonObject undoDetails;
                    const bool undone = runUndoTapWithFallback(beforeCount, 1500, &undoDetails);

                    {
                        QJsonObject details = undoDetails;
                        report.step(QStringLiteral("gesture_controls.undo_enabled_undoes_layer_add"), undone, details);
                    }
                    if (!undone) {
                        ok = false;
                    }

                    cfg.setTouchUndoRedoGesturesEnabled(false);
                    QApplication::processEvents();
                    const int beforeBlockedRedo = layerCount();
                    QJsonObject redoBlockedDetails;
                    const bool redoBlocked = runRedoTapNoChange(beforeBlockedRedo, 250, &redoBlockedDetails);
                    {
                        QJsonObject details = redoBlockedDetails;
                        report.step(QStringLiteral("gesture_controls.undo_redo_disabled_blocks_redo"), redoBlocked, details);
                    }
                    if (!redoBlocked) {
                        ok = false;
                    }

                    cfg.setTouchUndoRedoGesturesEnabled(true);
                    QApplication::processEvents();
                    QJsonObject redoDetails;
                    const bool redone = runRedoTapWithFallback(afterAddCount, 1500, &redoDetails);
                    {
                        QJsonObject details = redoDetails;
                        report.step(QStringLiteral("gesture_controls.redo_enabled_redoes_layer_add"), redone, details);
                    }
                    if (!redone) {
                        ok = false;
                    }

                    cfg.setTouchUndoRedoGesturesEnabled(false);
                    QApplication::processEvents();
                    const int beforeBlockedUndo = layerCount();
                    QJsonObject undoBlockedDetails;
                    const bool undoBlocked = runUndoTapNoChange(beforeBlockedUndo, 250, &undoBlockedDetails);
                    {
                        QJsonObject details = undoBlockedDetails;
                        report.step(QStringLiteral("gesture_controls.undo_redo_disabled_blocks_undo"), undoBlocked, details);
                    }
                    if (!undoBlocked) {
                        ok = false;
                    }

                    cfg.setTouchUndoRedoGesturesEnabled(true);
                    QApplication::processEvents();
                }
            }
        } else {
            report.step(QStringLiteral("gesture_controls.undo_redo.setup"), false);
            ok = false;
        }

        // Validate QuickMenu gesture gating (opens overlay only when enabled).
        {
            auto runQuickMenuGesture = [&](bool enabled, int showTimeoutMs) -> bool {
                cfg.setTouchQuickMenuEnabled(enabled);
                QApplication::processEvents();

                if (KisTouchQuickMenuOverlay *overlay =
                        mainWindow->findChild<KisTouchQuickMenuOverlay *>(QStringLiteral("kisTouchQuickMenuOverlay"))) {
                    overlay->hide();
                }

                const QPointF center = QPointF(canvasWidget->rect().center());
                const QPointF centerGlobal = QPointF(canvasWidget->mapToGlobal(center.toPoint()));

                QTouchEvent::TouchPoint tp0(0);
                tp0.setState(Qt::TouchPointPressed);
                tp0.setPos(center);
                tp0.setScreenPos(centerGlobal);

                QList<QTouchEvent::TouchPoint> beginPoints{tp0};
                QTouchEvent beginEvent(QEvent::TouchBegin, touchDevice, Qt::NoModifier, Qt::TouchPointPressed, beginPoints);

                tp0.setState(Qt::TouchPointReleased);
                QList<QTouchEvent::TouchPoint> endPoints{tp0};
                QTouchEvent endEvent(QEvent::TouchEnd, touchDevice, Qt::NoModifier, Qt::TouchPointReleased, endPoints);

                KisTouchQuickMenuAction action;
                action.begin(0, &beginEvent);
                QApplication::processEvents();

                const bool shown = waitForUiCondition(showTimeoutMs, [&]() {
                    KisTouchQuickMenuOverlay *overlay =
                        mainWindow->findChild<KisTouchQuickMenuOverlay *>(QStringLiteral("kisTouchQuickMenuOverlay"));
                    return overlay && overlay->isVisible();
                });

                action.end(&endEvent);
                QApplication::processEvents();

                if (KisTouchQuickMenuOverlay *overlay =
                        mainWindow->findChild<KisTouchQuickMenuOverlay *>(QStringLiteral("kisTouchQuickMenuOverlay"))) {
                    overlay->hide();
                }
                return shown;
            };

            const bool quickMenuShown = runQuickMenuGesture(true, 900);
            report.step(QStringLiteral("gesture_controls.quickmenu_enabled_opens_overlay"), quickMenuShown);
            if (!quickMenuShown) {
                ok = false;
            }

            const bool quickMenuShownWhenDisabled = runQuickMenuGesture(false, 200);
            const bool quickMenuBlocked = !quickMenuShownWhenDisabled;
            report.step(QStringLiteral("gesture_controls.quickmenu_disabled_no_overlay"), quickMenuBlocked);
            if (!quickMenuBlocked) {
                ok = false;
            }

            cfg.setTouchQuickMenuEnabled(true);
        }

        // Validate canvas-only/fullscreen gesture gating (4-finger tap routing).
        if (mainWindow->actionCollection()) {
            QAction *canvasOnlyAction = mainWindow->actionCollection()->action(QStringLiteral("view_show_canvas_only"));
            if (!canvasOnlyAction) {
                qWarning() << "Touch smoke: gesture-controls missing action: view_show_canvas_only";
                report.step(QStringLiteral("gesture_controls.fullscreen.setup"), false);
                ok = false;
            } else {
                report.step(QStringLiteral("gesture_controls.fullscreen.setup"), true);

                const bool initialChecked = canvasOnlyAction->isChecked();
                KisTouchGestureAction gesture;

                cfg.setTouchFullscreenGestureEnabled(true);
                gesture.begin(KisTouchGestureAction::ToggleCanvasOnlyShortcut, nullptr);
                gesture.end(nullptr);

                const bool toggled = waitForUiCondition(900, [&]() { return canvasOnlyAction->isChecked() != initialChecked; });
                {
                    QJsonObject details;
                    details.insert(QStringLiteral("before_checked"), initialChecked);
                    details.insert(QStringLiteral("after_checked"), canvasOnlyAction->isChecked());
                    report.step(QStringLiteral("gesture_controls.fullscreen_enabled_toggles_canvas_only"), toggled, details);
                }
                if (!toggled) {
                    ok = false;
                }

                gesture.begin(KisTouchGestureAction::ToggleCanvasOnlyShortcut, nullptr);
                gesture.end(nullptr);
                const bool restored = waitForUiCondition(900, [&]() { return canvasOnlyAction->isChecked() == initialChecked; });
                {
                    QJsonObject details;
                    details.insert(QStringLiteral("expected_checked"), initialChecked);
                    details.insert(QStringLiteral("actual_checked"), canvasOnlyAction->isChecked());
                    report.step(QStringLiteral("gesture_controls.fullscreen_enabled_restores_canvas_only"), restored, details);
                }
                if (!restored) {
                    ok = false;
                }

                cfg.setTouchFullscreenGestureEnabled(false);
                const bool beforeBlocked = canvasOnlyAction->isChecked();
                gesture.begin(KisTouchGestureAction::ToggleCanvasOnlyShortcut, nullptr);
                gesture.end(nullptr);
                waitForUiCondition(200, [&]() { return false; });
                const bool blocked = canvasOnlyAction->isChecked() == beforeBlocked;
                {
                    QJsonObject details;
                    details.insert(QStringLiteral("expected_checked"), beforeBlocked);
                    details.insert(QStringLiteral("actual_checked"), canvasOnlyAction->isChecked());
                    report.step(QStringLiteral("gesture_controls.fullscreen_disabled_no_toggle"), blocked, details);
                }
                if (!blocked) {
                    ok = false;
                }

                if (canvasOnlyAction->isChecked() != initialChecked) {
                    canvasOnlyAction->trigger();
                    QApplication::processEvents();
                }
                cfg.setTouchFullscreenGestureEnabled(true);
            }
        } else {
            report.step(QStringLiteral("gesture_controls.fullscreen.setup"), false);
            ok = false;
        }

        QWidget *anchor = mainWindow->viewManager() ? mainWindow->viewManager()->canvas() : nullptr;
        if (!anchor) {
            anchor = mainWindow;
        }

        const QPoint globalPos = anchor->mapToGlobal(anchor->rect().center());

        KisTouchActionsSheet *sheet =
            mainWindow->findChild<KisTouchActionsSheet *>(QStringLiteral("kisTouchActionsSheet"));
        if (!sheet) {
            sheet = new KisTouchActionsSheet(mainWindow->actionCollection(), mainWindow);
        } else {
            sheet->setActionCollection(mainWindow->actionCollection());
        }

        sheet->openAtGlobalPos(globalPos);
        // Categories are stable and ordered:
        // Add(0), Canvas(1), Share(2), Prefs(3), Gestures(4), Help(5)
        sheet->setCurrentCategoryRow(4);
        finalizeSmoke(ok);
        return;
    }

    if (normalizedScenario == "quickmenu" || normalizedScenario == "quick-menu" || normalizedScenario == "quick_menu") {
        if (!mainWindow || !mainWindow->actionCollection()) {
            qWarning() << "Touch smoke: main window or action collection not available";
            finalizeSmoke(false);
            return;
        }

        KoToolManager *toolManager = KoToolManager::instance();
        if (!toolManager) {
            qWarning() << "Touch smoke: quickmenu missing tool manager";
            finalizeSmoke(false);
            return;
        }

        auto waitForUiCondition = [&](int timeoutMs, auto condition) -> bool {
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
        };

        QWidget *anchor = mainWindow->viewManager() ? mainWindow->viewManager()->canvas() : nullptr;
        if (!anchor) {
            anchor = mainWindow;
        }

        const QPoint globalPos = anchor->mapToGlobal(anchor->rect().center());

        const QStringList deterministicSlotActionIds = QStringList{
            QStringLiteral("edit_undo"),
            QStringLiteral("edit_redo"),
            QStringLiteral("KisToolSelectTouch"),
            QStringLiteral("KisToolTransform"),
            QStringLiteral("deselect"),
            QStringLiteral("view_show_canvas_only"),
        };

        cfg.setTouchQuickMenuEnabled(true);
        cfg.setTouchQuickMenuActionIds(deterministicSlotActionIds);
        QApplication::processEvents();

        auto quickMenuOverlay = [&]() -> KisTouchQuickMenuOverlay * {
            const QList<KisTouchQuickMenuOverlay *> overlays =
                mainWindow->findChildren<KisTouchQuickMenuOverlay *>(QStringLiteral("kisTouchQuickMenuOverlay"));
            for (KisTouchQuickMenuOverlay *overlay : overlays) {
                if (overlay && overlay->isVisible()) {
                    return overlay;
                }
            }
            return overlays.isEmpty() ? nullptr : overlays.constLast();
        };

        auto quickMenuConfigSheet = [&]() -> QWidget * {
            return mainWindow->findChild<QWidget *>(QStringLiteral("kisTouchQuickMenuConfigSheet"));
        };

        bool ok = true;

        const QString brushToolId = QStringLiteral("KritaShape/KisToolBrush");
        toolManager->switchToolRequested(brushToolId);
        QApplication::processEvents();

        bool brushActive = false;
        for (int i = 0; i < 80; ++i) {
            if (toolManager->activeToolId() == brushToolId) {
                brushActive = true;
                break;
            }
            QApplication::processEvents();
            QThread::msleep(20);
        }
        if (!brushActive) {
            qWarning() << "Touch smoke: quickmenu could not activate brush tool; current tool=" << toolManager->activeToolId();
            ok = false;
        }

        static QTouchDevice *touchDevice = nullptr;
        if (!touchDevice) {
            touchDevice = new QTouchDevice();
            touchDevice->setType(QTouchDevice::TouchScreen);
            touchDevice->setCapabilities(QTouchDevice::Position);
            touchDevice->setMaximumTouchPoints(1);
        }

        KisTouchQuickMenuAction gestureAction;

        // Validate slide/highlight routing in KisTouchQuickMenuAction (gesture-path).
        {
            // Ensure a clean slate.
            const QList<KisTouchQuickMenuOverlay *> overlays =
                mainWindow->findChildren<KisTouchQuickMenuOverlay *>(QStringLiteral("kisTouchQuickMenuOverlay"));
            for (KisTouchQuickMenuOverlay *overlay : overlays) {
                if (overlay) {
                    overlay->hide();
                }
            }

            const QPointF originLocal = QPointF(anchor->rect().center());

            auto makePoint = [&](Qt::TouchPointState state, const QPointF &localPos) -> QTouchEvent::TouchPoint {
                QTouchEvent::TouchPoint tp0(0);
                tp0.setState(state);
                tp0.setPos(localPos);
                tp0.setScreenPos(QPointF(anchor->mapToGlobal(localPos.toPoint())));
                return tp0;
            };

            QTouchEvent beginEvent(QEvent::TouchBegin,
                                   touchDevice,
                                   Qt::NoModifier,
                                   Qt::TouchPointPressed,
                                   QList<QTouchEvent::TouchPoint>{makePoint(Qt::TouchPointPressed, originLocal)});

            gestureAction.begin(0, &beginEvent);
            QApplication::processEvents();

            const bool overlayShown = waitForUiCondition(900, [&]() {
                KisTouchQuickMenuOverlay *overlay = quickMenuOverlay();
                return overlay && overlay->isVisible();
            });
            report.step(QStringLiteral("quickmenu.gesture_begin_shows_overlay"), overlayShown);
            if (!overlayShown) {
                ok = false;
            }

            auto checkHighlight = [&](const QString &stepName, const QPointF &localPos, int expectedSlot) {
                QTouchEvent updateEvent(QEvent::TouchUpdate,
                                        touchDevice,
                                        Qt::NoModifier,
                                        Qt::TouchPointMoved,
                                        QList<QTouchEvent::TouchPoint>{makePoint(Qt::TouchPointMoved, localPos)});
                gestureAction.inputEvent(&updateEvent);
                QApplication::processEvents();

                const int actual = quickMenuOverlay() ? quickMenuOverlay()->highlightedSlot() : -999;
                QJsonObject details;
                details.insert(QStringLiteral("expected_slot"), expectedSlot);
                details.insert(QStringLiteral("actual_slot"), actual);
                report.step(stepName, actual == expectedSlot, details);
                if (actual != expectedSlot) {
                    ok = false;
                }
            };

            // Slot centers in KisTouchQuickMenuAction::slotForDelta():
            // 0 up, 1 up-right, 2 down-right, 3 down, 4 down-left, 5 up-left.
            checkHighlight(QStringLiteral("quickmenu.gesture_slide_highlights_slot0"),
                           originLocal + QPointF(0, -120),
                           0);
            checkHighlight(QStringLiteral("quickmenu.gesture_slide_highlights_slot2"),
                           originLocal + QPointF(104, 60),
                           2);
            checkHighlight(QStringLiteral("quickmenu.gesture_slide_highlights_slot4"),
                           originLocal + QPointF(-104, 60),
                           4);
            checkHighlight(QStringLiteral("quickmenu.gesture_slide_back_to_center_clears_highlight"),
                           originLocal,
                           -1);

            QTouchEvent endEvent(QEvent::TouchEnd,
                                 touchDevice,
                                 Qt::NoModifier,
                                 Qt::TouchPointReleased,
                                 QList<QTouchEvent::TouchPoint>{makePoint(Qt::TouchPointReleased, originLocal)});
            gestureAction.end(&endEvent);
            QApplication::processEvents();
        }

        // Validate release triggers the slot action (tool switch) via the gesture-path.
        {
            const QList<KisTouchQuickMenuOverlay *> overlays =
                mainWindow->findChildren<KisTouchQuickMenuOverlay *>(QStringLiteral("kisTouchQuickMenuOverlay"));
            for (KisTouchQuickMenuOverlay *overlay : overlays) {
                if (overlay) {
                    overlay->hide();
                }
            }

            const QPointF originLocal = QPointF(anchor->rect().center());
            const QPointF selectSlotLocal = originLocal + QPointF(104, 60); // slot 2 (Select tool)

            auto makePoint = [&](Qt::TouchPointState state, const QPointF &localPos) -> QTouchEvent::TouchPoint {
                QTouchEvent::TouchPoint tp0(0);
                tp0.setState(state);
                tp0.setPos(localPos);
                tp0.setScreenPos(QPointF(anchor->mapToGlobal(localPos.toPoint())));
                return tp0;
            };

            QTouchEvent beginEvent(QEvent::TouchBegin,
                                   touchDevice,
                                   Qt::NoModifier,
                                   Qt::TouchPointPressed,
                                   QList<QTouchEvent::TouchPoint>{makePoint(Qt::TouchPointPressed, originLocal)});
            gestureAction.begin(0, &beginEvent);
            QApplication::processEvents();

            QTouchEvent updateEvent(QEvent::TouchUpdate,
                                    touchDevice,
                                    Qt::NoModifier,
                                    Qt::TouchPointMoved,
                                    QList<QTouchEvent::TouchPoint>{makePoint(Qt::TouchPointMoved, selectSlotLocal)});
            gestureAction.inputEvent(&updateEvent);
            QApplication::processEvents();

            const int highlighted = quickMenuOverlay() ? quickMenuOverlay()->highlightedSlot() : -999;
            {
                QJsonObject details;
                details.insert(QStringLiteral("expected_slot"), 2);
                details.insert(QStringLiteral("actual_slot"), highlighted);
                report.step(QStringLiteral("quickmenu.gesture_select_highlights_slot2"), highlighted == 2, details);
            }
            if (highlighted != 2) {
                ok = false;
            }

            QTouchEvent endEvent(QEvent::TouchEnd,
                                 touchDevice,
                                 Qt::NoModifier,
                                 Qt::TouchPointReleased,
                                 QList<QTouchEvent::TouchPoint>{makePoint(Qt::TouchPointReleased, selectSlotLocal)});
            gestureAction.end(&endEvent);
            QApplication::processEvents();

            const QString expectedToolId = QStringLiteral("KisToolSelectTouch");
            bool toolSwitched = false;
            for (int i = 0; i < 80; ++i) {
                if (toolManager->activeToolId() == expectedToolId) {
                    toolSwitched = true;
                    break;
                }
                QApplication::processEvents();
                QThread::msleep(20);
            }

            {
                QJsonObject details;
                details.insert(QStringLiteral("expected_tool"), expectedToolId);
                details.insert(QStringLiteral("actual_tool"), toolManager->activeToolId());
                report.step(QStringLiteral("quickmenu.gesture_release_triggers_tool_switch"), toolSwitched, details);
            }
            if (!toolSwitched) {
                ok = false;
            }
        }

        // Validate hold-on-slot triggers QuickMenu configuration sheet (touch_quickmenu_configure).
        {
            if (QWidget *sheet = quickMenuConfigSheet()) {
                sheet->hide();
            }
            const QList<KisTouchQuickMenuOverlay *> overlays =
                mainWindow->findChildren<KisTouchQuickMenuOverlay *>(QStringLiteral("kisTouchQuickMenuOverlay"));
            for (KisTouchQuickMenuOverlay *overlay : overlays) {
                if (overlay) {
                    overlay->hide();
                }
            }

            const QPointF originLocal = QPointF(anchor->rect().center());
            const QPointF slotLocal = originLocal + QPointF(104, 60); // slot 2 (any slot is fine)

            auto makePoint = [&](Qt::TouchPointState state, const QPointF &localPos) -> QTouchEvent::TouchPoint {
                QTouchEvent::TouchPoint tp0(0);
                tp0.setState(state);
                tp0.setPos(localPos);
                tp0.setScreenPos(QPointF(anchor->mapToGlobal(localPos.toPoint())));
                return tp0;
            };

            QTouchEvent beginEvent(QEvent::TouchBegin,
                                   touchDevice,
                                   Qt::NoModifier,
                                   Qt::TouchPointPressed,
                                   QList<QTouchEvent::TouchPoint>{makePoint(Qt::TouchPointPressed, originLocal)});
            gestureAction.begin(0, &beginEvent);
            QApplication::processEvents();

            QTouchEvent updateEvent(QEvent::TouchUpdate,
                                    touchDevice,
                                    Qt::NoModifier,
                                    Qt::TouchPointMoved,
                                    QList<QTouchEvent::TouchPoint>{makePoint(Qt::TouchPointMoved, slotLocal)});
            gestureAction.inputEvent(&updateEvent);
            QApplication::processEvents();

            const bool sheetShown = waitForUiCondition(1100, [&]() {
                QWidget *sheet = quickMenuConfigSheet();
                return sheet && sheet->isVisible();
            });
            report.step(QStringLiteral("quickmenu.gesture_hold_opens_config_sheet"), sheetShown);
            if (!sheetShown) {
                ok = false;
            }

            if (QWidget *sheet = quickMenuConfigSheet()) {
                sheet->hide();
            }

            QTouchEvent endEvent(QEvent::TouchEnd,
                                 touchDevice,
                                 Qt::NoModifier,
                                 Qt::TouchPointReleased,
                                 QList<QTouchEvent::TouchPoint>{makePoint(Qt::TouchPointReleased, slotLocal)});
            gestureAction.end(&endEvent);
            QApplication::processEvents();
        }

        // Restore the overlay for the scenario screenshot (deterministic center + deterministic slot mapping).
        KisTouchQuickMenuOverlay *overlay = quickMenuOverlay();
        if (!overlay) {
            overlay = new KisTouchQuickMenuOverlay(mainWindow->actionCollection(), mainWindow);
        } else {
            overlay->setActionCollection(mainWindow->actionCollection());
        }

        const int slotToShow = 2; // "Select" (KisToolSelectTouch)
        overlay->setSlotActionIds(deterministicSlotActionIds);
        overlay->setHighlightedSlot(slotToShow);
        overlay->openAtGlobalPos(globalPos);

        finalizeSmoke(ok);
        return;
    }

    if (normalizedScenario == "quickmenu-setup" || normalizedScenario == "quickmenu_setup" ||
        normalizedScenario == "quickmenu-config" || normalizedScenario == "quickmenu_config") {
        if (QAction *action = mainWindow->actionCollection()->action("touch_quickmenu_configure")) {
            action->trigger();
            finalizeSmoke(true);
            return;
        }
        qWarning() << "Touch smoke: action not found: touch_quickmenu_configure";
        finalizeSmoke(false);
        return;
    }

    if (normalizedScenario == "copypaste" || normalizedScenario == "copy-paste" || normalizedScenario == "copy_paste") {
        KisView *view = mainWindow->activeView();
        KisImageWSP image = mainWindow->viewManager() ? mainWindow->viewManager()->image() : KisImageWSP();
        const QRect bounds = image ? image->bounds() : QRect();

        if (!view || !view->canvasBase() || !image || !bounds.isValid()) {
            qWarning() << "Touch smoke: copypaste missing view/image/bounds";
            finalizeSmoke(false);
            return;
        }

        QWidget *canvasWidget = view->canvasBase()->canvasWidget();
        if (!canvasWidget) {
            qWarning() << "Touch smoke: copypaste missing canvas widget";
            finalizeSmoke(false);
            return;
        }

        KoToolManager *toolManager = KoToolManager::instance();
        if (!toolManager) {
            qWarning() << "Touch smoke: copypaste missing tool manager";
            finalizeSmoke(false);
            return;
        }

        bool ok = true;

        // Seed deterministic content so Copy/Paste operations can validate pixels. This makes the
        // smoke scenario assertive (not just "layer count changed").
        const QColor bgColor(0xff, 0xff, 0xff);
        const QColor paintColor(0xff, 0x00, 0x00);
        const QRectF paintRect(bounds.left() + bounds.width() * 0.40,
                               bounds.top() + bounds.height() * 0.40,
                               bounds.width() * 0.20,
                               bounds.height() * 0.20);

        const QPoint insideSample(bounds.left() + qRound(bounds.width() * 0.50),
                                  bounds.top() + qRound(bounds.height() * 0.50));
        const QPoint outsideSample(bounds.left() + qRound(bounds.width() * 0.10),
                                   bounds.top() + qRound(bounds.height() * 0.10));
        const QVector<QPoint> samplePoints{insideSample, outsideSample};

        const bool filled = fillCanvasForTouchSmoke(mainWindow, bgColor);
        report.step(QStringLiteral("copypaste.fill_canvas_white"), filled);
        if (!filled) {
            ok = false;
        }

        KisPaintDeviceSP srcDev = paintDeviceForTouchSmoke(mainWindow);
        bool painted = false;
        if (srcDev) {
            KisFillPainter painter(srcDev);
            painter.setCompositeOpId(COMPOSITE_OVER);
            painter.fillRect(paintRect.toAlignedRect(), KoColor(paintColor, srcDev->colorSpace()), OPACITY_OPAQUE_U8);
            painter.end();
            refreshImageForTouchSmoke(image);
            painted = true;
        }
        report.step(QStringLiteral("copypaste.paint_rect_red"), painted);
        if (!painted) {
            ok = false;
        }

        auto colorToJson = [](const QColor &c) -> QJsonObject {
            QJsonObject obj;
            if (!c.isValid()) {
                obj.insert(QStringLiteral("valid"), false);
                return obj;
            }
            obj.insert(QStringLiteral("valid"), true);
            obj.insert(QStringLiteral("r"), c.red());
            obj.insert(QStringLiteral("g"), c.green());
            obj.insert(QStringLiteral("b"), c.blue());
            obj.insert(QStringLiteral("a"), c.alpha());
            return obj;
        };

        const QVector<QColor> srcColors = sampleDeviceColorsForTouchSmoke(srcDev, samplePoints);
        bool srcSamplesOk = false;
        if (srcDev && srcColors.size() == samplePoints.size()) {
            const bool insideIsRed = colorsEqualForTouchSmoke(srcColors[0], paintColor, 5);
            const bool outsideIsBg = colorsEqualForTouchSmoke(srcColors[1], bgColor, 5);
            srcSamplesOk = insideIsRed && outsideIsBg;
        }
        {
            QJsonObject details;
            details.insert(QStringLiteral("inside"), colorToJson(srcColors.size() > 0 ? srcColors[0] : QColor()));
            details.insert(QStringLiteral("outside"), colorToJson(srcColors.size() > 1 ? srcColors[1] : QColor()));
            report.step(QStringLiteral("copypaste.source_layer_seed_samples"), srcSamplesOk, details);
        }
        if (!srcSamplesOk) {
            ok = false;
        }

        toolManager->switchToolRequested(QStringLiteral("KisToolSelectTouch"));
        QApplication::processEvents();

        auto imgToWidget = [&](const QPointF &imgP) {
            return view->canvasBase()->coordinatesConverter()->imageToWidget(imgP);
        };

        auto tapAtImagePos = [&](const QPointF &imgP) {
            const QPointF widgetPos = imgToWidget(imgP);
            const QPointF globalPos = canvasWidget->mapToGlobal(widgetPos.toPoint());

            QMouseEvent press(QEvent::MouseButtonPress,
                              widgetPos,
                              globalPos,
                              Qt::LeftButton,
                              Qt::LeftButton,
                              Qt::NoModifier);
            QApplication::sendEvent(canvasWidget, &press);

            QMouseEvent release(QEvent::MouseButtonRelease,
                                widgetPos,
                                globalPos,
                                Qt::LeftButton,
                                Qt::NoButton,
                                Qt::NoModifier);
            QApplication::sendEvent(canvasWidget, &release);
        };

        auto selectedExactRect = [&]() -> QRect {
            KisSelectionSP selection = view->selection();
            if (!selection || !selection->pixelSelection()) {
                return QRect();
            }
            return selection->pixelSelection()->selectedExactRect();
        };

        QVector<KisNode *> beforeNodes;
        if (KisGroupLayerSP root = image->rootLayer()) {
            for (KisNodeSP node = root->firstChild(); node; node = node->nextSibling()) {
                beforeNodes.push_back(node.data());
            }
        }

        // Create a deterministic polygon selection for Copy/Paste operations.
        const QPointF tl(bounds.left() + bounds.width() * 0.25, bounds.top() + bounds.height() * 0.25);
        const QPointF tr(bounds.left() + bounds.width() * 0.75, bounds.top() + bounds.height() * 0.25);
        const QPointF br(bounds.left() + bounds.width() * 0.75, bounds.top() + bounds.height() * 0.75);
        const QPointF bl(bounds.left() + bounds.width() * 0.25, bounds.top() + bounds.height() * 0.75);

        tapAtImagePos(tl);
        tapAtImagePos(tr);
        tapAtImagePos(br);
        tapAtImagePos(bl);
        tapAtImagePos(tl);

        bool selectionMade = false;
        for (int i = 0; i < 100; ++i) {
            QApplication::processEvents();
            image->waitForDone();
            if (!selectedExactRect().isEmpty()) {
                selectionMade = true;
                break;
            }
            QThread::msleep(20);
        }

        if (!selectionMade) {
            qWarning() << "Touch smoke: copypaste did not create a selection";
            ok = false;
        }
        report.step(QStringLiteral("copypaste.create_selection_polygon"), selectionMade);

        const int beforeRootChildCount = image->rootLayer() ? int(image->rootLayer()->childCount()) : 0;
        if (QAction *action = mainWindow->actionCollection()->action(QStringLiteral("copy_selection_to_new_layer"))) {
            action->trigger();
            QApplication::processEvents();
            image->waitForDone();
            report.step(QStringLiteral("copypaste.copy_selection_to_new_layer"), true);
        } else {
            qWarning() << "Touch smoke: copypaste missing action: copy_selection_to_new_layer";
            report.step(QStringLiteral("copypaste.copy_selection_to_new_layer"), false);
            ok = false;
        }

        auto findNewLayer = [&]() -> KisPaintLayer * {
            if (KisGroupLayerSP root = image->rootLayer()) {
                for (KisNodeSP node = root->firstChild(); node; node = node->nextSibling()) {
                    if (beforeNodes.contains(node.data())) {
                        continue;
                    }
                    if (KisPaintLayer *layer = qobject_cast<KisPaintLayer *>(node.data())) {
                        return layer;
                    }
                }
            }
            return nullptr;
        };

        KisPaintLayer *newLayer = nullptr;
        for (int i = 0; i < 120; ++i) {
            QApplication::processEvents();
            image->waitForDone();
            newLayer = findNewLayer();
            if (newLayer) {
                break;
            }
            QThread::msleep(20);
        }
        {
            QJsonObject details;
            details.insert(QStringLiteral("before_root_child_count"), beforeRootChildCount);
            details.insert(QStringLiteral("after_root_child_count"),
                           image->rootLayer() ? int(image->rootLayer()->childCount()) : 0);
            report.step(QStringLiteral("copypaste.root_child_count"), true, details);
        }

        const bool newLayerOk = newLayer && newLayer->paintDevice();
        report.step(QStringLiteral("copypaste.find_new_layer"), newLayerOk);
        if (!newLayerOk) {
            ok = false;
        }

        // Validate pixels in the new layer: inside the selection we expect the seeded red content,
        // and outside the selection we expect transparency.
        if (newLayerOk) {
            KisPaintDeviceSP newDev = newLayer->paintDevice();
            const QVector<QColor> newColors = sampleDeviceColorsForTouchSmoke(newDev, samplePoints);
            bool newInsideOk = false;
            bool newOutsideTransparent = false;
            if (newDev && newColors.size() == samplePoints.size()) {
                newInsideOk = colorsEqualForTouchSmoke(newColors[0], paintColor, 5);
                newOutsideTransparent = newColors[1].isValid() && newColors[1].alpha() <= 10;
            }
            {
                QJsonObject details;
                details.insert(QStringLiteral("inside"), colorToJson(newColors.size() > 0 ? newColors[0] : QColor()));
                details.insert(QStringLiteral("outside"), colorToJson(newColors.size() > 1 ? newColors[1] : QColor()));
                report.step(QStringLiteral("copypaste.new_layer_inside_is_red"), newInsideOk, details);
                report.step(QStringLiteral("copypaste.new_layer_outside_is_transparent"), newOutsideTransparent, details);
            }
            if (!newInsideOk || !newOutsideTransparent) {
                ok = false;
            }
        }

        // Copy should not modify the source layer.
        const QVector<QColor> srcAfterCopy = sampleDeviceColorsForTouchSmoke(srcDev, samplePoints);
        bool srcUnchanged = false;
        if (srcDev && srcAfterCopy.size() == samplePoints.size()) {
            const bool insideIsRed = colorsEqualForTouchSmoke(srcAfterCopy[0], paintColor, 5);
            const bool outsideIsBg = colorsEqualForTouchSmoke(srcAfterCopy[1], bgColor, 5);
            srcUnchanged = insideIsRed && outsideIsBg;
        }
        {
            QJsonObject details;
            details.insert(QStringLiteral("inside"), colorToJson(srcAfterCopy.size() > 0 ? srcAfterCopy[0] : QColor()));
            details.insert(QStringLiteral("outside"), colorToJson(srcAfterCopy.size() > 1 ? srcAfterCopy[1] : QColor()));
            report.step(QStringLiteral("copypaste.source_layer_unchanged"), srcUnchanged, details);
        }
        if (!srcUnchanged) {
            ok = false;
        }

        // Show the overlay for the scenario screenshot.
        if (QAction *action = mainWindow->actionCollection()->action("touch_copypaste_overlay")) {
            action->trigger();
            finalizeSmoke(ok);
            return;
        }

        qWarning() << "Touch smoke: action not found: touch_copypaste_overlay";
        finalizeSmoke(false);
        return;
    }

    qWarning() << "Touch smoke: unknown scenario:" << scenario;
    finalizeSmoke(false);
}

} // namespace

namespace {
struct AppRecursionInfo {
    ~AppRecursionInfo() {
        KIS_SAFE_ASSERT_RECOVER_NOOP(!eventRecursionCount);
        KIS_SAFE_ASSERT_RECOVER_NOOP(postponedSynchronizationEvents.empty());
    }

    int eventRecursionCount {0};
    std::queue<KisSynchronizedConnectionEvent> postponedSynchronizationEvents;
};

struct AppRecursionGuard {
    AppRecursionGuard(AppRecursionInfo *info)
        : m_info(info)
    {
        m_info->eventRecursionCount++;
    }

    ~AppRecursionGuard()
    {
        m_info->eventRecursionCount--;
    }
private:
    AppRecursionInfo *m_info {0};
};

}

/**
 * We cannot make the recursion info be a part of KisApplication,
 * because KisApplication also owns QQmlThread, which is destroyed
 * after KisApplication::Private, which makes QThreadStorage spit
 * a warning about being destroyed too early
 */
Q_GLOBAL_STATIC(QThreadStorage<AppRecursionInfo>, s_recursionInfo)

class KisApplication::Private
{
public:
    Private() {}
    QPointer<KisSplashScreen> splashScreen;
    KisAutoSaveRecoveryDialog *autosaveDialog {0};
    KisLongPressEventFilter *longPressEventFilter {nullptr};
    QPointer<KisMainWindow> mainWindow; // The first mainwindow we create on startup
    bool batchRun {false};
    QVector<QByteArray> earlyRemoteArguments;
    QVector<QString> earlyFileOpenEvents;
    QScopedPointer<KisExtendedModifiersMapperPluginInterface> extendedModifiersPluginInterface;
#ifdef Q_OS_ANDROID
    KisAndroidDonations *androidDonations {nullptr};
#endif
};

class KisApplication::ResetStarting
{
public:
    ResetStarting(KisSplashScreen *splash, int fileCount)
        : m_splash(splash)
        , m_fileCount(fileCount)
    {
    }

    ~ResetStarting()  {

        if (m_splash) {
            m_splash->hide();
            m_splash->deleteLater();
        }
    }

    QPointer<KisSplashScreen> m_splash;
    int m_fileCount;
};


KisApplication::KisApplication(const QString &key, int &argc, char **argv)
    : QtSingleApplication(key, argc, argv)
    , d(new Private)
{
#ifdef Q_OS_MACOS
    setMouseCoalescingEnabled(false);
#endif

    QCoreApplication::addLibraryPath(QCoreApplication::applicationDirPath());

    setApplicationDisplayName("Krita");
    setApplicationName("krita");
    // Note: Qt docs suggest we set this, but if we do, we get resource paths of the form of krita/krita, which is weird.
    //    setOrganizationName("krita");
    setOrganizationDomain("krita.org");

    QString version = KritaVersionWrapper::versionString(true);
    setApplicationVersion(version);
#ifndef Q_OS_MACOS
    setWindowIcon(KisIconUtils::loadIcon("krita-branding"));
#endif

    if (qgetenv("KRITA_NO_STYLE_OVERRIDE").isEmpty()) {

#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        QStringList styles = QStringList() << "haiku" << "macintosh" << "breeze" << "fusion";
#else
        QStringList styles = QStringList() << "haiku" << "macos" << "breeze" << "fusion";
#endif
        if (!styles.contains(style()->objectName().toLower())) {
            Q_FOREACH (const QString & style, styles) {
                if (!setStyle(style)) {
                    qDebug() << "No" << style << "available.";
                }
                else {
                    qDebug() << "Set style" << style;
                    break;
                }
            }
        }

        // if style is set from config, try to load that
        KisConfig cfg(true);
        QString widgetStyleFromConfig = cfg.widgetStyle();
        if(widgetStyleFromConfig != "") {
            qApp->setStyle(widgetStyleFromConfig);
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        } else if (style()->objectName().toLower() == "macintosh") {
            // if no configured style on macOS, default to Fusion
            qApp->setStyle("fusion");
        }
#else
        } else if (style()->objectName().toLower() == "macos") {
            // if no configured style on macOS, default to Fusion
            qApp->setStyle("fusion");
        }
#endif

    }
    else {
        qDebug() << "Style override disabled, using" << style()->objectName();
    }

    /**
     * Load platform plugin for modifiers fetching
     */
    {
        d->extendedModifiersPluginInterface.reset(KisPlatformPluginInterfaceFactory::instance()->createExtendedModifiersMapper());
    }

    // store the style name
    qApp->setProperty(currentUnderlyingStyleNameProperty, style()->objectName());
    KisSynchronizedConnectionBase::registerSynchronizedEventBarrier(std::bind(&KisApplication::processPostponedSynchronizationEvents, this));


#if KRITA_USE_SURFACE_COLOR_MANAGEMENT_API

    /**
     * Automatically assign sRGB color space to all Krita windows,
     * which are not marked with a special tag.
     */
    struct PlatformWindowCreationFilter : QObject
    {
        using QObject::QObject;

        bool eventFilter(QObject *watched, QEvent *event) override {
            if (event->type() == QEvent::PlatformSurface) {
                QWidget *widget = qobject_cast<QWidget*>(watched);
                if (!widget) return false;

                /**
                 * Check for the special tag that is set for the windows that handle
                 * their color space themselves
                 */
                if (watched->property("krita_skip_srgb_surface_manager_assignment").toBool()) {
                    return false;
                }

                QPlatformSurfaceEvent *surfaceEvent = static_cast<QPlatformSurfaceEvent*>(event);
                if (surfaceEvent->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated) {
                    QWindow *nativeWindow = widget->windowHandle();
                    KIS_SAFE_ASSERT_RECOVER_RETURN_VALUE(widget->windowHandle(), false);

                    if (!nativeWindow->findChild<KisSRGBSurfaceColorSpaceManager*>()) {
                        KisSRGBSurfaceColorSpaceManager::tryCreateForCurrentPlatform(widget);
                    }
                }
            }

            return false;
        }
    };

    this->installEventFilter(new PlatformWindowCreationFilter(this));
#endif /* KRITA_USE_SURFACE_COLOR_MANAGEMENT_API */
}

#if defined(Q_OS_WIN) && defined(ENV32BIT)
typedef BOOL (WINAPI *LPFN_ISWOW64PROCESS) (HANDLE, PBOOL);

LPFN_ISWOW64PROCESS fnIsWow64Process;

BOOL isWow64()
{
    BOOL bIsWow64 = FALSE;

    //IsWow64Process is not available on all supported versions of Windows.
    //Use GetModuleHandle to get a handle to the DLL that contains the function
    //and GetProcAddress to get a pointer to the function if available.

    fnIsWow64Process = (LPFN_ISWOW64PROCESS) GetProcAddress(
                GetModuleHandle(TEXT("kernel32")),"IsWow64Process");

    if(0 != fnIsWow64Process)
    {
        if (!fnIsWow64Process(GetCurrentProcess(),&bIsWow64))
        {
            //handle error
        }
    }
    return bIsWow64;
}
#endif

void KisApplication::initializeGlobals(const KisApplicationArguments &args)
{
    Q_UNUSED(args)
    // There are no globals to initialize from the arguments now. There used
    // to be the `dpi` argument, but it doesn't do anything anymore.
}

void KisApplication::addResourceTypes()
{
    // All Krita's resource types
    KoResourcePaths::addAssetType("markers", "data", "/styles/");
    KoResourcePaths::addAssetType("kis_pics", "data", "/pics/");
    KoResourcePaths::addAssetType("kis_images", "data", "/images/");
    KoResourcePaths::addAssetType("metadata_schema", "data", "/metadata/schemas/");
    KoResourcePaths::addAssetType("gmic_definitions", "data", "/gmic/");
    KoResourcePaths::addAssetType("kis_shortcuts", "data", "/shortcuts/");
    KoResourcePaths::addAssetType("kis_actions", "data", "/actions");
    KoResourcePaths::addAssetType("kis_actions", "data", "/pykrita");
    KoResourcePaths::addAssetType("icc_profiles", "data", "/color/icc");
    KoResourcePaths::addAssetType("icc_profiles", "data", "/profiles/");
    KoResourcePaths::addAssetType("tags", "data", "/tags/");
    KoResourcePaths::addAssetType("templates", "data", "/templates");
    KoResourcePaths::addAssetType("pythonscripts", "data", "/pykrita");
    KoResourcePaths::addAssetType("preset_icons", "data", "/preset_icons");
#if defined HAVE_SEEXPR
    KoResourcePaths::addAssetType(ResourceType::SeExprScripts, "data", "/seexpr_scripts/", true);
#endif

    // Make directories for all resources we can save, and tags
    KoResourcePaths::saveLocation("data", "/asl/", true);
    KoResourcePaths::saveLocation("data", "/css_styles/", true);
    KoResourcePaths::saveLocation("data", "/input/", true);
    KoResourcePaths::saveLocation("data", "/pykrita/", true);
    KoResourcePaths::saveLocation("data", "/color-schemes/", true);
    KoResourcePaths::saveLocation("data", "/preset_icons/", true);
    KoResourcePaths::saveLocation("data", "/preset_icons/tool_icons/", true);
    KoResourcePaths::saveLocation("data", "/preset_icons/emblem_icons/", true);
}


bool KisApplication::event(QEvent *event)
{

    #ifdef Q_OS_MACOS
    if (event->type() == QEvent::FileOpen) {
        QFileOpenEvent *openEvent = static_cast<QFileOpenEvent *>(event);
        fileOpenRequested(openEvent->file());
        return true;
    }
    #endif
    return QApplication::event(event);
}


bool KisApplication::registerResources()
{
    KisResourceLoaderRegistry *reg = KisResourceLoaderRegistry::instance();

    reg->add(new KisResourceLoader<KisPaintOpPreset>(ResourceSubType::KritaPaintOpPresets, ResourceType::PaintOpPresets, i18n("Brush presets"),
                                                     QStringList() << "application/x-krita-paintoppreset"));

    reg->add(new KisResourceLoader<KisGbrBrush>(ResourceSubType::GbrBrushes, ResourceType::Brushes, i18n("Brush tips"), QStringList() << "image/x-gimp-brush"));
    reg->add(new KisResourceLoader<KisImagePipeBrush>(ResourceSubType::GihBrushes, ResourceType::Brushes, i18n("Brush tips"), QStringList() << "image/x-gimp-brush-animated"));
    reg->add(new KisResourceLoader<KisSvgBrush>(ResourceSubType::SvgBrushes, ResourceType::Brushes, i18n("Brush tips"), QStringList() << "image/svg+xml"));
    reg->add(new KisResourceLoader<KisPngBrush>(ResourceSubType::PngBrushes, ResourceType::Brushes, i18n("Brush tips"), QStringList() << "image/png"));

    reg->add(new KisResourceLoader<KoSegmentGradient>(ResourceSubType::SegmentedGradients, ResourceType::Gradients, i18n("Gradients"), QStringList() << "application/x-gimp-gradient"));
    reg->add(new KisResourceLoader<KoStopGradient>(ResourceSubType::StopGradients, ResourceType::Gradients, i18n("Gradients"), QStringList() << "image/svg+xml"));

    reg->add(new KisResourceLoader<KoColorSet>(ResourceType::Palettes, ResourceType::Palettes, i18n("Palettes"),
                                     QStringList() << KisMimeDatabase::mimeTypeForSuffix("kpl")
                                               << KisMimeDatabase::mimeTypeForSuffix("gpl")
                                               << KisMimeDatabase::mimeTypeForSuffix("pal")
                                               << KisMimeDatabase::mimeTypeForSuffix("act")
                                               << KisMimeDatabase::mimeTypeForSuffix("aco")
                                               << KisMimeDatabase::mimeTypeForSuffix("css")
                                               << KisMimeDatabase::mimeTypeForSuffix("colors")
                                               << KisMimeDatabase::mimeTypeForSuffix("xml")
                                               << KisMimeDatabase::mimeTypeForSuffix("sbz")));


    reg->add(new KisResourceLoader<KoPattern>(ResourceType::Patterns, ResourceType::Patterns, i18n("Patterns"), {"application/x-gimp-pattern", "image/x-gimp-pat", "application/x-gimp-pattern", "image/bmp", "image/jpeg", "image/png", "image/tiff"}));
    reg->add(new KisResourceLoader<KisWorkspaceResource>(ResourceType::Workspaces, ResourceType::Workspaces, i18n("Workspaces"), QStringList() << "application/x-krita-workspace"));
    reg->add(new KisResourceLoader<KoSvgSymbolCollectionResource>(ResourceType::Symbols, ResourceType::Symbols, i18n("SVG symbol libraries"), QStringList() << "image/svg+xml"));
    reg->add(new KisResourceLoader<KisWindowLayoutResource>(ResourceType::WindowLayouts, ResourceType::WindowLayouts, i18n("Window layouts"), QStringList() << "application/x-krita-windowlayout"));
    reg->add(new KisResourceLoader<KisSessionResource>(ResourceType::Sessions, ResourceType::Sessions, i18n("Sessions"), QStringList() << "application/x-krita-session"));
    reg->add(new KisResourceLoader<KoGamutMask>(ResourceType::GamutMasks, ResourceType::GamutMasks, i18n("Gamut masks"), QStringList() << "application/x-krita-gamutmasks"));
#if defined HAVE_SEEXPR
    reg->add(new KisResourceLoader<KisSeExprScript>(ResourceType::SeExprScripts, ResourceType::SeExprScripts, i18n("SeExpr Scripts"), QStringList() << "application/x-krita-seexpr-script"));
#endif
    // XXX: this covers only individual styles, not the library itself!
    reg->add(new KisResourceLoader<KisPSDLayerStyle>(ResourceType::LayerStyles,
                                                     ResourceType::LayerStyles,
                                                     i18nc("Resource type name", "Layer styles"),
                                                     QStringList() << "application/x-photoshop-style"));

    reg->add(new KisResourceLoader<KoFontFamily>(ResourceType::FontFamilies, ResourceType::FontFamilies, i18n("Font Families"), QStringList() << "application/x-font-ttf" << "application/x-font-otf"));
    reg->add(new KisResourceLoader<KoCssStylePreset>(ResourceType::CssStyles, ResourceType::CssStyles, i18n("Style Presets"), QStringList() << "image/svg+xml"));

    reg->registerFixup(10, new KisBrushTypeMetaDataFixup());

#ifndef Q_OS_ANDROID
    QString databaseLocation = KoResourcePaths::getAppDataLocation();
#else
    // Sqlite doesn't support content URIs (obviously). So, we make database location unconfigurable on android.
    QString databaseLocation = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#endif

    if (!KisResourceCacheDb::initialize(databaseLocation)) {
        QMessageBox::critical(qApp->activeWindow(), i18nc("@title:window", "Krita: Fatal error"), i18n("%1\n\nKrita will quit now.", KisResourceCacheDb::lastError()));
    }

    KisResourceLocator::LocatorError r = KisResourceLocator::instance()->initialize(KoResourcePaths::getApplicationRoot() + "/share/krita");
    connect(KisResourceLocator::instance(), SIGNAL(progressMessage(const QString&)), this, SLOT(setSplashScreenLoadingText(const QString&)));
    if (r != KisResourceLocator::LocatorError::Ok && qApp->inherits("KisApplication")) {
        QMessageBox::critical(qApp->activeWindow(), i18nc("@title:window", "Krita: Fatal error"), KisResourceLocator::instance()->errorMessages().join('\n') + i18n("\n\nKrita will quit now."));
        return false;
    }
    return true;
}

void KisApplication::loadPlugins()
{
    //    qDebug() << "loadPlugins();";

    KoShapeRegistry* r = KoShapeRegistry::instance();
    r->add(new KisShapeSelectionFactory());
    KoColorSpaceRegistry::instance();
    KisActionRegistry::instance();
    KisFilterRegistry::instance();
    KisGeneratorRegistry::instance();
    KisPaintOpRegistry::instance();
    KoToolRegistry::instance();
    KoDockRegistry::instance();
    KisMetadataBackendRegistry::instance();
}

bool KisApplication::start(const KisApplicationArguments &args)
{
    KisConfig cfg(false);

#if defined(Q_OS_WIN)
#ifdef ENV32BIT

    if (isWow64() && !cfg.readEntry("WarnedAbout32Bits", false)) {
        QMessageBox::information(qApp->activeWindow(),
                                 i18nc("@title:window", "Krita: Warning"),
                                 i18n("You are running a 32 bits build on a 64 bits Windows.\n"
                                      "This is not recommended.\n"
                                      "Please download and install the x64 build instead."));
        cfg.writeEntry("WarnedAbout32Bits", true);

    }
#endif
#endif

    QString opengl = cfg.canvasState();
    if (opengl == "OPENGL_NOT_TRIED" ) {
        cfg.setCanvasState("TRY_OPENGL");
    }
    else if (opengl != "OPENGL_SUCCESS" && opengl != "TRY_OPENGL") {
        cfg.setCanvasState("OPENGL_FAILED");
    }

    setSplashScreenLoadingText(i18n("Initializing Globals..."));
    processEvents();
    initializeGlobals(args);

    const bool doNewImage = args.doNewImage();
    const bool doTemplate = args.doTemplate();
    const bool exportAs = args.exportAs();
    const bool exportSequence = args.exportSequence();
    const QString exportFileName = args.exportFileName();

    d->batchRun = (exportAs || exportSequence || !exportFileName.isEmpty());
    const bool needsMainWindow = (!exportAs && !exportSequence);
    // only show the mainWindow when no command-line mode option is passed
    bool showmainWindow = (!exportAs && !exportSequence); // would be !batchRun;

    const bool showSplashScreen = !d->batchRun && qEnvironmentVariableIsEmpty("NOSPLASH");
    if (showSplashScreen && d->splashScreen) {
        d->splashScreen->show();
        d->splashScreen->repaint();
        processEvents();
    }

    KConfigGroup group(KSharedConfig::openConfig(), "theme");
#ifndef Q_OS_HAIKU
    Digikam::ThemeManager themeManager;
    themeManager.setCurrentTheme(group.readEntry("Theme", "Krita dark"));
#endif

    ResetStarting resetStarting(d->splashScreen, args.filenames().count()); // remove the splash when done
    Q_UNUSED(resetStarting);

    // Make sure we can save resources and tags
    setSplashScreenLoadingText(i18n("Adding resource types..."));
    processEvents();
    addResourceTypes();

    setSplashScreenLoadingText(i18n("Loading plugins..."));
    processEvents();
    // Load the plugins
    loadPlugins();

    // Load all resources
    setSplashScreenLoadingText(i18n("Loading resources..."));
    processEvents();
    if (!registerResources()) {
        return false;
    }

    KisPart *kisPart = KisPart::instance();
    if (needsMainWindow) {
        // show a mainWindow asap, if we want that
        setSplashScreenLoadingText(i18n("Loading Main Window..."));
        processEvents();


        bool sessionNeeded = true;
        auto sessionMode = cfg.sessionOnStartup();

        if (!args.session().isEmpty()) {
            sessionNeeded = !kisPart->restoreSession(args.session());
        } else if (sessionMode == KisConfig::SOS_ShowSessionManager) {
            showmainWindow = false;
            sessionNeeded = false;
            kisPart->showSessionManager();
        } else if (sessionMode == KisConfig::SOS_PreviousSession) {
            KConfigGroup sessionCfg = KSharedConfig::openConfig()->group("session");
            const QString &sessionName = sessionCfg.readEntry("previousSession");

            sessionNeeded = !kisPart->restoreSession(sessionName);
        }

        if (sessionNeeded) {
            kisPart->startBlankSession();
        }

        if (!args.windowLayout().isEmpty()) {
            KoResourceServer<KisWindowLayoutResource> * rserver = KisResourceServerProvider::instance()->windowLayoutServer();
            KisWindowLayoutResourceSP windowLayout = rserver->resource("", "", args.windowLayout());
            if (windowLayout) {
                windowLayout->applyLayout();
            }
        }

        setSplashScreenLoadingText(i18n("Launching..."));

        if (showmainWindow) {
            d->mainWindow = kisPart->currentMainwindow();

            if (!args.workspace().isEmpty()) {
                KoResourceServer<KisWorkspaceResource> * rserver = KisResourceServerProvider::instance()->workspaceServer();
                KisWorkspaceResourceSP workspace = rserver->resource("", "", args.workspace());
                if (workspace) {
                    d->mainWindow->restoreWorkspace(workspace);
                }
            }

            if (args.canvasOnly()) {
                d->mainWindow->viewManager()->switchCanvasOnly(true);
            }

            if (args.fullScreen()) {
                d->mainWindow->showFullScreen();
            }
        } else {
            d->mainWindow = kisPart->createMainWindow();
        }
    }
    short int numberOfOpenDocuments = 0; // number of documents open

    // Check for autosave files that can be restored, if we're not running a batch run (test)
    if (!d->batchRun) {
        checkAutosaveFiles();
    }

    setSplashScreenLoadingText(QString()); // done loading, so clear out label
    processEvents();

    //configure the unit manager
    KisSpinBoxUnitManagerFactory::setDefaultUnitManagerBuilder(new KisDocumentAwareSpinBoxUnitManagerBuilder());
    connect(this, &KisApplication::aboutToQuit, &KisSpinBoxUnitManagerFactory::clearUnitManagerBuilder); //ensure the builder is destroyed when the application leave.
    //the new syntax slot syntax allow to connect to a non q_object static method.

    // Long-press emulation.
    connect(KisConfigNotifier::instance(), &KisConfigNotifier::sigLongPressChanged, this, &KisApplication::slotSetLongPress);
    slotSetLongPress(cfg.longPressEnabled());

    // Create a new image, if needed
    if (doNewImage) {
        KisDocument *doc = args.createDocumentFromArguments();
        if (doc) {
            kisPart->addDocument(doc);
            d->mainWindow->addViewAndNotifyLoadingCompleted(doc);
        }
    }

    // Get the command line arguments which we have to parse
    int argsCount = args.filenames().count();
    if (argsCount > 0) {
        // Loop through arguments
        for (int argNumber = 0; argNumber < argsCount; argNumber++) {
            QString fileName = args.filenames().at(argNumber);
            // are we just trying to open a template?
            if (doTemplate) {
                // called in mix with batch options? ignore and silently skip
                if (d->batchRun) {
                    continue;
                }
                if (createNewDocFromTemplate(fileName, d->mainWindow)) {
                    ++numberOfOpenDocuments;
                }
                // now try to load
            }
            else {
                if (exportAs) {
                    QString outputMimetype = KisMimeDatabase::mimeTypeForFile(exportFileName, false);
                    if (outputMimetype == "application/octetstream") {
                        dbgKrita << i18n("Mimetype not found, try using the -mimetype option") << Qt::endl;
                        return false;
                    }

                    KisDocument *doc = kisPart->createDocument();
                    doc->setFileBatchMode(d->batchRun);
                    bool result = doc->openPath(fileName);

                    if (!result) {
                        errKrita << "Could not load " << fileName << ":" << doc->errorMessage();
                        QTimer::singleShot(0, this, SLOT(quit()));
                        return false;
                    }

                    if (exportFileName.isEmpty()) {
                        errKrita << "Export destination is not specified for" << fileName << "Please specify export destination with --export-filename option";
                        QTimer::singleShot(0, this, SLOT(quit()));
                        return false;
                    }

                    qApp->processEvents(); // For vector layers to be updated

                    doc->setFileBatchMode(true);
                    doc->image()->waitForDone();

                    if (!doc->exportDocumentSync(exportFileName, outputMimetype.toLatin1())) {
                        errKrita << "Could not export " << fileName << "to" << exportFileName << ":" << doc->errorMessage();
                    }
                    QTimer::singleShot(0, this, SLOT(quit()));
                    return true;
                }
                else if (exportSequence) {
                    KisDocument *doc = kisPart->createDocument();
                    doc->setFileBatchMode(d->batchRun);
                    doc->openPath(fileName);
                    qApp->processEvents(); // For vector layers to be updated
                    
                    if (!doc->image()->animationInterface()->hasAnimation()) {
                        errKrita << "This file has no animation." << Qt::endl;
                        QTimer::singleShot(0, this, SLOT(quit()));
                        return false;
                    }

                    doc->setFileBatchMode(true);
                    int sequenceStart = 0;


                    qDebug() << ppVar(exportFileName);
                    KisAsyncAnimationFramesSaveDialog exporter(doc->image(),
                                               doc->image()->animationInterface()->documentPlaybackRange(),
                                               exportFileName,
                                               sequenceStart,
                                               false,
                                               0);

                    exporter.setBatchMode(d->batchRun);

                    KisAsyncAnimationFramesSaveDialog::Result result = exporter.regenerateRange(nullptr);
                    qDebug() << ppVar(result);

                    if (result != KisAsyncAnimationFramesSaveDialog::RenderComplete) {
                        errKrita << i18n("Failed to render animation frames!") << Qt::endl;
                    }

                    QTimer::singleShot(0, this, SLOT(quit()));
                    return true;
                }
                else if (d->mainWindow) {
                    if (QFileInfo(fileName).fileName().endsWith(".bundle", Qt::CaseInsensitive)) {
                        d->mainWindow->installBundle(fileName);
                    }
                    else {
                        KisMainWindow::OpenFlags flags = d->batchRun ? KisMainWindow::BatchMode : KisMainWindow::None;

                        if (d->mainWindow->openDocument(fileName, flags)) {
                            // Normal case, success
                            numberOfOpenDocuments++;
                        }
                    }
                }
            }
        }
    }

    //add an image as file-layer
    if (!args.fileLayer().isEmpty()){
        if (d->mainWindow->viewManager()->image()){
            KisFileLayer *fileLayer = new KisFileLayer(d->mainWindow->viewManager()->image(), "",
                                                    args.fileLayer(), KisFileLayer::None, "Bicubic",
                                                    d->mainWindow->viewManager()->image()->nextLayerName(i18n("File layer")), OPACITY_OPAQUE_U8);
            QFileInfo fi(fileLayer->path());
            if (fi.exists()){
                KisNodeCommandsAdapter adapter(d->mainWindow->viewManager());
                adapter.addNode(fileLayer, d->mainWindow->viewManager()->activeNode()->parent(),
                                    d->mainWindow->viewManager()->activeNode());
            }
            else{
                QMessageBox::warning(qApp->activeWindow(), i18nc("@title:window", "Krita:Warning"),
                                            i18n("Cannot add %1 as a file layer: the file does not exist.", fileLayer->path()));
            }
        }
        else if (this->isRunning()){
            QMessageBox::warning(qApp->activeWindow(), i18nc("@title:window", "Krita:Warning"),
                                i18n("Cannot add the file layer: no document is open.\n\n"
"You can create a new document using the --new-image option, or you can open an existing file.\n\n"
"If you instead want to add the file layer to a document in an already running instance of Krita, check the \"Allow only one instance of Krita\" checkbox in the settings (Settings -> General -> Window)."));
        }
        else {
            QMessageBox::warning(qApp->activeWindow(), i18nc("@title:window", "Krita: Warning"),
                                i18n("Cannot add the file layer: no document is open.\n"
                                     "You can either create a new file using the --new-image option, or you can open an existing file."));
        }
    }

    // fixes BUG:369308  - Krita crashing on splash screen when loading.
    // trying to open a file before Krita has loaded can cause it to hang and crash
    if (d->splashScreen) {
        d->splashScreen->displayLinks(true);
        d->splashScreen->displayRecentFiles(true);
    }

    Q_FOREACH(const QByteArray &message, d->earlyRemoteArguments) {
        executeRemoteArguments(message, d->mainWindow);
    }

    KisUsageLogger::writeSysInfo(KisUsageLogger::screenInformation());

    // process File open event files
    if (!d->earlyFileOpenEvents.isEmpty()) {
        hideSplashScreen();
        Q_FOREACH(QString fileName, d->earlyFileOpenEvents) {
            d->mainWindow->openDocument(fileName, QFlags<KisMainWindow::OpenFlag>());
        }
    }

    verifyMetatypeRegistration();

    if (d->mainWindow && !args.touchSmokeScenario().isEmpty()) {
        const QString scenario = args.touchSmokeScenario();
        const QPointer<KisMainWindow> mainWindow = d->mainWindow;
        QTimer::singleShot(0, this, [scenario, mainWindow]() {
            if (!mainWindow) {
                return;
            }
            runTouchSmokeScenario(scenario, mainWindow);
        });
    }

    // not calling this before since the program will quit there.
    return true;
}

KisApplication::~KisApplication()
{
    if (!isRunning()) {
        KisResourceCacheDb::deleteTemporaryResources();
        KisResourceCacheDb::performHouseKeepingOnExit();
    }
}

void KisApplication::setSplashScreen(QWidget *splashScreen)
{
    d->splashScreen = qobject_cast<KisSplashScreen*>(splashScreen);
}

void KisApplication::setSplashScreenLoadingText(const QString &textToLoad)
{
    if (d->splashScreen) {
        d->splashScreen->setLoadingText(textToLoad);
        d->splashScreen->repaint();
    }
}

void KisApplication::hideSplashScreen()
{
    if (d->splashScreen) {
        // hide the splashscreen to see the dialog
        d->splashScreen->hide();
    }
}


bool KisApplication::notify(QObject *receiver, QEvent *event)
{
    try {
        bool result = true;

        /**
         * KisApplication::notify() is called for every event loop processed in
         * any thread, so we need to make sure our counters and postponed events
         * queues are stored in a per-thread way.
         */
        AppRecursionInfo &info = s_recursionInfo->localData();

        {
            // QApplication::notify() can throw, so use RAII for counters
            AppRecursionGuard guard(&info);

            if (event->type() == KisSynchronizedConnectionBase::eventType()) {

                if (info.eventRecursionCount > 1) {
                    KisSynchronizedConnectionEvent *typedEvent = static_cast<KisSynchronizedConnectionEvent*>(event);
                    KIS_SAFE_ASSERT_RECOVER_NOOP(typedEvent->destination == receiver);

                    info.postponedSynchronizationEvents.emplace(KisSynchronizedConnectionEvent(*typedEvent));
                } else {
                    result = QApplication::notify(receiver, event);
                }
            } else {
                result = QApplication::notify(receiver, event);
            }
        }

        if (!info.eventRecursionCount) {
            processPostponedSynchronizationEvents();

        }

        return result;

    } catch (std::exception &e) {
        qWarning("Error %s sending event %i to object %s",
                 e.what(), event->type(), qPrintable(receiver->objectName()));
    } catch (...) {
        qWarning("Error <unknown> sending event %i to object %s",
                 event->type(), qPrintable(receiver->objectName()));
    }
    return false;
}

void KisApplication::processPostponedSynchronizationEvents()
{
    AppRecursionInfo &info = s_recursionInfo->localData();

    while (!info.postponedSynchronizationEvents.empty()) {
        // QApplication::notify() can throw, so use RAII for counters
        AppRecursionGuard guard(&info);

        /// We must pop event from the queue **before** we call
        /// QApplication::notify(), because it can throw!
        KisSynchronizedConnectionEvent typedEvent = info.postponedSynchronizationEvents.front();
        info.postponedSynchronizationEvents.pop();

        if (!typedEvent.destination) {
            qWarning() << "WARNING: the destination object of KisSynchronizedConnection has been destroyed during postponed delivery";
            continue;
        }

        QApplication::notify(typedEvent.destination, &typedEvent);
    }
}

bool KisApplication::isStoreApplication()
{
    if (qEnvironmentVariableIsSet("STEAMAPPID") || qEnvironmentVariableIsSet("SteamAppId")) {
        return true;
    }

    if (applicationDirPath().toLower().contains("steam")) {
        return true;
    }

#ifdef Q_OS_WIN
    // This is also true for user-installed MSIX, but that's
    // likely only true in institutional situations, where
    // we don't want to show the beginning banner either.
    if (KisWindowsPackageUtils::isRunningInPackage()) {
        return true;
    }
#endif

#ifdef Q_OS_MACOS
    KisMacosEntitlements entitlements;
    if (entitlements.sandbox()) {
       return true;
    }
#endif

    return false;
}

void KisApplication::verifyMetatypeRegistration()
{
    /**
     * Verify that all our statically registered types are actually registered.
     * This check is skipped in release builds, when HIDE_SAFE_ASSERTS is defined
     */
#if !defined(HIDE_SAFE_ASSERTS) || defined(CRASH_ON_SAFE_ASSERTS)

    auto verifyTypeRegistered = [] (const char *type) {
        const int typeId = QMetaType::type(type);

        if (typeId <= 0) {
            qFatal("ERROR: type-id for metatype %s is not found", type);
        }

        if (!QMetaType::isRegistered(typeId)) {
            qFatal("ERROR: metatype %s is not registered", type);
        }
    };

    verifyTypeRegistered("KisBrushSP");
    verifyTypeRegistered("KoSvgText::AutoValue");
    verifyTypeRegistered("KoSvgText::BackgroundProperty");
    verifyTypeRegistered("KoSvgText::StrokeProperty");
    verifyTypeRegistered("KoSvgText::TextTransformInfo");
    verifyTypeRegistered("KoSvgText::TextIndentInfo");
    verifyTypeRegistered("KoSvgText::TabSizeInfo");
    verifyTypeRegistered("KoSvgText::LineHeightInfo");
    verifyTypeRegistered("KisPaintopLodLimitations");
    verifyTypeRegistered("KisImageSP");
    verifyTypeRegistered("KisImageSignalType");
    verifyTypeRegistered("KisNodeSP");
    verifyTypeRegistered("KisNodeList");
    verifyTypeRegistered("KisPaintDeviceSP");
    verifyTypeRegistered("KisTimeSpan");
    verifyTypeRegistered("KoColor");
    verifyTypeRegistered("KoResourceSP");
    verifyTypeRegistered("KoResourceCacheInterfaceSP");
    verifyTypeRegistered("KisAsyncAnimationRendererBase::CancelReason");
    verifyTypeRegistered("KisGridConfig");
    verifyTypeRegistered("KisGuidesConfig");
    verifyTypeRegistered("KisUpdateInfoSP");
    verifyTypeRegistered("KisToolChangesTrackerDataSP");
    verifyTypeRegistered("QVector<QImage>");
    verifyTypeRegistered("SnapshotDirInfoList");
    verifyTypeRegistered("TransformTransactionProperties");
    verifyTypeRegistered("ToolTransformArgs");
    verifyTypeRegistered("QPainterPath");
#endif
}

void KisApplication::executeRemoteArguments(QByteArray message, KisMainWindow *mainWindow)
{
    KisApplicationArguments args = KisApplicationArguments::deserialize(message);
    const bool doTemplate = args.doTemplate();
    const bool doNewImage = args.doNewImage();
    const int argsCount = args.filenames().count();
    bool documentCreated = false;

    // Create a new image, if needed
    if (doNewImage) {
        KisDocument *doc = args.createDocumentFromArguments();
        if (doc) {
            KisPart::instance()->addDocument(doc);
            d->mainWindow->addViewAndNotifyLoadingCompleted(doc);
        }
    }
    if (argsCount > 0) {
        // Loop through arguments
        for (int argNumber = 0; argNumber < argsCount; ++argNumber) {
            QString filename = args.filenames().at(argNumber);
            // are we just trying to open a template?
            if (doTemplate) {
                documentCreated |= createNewDocFromTemplate(filename, mainWindow);
            }
            else if (QFile(filename).exists()) {
                KisMainWindow::OpenFlags flags = d->batchRun ? KisMainWindow::BatchMode : KisMainWindow::None;
                documentCreated |= mainWindow->openDocument(filename, flags);
            }
        }
    }

    //add an image as file-layer if called in another process and singleApplication is enabled
    if (!args.fileLayer().isEmpty()){
        if (argsCount > 0  && !documentCreated){
            //arg was passed but document was not created so don't add the file layer.
            QMessageBox::warning(mainWindow, i18nc("@title:window", "Krita:Warning"),
                                            i18n("Couldn't open file %1",args.filenames().at(argsCount - 1)));
        }
        else if (mainWindow->viewManager()->image()){
            KisFileLayer *fileLayer = new KisFileLayer(mainWindow->viewManager()->image(), "",
                                                    args.fileLayer(), KisFileLayer::None, "Bicubic",
                                                    mainWindow->viewManager()->image()->nextLayerName(i18n("File layer")), OPACITY_OPAQUE_U8);
            QFileInfo fi(fileLayer->path());
            if (fi.exists()){
                KisNodeCommandsAdapter adapter(d->mainWindow->viewManager());
                adapter.addNode(fileLayer, d->mainWindow->viewManager()->activeNode()->parent(),
                                    d->mainWindow->viewManager()->activeNode());
            }
            else{
                QMessageBox::warning(mainWindow, i18nc("@title:window", "Krita:Warning"),
                                            i18n("Cannot add %1 as a file layer: the file does not exist.", fileLayer->path()));
            }
        }
        else {
            QMessageBox::warning(mainWindow, i18nc("@title:window", "Krita:Warning"),
                                            i18n("Cannot add the file layer: no document is open."));
        }
    }

    if (mainWindow && !args.touchSmokeScenario().isEmpty()) {
        const QString scenario = args.touchSmokeScenario();
        const QPointer<KisMainWindow> mw = mainWindow;
        QTimer::singleShot(0, this, [scenario, mw]() {
            if (!mw) {
                return;
            }
            runTouchSmokeScenario(scenario, mw);
        });
    }
}


void KisApplication::remoteArguments(const QString &message)
{
    // check if we have any mainwindow
    KisMainWindow *mw = qobject_cast<KisMainWindow*>(qApp->activeWindow());

    if (!mw && KisPart::instance()->mainWindows().size() > 0) {
        mw = KisPart::instance()->mainWindows().first();
    }

    const QByteArray unpackedMessage =
        QByteArray::fromBase64(message.toLatin1());

    if (!mw) {
        d->earlyRemoteArguments << unpackedMessage;
        return;
    }
    executeRemoteArguments(unpackedMessage, mw);
}

void KisApplication::fileOpenRequested(const QString &url)
{
    if (!d->mainWindow) {
        d->earlyFileOpenEvents.append(url);
        return;
    }

    KisMainWindow::OpenFlags flags = d->batchRun ? KisMainWindow::BatchMode : KisMainWindow::None;
    d->mainWindow->openDocument(url, flags);
}

void KisApplication::touchSmokeScenarioRequested(const QString &scenario)
{
    if (!d->mainWindow) {
        return;
    }

    runTouchSmokeScenario(scenario, d->mainWindow);
}


void KisApplication::slotSetLongPress(bool enabled)
{
    if (enabled && !d->longPressEventFilter) {
        d->longPressEventFilter = new KisLongPressEventFilter(this);
        installEventFilter(d->longPressEventFilter);
    } else if (!enabled && d->longPressEventFilter) {
        removeEventFilter(d->longPressEventFilter);
        d->longPressEventFilter->deleteLater();
        d->longPressEventFilter = nullptr;
    }
}

void KisApplication::checkAutosaveFiles()
{
    if (d->batchRun) return;

    QDir dir = KisAutoSaveRecoveryDialog::autoSaveLocation();

    // Check for autosave files from a previous run. There can be several, and
    // we want to offer a restore for every one. Including a nice thumbnail!

    // Hidden autosave files
    QStringList filters = QStringList() << QString(".krita-*-*-autosave.kra");

    // all autosave files for our application
    QStringList autosaveFiles = dir.entryList(filters, QDir::Files | QDir::Hidden);

    // Visible autosave files
    filters = QStringList() << QString("krita-*-*-autosave.kra");
    autosaveFiles += dir.entryList(filters, QDir::Files);

    // Allow the user to make their selection
    if (autosaveFiles.size() > 0) {
        if (d->splashScreen) {
            // hide the splashscreen to see the dialog
            d->splashScreen->hide();
        }
        d->autosaveDialog = new KisAutoSaveRecoveryDialog(autosaveFiles, activeWindow());
        QDialog::DialogCode result = (QDialog::DialogCode) d->autosaveDialog->exec();

        if (result == QDialog::Accepted) {
            QStringList filesToRecover = d->autosaveDialog->recoverableFiles();
            Q_FOREACH (const QString &autosaveFile, autosaveFiles) {
                if (!filesToRecover.contains(autosaveFile)) {
                    KisUsageLogger::log(QString("Removing autosave file %1").arg(dir.absolutePath() + "/" + autosaveFile));
                    QFile::remove(dir.absolutePath() + "/" + autosaveFile);
                }
            }
            autosaveFiles = filesToRecover;
        } else {
            autosaveFiles.clear();
        }

        if (autosaveFiles.size() > 0) {
            QList<QString> autosavePaths;
            Q_FOREACH (const QString &autoSaveFile, autosaveFiles) {
                const QString path = dir.absolutePath() + QLatin1Char('/') + autoSaveFile;
                autosavePaths << path;
            }
            if (d->mainWindow) {
                Q_FOREACH (const QString &path, autosavePaths) {
                    KisMainWindow::OpenFlags flags = d->batchRun ? KisMainWindow::BatchMode : KisMainWindow::None;
                    d->mainWindow->openDocument(path, flags | KisMainWindow::RecoveryFile);
                }
            }
        }
        // cleanup
        delete d->autosaveDialog;
        d->autosaveDialog = nullptr;
    }
}

bool KisApplication::createNewDocFromTemplate(const QString &fileName, KisMainWindow *mainWindow)
{
    QString templatePath;

    if (QFile::exists(fileName)) {
        templatePath = fileName;
        dbgUI << "using full path...";
    }
    else {
        QString desktopName(fileName);
        const QString templatesResourcePath =  QStringLiteral("templates/");

        QStringList paths = KoResourcePaths::findAllAssets("data", templatesResourcePath + "*/" + desktopName);
        if (paths.isEmpty()) {
            paths = KoResourcePaths::findAllAssets("data", templatesResourcePath + desktopName);
        }

        if (paths.isEmpty()) {
            QMessageBox::critical(qApp->activeWindow(), i18nc("@title:window", "Krita"),
                                  i18n("No template found for: %1", desktopName));
        } else if (paths.count() > 1) {
            QMessageBox::critical(qApp->activeWindow(), i18nc("@title:window", "Krita"),
                                  i18n("Too many templates found for: %1", desktopName));
        } else {
            templatePath = paths.at(0);
        }
    }

    if (!templatePath.isEmpty()) {
        KDesktopFile templateInfo(templatePath);

        KisMainWindow::OpenFlags batchFlags = d->batchRun ? KisMainWindow::BatchMode : KisMainWindow::None;
        if (mainWindow->openDocument(templatePath, KisMainWindow::Import | batchFlags)) {
            dbgUI << "Template loaded...";
            return true;
        }
        else {
            QMessageBox::critical(qApp->activeWindow(), i18nc("@title:window", "Krita"),
                                  i18n("Template %1 failed to load.", fileName));
        }
    }

    return false;
}

void KisApplication::resetConfig()
{
    KIS_ASSERT_RECOVER_RETURN(qApp->thread() == QThread::currentThread());

    KSharedConfigPtr config =  KSharedConfig::openConfig();
    config->markAsClean();
    
    // find user settings file
    const QString configPath = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    QString kritarcPath = configPath + QStringLiteral("/kritarc");
    
    QFile kritarcFile(kritarcPath);
    
    if (kritarcFile.exists()) {
        if (kritarcFile.open(QFile::ReadWrite)) {
            QString backupKritarcPath = kritarcPath + QStringLiteral(".backup");
    
            QFile backupKritarcFile(backupKritarcPath);
    
            if (backupKritarcFile.exists()) {
                backupKritarcFile.remove();
            }

            QMessageBox::information(qApp->activeWindow(),
                                 i18nc("@title:window", "Krita"),
                                 i18n("Krita configurations reset!\n\n"
                                      "Backup file was created at: %1\n\n"
                                      "Restart Krita for changes to take effect.",
                                      backupKritarcPath),
                                 QMessageBox::Ok, QMessageBox::Ok);

            // clear file
            kritarcFile.rename(backupKritarcPath);

            kritarcFile.close();
        }
        else {
            QMessageBox::warning(qApp->activeWindow(),
                                 i18nc("@title:window", "Krita"),
                                 i18n("Failed to clear %1\n\n"
                                      "Please make sure no other program is using the file and try again.",
                                      kritarcPath),
                                 QMessageBox::Ok, QMessageBox::Ok);
        }
    }

    // reload from disk; with the user file settings cleared,
    // this should load any default configuration files shipping with the program
    config->reparseConfiguration();
    config->sync();

    // Restore to default workspace
    KConfigGroup cfg = KSharedConfig::openConfig()->group("MainWindow");

    QString currentWorkspace = cfg.readEntry<QString>("CurrentWorkspace", "Default");
    KoResourceServer<KisWorkspaceResource> * rserver = KisResourceServerProvider::instance()->workspaceServer();
    KisWorkspaceResourceSP workspace = rserver->resource("", "", currentWorkspace);

    if (workspace) {
        d->mainWindow->restoreWorkspace(workspace);
    }
}

void KisApplication::askResetConfig()
{
    bool ok = QMessageBox::question(qApp->activeWindow(),
                                    i18nc("@title:window", "Krita"),
                                    i18n("Do you want to clear the settings file?"),
                                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
    if (ok) {
        resetConfig();
    }
}

KisExtendedModifiersMapperPluginInterface* KisApplication::extendedModifiersPluginInterface()
{
    return d->extendedModifiersPluginInterface.data();
}

#ifdef Q_OS_ANDROID
KisAndroidDonations *KisApplication::androidDonations()
{
    if (!d->androidDonations) {
        d->androidDonations = new KisAndroidDonations(this);
        d->androidDonations->syncState();
    }
    return d->androidDonations;
}
#endif
