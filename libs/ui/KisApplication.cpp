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
#include <QMimeData>
#include <QProcessEnvironment>
#include <QStringList>
#include <QStyle>
#include <QStyleFactory>
#include <QSysInfo>
#include <QThread>
#include <QTimer>
#include <QWidget>
#include <QDockWidget>
#include <QMenu>
#include <QImageReader>
#include <QImageWriter>
#include <QThread>

#include <klocalizedstring.h>
#include <kdesktopfile.h>
#include <kconfig.h>
#include <kconfiggroup.h>

#include "widgets/kis_touch_actions_sheet.h"
#include "widgets/kis_touch_quickmenu_overlay.h"

#include <KoColor.h>

#include <KoDockRegistry.h>
#include <KoToolRegistry.h>
#include <KoToolManager.h>
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
#include <KisUsageLogger.h>
#include "kis_popup_palette.h"
#include <kis_paint_layer.h>
#include <kis_fill_painter.h>
#include <kis_painter.h>

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
    const QPointF imgP1(bounds.left() + bounds.width() * 0.75, bounds.center().y() + bounds.height() * 0.03);

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

    auto finalizeSmoke = [&](bool ok) {
        // Give Qt a moment to settle widget creation + repaint so headless screenshots
        // capture the intended UI state (especially on Android).
        QApplication::processEvents();
        QThread::msleep(200);
        QApplication::processEvents();

        const QString status = ok ? QStringLiteral("OK") : QStringLiteral("ERROR");
        qInfo().noquote() << QStringLiteral("KRITA_TOUCH_SMOKE_DONE scenario=%1 status=%2").arg(normalizedScenario, status);
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
        KoToolManager::instance()->switchToolRequested(QStringLiteral("KisToolSelectTouch"));
        showDockerForTouchSmoke(mainWindow, QStringLiteral("sharedtooldocker"));
        finalizeSmoke(true);
        return;
    }

    if (normalizedScenario == "transform-tool" || normalizedScenario == "transform_tool") {
        KisImageWSP image = mainWindow->viewManager() ? mainWindow->viewManager()->image() : KisImageWSP();
        KisPaintDeviceSP dev = paintDeviceForTouchSmoke(mainWindow);
        const QRect bounds = image ? image->bounds() : QRect();
        const QVector<QPoint> samplePoints = bounds.isValid()
            ? QVector<QPoint>{bounds.center(), QPoint(bounds.center().x() - 12, bounds.center().y())}
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

    if (normalizedScenario == "layers-panel" || normalizedScenario == "layers_panel") {
        showDockerForTouchSmoke(mainWindow, QStringLiteral("KisLayerBox"));
        populateLayersForTouchSmoke(mainWindow, 6);
        finalizeSmoke(true);
        return;
    }

    if (normalizedScenario == "layer-options" || normalizedScenario == "layer_options") {
        showDockerForTouchSmoke(mainWindow, QStringLiteral("KisLayerBox"));
        populateLayersForTouchSmoke(mainWindow, 6);
        if (QAction *action = mainWindow->actionCollection()->action("touch_layer_options_sheet")) {
            action->trigger();
            finalizeSmoke(true);
            return;
        }
        if (QAction *action = mainWindow->actionCollection()->action("layer_properties")) {
            action->trigger();
            finalizeSmoke(true);
            return;
        }
        qWarning() << "Touch smoke: action not found: touch_layer_options_sheet (or layer_properties fallback)";
        finalizeSmoke(false);
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
        finalizeSmoke(true);
        return;
    }

    if (normalizedScenario == "quickmenu" || normalizedScenario == "quick-menu" || normalizedScenario == "quick_menu") {
        if (!mainWindow || !mainWindow->actionCollection()) {
            qWarning() << "Touch smoke: main window or action collection not available";
            return;
        }

        QWidget *anchor = mainWindow->viewManager() ? mainWindow->viewManager()->canvas() : nullptr;
        if (!anchor) {
            anchor = mainWindow;
        }

        const QPoint globalPos = anchor->mapToGlobal(anchor->rect().center());

        KisTouchQuickMenuOverlay *overlay =
            mainWindow->findChild<KisTouchQuickMenuOverlay *>(QStringLiteral("kisTouchQuickMenuOverlay"));
        if (!overlay) {
            overlay = new KisTouchQuickMenuOverlay(mainWindow->actionCollection(), mainWindow);
        } else {
            overlay->setActionCollection(mainWindow->actionCollection());
        }

        overlay->setHighlightedSlot(-1);
        overlay->openAtGlobalPos(globalPos);
        finalizeSmoke(true);
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
        if (QAction *action = mainWindow->actionCollection()->action("touch_copypaste_overlay")) {
            action->trigger();
            finalizeSmoke(true);
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
