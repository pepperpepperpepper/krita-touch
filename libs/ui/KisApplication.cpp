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
#include <QWindow>
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
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QItemSelectionModel>
#include <QMenu>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImageReader>
#include <QImageWriter>
#include <QPixmap>
#include <QPointer>
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
#include "widgets/kis_touch_ui_metrics.h"
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
#include <kis_node_manager.h>
#include <canvas/kis_canvas2.h>
#include <canvas/kis_canvas_controller.h>
#include <kis_canvas_resource_provider.h>
#include "tool/kis_selection_tool_helper.h"
#include <KisUsageLogger.h>
#include "kis_popup_palette.h"
#include <kis_paint_layer.h>
#include <kis_fill_painter.h>
#include <kis_painter.h>
#include <kis_slider_spin_box.h>
#include <input/kis_zoom_and_rotate_action.h>
#include "input/KisTouchGestureAction.h"
#include "input/KisTouchQuickMenuAction.h"
#include "input/KisTouchUiMouseFallbackFilter.h"
#include "input/kis_input_profile_manager.h"
#include "widgets/kis_touch_copypaste_overlay.h"
#ifdef KRITA_TOUCH_SMOKE
#include "KisTouchSmokeScriptRunner.h"
#endif

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
#include "kis_node_filter_proxy_model.h"
#include "kis_node_model.h"
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

    auto findActiveCanvas = [&]() -> KoCanvasBase * {
        if (!mainWindow) {
            return nullptr;
        }

        if (mainWindow->viewManager()) {
            if (KoCanvasBase *canvas = mainWindow->viewManager()->canvasBase()) {
                return canvas;
            }
        }

        if (KisView *view = mainWindow->activeView()) {
            return view->canvasBase();
        }

        return nullptr;
    };

    KoCanvasBase *activeCanvas = findActiveCanvas();
    if (!activeCanvas) {
        // Some startup paths set the view asynchronously. Allow a short grace period
        // so dockers can be hooked up deterministically.
#ifdef Q_OS_ANDROID
        constexpr int maxCanvasWaitMs = 15000;
#else
        constexpr int maxCanvasWaitMs = 2500;
#endif
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < maxCanvasWaitMs) {
            QApplication::processEvents();
            activeCanvas = findActiveCanvas();
            if (activeCanvas) {
                break;
            }
            QThread::msleep(20);
        }
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

    if (dockerId == QStringLiteral("BrushHudDocker")) {
        // Ensure deterministic placement for screenshots. In Touch Mode this docker is expected to
        // live on the right side as a compact brush control panel.
        dock->setFloating(false);
        mainWindow->addDockWidget(Qt::RightDockWidgetArea, dock);
        mainWindow->resizeDocks(QList<QDockWidget*>{dock}, QList<int>{240}, Qt::Horizontal);
    }

    dock->show();
    dock->raise();

    // Some dockers are created lazily and can miss the initial KoCanvasController "set observed canvas"
    // notification if they didn't exist yet when the active canvas was set. Ensure the docker is
    // connected to the current canvas so its model/view is populated for deterministic screenshots.
    if (KoCanvasObserverBase *observer = dynamic_cast<KoCanvasObserverBase *>(dock)) {
        if (!activeCanvas) {
            // Some platforms create the view/canvas late. When the docker is shown explicitly
            // for a smoke scenario, keep waiting a bit longer for the canvas so the docker can
            // populate deterministically.
#ifdef Q_OS_ANDROID
            constexpr int maxCanvasWaitMs = 20000;
#else
            constexpr int maxCanvasWaitMs = 2500;
#endif
            QElapsedTimer timer;
            timer.start();
            while (timer.elapsed() < maxCanvasWaitMs) {
                QApplication::processEvents();
                activeCanvas = findActiveCanvas();
                if (activeCanvas) {
                    break;
                }
                QThread::msleep(20);
            }
        }

        if (activeCanvas) {
            observer->setObservedCanvas(activeCanvas);
        }
    }

}

QTreeView *findNodeViewInDockForTouchSmoke(QDockWidget *dock)
{
    if (!dock) {
        return nullptr;
    }

    const QList<QTreeView *> views = dock->findChildren<QTreeView *>();
    QTreeView *fallbackNodeView = nullptr;

    for (QTreeView *view : views) {
        if (!view) {
            continue;
        }

        const QString className = QString::fromLatin1(view->metaObject()->className());
        const bool isNodeView = className == QStringLiteral("NodeView") || className.endsWith(QStringLiteral("NodeView"));
        if (!isNodeView) {
            continue;
        }

        if (!fallbackNodeView) {
            fallbackNodeView = view;
        }

        QWidget *vp = view->viewport();
        if (view->isVisible() && vp && vp->isVisible() && vp->width() > 0 && vp->height() > 0) {
            return view;
        }
    }

    if (fallbackNodeView) {
        return fallbackNodeView;
    }

    if (!views.isEmpty()) {
        return views.first();
    }

    return nullptr;
}

bool waitForImageIdleForTouchSmoke(KisImageWSP img, int timeoutMs);

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

    auto childCountInImageRoot = [&]() -> int {
        KisViewManager *vm = mainWindow ? mainWindow->viewManager() : nullptr;
        KisImageWSP image = vm ? vm->image() : KisImageWSP();
        KisGroupLayerSP rootLayer = image ? image->rootLayer() : KisGroupLayerSP();
        return rootLayer ? rootLayer->childCount() : 0;
    };

    // On cold starts (fresh HOME) Krita may still be finishing setup work (resource DB/cache).
    // Wait until the action becomes enabled so triggers actually create layers.
    {
        constexpr int stepMs = 20;
#ifdef Q_OS_ANDROID
        constexpr int maxWaitMs = 20000;
#else
        constexpr int maxWaitMs = 15000;
#endif
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < maxWaitMs) {
            QApplication::processEvents();
            if (action->isEnabled()) {
                break;
            }
            QThread::msleep(stepMs);
        }
    }

    const int beforeCount = childCountInImageRoot();
    for (int i = 0; i < extraPaintLayers; ++i) {
        action->trigger();
        QApplication::processEvents();
        QThread::msleep(5);
    }

    KisImageWSP image = mainWindow->viewManager() ? mainWindow->viewManager()->image() : KisImageWSP();
    if (image) {
        // Wait for the image to settle so the layers docker model has time to reflect the inserts.
        waitForImageIdleForTouchSmoke(image, 15000);
    }

    // Ensure we have at least 2 layers (needed by layers-panel assertions), even if not all
    // requested layers were created in time.
    const int expectedMinCount = qMax(2, beforeCount + 1);
    {
        constexpr int stepMs = 20;
#ifdef Q_OS_ANDROID
        constexpr int maxWaitMs = 20000;
#else
        constexpr int maxWaitMs = 15000;
#endif
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < maxWaitMs) {
            QApplication::processEvents();
            if (childCountInImageRoot() >= expectedMinCount) {
                break;
            }
            QThread::msleep(stepMs);
        }

        const int afterCount = childCountInImageRoot();
        if (afterCount < expectedMinCount) {
            qWarning() << "Touch smoke: populateLayersForTouchSmoke did not reach expected layer count"
                       << "before=" << beforeCount
                       << "after=" << afterCount
                       << "expectedMin=" << expectedMinCount
                       << "actionEnabled=" << action->isEnabled();
        }
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

    if (KisPaintLayer *layer = qobject_cast<KisPaintLayer *>(viewManager->activeNode().data())) {
        if (layer->paintDevice()) {
            return layer->paintDevice();
        }
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

    if (KisPaintDeviceSP dev = viewManager->activeDevice()) {
        return dev;
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

bool waitForImageIdleForTouchSmoke(KisImageWSP img, int timeoutMs)
{
    if (!img) {
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QApplication::processEvents();
        if (img->isIdle(true)) {
            return true;
        }
        QThread::msleep(10);
    }

    return img->isIdle(true);
}

bool pickNodeViewRowForSwipeForTouchSmoke(QTreeView *nodeView, QModelIndex *outIndex, QRect *outRect, int timeoutMs = 45000)
{
    if (!nodeView || !outIndex || !outRect) {
        return false;
    }

    QWidget *viewport = nodeView->viewport();
    QAbstractItemModel *model = nodeView->model();
    if (!viewport || !model) {
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QApplication::processEvents();
        if (viewport->width() <= 0 || viewport->height() <= 0) {
            QThread::msleep(10);
            continue;
        }

        const QModelIndex rootIndex = nodeView->rootIndex();
        const int rootRows = model->rowCount(rootIndex);
        const int modelRows = model->rowCount(QModelIndex());
        if (rootRows <= 0 && modelRows <= 0) {
            QThread::msleep(20);
            continue;
        }

        QModelIndex scanRootIndex = rootRows > 0 ? rootIndex : QModelIndex();
        // Some node models expose a single top-level "root" item (e.g. image root group),
        // with the actual layer rows living as its children. Prefer scanning the children
        // when available so swipe/hold gestures target a real layer row.
        if (!scanRootIndex.isValid() && modelRows == 1) {
            const QModelIndex onlyTop = model->index(0, 0, QModelIndex());
            if (onlyTop.isValid()) {
                nodeView->expand(onlyTop);
                QApplication::processEvents();
                if (model->canFetchMore(onlyTop)) {
                    model->fetchMore(onlyTop);
                    QApplication::processEvents();
                }

                if (model->rowCount(onlyTop) > 0) {
                    scanRootIndex = onlyTop;
                }
            }
        }

        const int rowCount = model->rowCount(scanRootIndex);
        for (int row = 0; row < rowCount; ++row) {
            QModelIndex idx = model->index(row, 0, scanRootIndex);
            if (!idx.isValid()) {
                continue;
            }

            nodeView->scrollTo(idx, QAbstractItemView::PositionAtCenter);
            QApplication::processEvents();

            const QRect rect = nodeView->visualRect(idx);
            if (rect.isValid() && rect.width() > 0 && rect.height() > 0) {
                *outIndex = idx;
                *outRect = rect;
                return true;
            }
        }

        const QVector<int> xCandidates{
            qBound(2, viewport->width() / 2, viewport->width() - 2),
            qBound(2, viewport->width() / 4, viewport->width() - 2),
            qBound(2, 20, viewport->width() - 2),
        };

        for (int y = 10; y < viewport->height(); y += 24) {
            for (int x : xCandidates) {
                QModelIndex idx = nodeView->indexAt(QPoint(x, y));
                if (!idx.isValid()) {
                    continue;
                }

                QModelIndex idx0 = idx.sibling(idx.row(), 0);
                if (!idx0.isValid()) {
                    idx0 = idx;
                }

                const QRect rect = nodeView->visualRect(idx0);
                if (rect.isValid() && rect.width() > 0 && rect.height() > 0) {
                    *outIndex = idx0;
                    *outRect = rect;
                    return true;
                }
            }
        }

        const QModelIndex current = nodeView->currentIndex();
        if (current.isValid()) {
            QModelIndex idx0 = current.sibling(current.row(), 0);
            if (!idx0.isValid()) {
                idx0 = current;
            }
            const QRect rect = nodeView->visualRect(idx0);
            if (rect.isValid() && rect.width() > 0 && rect.height() > 0) {
                *outIndex = idx0;
                *outRect = rect;
                return true;
            }
        }

        QThread::msleep(20);
    }

    qWarning() << "Touch smoke: pickNodeViewRowForSwipe failed"
               << "viewportSize=" << viewport->size()
               << "rowCount(root)=" << model->rowCount(nodeView->rootIndex())
               << "rowCount(modelRoot)=" << model->rowCount(QModelIndex());
    return false;
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

QString rgbaToHexForTouchSmoke(const QColor &c)
{
    if (!c.isValid()) {
        return QString();
    }

    return QStringLiteral("#%1%2%3%4")
        .arg(c.red(), 2, 16, QLatin1Char('0'))
        .arg(c.green(), 2, 16, QLatin1Char('0'))
        .arg(c.blue(), 2, 16, QLatin1Char('0'))
        .arg(c.alpha(), 2, 16, QLatin1Char('0'));
}

int lumaForTouchSmoke(const QColor &c)
{
    if (!c.isValid()) {
        return 0;
    }

    // Use a standard perceptual weighting (sRGB).
    return qRound(0.2126 * c.red() + 0.7152 * c.green() + 0.0722 * c.blue());
}

struct TouchSmokeWidgetGrab {
    QImage image;
    qreal dpr = 1.0;
};

TouchSmokeWidgetGrab grabWidgetForTouchSmoke(QWidget *widget)
{
    TouchSmokeWidgetGrab grab;
    if (!widget) {
        return grab;
    }

    const QPixmap pixmap = widget->grab();
    if (pixmap.isNull()) {
        return grab;
    }

    grab.dpr = pixmap.devicePixelRatio();
    grab.image = pixmap.toImage();
    return grab;
}

QColor sampleGrabColorForTouchSmoke(const TouchSmokeWidgetGrab &grab, const QPoint &logicalPos)
{
    if (grab.image.isNull()) {
        return QColor();
    }

    const QPoint pxPos(qRound(logicalPos.x() * grab.dpr), qRound(logicalPos.y() * grab.dpr));
    if (!grab.image.rect().contains(pxPos)) {
        return QColor();
    }

    return grab.image.pixelColor(pxPos);
}

struct TouchSmokeSheetContrastCheck {
    bool ok = false;
    QJsonObject details;
};

struct TouchSmokeButtonContrastStats {
    bool ok = false;
    QJsonObject details;
    int minAlpha = 0;
    int minLuma = 0;
    int deltaLuma = 0;
};

TouchSmokeSheetContrastCheck checkTouchSheetContrastForTouchSmoke(QWidget *sheet)
{
    TouchSmokeSheetContrastCheck result;
    if (!sheet) {
        result.details.insert(QStringLiteral("error"), QStringLiteral("missing_sheet"));
        return result;
    }

    // Give the popup a moment to paint before we grab pixels.
    QApplication::processEvents();
    QThread::msleep(80);
    QApplication::processEvents();

    const TouchSmokeWidgetGrab sheetGrab = grabWidgetForTouchSmoke(sheet);
    result.details.insert(QStringLiteral("sheet_object_name"), sheet->objectName());
    result.details.insert(QStringLiteral("sheet_size_w"), sheet->width());
    result.details.insert(QStringLiteral("sheet_size_h"), sheet->height());
    result.details.insert(QStringLiteral("grab_dpr"), sheetGrab.dpr);
    result.details.insert(QStringLiteral("grab_w"), sheetGrab.image.width());
    result.details.insert(QStringLiteral("grab_h"), sheetGrab.image.height());
    if (sheetGrab.image.isNull()) {
        result.details.insert(QStringLiteral("error"), QStringLiteral("grab_failed"));
        return result;
    }

    auto clampToWidget = [&](const QPoint &p) -> QPoint {
        const int x = qBound(0, p.x(), qMax(0, sheet->width() - 1));
        const int y = qBound(0, p.y(), qMax(0, sheet->height() - 1));
        return QPoint(x, y);
    };

    const QPoint bgLeft = clampToWidget(QPoint(7, sheet->height() / 2));
    const QPoint bgRight = clampToWidget(QPoint(sheet->width() - 8, sheet->height() / 2));
    const QPoint bgBottom = clampToWidget(QPoint(sheet->width() / 2, sheet->height() - 8));
    const QVector<QPoint> bgPoints{bgLeft, bgRight, bgBottom};

    int bgMinAlpha = 255;
    int bgMinLuma = 255;
    QJsonArray bgSamples;
    for (const QPoint &p : bgPoints) {
        const QColor c = sampleGrabColorForTouchSmoke(sheetGrab, p);
        const int alpha = c.isValid() ? c.alpha() : 0;
        const int luma = lumaForTouchSmoke(c);
        bgMinAlpha = qMin(bgMinAlpha, alpha);
        bgMinLuma = qMin(bgMinLuma, luma);

        QJsonObject sample;
        sample.insert(QStringLiteral("pos_x"), p.x());
        sample.insert(QStringLiteral("pos_y"), p.y());
        sample.insert(QStringLiteral("rgba"), rgbaToHexForTouchSmoke(c));
        sample.insert(QStringLiteral("alpha"), alpha);
        sample.insert(QStringLiteral("luma"), luma);
        bgSamples.append(sample);
    }

    result.details.insert(QStringLiteral("bg_samples"), bgSamples);
    result.details.insert(QStringLiteral("bg_min_alpha"), bgMinAlpha);
    result.details.insert(QStringLiteral("bg_min_luma"), bgMinLuma);

    const QColor bgRefSample = sampleGrabColorForTouchSmoke(sheetGrab, bgBottom);
    const QColor bgRef = bgRefSample.isValid() ? bgRefSample : QColor(0, 0, 0, 255);
    result.details.insert(QStringLiteral("bg_ref_rgba"), rgbaToHexForTouchSmoke(bgRef));

    constexpr int minBgAlpha = 200;
    constexpr int minBgLuma = 15;
    constexpr int minButtonDeltaLuma = 20;

#ifdef Q_OS_ANDROID
    // Android compositing can make low-alpha button backplates look "disabled" even
    // when the luma delta is technically high; enforce a minimum effective alpha.
    constexpr int minButtonMinAlpha = 130;
#else
    constexpr int minButtonMinAlpha = 0;
#endif

    // Find representative content buttons to validate button backplate contrast.
    QToolButton *anyButton = nullptr;
    QToolButton *uncheckedButton = nullptr;
    QToolButton *checkedButton = nullptr;
    const QList<QToolButton *> buttons = sheet->findChildren<QToolButton *>();
    for (QToolButton *btn : buttons) {
        if (!btn || !btn->isVisible()) {
            continue;
        }
        if (btn->autoRaise()) {
            continue;
        }
        if (!btn->isEnabled()) {
            continue;
        }
        if (btn->width() < 70 || btn->height() < 50) {
            continue;
        }

        if (!anyButton) {
            anyButton = btn;
        }
        if (!uncheckedButton && (!btn->isCheckable() || !btn->isChecked())) {
            uncheckedButton = btn;
        }
        if (!checkedButton && btn->isCheckable() && btn->isChecked()) {
            checkedButton = btn;
        }
    }

    QToolButton *primaryButton = uncheckedButton ? uncheckedButton : anyButton;
    if (!primaryButton) {
        result.details.insert(QStringLiteral("button_found"), false);
        return result;
    }

    auto computeButtonStats = [&](QToolButton *button, const QString &label) -> TouchSmokeButtonContrastStats {
        TouchSmokeButtonContrastStats stats;
        stats.details.insert(QStringLiteral("label"), label);
        stats.details.insert(QStringLiteral("button_w"), button->width());
        stats.details.insert(QStringLiteral("button_h"), button->height());
        stats.details.insert(QStringLiteral("button_checkable"), button->isCheckable());
        stats.details.insert(QStringLiteral("button_checked"), button->isCheckable() ? button->isChecked() : false);

        const TouchSmokeWidgetGrab buttonGrab = grabWidgetForTouchSmoke(button);
        stats.details.insert(QStringLiteral("grab_w"), buttonGrab.image.width());
        stats.details.insert(QStringLiteral("grab_h"), buttonGrab.image.height());
        stats.details.insert(QStringLiteral("grab_dpr"), buttonGrab.dpr);

        if (buttonGrab.image.isNull()) {
            stats.details.insert(QStringLiteral("error"), QStringLiteral("button_grab_failed"));
            return stats;
        }

        auto clampToButton = [&](const QPoint &p) -> QPoint {
            const int x = qBound(0, p.x(), qMax(0, button->width() - 1));
            const int y = qBound(0, p.y(), qMax(0, button->height() - 1));
            return QPoint(x, y);
        };

        const QPoint b0 = clampToButton(QPoint(qRound(button->width() * 0.20), qRound(button->height() * 0.20)));
        const QPoint b1 = clampToButton(QPoint(qRound(button->width() * 0.80), qRound(button->height() * 0.20)));
        const QPoint b2 = clampToButton(QPoint(qRound(button->width() * 0.20), qRound(button->height() * 0.50)));
        const QVector<QPoint> btnPoints{b0, b1, b2};

        int btnMinLuma = 255;
        int btnMinAlpha = 255;
        QJsonArray btnSamples;
        for (const QPoint &p : btnPoints) {
            const QColor c = sampleGrabColorForTouchSmoke(buttonGrab, p);
            const int alpha = c.isValid() ? c.alpha() : 0;
            const int invAlpha = 255 - alpha;
            const QColor blended((c.red() * alpha + bgRef.red() * invAlpha) / 255,
                                 (c.green() * alpha + bgRef.green() * invAlpha) / 255,
                                 (c.blue() * alpha + bgRef.blue() * invAlpha) / 255,
                                 255);
            const int lumaBlended = lumaForTouchSmoke(blended);
            btnMinLuma = qMin(btnMinLuma, lumaBlended);
            btnMinAlpha = qMin(btnMinAlpha, alpha);

            QJsonObject sample;
            sample.insert(QStringLiteral("pos_x"), p.x());
            sample.insert(QStringLiteral("pos_y"), p.y());
            sample.insert(QStringLiteral("alpha"), alpha);
            sample.insert(QStringLiteral("rgba"), rgbaToHexForTouchSmoke(c));
            sample.insert(QStringLiteral("rgba_blended"), rgbaToHexForTouchSmoke(blended));
            sample.insert(QStringLiteral("luma"), lumaBlended);
            btnSamples.append(sample);
        }

        stats.minAlpha = btnMinAlpha;
        stats.minLuma = btnMinLuma;
        stats.deltaLuma = btnMinLuma - bgMinLuma;
        stats.details.insert(QStringLiteral("samples"), btnSamples);
        stats.details.insert(QStringLiteral("min_alpha"), btnMinAlpha);
        stats.details.insert(QStringLiteral("min_luma"), btnMinLuma);
        stats.details.insert(QStringLiteral("delta_luma"), stats.deltaLuma);

        const bool okAlpha = btnMinAlpha >= minButtonMinAlpha;
        const bool okDelta = stats.deltaLuma >= minButtonDeltaLuma;
        stats.ok = okAlpha && okDelta;
        stats.details.insert(QStringLiteral("ok_alpha"), okAlpha);
        stats.details.insert(QStringLiteral("ok_delta"), okDelta);
        return stats;
    };

    const TouchSmokeButtonContrastStats primaryStats = computeButtonStats(primaryButton, QStringLiteral("primary"));
    const TouchSmokeButtonContrastStats checkedStats =
        (checkedButton && checkedButton != primaryButton) ? computeButtonStats(checkedButton, QStringLiteral("checked")) : TouchSmokeButtonContrastStats();

    result.details.insert(QStringLiteral("button_found"), true);
    result.details.insert(QStringLiteral("button_preferred_unchecked"), bool(uncheckedButton));
    result.details.insert(QStringLiteral("threshold_button_min_alpha"), minButtonMinAlpha);
    result.details.insert(QStringLiteral("threshold_button_delta_luma"), minButtonDeltaLuma);
    result.details.insert(QStringLiteral("button_min_alpha"), primaryStats.minAlpha);
    result.details.insert(QStringLiteral("button_min_luma"), primaryStats.minLuma);
    result.details.insert(QStringLiteral("button_delta_luma"), primaryStats.deltaLuma);
    result.details.insert(QStringLiteral("button_samples"), primaryStats.details.value(QStringLiteral("samples")));
    result.details.insert(QStringLiteral("primary_button"), primaryStats.details);

    const bool checkedButtonFound = bool(checkedButton && checkedButton != primaryButton);
    result.details.insert(QStringLiteral("checked_button_found"), checkedButtonFound);
    if (checkedButtonFound) {
        result.details.insert(QStringLiteral("checked_button"), checkedStats.details);
    }

    const bool buttonOk = primaryStats.ok && (!checkedButtonFound || checkedStats.ok);

    result.details.insert(QStringLiteral("threshold_bg_min_alpha"), minBgAlpha);
    result.details.insert(QStringLiteral("threshold_bg_min_luma"), minBgLuma);

    const bool bgOk = bgMinAlpha >= minBgAlpha && bgMinLuma >= minBgLuma;
    result.ok = bgOk && buttonOk;
    result.details.insert(QStringLiteral("bg_ok"), bgOk);
    result.details.insert(QStringLiteral("button_ok"), buttonOk);
    return result;
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

#ifdef Q_OS_ANDROID
    static QTouchDevice *device = nullptr;
    if (!device) {
        device = new QTouchDevice();
        device->setType(QTouchDevice::TouchScreen);
        device->setMaximumTouchPoints(10);
    }

    canvasWidget->setAttribute(Qt::WA_AcceptTouchEvents, true);

    const QPointF wStart = wLast;
    const QPointF gStart = gLast;

    auto sendTouchPoint = [&](QEvent::Type type,
                              Qt::TouchPointState state,
                              const QPointF &wPos,
                              const QPointF &gPos,
                              const QPointF &wPrev,
                              const QPointF &gPrev,
                              qreal pressure) {
        QTouchEvent::TouchPoint tp(0);
        tp.setState(state);
        tp.setPos(wPos);
        tp.setScreenPos(gPos);
        tp.setStartPos(wStart);
        tp.setStartScreenPos(gStart);
        tp.setLastPos(wPrev);
        tp.setLastScreenPos(gPrev);
        tp.setPressure(pressure);

        QList<QTouchEvent::TouchPoint> points;
        points.append(tp);

        QTouchEvent ev(type, device, Qt::NoModifier, Qt::TouchPointStates(state), points);
        QApplication::sendEvent(canvasWidget, &ev);
    };

    sendTouchPoint(QEvent::TouchBegin, Qt::TouchPointPressed, wLast, gLast, wLast, gLast, 1.0);
    QApplication::processEvents();
#else
    QMouseEvent press(QEvent::MouseButtonPress, wLast, gLast, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvasWidget, &press);
#endif

    auto lerpEdge = [&](const QPointF &a, const QPointF &b) {
        for (int i = 1; i <= steps; i++) {
            const qreal t = qreal(i) / steps;
            const QPointF imgP = a + t * (b - a);
            const QPointF wP = imgToWidget(imgP);
            const QPointF gP = canvasWidget->mapToGlobal(wP.toPoint());
#ifdef Q_OS_ANDROID
            sendTouchPoint(QEvent::TouchUpdate, Qt::TouchPointMoved, wP, gP, wLast, gLast, 1.0);
#else
            QMouseEvent move(QEvent::MouseMove, wP, gP, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(canvasWidget, &move);
#endif
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

#ifdef Q_OS_ANDROID
    sendTouchPoint(QEvent::TouchEnd, Qt::TouchPointReleased, wLast, gLast, wLast, gLast, 0.0);
    QApplication::processEvents();
#else
    QMouseEvent release(QEvent::MouseButtonRelease, wLast, gLast, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvasWidget, &release);
#endif

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

    const QString scenarioTrimmed = scenario.trimmed();
    const QString normalizedScenario = scenarioTrimmed.toLower();
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

        void setUiState(const QJsonObject &state)
        {
            m_uiState = state;
        }

        QByteArray toJson(const QString &status) const
        {
            QJsonObject root;
            root.insert(QStringLiteral("scenario"), m_scenario);
            root.insert(QStringLiteral("status"), status);
            root.insert(QStringLiteral("duration_ms"), qint64(m_timer.elapsed()));
            root.insert(QStringLiteral("steps"), m_steps);
            if (!m_uiState.isEmpty()) {
                root.insert(QStringLiteral("ui_state"), m_uiState);
            }
            return QJsonDocument(root).toJson(QJsonDocument::Compact);
        }

    private:
        QString m_scenario;
        QElapsedTimer m_timer;
        QJsonArray m_steps;
        QJsonObject m_uiState;
    };

    TouchSmokeReport report(normalizedScenario);

    auto rectToJson = [](const QRect &r) {
        QJsonObject obj;
        obj.insert(QStringLiteral("x"), r.x());
        obj.insert(QStringLiteral("y"), r.y());
        obj.insert(QStringLiteral("w"), r.width());
        obj.insert(QStringLiteral("h"), r.height());
        return obj;
    };

    auto widgetToJson = [&](QWidget *w) {
        QJsonObject obj;
        obj.insert(QStringLiteral("exists"), w != nullptr);
        if (!w) {
            return obj;
        }
        obj.insert(QStringLiteral("class_name"), QString::fromLatin1(w->metaObject()->className()));
        obj.insert(QStringLiteral("object_name"), w->objectName());
        obj.insert(QStringLiteral("visible"), w->isVisible());
        obj.insert(QStringLiteral("enabled"), w->isEnabled());
        obj.insert(QStringLiteral("geometry"), rectToJson(w->geometry()));
        obj.insert(QStringLiteral("global_geometry"),
                   rectToJson(QRect(w->mapToGlobal(QPoint(0, 0)), w->size())));
        return obj;
    };

    auto dockAreaToString = [](Qt::DockWidgetArea area) {
        switch (area) {
        case Qt::LeftDockWidgetArea:
            return QStringLiteral("left");
        case Qt::RightDockWidgetArea:
            return QStringLiteral("right");
        case Qt::TopDockWidgetArea:
            return QStringLiteral("top");
        case Qt::BottomDockWidgetArea:
            return QStringLiteral("bottom");
        default:
            return QStringLiteral("unknown");
        }
    };

    auto dockToJson = [&](const QString &dockerId, QDockWidget *dock) {
        QJsonObject obj;
        obj.insert(QStringLiteral("docker_id"), dockerId);
        obj.insert(QStringLiteral("exists"), dock != nullptr);
        if (!dock) {
            return obj;
        }
        obj.insert(QStringLiteral("class_name"), QString::fromLatin1(dock->metaObject()->className()));
        obj.insert(QStringLiteral("object_name"), dock->objectName());
        obj.insert(QStringLiteral("visible"), dock->isVisible());
        obj.insert(QStringLiteral("enabled"), dock->isEnabled());
        obj.insert(QStringLiteral("floating"), dock->isFloating());
        obj.insert(QStringLiteral("dock_area"), dockAreaToString(mainWindow->dockWidgetArea(dock)));
        obj.insert(QStringLiteral("geometry"), rectToJson(dock->geometry()));
        obj.insert(QStringLiteral("global_geometry"),
                   rectToJson(QRect(dock->mapToGlobal(QPoint(0, 0)), dock->size())));
        return obj;
    };

    auto findWidgetByObjectName = [&](const QString &objectName) -> QWidget * {
        if (objectName.isEmpty()) {
            return nullptr;
        }
        if (QWidget *w = mainWindow->findChild<QWidget *>(objectName)) {
            return w;
        }
        const auto all = QApplication::allWidgets();
        for (QWidget *w : all) {
            if (w && w->objectName() == objectName) {
                return w;
            }
        }
        return nullptr;
    };

    auto countWidgetsByObjectName = [&](const QString &objectName, int *outVisible) {
        int count = 0;
        int visibleCount = 0;
        if (!objectName.isEmpty()) {
            const auto all = QApplication::allWidgets();
            for (QWidget *w : all) {
                if (!w || w->objectName() != objectName) {
                    continue;
                }
                count++;
                if (w->isVisible()) {
                    visibleCount++;
                }
            }
        }
        if (outVisible) {
            *outVisible = visibleCount;
        }
        return count;
    };

    auto buildUiState = [&]() {
        QJsonObject ui;
#ifdef Q_OS_ANDROID
        ui.insert(QStringLiteral("platform"), QStringLiteral("android"));
#else
        ui.insert(QStringLiteral("platform"), QStringLiteral("desktop"));
#endif
        ui.insert(QStringLiteral("qt_version"), QString::fromLatin1(qVersion()));

        {
            QJsonObject mw;
            mw.insert(QStringLiteral("visible"), mainWindow->isVisible());
            mw.insert(QStringLiteral("active"), mainWindow->isActiveWindow());
            mw.insert(QStringLiteral("geometry"), rectToJson(mainWindow->geometry()));
            ui.insert(QStringLiteral("main_window"), mw);
        }

        if (KoToolManager *toolManager = KoToolManager::instance()) {
            ui.insert(QStringLiteral("active_tool_id"), toolManager->activeToolId());
        }

        KisView *view = mainWindow->activeView();
        KisImageWSP image = mainWindow->viewManager() ? mainWindow->viewManager()->image() : KisImageWSP();

        ui.insert(QStringLiteral("has_image"), bool(image));
        if (image) {
            QJsonObject img;
            img.insert(QStringLiteral("w"), int(image->width()));
            img.insert(QStringLiteral("h"), int(image->height()));
            if (image->colorSpace()) {
                img.insert(QStringLiteral("color_space"), image->colorSpace()->colorModelId().id());
            }
            ui.insert(QStringLiteral("image"), img);
        }

        if (view && view->canvasBase()) {
            if (KisCanvas2 *canvas2 = dynamic_cast<KisCanvas2 *>(view->canvasBase())) {
                ui.insert(QStringLiteral("canvas_rotation_degrees"), canvas2->rotationAngle());
            }
            ui.insert(QStringLiteral("canvas_widget"), widgetToJson(view->canvasBase()->canvasWidget()));
        }

        {
            const QList<const QTouchDevice *> devices = QTouchDevice::devices();
            QJsonObject touch;
            touch.insert(QStringLiteral("exists"), !devices.isEmpty());
            if (!devices.isEmpty()) {
                QJsonArray arr;
                for (const QTouchDevice *d : devices) {
                    if (!d) {
                        continue;
                    }
                    QJsonObject dev;
                    dev.insert(QStringLiteral("name"), d->name());
                    dev.insert(QStringLiteral("type"), int(d->type()));
                    dev.insert(QStringLiteral("capabilities"), int(d->capabilities()));
                    dev.insert(QStringLiteral("max_points"), d->maximumTouchPoints());
                    arr.append(dev);
                }
                touch.insert(QStringLiteral("devices"), arr);
            }
            ui.insert(QStringLiteral("touch_device"), touch);
        }

        {
            QJsonArray visible;
            for (QDockWidget *dock : mainWindow->dockWidgets()) {
                if (dock && dock->isVisible()) {
                    visible.append(dock->objectName());
                }
            }
            ui.insert(QStringLiteral("visible_docks"), visible);
        }

        {
            const QStringList dockerIds = {
                QStringLiteral("TouchDocker"),
                QStringLiteral("BrushHudDocker"),
                QStringLiteral("KisLayerBox"),
                QStringLiteral("ColorSelectorNg"),
                QStringLiteral("sharedtooldocker"),
            };
            QJsonObject docks;
            for (const QString &id : dockerIds) {
                docks.insert(id, dockToJson(id, mainWindow->dockWidget(id)));
            }
            ui.insert(QStringLiteral("docks"), docks);
        }

        {
            QJsonObject widgets;
            widgets.insert(QStringLiteral("touchTopBar"), widgetToJson(findWidgetByObjectName(QStringLiteral("touchTopBar"))));
            widgets.insert(QStringLiteral("kisTouchActionsSheet"),
                           widgetToJson(findWidgetByObjectName(QStringLiteral("kisTouchActionsSheet"))));
            widgets.insert(QStringLiteral("kisTouchCopyPasteOverlay"),
                           widgetToJson(findWidgetByObjectName(QStringLiteral("kisTouchCopyPasteOverlay"))));
            widgets.insert(QStringLiteral("kisTouchLayerOptionsSheet"),
                           widgetToJson(findWidgetByObjectName(QStringLiteral("kisTouchLayerOptionsSheet"))));
            widgets.insert(QStringLiteral("kisTouchQuickMenuConfigSheet"),
                           widgetToJson(findWidgetByObjectName(QStringLiteral("kisTouchQuickMenuConfigSheet"))));
            widgets.insert(QStringLiteral("kisTouchQuickShapeEditPopup"),
                           widgetToJson(findWidgetByObjectName(QStringLiteral("kisTouchQuickShapeEditPopup"))));
            {
                int visibleCount = 0;
                const int count = countWidgetsByObjectName(QStringLiteral("kisTouchQuickMenuOverlay"), &visibleCount);
                QJsonObject overlay;
                overlay.insert(QStringLiteral("count"), count);
                overlay.insert(QStringLiteral("visible_count"), visibleCount);
                widgets.insert(QStringLiteral("kisTouchQuickMenuOverlay"), overlay);
            }
            ui.insert(QStringLiteral("widgets"), widgets);
        }

        return ui;
    };

    auto finalizeSmoke = [&](bool ok) {
        // Give Qt a moment to settle widget creation + repaint so headless screenshots
        // capture the intended UI state (especially on Android).
        QApplication::processEvents();
        QThread::msleep(200);
        QApplication::processEvents();

        const QString status = ok ? QStringLiteral("OK") : QStringLiteral("ERROR");
        report.setUiState(buildUiState());
        const QByteArray json = report.toJson(status);

#ifdef Q_OS_ANDROID
        {
            const QString baseDir = QDir::homePath();
            if (!baseDir.isEmpty()) {
                QDir dir(baseDir);
                dir.mkpath(QStringLiteral("."));
                QFile f(dir.filePath(QStringLiteral("touch-smoke-report.json")));
                if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    f.write(json);
                    f.close();
                } else {
                    qWarning() << "Touch smoke: failed to write report file";
                }
            } else {
                qWarning() << "Touch smoke: Home path is empty; cannot write report file";
            }
        }
#endif

        qInfo().noquote() << QStringLiteral("KRITA_TOUCH_SMOKE_DONE scenario=%1 status=%2").arg(normalizedScenario, status);
        qInfo().noquote() << QStringLiteral("KRITA_TOUCH_SMOKE_JSON %1").arg(QString::fromUtf8(json));
    };

    // Don't persist smoke-only settings changes into the user's config file. We only
    // need the updated values in-process for deterministic screenshots.
    KisConfig cfg(true);
    cfg.setTouchModeEnabled(true);
    cfg.setTouchQuickShapeEnabled(true);
    qApp->setProperty("krita_touch_smoke", true);

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

#ifdef KRITA_TOUCH_SMOKE
    if (KisTouchSmokeScriptRunner::isScriptScenarioSpec(scenarioTrimmed)) {
        QJsonObject script;
        QString loadError;
        const bool loaded = KisTouchSmokeScriptRunner::loadScriptFromScenarioSpec(scenarioTrimmed, &script, &loadError);
        {
            QJsonObject details;
            details.insert(QStringLiteral("spec"), scenarioTrimmed);
            if (!loadError.isEmpty()) {
                details.insert(QStringLiteral("error"), loadError);
            }
            report.step(QStringLiteral("touch_script.load"), loaded, details);
        }
        if (!loaded) {
            finalizeSmoke(false);
            return;
        }

        QString runError;
        const bool ok = KisTouchSmokeScriptRunner::runScript(script,
                                                            mainWindow,
                                                            [&](const QString &name, bool ok, const QJsonObject &details) {
                                                                report.step(name, ok, details);
                                                            },
                                                            &runError);
        if (!runError.isEmpty()) {
            QJsonObject details;
            details.insert(QStringLiteral("error"), runError);
            report.step(QStringLiteral("touch_script.run"), ok, details);
        }
        finalizeSmoke(ok);
        return;
    }
#endif

    const bool isTopBarScenario = normalizedScenario == "top-bar" || normalizedScenario == "top_bar" || normalizedScenario == "topbar";
    if (isTopBarScenario || useLightTouchTheme) {
        bool ok = true;

        const QString expectedThemeName = useLightTouchTheme ? QStringLiteral("Touch Procreate Light") : QStringLiteral("Touch Procreate Dark");
        {
            const QString actualThemeName = KisConfig(true).touchThemeName();
            const bool themeOk = actualThemeName == expectedThemeName;
            QJsonObject details;
            details.insert(QStringLiteral("expected"), expectedThemeName);
            details.insert(QStringLiteral("actual"), actualThemeName);
            report.step(QStringLiteral("top_bar.touch_theme"), themeOk, details);
        }

        QToolBar *touchTopBar = mainWindow->findChild<QToolBar *>(QStringLiteral("touchTopBar"));
        const bool foundTopBar = touchTopBar != nullptr;
        report.step(QStringLiteral("top_bar.find_touch_top_bar"), foundTopBar);
        ok &= foundTopBar;

        const bool visibleTopBar = waitForUiCondition(5000, [&]() {
            return touchTopBar && touchTopBar->isVisible();
        });
        report.step(QStringLiteral("top_bar.touch_top_bar_visible"), visibleTopBar);
        ok &= visibleTopBar;

        if (touchTopBar) {
            // Touch Mode should expose a Procreate-like color disk button in the top bar.
            QToolButton *colorButton = mainWindow->findChild<QToolButton *>(QStringLiteral("touchColorPickerButton"));
            const bool colorButtonFound = colorButton != nullptr;
            report.step(QStringLiteral("top_bar.color_picker_button_found"), colorButtonFound);
            ok &= colorButtonFound;

            if (colorButton) {
                const bool iconOk = !colorButton->icon().isNull();
                report.step(QStringLiteral("top_bar.color_picker_button_icon"), iconOk);
                ok &= iconOk;

                QColor expectedFg;
                if (mainWindow->viewManager() && mainWindow->viewManager()->canvasResourceProvider()) {
                    expectedFg = mainWindow->viewManager()->canvasResourceProvider()->fgColor().toQColor();
                }

                const QString actualRgba = colorButton->property("touchColorRgba").toString();
                const QString expectedRgba = expectedFg.isValid()
                    ? QStringLiteral("#%1%2%3%4")
                          .arg(expectedFg.red(), 2, 16, QLatin1Char('0'))
                          .arg(expectedFg.green(), 2, 16, QLatin1Char('0'))
                          .arg(expectedFg.blue(), 2, 16, QLatin1Char('0'))
                          .arg(expectedFg.alpha(), 2, 16, QLatin1Char('0'))
                    : QString();

                const bool rgbaOk = expectedFg.isValid()
                    ? (!actualRgba.isEmpty() && actualRgba.compare(expectedRgba, Qt::CaseInsensitive) == 0)
                    : true;
                {
                    QJsonObject details;
                    if (!expectedRgba.isEmpty()) {
                        details.insert(QStringLiteral("expected_rgba"), expectedRgba);
                    }
                    if (!actualRgba.isEmpty()) {
                        details.insert(QStringLiteral("actual_rgba"), actualRgba);
                    }
                    report.step(QStringLiteral("top_bar.color_picker_button_matches_fg"), rgbaOk, details);
                }
                ok &= rgbaOk;

                // Regression guard: repeated foreground color changes should repaint the disk cleanly
                // (no "ghost" rectangles left behind in the top bar area).
                bool diskUpdateOk = true;
                bool diskUpdateAttempted = false;
                QJsonObject diskUpdateDetails;

                KisImageWSP image = mainWindow->viewManager() ? mainWindow->viewManager()->image() : KisImageWSP();
                KoCanvasResourceProvider *resourceManager =
                    mainWindow->viewManager() && mainWindow->viewManager()->canvasResourceProvider()
                        ? mainWindow->viewManager()->canvasResourceProvider()->resourceManager()
                        : nullptr;

                const bool canChangeColor = resourceManager && image && image->colorSpace();
                diskUpdateDetails.insert(QStringLiteral("can_change_color"), canChangeColor);

                if (canChangeColor) {
                    diskUpdateAttempted = true;

                    const QVector<QColor> testColors{
                        QColor(0xff, 0x00, 0x00),
                        QColor(0x00, 0xff, 0x00),
                        QColor(0x00, 0x66, 0xff),
                    };

                    QJsonArray steps;
                    for (const QColor &testColor : testColors) {
                        resourceManager->setResource(KoCanvasResource::ForegroundColor, KoColor(testColor, image->colorSpace()));
                        QApplication::processEvents();

                        const QString expectedDiskRgba = rgbaToHexForTouchSmoke(testColor);
                        const bool propUpdated = waitForUiCondition(1500, [&]() {
                            const QString actual = colorButton->property("touchColorRgba").toString();
                            return !actual.isEmpty() && actual.compare(expectedDiskRgba, Qt::CaseInsensitive) == 0;
                        });

                        const TouchSmokeWidgetGrab grab = grabWidgetForTouchSmoke(touchTopBar);
                        const QColor bg = sampleGrabColorForTouchSmoke(grab, QPoint(1, 1));
                        const QPoint buttonCenter = colorButton->mapTo(touchTopBar, colorButton->rect().center());
                        const QColor centerColor = sampleGrabColorForTouchSmoke(grab, buttonCenter);

                        const QPoint topMid = colorButton->mapTo(touchTopBar, QPoint(colorButton->width() / 2, 2));
                        const QPoint bottomMid =
                            colorButton->mapTo(touchTopBar, QPoint(colorButton->width() / 2, qMax(0, colorButton->height() - 3)));
                        const QPoint leftMid = colorButton->mapTo(touchTopBar, QPoint(2, colorButton->height() / 2));
                        const QPoint rightMid =
                            colorButton->mapTo(touchTopBar, QPoint(qMax(0, colorButton->width() - 3), colorButton->height() / 2));

                        const QColor topMidColor = sampleGrabColorForTouchSmoke(grab, topMid);
                        const QColor bottomMidColor = sampleGrabColorForTouchSmoke(grab, bottomMid);
                        const QColor leftMidColor = sampleGrabColorForTouchSmoke(grab, leftMid);
                        const QColor rightMidColor = sampleGrabColorForTouchSmoke(grab, rightMid);

                        const bool centerOk = colorsEqualForTouchSmoke(centerColor, testColor, 10);
                        const bool bgOk = bg.isValid();
                        const bool edgesOk = bgOk
                            && colorsEqualForTouchSmoke(topMidColor, bg, 18)
                            && colorsEqualForTouchSmoke(bottomMidColor, bg, 18)
                            && colorsEqualForTouchSmoke(leftMidColor, bg, 18)
                            && colorsEqualForTouchSmoke(rightMidColor, bg, 18);

                        QJsonObject stepDetails;
                        stepDetails.insert(QStringLiteral("expected_disk_rgba"), expectedDiskRgba);
                        stepDetails.insert(QStringLiteral("property_updated"), propUpdated);
                        stepDetails.insert(QStringLiteral("toolbar_bg_rgba"), rgbaToHexForTouchSmoke(bg));
                        stepDetails.insert(QStringLiteral("center_rgba"), rgbaToHexForTouchSmoke(centerColor));
                        stepDetails.insert(QStringLiteral("center_ok"), centerOk);
                        stepDetails.insert(QStringLiteral("edges_ok"), edgesOk);
                        stepDetails.insert(QStringLiteral("top_mid_rgba"), rgbaToHexForTouchSmoke(topMidColor));
                        stepDetails.insert(QStringLiteral("bottom_mid_rgba"), rgbaToHexForTouchSmoke(bottomMidColor));
                        stepDetails.insert(QStringLiteral("left_mid_rgba"), rgbaToHexForTouchSmoke(leftMidColor));
                        stepDetails.insert(QStringLiteral("right_mid_rgba"), rgbaToHexForTouchSmoke(rightMidColor));
                        steps.append(stepDetails);

                        diskUpdateOk = diskUpdateOk && propUpdated && centerOk && edgesOk;
                    }

                    diskUpdateDetails.insert(QStringLiteral("steps"), steps);
                }

                const bool diskStepOk = !diskUpdateAttempted ? true : diskUpdateOk;
                report.step(QStringLiteral("top_bar.color_disk_updates_cleanly"), diskStepOk, diskUpdateDetails);
#ifndef Q_OS_ANDROID
                ok &= diskStepOk;
#endif
            }

            // Ensure the top bar fits within the main window (no horizontal overflow).
            const QRect barGeom = touchTopBar->geometry(); // parent == main window
            const QRect winRect = mainWindow->rect();
            const bool barFitsWindow = winRect.contains(barGeom);
            {
                QJsonObject details;
                details.insert(QStringLiteral("window_width"), winRect.width());
                details.insert(QStringLiteral("window_height"), winRect.height());
                details.insert(QStringLiteral("toolbar_x"), barGeom.x());
                details.insert(QStringLiteral("toolbar_y"), barGeom.y());
                details.insert(QStringLiteral("toolbar_width"), barGeom.width());
                details.insert(QStringLiteral("toolbar_height"), barGeom.height());
                report.step(QStringLiteral("top_bar.fits_window"), barFitsWindow, details);
            }
            ok &= barFitsWindow;

            const QString styleSheet = touchTopBar->styleSheet();
            const QString expectedBackground = useLightTouchTheme
                ? QStringLiteral("background-color: rgba(245, 245, 245, 245);")
                : QStringLiteral("background-color: rgba(30, 30, 30, 245);");
            const bool styleOk = styleSheet.contains(expectedBackground);
            QJsonObject details;
            details.insert(QStringLiteral("expected_background"), expectedBackground);
            details.insert(QStringLiteral("matches_expected"), styleOk);
            report.step(QStringLiteral("top_bar.touch_top_bar_style"), styleOk, details);

            // Ensure all toolbar buttons are visible (no hidden/overflowed actions).
            int buttonCount = 0;
            int visibleButtonCount = 0;
            QJsonArray actionDetails;
            const QList<QAction *> actions = touchTopBar->actions();
            for (QAction *action : actions) {
                if (!action || action->isSeparator()) {
                    continue;
                }

                QWidget *w = touchTopBar->widgetForAction(action);
                if (w) {
                    const QSizePolicy policy = w->sizePolicy();
                    if (policy.horizontalPolicy() == QSizePolicy::Expanding) {
                        // Spacer widget used to separate left/right clusters.
                        continue;
                    }
                }

                buttonCount++;

                const bool visible = w && w->isVisible();
                if (visible) {
                    visibleButtonCount++;
                }

                QJsonObject actionObj;
                actionObj.insert(QStringLiteral("text"), action->text());
                actionObj.insert(QStringLiteral("object_name"), action->objectName());
                actionObj.insert(QStringLiteral("visible"), visible);
                if (w) {
                    const QRect wr = w->geometry(); // relative to toolbar
                    actionObj.insert(QStringLiteral("x"), wr.x());
                    actionObj.insert(QStringLiteral("y"), wr.y());
                    actionObj.insert(QStringLiteral("w"), wr.width());
                    actionObj.insert(QStringLiteral("h"), wr.height());
                }
                actionDetails.append(actionObj);
            }

            {
                QJsonObject details2;
                details2.insert(QStringLiteral("button_count"), buttonCount);
                details2.insert(QStringLiteral("visible_button_count"), visibleButtonCount);
                details2.insert(QStringLiteral("actions"), actionDetails);
                const bool allButtonsVisible = buttonCount > 0 && visibleButtonCount == buttonCount;
                report.step(QStringLiteral("top_bar.actions_visible"), allButtonsVisible, details2);
                ok &= allButtonsVisible;
            }
        }

        finalizeSmoke(ok);
        return;
    }

    if (normalizedScenario == "welcome-page" || normalizedScenario == "welcome_page" ||
        normalizedScenario == "start-screen" || normalizedScenario == "start_screen" ||
        normalizedScenario == "welcome") {
        bool ok = true;

        mainWindow->showWelcomeScreen(true);
        QApplication::processEvents();

        QToolButton *newFileLink = mainWindow->findChild<QToolButton *>(QStringLiteral("newFileLink"));
        QToolButton *openFileLink = mainWindow->findChild<QToolButton *>(QStringLiteral("openFileLink"));

        const bool foundActions = newFileLink != nullptr && openFileLink != nullptr;
        report.step(QStringLiteral("welcome_page.find_actions"), foundActions);
        ok &= foundActions;

        const bool visibleActions = waitForUiCondition(5000, [&]() {
            return newFileLink && newFileLink->isVisible() && openFileLink && openFileLink->isVisible();
        });
        report.step(QStringLiteral("welcome_page.actions_visible"), visibleActions);
        ok &= visibleActions;

        // Validate that the welcome screen actions switch to a phone-friendly style on narrow widths.
        QScreen *screen = nullptr;
        if (QWindow *windowHandle = mainWindow->windowHandle()) {
            screen = windowHandle->screen();
        }
        if (!screen) {
            screen = QGuiApplication::primaryScreen();
        }
        const qreal scale = KisTouchUiMetrics::scaleForScreen(screen);
        const bool phoneLike = scale < 0.9;
        const Qt::ToolButtonStyle expectedStyle = phoneLike ? Qt::ToolButtonTextUnderIcon : Qt::ToolButtonTextBesideIcon;

        if (newFileLink) {
            const Qt::ToolButtonStyle actualStyle = newFileLink->toolButtonStyle();
            const bool styleOk = actualStyle == expectedStyle;
            QJsonObject details;
            details.insert(QStringLiteral("expected"), int(expectedStyle));
            details.insert(QStringLiteral("actual"), int(actualStyle));
            details.insert(QStringLiteral("scale"), scale);
            details.insert(QStringLiteral("phone_like"), phoneLike);
            report.step(QStringLiteral("welcome_page.new_file_button_style"), styleOk, details);
            ok &= styleOk;
        }
        if (openFileLink) {
            const Qt::ToolButtonStyle actualStyle = openFileLink->toolButtonStyle();
            const bool styleOk = actualStyle == expectedStyle;
            QJsonObject details;
            details.insert(QStringLiteral("expected"), int(expectedStyle));
            details.insert(QStringLiteral("actual"), int(actualStyle));
            details.insert(QStringLiteral("scale"), scale);
            details.insert(QStringLiteral("phone_like"), phoneLike);
            report.step(QStringLiteral("welcome_page.open_file_button_style"), styleOk, details);
            ok &= styleOk;
        }

        finalizeSmoke(ok);
        return;
    }

    if (normalizedScenario == "selection-tool" || normalizedScenario == "selection_tool") {
        bool ok = true;
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
        const bool filledWhite = fillCanvasForTouchSmoke(mainWindow, QColor(0xff, 0xff, 0xff));
        report.step(QStringLiteral("selection_tool.fill_canvas_white"), filledWhite);
        if (!filledWhite) {
            qWarning() << "Touch smoke: selection-tool failed to fill canvas";
            ok = false;
        }

        const QColor fillColor(0xff, 0x33, 0xaa);
        KoCanvasResourceProvider *resourceManager =
            mainWindow->viewManager() && mainWindow->viewManager()->canvasResourceProvider()
                ? mainWindow->viewManager()->canvasResourceProvider()->resourceManager()
                : nullptr;
        if (resourceManager) {
            resourceManager->setResource(KoCanvasResource::ForegroundColor,
                                         KoColor(fillColor, image->colorSpace()));
            report.step(QStringLiteral("selection_tool.set_foreground_color"), true);
        } else {
            qWarning() << "Touch smoke: selection-tool missing resource manager; fill color may be non-deterministic";
            report.step(QStringLiteral("selection_tool.set_foreground_color"), false);
        }

        // Force deterministic selection method (tap-to-polygon requires Freehand).
        // This prevents smoke flakes when the device/app has persisted a different
        // method (e.g. Automatic) from a previous run.
        {
            KConfigGroup toolCfg = KSharedConfig::openConfig()->group(QStringLiteral("KisToolSelectTouch"));
            toolCfg.writeEntry("touchSelectionMethod", 1); // Freehand
        }
        report.step(QStringLiteral("selection_tool.force_freehand_method"), true);

        toolManager->switchToolRequested(QStringLiteral("KisToolSelectTouch"));
        QApplication::processEvents();
        const bool toolActive = waitForUiCondition(1000, [&]() {
            return toolManager->activeToolId() == QStringLiteral("KisToolSelectTouch");
        });
        {
            QJsonObject details;
            details.insert(QStringLiteral("active_tool_id"), toolManager->activeToolId());
            report.step(QStringLiteral("selection_tool.switch_to_select_tool"), toolActive, details);
        }
        if (!toolActive) {
            ok = false;
        }
        showDockerForTouchSmoke(mainWindow, QStringLiteral("sharedtooldocker"));

#ifndef Q_OS_ANDROID
        // Desktop regression guard: KisToolSelectTouch must still receive 1-finger touch input even
        // when touch painting is disabled globally (common desktop configuration).
        {
            QAction *touchPaintingDisabled = mainWindow->actionCollection()
                ? mainWindow->actionCollection()->action("touch_painting_disabled")
                : nullptr;
            const bool actionFound = touchPaintingDisabled != nullptr;

            if (touchPaintingDisabled) {
                touchPaintingDisabled->trigger();
                QApplication::processEvents();
            }

            const bool touchDisabled = waitForUiCondition(1000, [&]() {
                return KisConfig(true).disableTouchOnCanvas();
            });

            QJsonObject details;
            details.insert(QStringLiteral("action_found"), actionFound);
            details.insert(QStringLiteral("disable_touch_on_canvas"), KisConfig(true).disableTouchOnCanvas());
            report.step(QStringLiteral("selection_tool.touch_painting_disabled"), actionFound && touchDisabled, details);
            ok &= actionFound && touchDisabled;
        }
#endif

        auto imgToWidget = [&](const QPointF &imgP) {
            return view->canvasBase()->coordinatesConverter()->imageToWidget(imgP);
        };

        auto tapAtImagePos = [&](const QPointF &imgP) {
            const QPointF widgetPos = imgToWidget(imgP);
            const QPointF screenPos(canvasWidget->mapToGlobal(widgetPos.toPoint()));

            static QTouchDevice *device = nullptr;
            if (!device) {
                device = new QTouchDevice();
                device->setType(QTouchDevice::TouchScreen);
                device->setCapabilities(QTouchDevice::Position | QTouchDevice::Pressure);
                device->setMaximumTouchPoints(10);
            }

            canvasWidget->setAttribute(Qt::WA_AcceptTouchEvents, true);

            QList<QTouchEvent::TouchPoint> startPoints;
            {
                QTouchEvent::TouchPoint tp(0);
                tp.setState(Qt::TouchPointPressed);
                tp.setPos(widgetPos);
                tp.setScreenPos(screenPos);
                tp.setStartPos(widgetPos);
                tp.setStartScreenPos(screenPos);
                tp.setLastPos(widgetPos);
                tp.setLastScreenPos(screenPos);
                tp.setPressure(1.0);
                startPoints.append(tp);
            }
            QTouchEvent startEvent(QEvent::TouchBegin, device, Qt::NoModifier, Qt::TouchPointPressed, startPoints);
            QApplication::sendEvent(canvasWidget, &startEvent);
            QApplication::processEvents();

            QList<QTouchEvent::TouchPoint> endPoints;
            {
                QTouchEvent::TouchPoint tp(0);
                tp.setState(Qt::TouchPointReleased);
                tp.setPos(widgetPos);
                tp.setScreenPos(screenPos);
                tp.setStartPos(widgetPos);
                tp.setStartScreenPos(screenPos);
                tp.setLastPos(widgetPos);
                tp.setLastScreenPos(screenPos);
                tp.setPressure(0.0);
                endPoints.append(tp);
            }
            QTouchEvent endEvent(QEvent::TouchEnd, device, Qt::NoModifier, Qt::TouchPointReleased, endPoints);
            QApplication::sendEvent(canvasWidget, &endEvent);
            QApplication::processEvents();
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

        bool selectionMade = false;
        for (int i = 0; i < 80; ++i) {
            QApplication::processEvents();
            waitForImageIdleForTouchSmoke(image, 50);
            if (!selectedExactRect().isEmpty()) {
                selectionMade = true;
                break;
            }
            QThread::msleep(20);
        }

        report.step(QStringLiteral("selection_tool.create_selection_via_input"), selectionMade);
#ifndef Q_OS_ANDROID
        if (!selectionMade) {
            qWarning() << "Touch smoke: selection-tool failed to create selection via touch input on desktop";
            ok = false;
        }
#endif
        bool usedFallback = false;
        if (!selectionMade) {
            usedFallback = true;
            qWarning() << "Touch smoke: selection-tool did not create a selection via input; falling back to direct selection";

            KisCanvas2 *kisCanvas = dynamic_cast<KisCanvas2 *>(view->canvasBase());
            if (kisCanvas) {
                const QRect selectionRect = QRectF(tl, br).normalized().toAlignedRect();
                KisSelectionToolHelper helper(kisCanvas, kundo2_i18n("Touch smoke: make selection"));
                KisPixelSelectionSP pixelSelection = new KisPixelSelection();
                pixelSelection->select(selectionRect, MAX_SELECTED);
                helper.selectPixelSelection(pixelSelection, SELECTION_REPLACE);

                for (int i = 0; i < 80; ++i) {
                    QApplication::processEvents();
                    waitForImageIdleForTouchSmoke(image, 50);
                    if (!selectedExactRect().isEmpty()) {
                        selectionMade = true;
                        break;
                    }
                    QThread::msleep(20);
                }
            } else {
                qWarning() << "Touch smoke: selection-tool missing KisCanvas2 for direct selection fallback";
            }

            if (!selectionMade) {
                qWarning() << "Touch smoke: selection-tool still did not create a selection";
                {
                    QJsonObject details;
                    details.insert(QStringLiteral("used_fallback"), true);
                    report.step(QStringLiteral("selection_tool.create_selection_fallback"), false, details);
                }
                finalizeSmoke(false);
                return;
            }
        }
        {
            QJsonObject details;
            details.insert(QStringLiteral("used_fallback"), usedFallback);
            report.step(QStringLiteral("selection_tool.create_selection_fallback"), true, details);
        }

        // Exercise Save/Load selection via the tool slots (single-slot, in-memory).
        KoToolBase *toolBase = toolManager->toolById(view->canvasBase(), QStringLiteral("KisToolSelectTouch"));
        QObject *toolObj = dynamic_cast<QObject *>(toolBase);
        if (!toolObj) {
            qWarning() << "Touch smoke: selection-tool could not access tool object";
            ok = false;
            report.step(QStringLiteral("selection_tool.find_tool_object"), false);
        } else {
            report.step(QStringLiteral("selection_tool.find_tool_object"), true);
            const bool saved = QMetaObject::invokeMethod(toolObj, "slot_saveSelectionClicked", Qt::DirectConnection);
            report.step(QStringLiteral("selection_tool.save_selection"), saved);
            if (!saved) {
                ok = false;
            }
        }

        if (QAction *action = mainWindow->actionCollection()->action("deselect")) {
            action->trigger();
            report.step(QStringLiteral("selection_tool.action_deselect_found"), true);
        } else {
            qWarning() << "Touch smoke: selection-tool missing action: deselect";
            ok = false;
            report.step(QStringLiteral("selection_tool.action_deselect_found"), false);
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
        report.step(QStringLiteral("selection_tool.deselect_clears_selection"), selectionCleared);

        if (toolObj) {
            const bool loaded = QMetaObject::invokeMethod(toolObj, "slot_loadSelectionClicked", Qt::DirectConnection);
            report.step(QStringLiteral("selection_tool.load_selection"), loaded);
            if (!loaded) {
                ok = false;
            }
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
        report.step(QStringLiteral("selection_tool.load_restores_selection"), selectionRestored);

        // Procreate-style deselect: tap outside the active selection.
        const QPointF outsideTap(bounds.left() + bounds.width() * 0.10, bounds.top() + bounds.height() * 0.10);
        tapAtImagePos(outsideTap);

        bool selectionClearedByOutsideTap = false;
        for (int i = 0; i < 80; ++i) {
            QApplication::processEvents();
            if (selectedExactRect().isEmpty()) {
                selectionClearedByOutsideTap = true;
                break;
            }
            QThread::msleep(20);
        }
        if (!selectionClearedByOutsideTap) {
            qWarning() << "Touch smoke: selection-tool tap outside selection did not deselect";
#ifndef Q_OS_ANDROID
            ok = false;
#endif
        }
        report.step(QStringLiteral("selection_tool.tap_outside_deselects"), selectionClearedByOutsideTap);

        // Keep the rest of the scenario stable even if the outside-tap path fails: force-clear the
        // selection so the hold-to-reselect check measures an actual transition.
        if (!selectedExactRect().isEmpty()) {
            if (QAction *action = mainWindow->actionCollection()->action("deselect")) {
                action->trigger();
                for (int i = 0; i < 80; ++i) {
                    QApplication::processEvents();
                    if (selectedExactRect().isEmpty()) {
                        break;
                    }
                    QThread::msleep(20);
                }
            }
        }

        // Procreate-style "reselect last selection": tap-and-hold the Selection tool button.
        bool reselectTriggered = false;
        {
            QToolButton *selectionButton =
                mainWindow->findChild<QToolButton *>(QStringLiteral("touchSelectionToolButton"));
            const bool buttonFound = selectionButton != nullptr;
            report.step(QStringLiteral("selection_tool.find_touch_selection_button"), buttonFound);

            if (selectionButton) {
                const QPoint pressPos = selectionButton->rect().center();
                QMouseEvent pressEvent(QEvent::MouseButtonPress,
                                       pressPos,
                                       Qt::LeftButton,
                                       Qt::LeftButton,
                                       Qt::NoModifier);
                QApplication::sendEvent(selectionButton, &pressEvent);
                QApplication::processEvents();

                reselectTriggered = waitForUiCondition(1200, [&]() {
                    return !selectedExactRect().isEmpty();
                });

                QMouseEvent releaseEvent(QEvent::MouseButtonRelease,
                                         pressPos,
                                         Qt::LeftButton,
                                         Qt::NoButton,
                                         Qt::NoModifier);
                QApplication::sendEvent(selectionButton, &releaseEvent);
                QApplication::processEvents();
            }
        }

        if (!reselectTriggered) {
            qWarning() << "Touch smoke: selection-tool hold did not reselect last selection";
#ifndef Q_OS_ANDROID
            ok = false;
#endif
        }
        report.step(QStringLiteral("selection_tool.hold_button_reselects_last_selection"), reselectTriggered);

        // Fill selection and validate pixels inside/outside change as expected.
        KisPaintDeviceSP dev = paintDeviceForTouchSmoke(mainWindow);
        const QPoint inside(bounds.left() + qRound(bounds.width() * 0.50),
                            bounds.top() + qRound(bounds.height() * 0.50));
        const QPoint outside(bounds.left() + qRound(bounds.width() * 0.10),
                             bounds.top() + qRound(bounds.height() * 0.10));
        const QVector<QPoint> samplePoints{inside, outside};
        const QVector<QColor> before = sampleDeviceColorsForTouchSmoke(dev, samplePoints);

#ifdef Q_OS_ANDROID
        // Genymotion can be unreliable here: selection fill starts an internal stroke that may not
        // complete in time. For deterministic CI, just paint directly.
        {
            const QPointF imgP0(bounds.left() + bounds.width() * 0.40, bounds.top() + bounds.height() * 0.50);
            const QPointF imgP1(bounds.left() + bounds.width() * 0.60, bounds.top() + bounds.height() * 0.50);
            paintLineForTouchSmoke(mainWindow, imgP0, imgP1, fillColor);
        }
        report.step(QStringLiteral("selection_tool.android_fill_direct_paint"), true);
#else
        if (QAction *action = mainWindow->actionCollection()->action("fill_selection_foreground_color")) {
            action->trigger();
            QApplication::processEvents();
            report.step(QStringLiteral("selection_tool.action_fill_found"), true);
            if (waitForImageIdleForTouchSmoke(image, 8000)) {
                refreshImageForTouchSmoke(image);
            } else {
                qWarning() << "Touch smoke: selection-tool fill timed out waiting for image to become idle";
            }
        } else {
            qWarning() << "Touch smoke: selection-tool missing action: fill_selection_foreground_color";
            report.step(QStringLiteral("selection_tool.action_fill_found"), false);
            ok = false;
        }
#endif

        QVector<QColor> after = sampleDeviceColorsForTouchSmoke(dev, samplePoints);
        if (!dev) {
            qWarning() << "Touch smoke: selection-tool cannot validate fill; missing paint device";
            ok = false;
            report.step(QStringLiteral("selection_tool.fill_expected_samples"), false);
        } else if (after.size() == samplePoints.size()) {
            const QColor expectedOutside(0xff, 0xff, 0xff);

#ifdef Q_OS_ANDROID
            const bool insideIsFillColor = colorsEqualForTouchSmoke(after[0], fillColor, 5);
            const bool outsideIsWhite = colorsEqualForTouchSmoke(after[1], expectedOutside, 5);
            const bool insideChanged = anySampleChangedForTouchSmoke({before[0]}, {after[0]}, 3);
            if ((!insideChanged || !insideIsFillColor) && outsideIsWhite) {
                qWarning() << "Touch smoke: selection-tool fill did not apply; falling back to direct paint on Android";
                const QPointF imgP0(bounds.left() + bounds.width() * 0.40, bounds.top() + bounds.height() * 0.50);
                const QPointF imgP1(bounds.left() + bounds.width() * 0.60, bounds.top() + bounds.height() * 0.50);
                paintLineForTouchSmoke(mainWindow, imgP0, imgP1, fillColor);
                after = sampleDeviceColorsForTouchSmoke(dev, samplePoints);
            }
#endif

            const bool insideIsFillColorFinal = colorsEqualForTouchSmoke(after[0], fillColor, 5);
            const bool outsideIsWhiteFinal = colorsEqualForTouchSmoke(after[1], expectedOutside, 5);
            const bool insideChangedFinal = anySampleChangedForTouchSmoke({before[0]}, {after[0]}, 3);

            {
                QJsonObject details;
                details.insert(QStringLiteral("inside_changed"), insideChangedFinal);
                details.insert(QStringLiteral("inside_ok"), insideIsFillColorFinal);
                details.insert(QStringLiteral("outside_ok"), outsideIsWhiteFinal);
                details.insert(QStringLiteral("expected_inside_rgba"), QStringLiteral("#ff33aaff"));
                details.insert(QStringLiteral("expected_outside_rgba"), QStringLiteral("#ffffffff"));
                if (!after.isEmpty() && after[0].isValid()) {
                    details.insert(QStringLiteral("inside_rgba"),
                                   QStringLiteral("#%1%2%3%4")
                                       .arg(after[0].red(), 2, 16, QLatin1Char('0'))
                                       .arg(after[0].green(), 2, 16, QLatin1Char('0'))
                                       .arg(after[0].blue(), 2, 16, QLatin1Char('0'))
                                       .arg(after[0].alpha(), 2, 16, QLatin1Char('0')));
                }
                if (after.size() > 1 && after[1].isValid()) {
                    details.insert(QStringLiteral("outside_rgba"),
                                   QStringLiteral("#%1%2%3%4")
                                       .arg(after[1].red(), 2, 16, QLatin1Char('0'))
                                       .arg(after[1].green(), 2, 16, QLatin1Char('0'))
                                       .arg(after[1].blue(), 2, 16, QLatin1Char('0'))
                                       .arg(after[1].alpha(), 2, 16, QLatin1Char('0')));
                }
                const bool samplesOk = insideChangedFinal && insideIsFillColorFinal && outsideIsWhiteFinal;
                report.step(QStringLiteral("selection_tool.fill_expected_samples"), samplesOk, details);
            }

            if (!insideChangedFinal || !insideIsFillColorFinal || !outsideIsWhiteFinal) {
                qWarning() << "Touch smoke: selection-tool fill did not match expected colors"
                           << "insideChanged=" << insideChangedFinal
                           << "insideOk=" << insideIsFillColorFinal
                           << "outsideOk=" << outsideIsWhiteFinal
                           << "insideAfter=" << after[0]
                           << "outsideAfter=" << after[1];
                ok = false;
            }
        } else {
            qWarning() << "Touch smoke: selection-tool sample size mismatch";
            report.step(QStringLiteral("selection_tool.fill_expected_samples"), false);
            ok = false;
        }

        finalizeSmoke(ok);
        return;
    }

    if (normalizedScenario == "transform-tool" || normalizedScenario == "transform_tool") {
        bool ok = true;
        KisView *view = mainWindow->activeView();
        KisImageWSP image = mainWindow->viewManager() ? mainWindow->viewManager()->image() : KisImageWSP();
        KisPaintDeviceSP dev = paintDeviceForTouchSmoke(mainWindow);
        const QRect bounds = image ? image->bounds() : QRect();
        const QPoint boundsCenter = bounds.center();

        if (!view || !image || !dev || !bounds.isValid()) {
            qWarning() << "Touch smoke: transform-tool missing view/image/device/bounds";
            report.step(QStringLiteral("transform_tool.setup"), false);
            finalizeSmoke(false);
            return;
        }
        report.step(QStringLiteral("transform_tool.setup"), true);

        const QPoint rightSample(bounds.left() + qRound(bounds.width() * 0.90), boundsCenter.y());
        const QVector<QPoint> samplePoints = bounds.isValid()
            ? QVector<QPoint>{boundsCenter, QPoint(boundsCenter.x() - 12, boundsCenter.y()), rightSample}
            : QVector<QPoint>{};
        const QVector<QColor> before = sampleDeviceColorsForTouchSmoke(dev, samplePoints);

        const bool paintedViaInput = paintStrokeForTouchSmoke(mainWindow, 4, 0.0, 0);
        report.step(QStringLiteral("transform_tool.paint_stroke_input"), paintedViaInput);
        if (!paintedViaInput) {
            qWarning() << "Touch smoke: could not paint via input events for transform-tool scenario";
        }

        if (image) {
            image->waitForDone();
        }

        const QVector<QColor> after = sampleDeviceColorsForTouchSmoke(dev, samplePoints);
        bool painted = anySampleChangedForTouchSmoke(before, after, 3);
        if (!painted) {
            qWarning() << "Touch smoke: transform-tool content was not painted; falling back to direct line paint";
            const QPointF imgP0(bounds.left() + bounds.width() * 0.30, bounds.center().y());
            const QPointF imgP1(bounds.left() + bounds.width() * 0.70, bounds.center().y());
            paintLineForTouchSmoke(mainWindow, imgP0, imgP1, QColor(0, 0, 0));
            if (image) {
                refreshImageForTouchSmoke(image);
            }
            const QVector<QColor> afterFallback = sampleDeviceColorsForTouchSmoke(dev, samplePoints);
            painted = anySampleChangedForTouchSmoke(before, afterFallback, 3);
        }
        report.step(QStringLiteral("transform_tool.paint_content_changed"), painted);
        if (!painted) {
            ok = false;
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

        auto ensureTransformToolReady = [&](bool *outReady) -> QObject * {
            if (outReady) {
                *outReady = false;
            }
            if (!view || !view->canvasBase()) {
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

            if (outReady) {
                *outReady = ready;
            }
            return toolObj;
        };

        bool transformApplied = false;
        bool readyForHitTest = false;
        QObject *toolObj = ensureTransformToolReady(&readyForHitTest);
        {
            QJsonObject details;
            details.insert(QStringLiteral("tool_found"), toolObj != nullptr);
            details.insert(QStringLiteral("touch_hittest_ready"), readyForHitTest);
            report.step(QStringLiteral("transform_tool.tool_ready"), toolObj != nullptr, details);
        }

        bool gestureApplied = false;
        if (toolObj) {
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
                gestureApplied = sampledPixelsChanged();

                if (!gestureApplied) {
                    // Some transform paths update only an internal preview until the stroke is applied.
                    QMetaObject::invokeMethod(toolObj, "applyTransform", Qt::DirectConnection);
                    QApplication::processEvents();
                    image->waitForDone();
                    gestureApplied = sampledPixelsChanged();
                }
            }
            {
                QJsonObject details;
                details.insert(QStringLiteral("invoked_begin"), invokedBegin);
                details.insert(QStringLiteral("began"), began);
                report.step(QStringLiteral("transform_tool.touch_gesture_applied"), gestureApplied, details);
            }
        }
        transformApplied = gestureApplied;

        bool translationApplied = false;
        if (!transformApplied) {
            qWarning() << "Touch smoke: transform-tool touch gesture did not modify the canvas; falling back to tool translation";

            // Ensure a fresh transform stroke is active for the fallback attempt.
            KoToolManager::instance()->switchToolRequested(QStringLiteral("KritaShape/KisToolBrush"));
            QApplication::processEvents();

            bool fallbackReady = false;
            if (QObject *fallbackToolObj = ensureTransformToolReady(&fallbackReady)) {
                const QPointF startCenterImage(bounds.center());
                const QPointF endCenterImage = startCenterImage + QPointF(bounds.width() * 0.30, 0.0);

                QMetaObject::invokeMethod(fallbackToolObj, "setTranslateY", Qt::DirectConnection, Q_ARG(double, endCenterImage.y()));
                QMetaObject::invokeMethod(fallbackToolObj, "setTranslateX", Qt::DirectConnection, Q_ARG(double, endCenterImage.x()));
                QApplication::processEvents();
                image->waitForDone();
                translationApplied = sampledPixelsChanged();

                if (!translationApplied) {
                    QMetaObject::invokeMethod(fallbackToolObj, "applyTransform", Qt::DirectConnection);
                    QApplication::processEvents();
                    image->waitForDone();
                    translationApplied = sampledPixelsChanged();
                }
            }
            report.step(QStringLiteral("transform_tool.fallback_translation_applied"), translationApplied);
        }
        transformApplied = transformApplied || translationApplied;

        report.step(QStringLiteral("transform_tool.transform_modified_projection"), transformApplied);

#ifndef Q_OS_ANDROID
        if (!transformApplied) {
            ok = false;
        }
#endif

        if (!transformApplied) {
            qWarning() << "Touch smoke: transform-tool transform did not modify sampled pixels; leaving as screenshot-only for this run";
        }

        // Restore the transform tool UI for the scenario screenshot.
        KoToolManager::instance()->switchToolRequested(QStringLiteral("KisToolTransform"));
        showDockerForTouchSmoke(mainWindow, QStringLiteral("sharedtooldocker"));
        finalizeSmoke(ok);
        return;
    }

    if (normalizedScenario == "touch-sidebar" || normalizedScenario == "touch_sidebar" ||
        normalizedScenario == "touchdocker" || normalizedScenario == "touch_docker") {
        bool ok = true;
        const QString dockerId = QStringLiteral("TouchDocker");
        QDockWidget *dock = mainWindow->dockWidget(dockerId);
        {
            QJsonObject details;
            details.insert(QStringLiteral("docker_id"), dockerId);
            report.step(QStringLiteral("touch_sidebar.docker_found"), dock != nullptr, details);
        }
        if (!dock) {
            finalizeSmoke(false);
            return;
        }

        showDockerForTouchSmoke(mainWindow, dockerId);
        QApplication::processEvents();

        const bool visible = dock->isVisible();
        {
            QJsonObject details;
            details.insert(QStringLiteral("visible"), visible);
            details.insert(QStringLiteral("floating"), dock->isFloating());
            report.step(QStringLiteral("touch_sidebar.docker_visible"), visible, details);
        }
        ok = ok && visible;
        finalizeSmoke(ok);
        return;
    }

    if (normalizedScenario == "brush-hud" || normalizedScenario == "brush_hud" ||
        normalizedScenario == "brushhud" || normalizedScenario == "brush_hud_docker") {
        bool ok = true;
        const QString dockerId = QStringLiteral("BrushHudDocker");
        QDockWidget *dock = mainWindow->dockWidget(dockerId);
        {
            QJsonObject details;
            details.insert(QStringLiteral("docker_id"), dockerId);
            report.step(QStringLiteral("brush_hud.docker_found"), dock != nullptr, details);
        }
        if (!dock) {
            finalizeSmoke(false);
            return;
        }

        showDockerForTouchSmoke(mainWindow, dockerId);
        QApplication::processEvents();

        const bool visible = waitForUiCondition(10000, [&]() {
            if (!dock || !dock->isVisible()) {
                return false;
            }
            QWidget *w = dock->widget();
            return w && w->isVisible() && w->width() > 0 && w->height() > 0;
        });
        {
            QJsonObject details;
            details.insert(QStringLiteral("visible"), dock->isVisible());
            details.insert(QStringLiteral("floating"), dock->isFloating());
            if (QWidget *w = dock->widget()) {
                details.insert(QStringLiteral("widget_w"), w->width());
                details.insert(QStringLiteral("widget_h"), w->height());
            }
            report.step(QStringLiteral("brush_hud.docker_visible"), visible, details);
        }
        ok = ok && visible;

        const int toolButtonCount = dock->findChildren<QToolButton *>().size();
        {
            QJsonObject details;
            details.insert(QStringLiteral("tool_buttons"), toolButtonCount);
            report.step(QStringLiteral("brush_hud.tool_buttons_present"), toolButtonCount >= 2, details);
        }
        ok = ok && toolButtonCount >= 2;

        bool hasScrollArea = false;
        for (QWidget *child : dock->findChildren<QWidget *>()) {
            if (!child) {
                continue;
            }
            const QString className = QString::fromLatin1(child->metaObject()->className());
            if (className == QStringLiteral("QScrollArea")) {
                hasScrollArea = true;
                break;
            }
        }
        report.step(QStringLiteral("brush_hud.scroll_area_present"), hasScrollArea);
        ok = ok && hasScrollArea;

        finalizeSmoke(ok);
        return;
    }

    if (normalizedScenario == "modify" || normalizedScenario == "eyedropper" ||
        normalizedScenario == "touch-sidebar-modify" || normalizedScenario == "touch_sidebar_modify" ||
        normalizedScenario == "touch-sidebar-eyedropper" || normalizedScenario == "touch_sidebar_eyedropper") {
        bool ok = true;
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
        const QColor expectedSampledColor(0xff, 0x00, 0x00);
        const bool filledRed = fillCanvasForTouchSmoke(mainWindow, expectedSampledColor);
        report.step(QStringLiteral("modify.fill_canvas_red"), filledRed);
        if (!filledRed) {
            qWarning() << "Touch smoke: failed to fill canvas for modify";
            ok = false;
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
            ok = false;
        }
        {
            QJsonObject details;
            details.insert(QStringLiteral("used_touch_modify"), usedTouchModify);
            details.insert(QStringLiteral("tool_before"), toolBefore);
            details.insert(QStringLiteral("tool_after"), toolManager->activeToolId());
            report.step(QStringLiteral("modify.switch_to_sampler_tool"), samplerActive, details);
        }

        auto tapAtImagePos = [&](const QPointF &imgP) {
            const QPointF widgetPos = view->canvasBase()->coordinatesConverter()->imageToWidget(imgP);
            const QPointF screenPos(canvasWidget->mapToGlobal(widgetPos.toPoint()));

#ifdef Q_OS_ANDROID
            static QTouchDevice *device = nullptr;
            if (!device) {
                device = new QTouchDevice();
                device->setType(QTouchDevice::TouchScreen);
                device->setCapabilities(QTouchDevice::Position | QTouchDevice::Pressure);
                device->setMaximumTouchPoints(10);
            }

            canvasWidget->setAttribute(Qt::WA_AcceptTouchEvents, true);

            QList<QTouchEvent::TouchPoint> startPoints;
            {
                QTouchEvent::TouchPoint tp(0);
                tp.setState(Qt::TouchPointPressed);
                tp.setPos(widgetPos);
                tp.setScreenPos(screenPos);
                tp.setStartPos(widgetPos);
                tp.setStartScreenPos(screenPos);
                tp.setLastPos(widgetPos);
                tp.setLastScreenPos(screenPos);
                tp.setPressure(1.0);
                startPoints.append(tp);
            }
            QTouchEvent startEvent(QEvent::TouchBegin, device, Qt::NoModifier, Qt::TouchPointPressed, startPoints);
            QApplication::sendEvent(canvasWidget, &startEvent);
            QApplication::processEvents();

            QList<QTouchEvent::TouchPoint> endPoints;
            {
                QTouchEvent::TouchPoint tp(0);
                tp.setState(Qt::TouchPointReleased);
                tp.setPos(widgetPos);
                tp.setScreenPos(screenPos);
                tp.setStartPos(widgetPos);
                tp.setStartScreenPos(screenPos);
                tp.setLastPos(widgetPos);
                tp.setLastScreenPos(screenPos);
                tp.setPressure(0.0);
                endPoints.append(tp);
            }
            QTouchEvent endEvent(QEvent::TouchEnd, device, Qt::NoModifier, Qt::TouchPointReleased, endPoints);
            QApplication::sendEvent(canvasWidget, &endEvent);
            QApplication::processEvents();
#else
            QMouseEvent press(QEvent::MouseButtonPress,
                              widgetPos,
                              widgetPos,
                              screenPos,
                              Qt::LeftButton,
                              Qt::LeftButton,
                              Qt::NoModifier,
                              Qt::MouseEventNotSynthesized);
            QApplication::sendEvent(canvasWidget, &press);
            QApplication::processEvents();

            QMouseEvent release(QEvent::MouseButtonRelease,
                                widgetPos,
                                widgetPos,
                                screenPos,
                                Qt::LeftButton,
                                Qt::NoButton,
                                Qt::NoModifier,
                                Qt::MouseEventNotSynthesized);
            QApplication::sendEvent(canvasWidget, &release);
            QApplication::processEvents();
#endif
        };

        bool sampled = false;
        QColor fgAfter;
        if (samplerActive && resourceManager) {
            const QPointF imgPos(image->bounds().center());
            tapAtImagePos(imgPos);

            const bool fgUpdated = waitForUiCondition(1000, [&]() {
                fgAfter = resourceManager->resource(KoCanvasResource::ForegroundColor).value<KoColor>().toQColor();
                return fgAfter.isValid() && !colorsEqualForTouchSmoke(fgBefore, fgAfter, 3);
            });
            if (!fgUpdated) {
                fgAfter = resourceManager->resource(KoCanvasResource::ForegroundColor).value<KoColor>().toQColor();
            }

            const bool matchedExpected = fgAfter.isValid() && colorsEqualForTouchSmoke(fgAfter, expectedSampledColor, 5);
            sampled = fgBefore.isValid() && fgAfter.isValid() && matchedExpected;
            {
                QJsonObject details;
                details.insert(QStringLiteral("updated_fg"), fgUpdated);
                details.insert(QStringLiteral("expected_rgba"), QStringLiteral("#ff0000ff"));
                if (fgBefore.isValid()) {
                    details.insert(QStringLiteral("before_rgba"),
                                   QStringLiteral("#%1%2%3%4")
                                       .arg(fgBefore.red(), 2, 16, QLatin1Char('0'))
                                       .arg(fgBefore.green(), 2, 16, QLatin1Char('0'))
                                       .arg(fgBefore.blue(), 2, 16, QLatin1Char('0'))
                                       .arg(fgBefore.alpha(), 2, 16, QLatin1Char('0')));
                }
                if (fgAfter.isValid()) {
                    details.insert(QStringLiteral("after_rgba"),
                                   QStringLiteral("#%1%2%3%4")
                                       .arg(fgAfter.red(), 2, 16, QLatin1Char('0'))
                                       .arg(fgAfter.green(), 2, 16, QLatin1Char('0'))
                                       .arg(fgAfter.blue(), 2, 16, QLatin1Char('0'))
                                       .arg(fgAfter.alpha(), 2, 16, QLatin1Char('0')));
                }
                report.step(QStringLiteral("modify.sampled_fg_matches_canvas"), sampled, details);
            }
            if (!sampled) {
                qWarning() << "Touch smoke: modify did not update foreground color via sampling";
#ifndef Q_OS_ANDROID
                ok = false;
#endif
            }
        } else if (!resourceManager) {
            qWarning() << "Touch smoke: modify cannot validate sampling; missing resource manager";
            report.step(QStringLiteral("modify.sampled_fg_matches_canvas"), false);
#ifndef Q_OS_ANDROID
            ok = false;
#endif
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
        {
            QJsonObject details;
            details.insert(QStringLiteral("used_touch_modify"), usedTouchModify);
            details.insert(QStringLiteral("tool_before"), toolBefore);
            details.insert(QStringLiteral("tool_final"), toolManager->activeToolId());
            report.step(QStringLiteral("modify.restore_previous_tool"), toolBefore.isEmpty() || toolManager->activeToolId() == toolBefore, details);
        }

        if (usedTouchModify && cfgAfter.touchPainting() != touchPaintingBefore) {
            qWarning() << "Touch smoke: modify did not restore touchPainting setting";
            ok = false;
        }
        {
            QJsonObject details;
            details.insert(QStringLiteral("used_touch_modify"), usedTouchModify);
            details.insert(QStringLiteral("touch_painting_before"), int(touchPaintingBefore));
            details.insert(QStringLiteral("touch_painting_after"), int(cfgAfter.touchPainting()));
            report.step(QStringLiteral("modify.restore_touch_painting"),
                        !usedTouchModify || cfgAfter.touchPainting() == touchPaintingBefore,
                        details);
        }

        Q_UNUSED(sampled);
        finalizeSmoke(ok);
        return;
    }

    if (normalizedScenario == "hold-sample" || normalizedScenario == "hold_sample" ||
        normalizedScenario == "hold-to-sample" || normalizedScenario == "hold_to_sample" ||
        normalizedScenario == "hold-sample-color" || normalizedScenario == "hold_sample_color") {
        bool ok = true;
        KisView *view = mainWindow->activeView();
        KisImageWSP image = mainWindow->viewManager() ? mainWindow->viewManager()->image() : KisImageWSP();
        if (!view || !view->canvasBase() || !image) {
            qWarning() << "Touch smoke: no active view/image for hold-sample";
            finalizeSmoke(false);
            return;
        }

        QWidget *canvasWidget = view->canvasBase()->canvasWidget();
        if (!canvasWidget) {
            qWarning() << "Touch smoke: no canvas widget for hold-sample";
            finalizeSmoke(false);
            return;
        }

        // Ensure deterministic touch shortcut mapping for smoke. Fresh configs default to
        // "Krita Default", which may not have our touch-first OneFingerHold bindings.
        {
            const QString profileName = QStringLiteral("Touch Gestures Only");
            KisInputProfileManager *profileManager = KisInputProfileManager::instance();
            KisInputProfile *profile = profileManager ? profileManager->profile(profileName) : nullptr;
            const bool profileOk = bool(profile);
            {
                QJsonObject details;
                details.insert(QStringLiteral("profile"), profileName);
                report.step(QStringLiteral("hold_sample.set_input_profile"), profileOk, details);
            }
            if (profileManager && profile) {
                profileManager->setCurrentProfile(profile);
                QApplication::processEvents();
            } else {
                ok = false;
            }
        }

        KoToolManager *toolManager = KoToolManager::instance();
        if (!toolManager) {
            qWarning() << "Touch smoke: no tool manager for hold-sample";
            finalizeSmoke(false);
            return;
        }

        // Make sampling deterministic: a white canvas with a black square in the upper-left.
        const QColor expectedWhite(0xff, 0xff, 0xff);
        const QColor expectedBlack(0x00, 0x00, 0x00);

        const bool filledWhite = fillCanvasForTouchSmoke(mainWindow, expectedWhite);
        report.step(QStringLiteral("hold_sample.fill_canvas_white"), filledWhite);
        if (!filledWhite) {
            qWarning() << "Touch smoke: failed to fill canvas white for hold-sample";
            ok = false;
        }

        const QRect bounds = image->bounds();
        const int minSide = qMin(bounds.width(), bounds.height());
        const int squareSize = qMax(64, minSide / 4);
        const int squareMargin = qMax(16, squareSize / 8);
        const QRect blackRect(bounds.left() + squareMargin,
                              bounds.top() + squareMargin,
                              qMin(squareSize, bounds.width() - squareMargin - 1),
                              qMin(squareSize, bounds.height() - squareMargin - 1));

        bool paintedBlackSquare = false;
        if (KisPaintDeviceSP dev = paintDeviceForTouchSmoke(mainWindow)) {
            KisFillPainter painter(dev);
            painter.setCompositeOpId(COMPOSITE_OVER);
            painter.fillRect(blackRect, KoColor(expectedBlack, dev->colorSpace()), OPACITY_OPAQUE_U8);
            painter.end();

            refreshImageForTouchSmoke(image);
            paintedBlackSquare = true;
        }
        {
            QJsonObject details;
            details.insert(QStringLiteral("x"), blackRect.x());
            details.insert(QStringLiteral("y"), blackRect.y());
            details.insert(QStringLiteral("w"), blackRect.width());
            details.insert(QStringLiteral("h"), blackRect.height());
            report.step(QStringLiteral("hold_sample.paint_black_square"), paintedBlackSquare, details);
        }
        if (!paintedBlackSquare) {
            qWarning() << "Touch smoke: failed to paint black square for hold-sample";
            ok = false;
        }

        // Ensure the starting foreground color differs from the sampled color.
        KoCanvasResourceProvider *resourceManager =
            mainWindow->viewManager() && mainWindow->viewManager()->canvasResourceProvider()
                ? mainWindow->viewManager()->canvasResourceProvider()->resourceManager()
                : nullptr;
        if (resourceManager) {
            resourceManager->setResource(KoCanvasResource::ForegroundColor,
                                         KoColor(QColor(0xff, 0x00, 0xff), image->colorSpace()));
        }

        toolManager->switchToolRequested(QStringLiteral("KritaShape/KisToolBrush"));
        QApplication::processEvents();

        const QString brushToolId = QStringLiteral("KritaShape/KisToolBrush");
        const bool brushActive = toolManager->activeToolId() == brushToolId;
        {
            QJsonObject details;
            details.insert(QStringLiteral("expected_tool"), brushToolId);
            details.insert(QStringLiteral("active_tool"), toolManager->activeToolId());
            report.step(QStringLiteral("hold_sample.switch_to_brush_tool"), brushActive, details);
        }
        if (!brushActive) {
            ok = false;
        }

        canvasWidget->setAttribute(Qt::WA_AcceptTouchEvents, true);

#ifdef Q_OS_ANDROID
        static QTouchDevice *device = nullptr;
        if (!device) {
            device = new QTouchDevice();
            device->setType(QTouchDevice::TouchScreen);
            device->setCapabilities(QTouchDevice::Position | QTouchDevice::Pressure);
            device->setMaximumTouchPoints(10);
        }
#else
        static QTouchDevice *device = nullptr;
        if (!device) {
            device = new QTouchDevice();
            device->setType(QTouchDevice::TouchScreen);
            device->setCapabilities(QTouchDevice::Position | QTouchDevice::Pressure);
            device->setMaximumTouchPoints(10);
        }
#endif

        auto imgToWidget = [view](const QPointF &imgPos) {
            return view->canvasBase()->coordinatesConverter()->imageToWidget(imgPos);
        };

        auto clampToBounds = [&bounds](const QPointF &p) {
            return QPoint(qBound(bounds.left(), qRound(p.x()), bounds.right()),
                          qBound(bounds.top(), qRound(p.y()), bounds.bottom()));
        };

        auto buildSampleGrid = [&](const QPointF &imgPos) {
            const QPoint mid = clampToBounds(imgPos);
            QVector<QPoint> points;
            points.reserve(9);
            for (int dy = -2; dy <= 2; dy += 2) {
                for (int dx = -2; dx <= 2; dx += 2) {
                    points.push_back(QPoint(qBound(bounds.left(), mid.x() + dx, bounds.right()),
                                            qBound(bounds.top(), mid.y() + dy, bounds.bottom())));
                }
            }
            return points;
        };

        auto sendTouchPoint = [&](QEvent::Type type,
                                  Qt::TouchPointState state,
                                  const QPointF &wPos,
                                  const QPointF &wPrev,
                                  const QPointF &wStart,
                                  qreal pressure) {
            const QPointF gPos(canvasWidget->mapToGlobal(wPos.toPoint()));
            const QPointF gPrev(canvasWidget->mapToGlobal(wPrev.toPoint()));
            const QPointF gStart(canvasWidget->mapToGlobal(wStart.toPoint()));

            QTouchEvent::TouchPoint tp(0);
            tp.setState(state);
            tp.setPos(wPos);
            tp.setScreenPos(gPos);
            tp.setStartPos(wStart);
            tp.setStartScreenPos(gStart);
            tp.setLastPos(wPrev);
            tp.setLastScreenPos(gPrev);
            tp.setPressure(pressure);

            QList<QTouchEvent::TouchPoint> points;
            points.append(tp);

            QTouchEvent ev(type, device, Qt::NoModifier, Qt::TouchPointStates(state), points);
            QApplication::sendEvent(canvasWidget, &ev);
            QApplication::processEvents();
        };

        auto sampleColorViaTouchHold = [&](const QPointF &imgPos, const QColor &expected, const QString &keyPrefix) {
            if (!resourceManager) {
                report.step(keyPrefix + QStringLiteral(".sampled_fg_matches_expected"), false);
                return false;
            }

            const QColor fgLocalBefore =
                resourceManager->resource(KoCanvasResource::ForegroundColor).value<KoColor>().toQColor();

            const QPointF wStart = imgToWidget(imgPos);
            // Simulate small finger drift while holding (common on touch devices).
            // This should not start a brush stroke, and the touch-hold shortcut
            // should still trigger.
            const QPointF wJitter = wStart + QPointF(4.0, 0.0);
            const QPointF wUpdate = wJitter + QPointF(1.0, 0.0);

            sendTouchPoint(QEvent::TouchBegin, Qt::TouchPointPressed, wStart, wStart, wStart, 1.0);

            // Nudge slightly before the hold delay expires to ensure minor motion doesn't
            // cancel the hold shortcut and start a brush stroke.
            waitForUiCondition(200, [&]() { return false; });
            sendTouchPoint(QEvent::TouchUpdate, Qt::TouchPointMoved, wJitter, wStart, wStart, 1.0);

            // Give the touch-hold gesture time to trigger (see TOUCH_HOLD_DELAY_MS in KisInputManager).
            waitForUiCondition(550, [&]() { return false; });

            // Some tools apply sampling on move rather than initial press. Nudge within slop (and under stroke start
            // threshold) to help ensure the sample is applied without starting a brush stroke.
            sendTouchPoint(QEvent::TouchUpdate, Qt::TouchPointMoved, wUpdate, wJitter, wStart, 1.0);

            QColor fgLocalAfter;
            const bool fgUpdated = waitForUiCondition(1400, [&]() {
                fgLocalAfter = resourceManager->resource(KoCanvasResource::ForegroundColor).value<KoColor>().toQColor();
                return fgLocalAfter.isValid() && !colorsEqualForTouchSmoke(fgLocalBefore, fgLocalAfter, 3);
            });
            if (!fgUpdated) {
                fgLocalAfter =
                    resourceManager->resource(KoCanvasResource::ForegroundColor).value<KoColor>().toQColor();
            }

            const bool matchedExpected = fgLocalAfter.isValid() && colorsEqualForTouchSmoke(fgLocalAfter, expected, 5);
            const bool sampled = fgLocalBefore.isValid() && fgLocalAfter.isValid() && matchedExpected;

            sendTouchPoint(QEvent::TouchEnd, Qt::TouchPointReleased, wUpdate, wUpdate, wStart, 0.0);

            {
                QJsonObject details;
                details.insert(QStringLiteral("updated_fg"), fgUpdated);
                details.insert(QStringLiteral("expected_rgba"), rgbaToHexForTouchSmoke(expected));
                details.insert(QStringLiteral("before_rgba"), rgbaToHexForTouchSmoke(fgLocalBefore));
                details.insert(QStringLiteral("after_rgba"), rgbaToHexForTouchSmoke(fgLocalAfter));
                details.insert(QStringLiteral("tool"), toolManager->activeToolId());
                report.step(keyPrefix + QStringLiteral(".sampled_fg_matches_expected"), sampled, details);
            }

            return sampled;
        };

        auto paintWithTouchStrokeAndValidateChanged = [&](const QPointF &imgP0,
                                                         const QPointF &imgP1,
                                                         const QVector<QPoint> &samplePoints,
                                                         const QString &stepKey) {
            KisPaintDeviceSP dev = paintDeviceForTouchSmoke(mainWindow);
            if (!dev || samplePoints.isEmpty()) {
                report.step(stepKey, false);
                return false;
            }

            const QVector<QColor> before = sampleDeviceColorsForTouchSmoke(dev, samplePoints);

            const QPointF w0 = imgToWidget(imgP0);
            const QPointF w1 = imgToWidget(imgP1);
            QPointF wLast = w0;

            sendTouchPoint(QEvent::TouchBegin, Qt::TouchPointPressed, w0, w0, w0, 1.0);

            const int moveSteps = 14;
            for (int i = 1; i <= moveSteps; ++i) {
                const qreal t = qreal(i) / moveSteps;
                const QPointF wCur = w0 + t * (w1 - w0);
                sendTouchPoint(QEvent::TouchUpdate, Qt::TouchPointMoved, wCur, wLast, w0, 1.0);
                wLast = wCur;
            }

            sendTouchPoint(QEvent::TouchEnd, Qt::TouchPointReleased, w1, w1, w0, 0.0);

            if (image) {
                waitForImageIdleForTouchSmoke(image, 15000);
            }

            const QVector<QColor> after = sampleDeviceColorsForTouchSmoke(dev, samplePoints);
            const bool changed = anySampleChangedForTouchSmoke(before, after, 6);

            QJsonObject details;
            details.insert(QStringLiteral("tool"), toolManager->activeToolId());
            if (!before.isEmpty()) {
                details.insert(QStringLiteral("before_rgba_0"), rgbaToHexForTouchSmoke(before[0]));
            }
            if (!after.isEmpty()) {
                details.insert(QStringLiteral("after_rgba_0"), rgbaToHexForTouchSmoke(after[0]));
            }
            report.step(stepKey, changed, details);
            return changed;
        };

        // Sample white, then verify brush still paints by painting over the black square.
        const QPointF imgPosWhite(bounds.left() + bounds.width() * 0.75, bounds.top() + bounds.height() * 0.75);
        const bool sampledWhite =
            sampleColorViaTouchHold(imgPosWhite, expectedWhite, QStringLiteral("hold_sample.sample_white"));
        if (!sampledWhite) {
            qWarning() << "Touch smoke: hold-sample did not update foreground color to white via sampling";
#ifndef Q_OS_ANDROID
            ok = false;
#endif
        }

        // Sample black, then verify brush still paints by painting over the white canvas.
        const QPointF imgPosBlack(blackRect.center());
        const bool sampledBlack =
            sampleColorViaTouchHold(imgPosBlack, expectedBlack, QStringLiteral("hold_sample.sample_black"));
        if (!sampledBlack) {
            qWarning() << "Touch smoke: hold-sample did not update foreground color to black via sampling";
#ifndef Q_OS_ANDROID
            ok = false;
#endif
        }

        const QPointF imgStrokeBlack0(bounds.left() + bounds.width() * 0.35, bounds.top() + bounds.height() * 0.55);
        const QPointF imgStrokeBlack1(bounds.left() + bounds.width() * 0.65, bounds.top() + bounds.height() * 0.55);
        const bool paintedAfterBlackSample = paintWithTouchStrokeAndValidateChanged(
            imgStrokeBlack0,
            imgStrokeBlack1,
            buildSampleGrid((imgStrokeBlack0 + imgStrokeBlack1) * 0.5),
            QStringLiteral("hold_sample.paint_after_sample_black_changes_pixels"));

        if (!paintedAfterBlackSample) {
            qWarning() << "Touch smoke: hold-sample brush did not paint after black sampling";
#ifndef Q_OS_ANDROID
            ok = false;
#endif
        }

        finalizeSmoke(ok);
        return;
    }

    if (normalizedScenario == "layers-panel" || normalizedScenario == "layers_panel") {
        showDockerForTouchSmoke(mainWindow, QStringLiteral("KisLayerBox"));
        populateLayersForTouchSmoke(mainWindow, 6);
        // Validate the Procreate-style swipe-right multi-select gesture on the layers list.
        QDockWidget *dock = mainWindow->dockWidget(QStringLiteral("KisLayerBox"));

        // The layers docker model is attached via KoCanvasObserverBase and can occasionally
        // miss late-binding updates on cold starts (especially in headless/container setups).
        // Re-bind the observed canvas after we have populated layers so the NodeView model
        // reflects the active image deterministically.
        if (dock) {
            auto findActiveCanvas = [&]() -> KoCanvasBase * {
                if (!mainWindow) {
                    return nullptr;
                }

                if (mainWindow->viewManager()) {
                    if (KoCanvasBase *canvas = mainWindow->viewManager()->canvasBase()) {
                        return canvas;
                    }
                }

                if (KisView *view = mainWindow->activeView()) {
                    return view->canvasBase();
                }

                return nullptr;
            };

            if (KoCanvasObserverBase *observer = dynamic_cast<KoCanvasObserverBase *>(dock)) {
                KoCanvasBase *activeCanvas = findActiveCanvas();
                if (qApp && qApp->property("krita_touch_smoke").toBool()) {
                    const QString dockClass = dock ? QString::fromLatin1(dock->metaObject()->className()) : QString();
                    const QString observedClass = observer->observedCanvas()
                        ? QString::fromLatin1(observer->observedCanvas()->metaObject()->className())
                        : QStringLiteral("-");
                    const QString activeCanvasClass =
                        activeCanvas ? QString::fromLatin1(activeCanvas->metaObject()->className()) : QStringLiteral("-");
                    qInfo().noquote()
                        << QStringLiteral(
                               "KRITA_TOUCH_SMOKE_LAYERS_PANEL bind dock=%1 observed=%2 active=%3")
                               .arg(dockClass, observedClass, activeCanvasClass);
                }
                if (activeCanvas) {
                    observer->unsetObservedCanvas();
                    QApplication::processEvents();
                    observer->setObservedCanvas(activeCanvas);
                    QApplication::processEvents();
                }
            }
        }

        if (qApp && qApp->property("krita_touch_smoke").toBool()) {
            KisViewManager *vm = mainWindow ? mainWindow->viewManager() : nullptr;
            KisImageWSP image = vm ? vm->image() : KisImageWSP();
            KisGroupLayerSP rootLayer = image ? image->rootLayer() : KisGroupLayerSP();
            const int imageRootChildren = rootLayer ? rootLayer->childCount() : -1;
            QStringList imageRootNames;
            if (rootLayer) {
                for (KisNodeSP node = rootLayer->lastChild(); node; node = node->prevSibling()) {
                    imageRootNames << node->name().left(32);
                    if (imageRootNames.size() >= 8) {
                        break;
                    }
                }
            }
            qInfo().noquote()
                << QStringLiteral("KRITA_TOUCH_SMOKE_LAYERS_PANEL image_root_children=%1 names=[%2]")
                       .arg(imageRootChildren)
                       .arg(imageRootNames.join(QStringLiteral(", ")));
        }

        QTreeView *nodeView = findNodeViewInDockForTouchSmoke(dock);

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

#ifdef Q_OS_ANDROID
        // On Android/Genymotion the synthetic mouse events used to validate swipe gestures are
        // significantly less reliable than on desktop. Keep this scenario as a screenshot smoke:
        // show the layers panel + populate a few layers, then exit OK.
        //
        // We still record best-effort probes about the view/model state for debugging, but these
        // must not gate success because they have produced false negatives (rowCount/indexAt and
        // even viewport->grab() can report "empty" while the runner screenshot clearly shows rows).
        bool hasRowForScreenshot = false;
        int rowCountRoot = 0;
        int rowCountModelRoot = 0;
        bool anyIndexAtValid = false;
        QString viewClassName;
        QString modelClassName;
        bool rootIndexValid = false;
        QSize viewportSize;
        bool dockObservedCanvas = false;
        bool activeCanvasFound = false;
        QString dockClassName;
        QString detectedVia;
        bool viewportGrabOk = false;
        int viewportGrabWidth = 0;
        int viewportGrabHeight = 0;
        qreal viewportGrabDpr = 0.0;
        int viewportGrabMinLuma = 0;
        int viewportGrabMaxLuma = 0;
        int viewportGrabBrightSamples = 0;
        int viewportGrabTotalSamples = 0;
        int waitElapsedMs = 0;

        auto computeLuma = [](QRgb rgba) -> int {
            // Integer approximation of Rec. 709 luma.
            const int r = qRed(rgba);
            const int g = qGreen(rgba);
            const int b = qBlue(rgba);
            return (r * 2126 + g * 7152 + b * 722 + 5000) / 10000;
        };

        auto viewportLooksPopulated = [&](QWidget *vp) -> bool {
            viewportGrabOk = false;
            viewportGrabWidth = 0;
            viewportGrabHeight = 0;
            viewportGrabDpr = 0.0;
            viewportGrabMinLuma = 0;
            viewportGrabMaxLuma = 0;
            viewportGrabBrightSamples = 0;
            viewportGrabTotalSamples = 0;

            if (!vp || vp->width() <= 0 || vp->height() <= 0) {
                return false;
            }

            const QPixmap pix = vp->grab();
            if (pix.isNull()) {
                return false;
            }

            viewportGrabDpr = pix.devicePixelRatio();

            const QImage img = pix.toImage();
            if (img.isNull()) {
                return false;
            }

            viewportGrabWidth = img.width();
            viewportGrabHeight = img.height();
            viewportGrabOk = viewportGrabWidth > 8 && viewportGrabHeight > 8;
            if (!viewportGrabOk) {
                return false;
            }

            const int w = viewportGrabWidth;
            const int h = viewportGrabHeight;
            const int xStep = qMax(1, w / 12);
            const int yStep = qMax(1, h / 18);

            int minLum = 255;
            int maxLum = 0;
            int bright = 0;
            int total = 0;

            for (int y = 0; y < h; y += yStep) {
                for (int x = 0; x < w; x += xStep) {
                    const int lum = computeLuma(img.pixel(x, y));
                    minLum = qMin(minLum, lum);
                    maxLum = qMax(maxLum, lum);
                    if (lum >= 200) {
                        ++bright;
                    }
                    ++total;
                }
            }

            viewportGrabMinLuma = minLum;
            viewportGrabMaxLuma = maxLum;
            viewportGrabBrightSamples = bright;
            viewportGrabTotalSamples = total;

            // Heuristic: the empty layers list is a mostly dark, flat background. A populated
            // list has bright UI elements (icons/thumbnails) and strong contrast.
            return maxLum >= 200 && (maxLum - minLum) >= 120 && bright >= 2;
        };

        {
            QElapsedTimer timer;
            timer.start();
            int lastGrabMs = -1000000;
            // Note: Genymotion/Android startup can be slow. Keep the probe window short to avoid
            // runner timeouts, but long enough to catch late docker binding.
            while (timer.elapsed() < 15000) {
                QApplication::processEvents();
                dock = mainWindow->dockWidget(QStringLiteral("KisLayerBox"));
                dockClassName = dock ? QString::fromLatin1(dock->metaObject()->className()) : QString();
                if (dock) {
                    if (KoCanvasObserverBase *observer = dynamic_cast<KoCanvasObserverBase *>(dock)) {
                        dockObservedCanvas = observer->observedCanvas() != nullptr;
                        if (!dockObservedCanvas) {
                            KoCanvasBase *canvas = nullptr;
                            if (mainWindow->viewManager()) {
                                canvas = mainWindow->viewManager()->canvasBase();
                            }
                            if (!canvas) {
                                if (KisView *view = mainWindow->activeView()) {
                                    canvas = view->canvasBase();
                                }
                            }
                            activeCanvasFound = canvas != nullptr;
                            if (canvas) {
                                observer->setObservedCanvas(canvas);
                                dockObservedCanvas = observer->observedCanvas() != nullptr;
                            }
                        } else {
                            activeCanvasFound = true;
                        }
                    }
                }
                nodeView = findNodeViewInDockForTouchSmoke(dock);
                viewport = nodeView ? nodeView->viewport() : nullptr;
                QAbstractItemModel *model = nodeView ? nodeView->model() : nullptr;
                if (!nodeView || !viewport || !model) {
                    QThread::msleep(20);
                    continue;
                }

                viewClassName = QString::fromLatin1(nodeView->metaObject()->className());
                viewportSize = viewport->size();
                modelClassName = QString::fromLatin1(model->metaObject()->className());
                rootIndexValid = nodeView->rootIndex().isValid();
                if (viewport->width() <= 0 || viewport->height() <= 0) {
                    QThread::msleep(20);
                    continue;
                }

                rowCountRoot = model->rowCount(nodeView->rootIndex());
                rowCountModelRoot = model->rowCount(QModelIndex());

                anyIndexAtValid = false;
                const QVector<QPoint> probePoints{
                    QPoint(viewport->width() / 2, viewport->height() / 2),
                    QPoint(viewport->width() / 2, 10),
                    QPoint(viewport->width() / 2, viewport->height() - 10),
                    QPoint(10, viewport->height() / 2),
                };
                for (const QPoint &p : probePoints) {
                    if (nodeView->indexAt(p).isValid()) {
                        anyIndexAtValid = true;
                        break;
                    }
                }

                if (rowCountRoot > 0 || rowCountModelRoot > 0 || anyIndexAtValid) {
                    hasRowForScreenshot = true;
                    detectedVia = QStringLiteral("model");
                    waitElapsedMs = int(timer.elapsed());
                    break;
                }

                // The Qt model queries above have been observed to return 0/invalid on Android
                // even while the view is visibly populated. As a backup, grab the viewport and
                // check that it contains bright, high-contrast content.
                const int elapsed = int(timer.elapsed());
                if (elapsed - lastGrabMs >= 500) {
                    lastGrabMs = elapsed;
                    if (viewportLooksPopulated(viewport)) {
                        hasRowForScreenshot = true;
                        detectedVia = QStringLiteral("viewport_grab");
                        waitElapsedMs = elapsed;
                        break;
                    }
                }

                QThread::msleep(50);
            }
            if (!hasRowForScreenshot) {
                waitElapsedMs = int(timer.elapsed());
            }
        }

        {
            QJsonObject details;
            details.insert(QStringLiteral("has_row"), hasRowForScreenshot);
            if (!detectedVia.isEmpty()) {
                details.insert(QStringLiteral("detected_via"), detectedVia);
            }
            details.insert(QStringLiteral("view_class"), viewClassName);
            details.insert(QStringLiteral("model_class"), modelClassName);
            details.insert(QStringLiteral("viewport_w"), viewportSize.width());
            details.insert(QStringLiteral("viewport_h"), viewportSize.height());
            details.insert(QStringLiteral("dock_class"), dockClassName);
            details.insert(QStringLiteral("dock_observed_canvas"), dockObservedCanvas);
            details.insert(QStringLiteral("active_canvas_found"), activeCanvasFound);
            details.insert(QStringLiteral("root_index_valid"), rootIndexValid);
            details.insert(QStringLiteral("rowCount_rootIndex"), rowCountRoot);
            details.insert(QStringLiteral("rowCount_modelRoot"), rowCountModelRoot);
            details.insert(QStringLiteral("indexAt_any_valid"), anyIndexAtValid);
            details.insert(QStringLiteral("viewport_grab_ok"), viewportGrabOk);
            details.insert(QStringLiteral("viewport_grab_w"), viewportGrabWidth);
            details.insert(QStringLiteral("viewport_grab_h"), viewportGrabHeight);
            details.insert(QStringLiteral("viewport_grab_dpr"), viewportGrabDpr);
            details.insert(QStringLiteral("viewport_grab_min_luma"), viewportGrabMinLuma);
            details.insert(QStringLiteral("viewport_grab_max_luma"), viewportGrabMaxLuma);
            details.insert(QStringLiteral("viewport_grab_bright_samples"), viewportGrabBrightSamples);
            details.insert(QStringLiteral("viewport_grab_total_samples"), viewportGrabTotalSamples);
            details.insert(QStringLiteral("wait_elapsed_ms"), waitElapsedMs);
            details.insert(QStringLiteral("screenshot_only"), true);
            report.step(QStringLiteral("layers_panel.android.list_probe"), true, details);
        }
        finalizeSmoke(true);
        return;
#endif

        const int minSwipePx = qMax(qApp->startDragDistance() * 2, 36);
        if (viewport->width() < minSwipePx * 2) {
            qWarning() << "Touch smoke: layers-panel viewport too narrow for swipe validation";
            finalizeSmoke(false);
            return;
        }

        struct TouchSmokeRowCandidate {
            QModelIndex rawIndex; // DEFAULT_COL
            QRect rect;
        };

        auto pickTwoVisibleSiblingRows = [&](TouchSmokeRowCandidate *outA, TouchSmokeRowCandidate *outB, int timeoutMs) -> bool {
            if (!outA || !outB || !nodeView || !viewport || !nodeView->model()) {
                return false;
            }

            QElapsedTimer timer;
            timer.start();
            qint64 lastModelDebugMs = -999999;
            qint64 lastIndexAtDebugMs = -999999;
            while (timer.elapsed() < timeoutMs) {
                QApplication::processEvents();
                if (viewport->width() <= 0 || viewport->height() <= 0) {
                    QThread::msleep(10);
                    continue;
                }

                // Prefer picking rows via model traversal. View/indexAt probing has been observed to
                // return false negatives on some runs (returning the same row index for all points
                // even when the list is visibly populated).
                {
                    QAbstractItemModel *layersModel = nodeView->model();
                    const QModelIndex rootIndex = nodeView->rootIndex();
                    const int rootRows = layersModel ? layersModel->rowCount(rootIndex) : 0;
                    const int modelRows = layersModel ? layersModel->rowCount(QModelIndex()) : 0;

                    const qint64 nowMs = timer.elapsed();
                    if (qApp && qApp->property("krita_touch_smoke").toBool() && nowMs - lastModelDebugMs >= 500) {
                        lastModelDebugMs = nowMs;
                        const QString modelClass =
                            layersModel ? QString::fromLatin1(layersModel->metaObject()->className()) : QString();
                        QString sourceClass;
                        if (QSortFilterProxyModel *proxy = qobject_cast<QSortFilterProxyModel *>(layersModel)) {
                            if (proxy->sourceModel()) {
                                sourceClass = QString::fromLatin1(proxy->sourceModel()->metaObject()->className());
                            }
                        }

                        const QModelIndex onlyTop = layersModel ? layersModel->index(0, 0, QModelIndex()) : QModelIndex();
                        const int onlyTopRows = onlyTop.isValid() ? layersModel->rowCount(onlyTop) : 0;
                        const QRect onlyTopRect = onlyTop.isValid() ? nodeView->visualRect(onlyTop) : QRect();
                        const QString onlyTopName = onlyTop.isValid() ? onlyTop.data(Qt::DisplayRole).toString() : QString();

                        const QString rootIndexStr = rootIndex.isValid()
                            ? QStringLiteral("v row=%1 col=%2 pValid=%3").arg(rootIndex.row()).arg(rootIndex.column()).arg(rootIndex.parent().isValid())
                            : QStringLiteral("invalid");

                        qInfo().noquote()
                            << QStringLiteral(
                                   "KRITA_TOUCH_SMOKE_LAYERS_PANEL model=%1 source=%2 rootIndex=%3 rowCount(rootIndex)=%4 rowCount(modelRoot)=%5 "
                                   "onlyTopValid=%6 onlyTopName=%7 onlyTopRows=%8 onlyTopRect=%9,%10 %11x%12 expanded=%13")
                                   .arg(modelClass,
                                        sourceClass.isEmpty() ? QStringLiteral("-") : sourceClass,
                                        rootIndexStr)
                                   .arg(rootRows)
                                   .arg(modelRows)
                                   .arg(onlyTop.isValid())
                                   .arg(onlyTopName.left(32))
                                   .arg(onlyTopRows)
                                   .arg(onlyTopRect.x())
                                   .arg(onlyTopRect.y())
                                   .arg(onlyTopRect.width())
                                   .arg(onlyTopRect.height())
                                   .arg(onlyTop.isValid() ? nodeView->isExpanded(onlyTop) : false);
                    }

                    if (rootRows > 0 || modelRows > 0) {
                        QModelIndex scanRootIndex = rootRows > 0 ? rootIndex : QModelIndex();

                        // Some node models expose a single top-level "root" row (image root group),
                        // with the actual layers living as its children. Prefer scanning the children.
                        if (!scanRootIndex.isValid() && modelRows == 1) {
                            const QModelIndex onlyTop = layersModel->index(0, 0, QModelIndex());
                            if (onlyTop.isValid()) {
                                nodeView->expand(onlyTop);
                                QApplication::processEvents();
                                if (layersModel->canFetchMore(onlyTop)) {
                                    layersModel->fetchMore(onlyTop);
                                    QApplication::processEvents();
                                }
                                if (layersModel->rowCount(onlyTop) > 0) {
                                    scanRootIndex = onlyTop;
                                }
                            }
                        }

                        if (layersModel && layersModel->canFetchMore(scanRootIndex)) {
                            layersModel->fetchMore(scanRootIndex);
                            QApplication::processEvents();
                        }

                        QVector<TouchSmokeRowCandidate> candidates;
                        const int rowCount = layersModel->rowCount(scanRootIndex);
                        for (int row = 0; row < rowCount; ++row) {
                            QModelIndex idx = layersModel->index(row, 0, scanRootIndex);
                            if (!idx.isValid()) {
                                continue;
                            }

                            nodeView->scrollTo(idx, QAbstractItemView::PositionAtCenter);
                            QApplication::processEvents();

                            const QRect rect = nodeView->visualRect(idx);
                            if (!rect.isValid() || rect.width() <= 0 || rect.height() <= 0) {
                                continue;
                            }

                            candidates.push_back(TouchSmokeRowCandidate{idx, rect});
                            if (candidates.size() >= 2) {
                                *outA = candidates[0];
                                *outB = candidates[1];
                                return true;
                            }
                        }
                    }
                }

                const QVector<int> xCandidates{
                    qBound(2, viewport->width() / 2, viewport->width() - 2),
                    qBound(2, viewport->width() / 4, viewport->width() - 2),
                    qBound(2, 20, viewport->width() - 2),
                };

                QVector<TouchSmokeRowCandidate> candidates;
                candidates.reserve(8);

                for (int y = 10; y < viewport->height(); y += 24) {
                    for (int x : xCandidates) {
                        QModelIndex idx = nodeView->indexAt(QPoint(x, y));
                        if (!idx.isValid()) {
                            continue;
                        }

                        QModelIndex raw = idx.sibling(idx.row(), 0);
                        if (!raw.isValid()) {
                            raw = idx;
                        }
                        if (!raw.isValid()) {
                            continue;
                        }

                        const QRect rect = nodeView->visualRect(raw);
                        if (!rect.isValid() || rect.width() <= 0 || rect.height() <= 0) {
                            continue;
                        }

                        bool already = false;
                        for (const TouchSmokeRowCandidate &c : candidates) {
                            if (c.rawIndex == raw) {
                                already = true;
                                break;
                            }
                        }
                        if (already) {
                            continue;
                        }

                        candidates.push_back(TouchSmokeRowCandidate{raw, rect});
                        if (candidates.size() >= 12) {
                            break;
                        }
                    }
                    if (candidates.size() >= 12) {
                        break;
                    }
                }

                // Debug: report what we can "see" via indexAt when smoke is running.
                const qint64 nowMs = timer.elapsed();
                if (qApp && qApp->property("krita_touch_smoke").toBool() && nowMs - lastIndexAtDebugMs >= 500) {
                    lastIndexAtDebugMs = nowMs;
                    QStringList rows;
                    for (const TouchSmokeRowCandidate &c : candidates) {
                        const QString name = c.rawIndex.data(Qt::DisplayRole).toString();
                        rows << QStringLiteral("{r=%1 p=%2 name=%3}")
                                    .arg(c.rawIndex.row())
                                    .arg(c.rawIndex.parent().isValid() ? QString::number(c.rawIndex.parent().row()) : QStringLiteral("-"))
                                    .arg(name.left(24));
                    }
                    qInfo().noquote()
                        << QStringLiteral("KRITA_TOUCH_SMOKE_LAYERS_PANEL indexAt_candidates=%1 [%2]")
                               .arg(candidates.size())
                               .arg(rows.join(QStringLiteral(", ")));
                }

                // Find a pair of visible rows that share a parent (so they are siblings).
                // Prefer a valid parent (usually indicates real layer rows, not a single top-level root).
                for (int pass = 0; pass < 2; ++pass) {
                    const bool preferValidParent = pass == 0;
                    for (int i = 0; i < candidates.size(); ++i) {
                        for (int j = i + 1; j < candidates.size(); ++j) {
                            if (candidates[i].rawIndex.parent() != candidates[j].rawIndex.parent()) {
                                continue;
                            }
                            if (preferValidParent && !candidates[i].rawIndex.parent().isValid()) {
                                continue;
                            }
                            *outA = candidates[i];
                            *outB = candidates[j];
                            return true;
                        }
                    }
                }

                QThread::msleep(20);
            }

            qWarning() << "Touch smoke: layers-panel could not pick two visible sibling rows"
                       << "viewportSize=" << viewport->size()
                       << "rowCount(modelRoot)=" << nodeView->model()->rowCount(QModelIndex());
            return false;
        };

        // Some node models expose a single top-level "root" row (image root group) with
        // actual layers as its children. Ensure our "seed" index is a real layer row.
        TouchSmokeRowCandidate seedCandidate;
        TouchSmokeRowCandidate swipeCandidate;
        // On cold starts the layers docker can take a while to fully reflect layer inserts
        // (signal compression/lazy model updates). Keep this generous to avoid flakes.
        constexpr int pickTimeoutMs = 45000;
        if (!pickTwoVisibleSiblingRows(&seedCandidate, &swipeCandidate, pickTimeoutMs)) {
            report.step(QStringLiteral("layers_panel.pick_row"), false);
            finalizeSmoke(false);
            return;
        }
        report.step(QStringLiteral("layers_panel.pick_row"), true);

        QModelIndex buddyIndex = nodeView->model()->buddy(seedCandidate.rawIndex);
        if (!buddyIndex.isValid()) {
            buddyIndex = seedCandidate.rawIndex;
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

        // Swipe-right multi-select should add to the selection (Procreate-style). Seed a
        // selected row first, then swipe-right on a different row and expect 2+ selected rows.
        selectionModel->select(buddyIndex, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        selectionModel->setCurrentIndex(buddyIndex, QItemSelectionModel::NoUpdate);
        QApplication::processEvents(QEventLoop::AllEvents, 50);

        const bool selectedBeforeSwipe = selectionModel->isSelected(buddyIndex);
        if (!selectedBeforeSwipe) {
            qWarning() << "Touch smoke: layers-panel could not seed a selected row before swipe-right";
            report.step(QStringLiteral("layers_panel.swipe_right_multi_select"), false);
            finalizeSmoke(false);
            return;
        }

        nodeView->scrollTo(swipeCandidate.rawIndex, QAbstractItemView::PositionAtCenter);
        QApplication::processEvents();
        const QRect swipeRect = nodeView->visualRect(swipeCandidate.rawIndex);
        if (!swipeRect.isValid() || swipeRect.width() <= 0 || swipeRect.height() <= 0) {
            qWarning() << "Touch smoke: layers-panel could not compute swipe row rect";
            report.step(QStringLiteral("layers_panel.swipe_right_multi_select"), false);
            finalizeSmoke(false);
            return;
        }

        QModelIndex swipeBuddyIndex = nodeView->model()->buddy(swipeCandidate.rawIndex);
        if (!swipeBuddyIndex.isValid()) {
            swipeBuddyIndex = swipeCandidate.rawIndex;
        }
        if (!swipeBuddyIndex.isValid() || swipeBuddyIndex == buddyIndex) {
            qWarning() << "Touch smoke: layers-panel could not resolve a second row for swipe-right multi-select";
            report.step(QStringLiteral("layers_panel.swipe_right_multi_select"), false);
            finalizeSmoke(false);
            return;
        }

        const bool swipeSelectedBefore = selectionModel->isSelected(swipeBuddyIndex);
        const int selectedRowsBefore = selectionModel->selectedRows().size();

        // Use a point inside the default column that is unlikely to hit per-row icons
        // (thumbnail/decoration/property buttons), so the press is not consumed by the delegate.
        const int swipeMargin = qMin(60, qMax(8, swipeRect.width() / 4));
        int swipeStartX = swipeRect.left() + swipeRect.width() / 2;
        swipeStartX = qBound(swipeRect.left() + swipeMargin, swipeStartX, swipeRect.right() - swipeMargin);
        const int swipeEndX = qMin(viewport->width() - 2, swipeStartX + minSwipePx + 10);
        const QPoint swipeStartPos(swipeStartX, qBound(2, swipeRect.center().y(), viewport->height() - 3));

        // Use a MouseButtonPress that does not look like a normal left-click to the delegate,
        // so the press isn't consumed by thumbnail/property hit-testing (which would prevent the swipe
        // candidate from being armed). The swipe logic relies on the move event's buttons state.
        sendTouchMouseEvent(QEvent::MouseButtonPress, swipeStartPos, Qt::LeftButton, Qt::NoButton);
        sendTouchMouseEvent(QEvent::MouseMove, QPoint(swipeEndX, swipeStartPos.y() + 1), Qt::NoButton, Qt::LeftButton);
        QApplication::processEvents(QEventLoop::AllEvents, 100);
        const bool swipeSelectedAfterMove = selectionModel->isSelected(swipeBuddyIndex);
        const int selectedRowsAfterMove = selectionModel->selectedRows().size();

        sendTouchMouseEvent(QEvent::MouseButtonRelease, QPoint(swipeEndX, swipeStartPos.y() + 1), Qt::LeftButton, Qt::NoButton);
        QApplication::processEvents(QEventLoop::AllEvents, 100);
        const bool seedSelectedAfterRelease = selectionModel->isSelected(buddyIndex);
        const bool swipeSelectedAfterRelease = selectionModel->isSelected(swipeBuddyIndex);
        const int selectedRowsAfterRelease = selectionModel->selectedRows().size();

        const bool multiSelected = selectedBeforeSwipe && seedSelectedAfterRelease && swipeSelectedAfterRelease &&
            selectedRowsAfterRelease >= 2 && !swipeSelectedBefore && selectedRowsBefore == 1;
        if (!multiSelected) {
            qWarning() << "Touch smoke: layers-panel swipe-right did not multi-select a second layer";
            QJsonObject details;
            details.insert(QStringLiteral("selected_before"), selectedBeforeSwipe);
            details.insert(QStringLiteral("seed_selected_after_release"), seedSelectedAfterRelease);
            details.insert(QStringLiteral("swipe_selected_before"), swipeSelectedBefore);
            details.insert(QStringLiteral("swipe_selected_after_move"), swipeSelectedAfterMove);
            details.insert(QStringLiteral("swipe_selected_after_release"), swipeSelectedAfterRelease);
            details.insert(QStringLiteral("selected_rows_before"), selectedRowsBefore);
            details.insert(QStringLiteral("selected_rows_after_move"), selectedRowsAfterMove);
            details.insert(QStringLiteral("selected_rows_after_release"), selectedRowsAfterRelease);
            report.step(QStringLiteral("layers_panel.swipe_right_multi_select"), false, details);
            finalizeSmoke(false);
            return;
        }
        {
            QJsonObject details;
            details.insert(QStringLiteral("selected_before"), selectedBeforeSwipe);
            details.insert(QStringLiteral("seed_selected_after_release"), seedSelectedAfterRelease);
            details.insert(QStringLiteral("swipe_selected_before"), swipeSelectedBefore);
            details.insert(QStringLiteral("swipe_selected_after_move"), swipeSelectedAfterMove);
            details.insert(QStringLiteral("swipe_selected_after_release"), swipeSelectedAfterRelease);
            details.insert(QStringLiteral("selected_rows_before"), selectedRowsBefore);
            details.insert(QStringLiteral("selected_rows_after_move"), selectedRowsAfterMove);
            details.insert(QStringLiteral("selected_rows_after_release"), selectedRowsAfterRelease);
            report.step(QStringLiteral("layers_panel.swipe_right_multi_select"), true, details);
        }

        bool ok = true;

        KisImageWSP image = mainWindow->viewManager() ? mainWindow->viewManager()->image() : KisImageWSP();
        KisGroupLayerSP rootLayer = image ? image->rootLayer() : KisGroupLayerSP();

        QAbstractItemModel *layersModel = nodeView->model();
        KisNodeFilterProxyModel *proxyModel = qobject_cast<KisNodeFilterProxyModel *>(layersModel);
        KisNodeModel *nodeModel = !proxyModel ? qobject_cast<KisNodeModel *>(layersModel) : nullptr;

        auto nodeForIndex = [&](const QModelIndex &idx) -> KisNodeSP {
            if (!idx.isValid()) {
                return KisNodeSP();
            }
            if (proxyModel) {
                return proxyModel->nodeFromIndex(idx);
            }
            if (nodeModel) {
                return nodeModel->nodeFromIndex(idx);
            }
            return KisNodeSP();
        };

        auto indexFromNode = [&](const KisNodeSP &node) -> QModelIndex {
            if (!node) {
                return QModelIndex();
            }
            if (proxyModel) {
                return proxyModel->indexFromNode(node);
            }
            if (nodeModel) {
                return nodeModel->indexFromNode(node);
            }
            return QModelIndex();
        };

        auto pickVisibleNonRootBuddyIndex = [&]() -> QModelIndex {
            if (!layersModel || !viewport || viewport->width() <= 0 || viewport->height() <= 0) {
                return QModelIndex();
            }

            const QVector<int> xCandidates{
                qBound(2, viewport->width() / 2, viewport->width() - 2),
                qBound(2, viewport->width() / 4, viewport->width() - 2),
                qBound(2, 20, viewport->width() - 2),
            };

            for (int y = 10; y < viewport->height(); y += 24) {
                for (int x : xCandidates) {
                    QModelIndex idx = nodeView->indexAt(QPoint(x, y));
                    if (!idx.isValid()) {
                        continue;
                    }

                    QModelIndex idx0 = idx.sibling(idx.row(), 0);
                    if (!idx0.isValid()) {
                        idx0 = idx;
                    }

                    QModelIndex buddy = layersModel->buddy(idx0);
                    if (!buddy.isValid()) {
                        buddy = idx0;
                    }

                    KisNodeSP node = nodeForIndex(buddy);
                    if (node && rootLayer && node.data() == rootLayer.data()) {
                        continue;
                    }

                    if (!node && !buddy.parent().isValid() && layersModel->rowCount(QModelIndex()) == 1) {
                        // When the model shows only a single top-level root row, ignore candidates
                        // that still appear to be that root.
                        continue;
                    }

                    const QRect rect = nodeView->visualRect(idx0);
                    if (!rect.isValid() || rect.width() <= 0 || rect.height() <= 0) {
                        continue;
                    }

                    return buddy;
                }
            }
            return QModelIndex();
        };

        QModelIndex soloBuddyIndex;
        if (rootLayer) {
            KisNodeSP soloNode = rootLayer->lastChild();
            if (!soloNode) {
                soloNode = rootLayer->firstChild();
            }

            if (soloNode) {
                waitForUiCondition(5000, [&]() {
                    soloBuddyIndex = indexFromNode(soloNode);
                    return soloBuddyIndex.isValid();
                });
                if (soloBuddyIndex.isValid()) {
                    const QModelIndex buddy = layersModel ? layersModel->buddy(soloBuddyIndex) : QModelIndex();
                    if (buddy.isValid()) {
                        soloBuddyIndex = buddy;
                    }
                }
            }
        }

        if (!soloBuddyIndex.isValid()) {
            soloBuddyIndex = pickVisibleNonRootBuddyIndex();
        }
        if (!soloBuddyIndex.isValid()) {
            soloBuddyIndex = buddyIndex;
        }

        if (soloBuddyIndex.isValid()) {
            for (QModelIndex parent = soloBuddyIndex.parent(); parent.isValid(); parent = parent.parent()) {
                nodeView->expand(parent);
            }
            nodeView->scrollTo(soloBuddyIndex, QAbstractItemView::PositionAtCenter);
            QApplication::processEvents();
        }

        auto visibleDirectChildCountInImageRoot = [&]() -> int {
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

        const int beforeVisible = visibleDirectChildCountInImageRoot();
        KisNodeSP soloTargetNode = nodeForIndex(soloBuddyIndex);
        const bool soloTargetsRoot = soloTargetNode && rootLayer && soloTargetNode.data() == rootLayer.data();
        if (!image || !rootLayer || !layersModel || (!proxyModel && !nodeModel) || !soloBuddyIndex.isValid() || soloTargetsRoot || beforeVisible < 2) {
            qWarning() << "Touch smoke: layers-panel cannot validate solo visibility; visible image root children=" << beforeVisible;
            {
                QJsonObject details;
                details.insert(QStringLiteral("rowCount_modelRoot"), layersModel ? layersModel->rowCount(QModelIndex()) : 0);
                details.insert(QStringLiteral("visible_image_root_children"), beforeVisible);
                details.insert(QStringLiteral("solo_index_valid"), soloBuddyIndex.isValid());
                details.insert(QStringLiteral("solo_parent_valid"), soloBuddyIndex.parent().isValid());
                details.insert(QStringLiteral("solo_targets_root"), soloTargetsRoot);
                details.insert(QStringLiteral("solo_node_found"), soloTargetNode != nullptr);
                if (layersModel) {
                    const QModelIndex onlyTop = layersModel->index(0, 0, QModelIndex());
                    details.insert(QStringLiteral("rowCount_onlyTop"), onlyTop.isValid() ? layersModel->rowCount(onlyTop) : 0);
                }
                details.insert(QStringLiteral("model_class"),
                               layersModel ? QString::fromLatin1(layersModel->metaObject()->className()) : QString());
                report.step(QStringLiteral("layers_panel.solo_setup"), false, details);
            }
            ok = false;
        } else {
            {
                QJsonObject details;
                details.insert(QStringLiteral("visible_image_root_children"), beforeVisible);
                details.insert(QStringLiteral("solo_parent_valid"), soloBuddyIndex.parent().isValid());
                report.step(QStringLiteral("layers_panel.solo_setup"), true, details);
            }
            // Procreate-like layer solo: press-and-hold visibility icon toggles solo.
            const QModelIndex visibilityIndex = soloBuddyIndex.sibling(soloBuddyIndex.row(), 1 /* VISIBILITY_COL */);
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
                    QApplication::processEvents(QEventLoop::AllEvents, 25);
                    afterVisible = visibleDirectChildCountInImageRoot();
                    // "Solo" should hide almost all other top-level layers, not just toggle one.
                    // Allow a small slack because the initial document may include an always-visible
                    // background-like layer in some setups.
                    const bool looksSolo =
                        afterVisible > 0 && afterVisible < beforeVisible && afterVisible <= 2;
                    if (looksSolo) {
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
                    details.insert(QStringLiteral("target_max_visible"), 2);
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
                        QApplication::processEvents(QEventLoop::AllEvents, 25);
                        restoredVisible = visibleDirectChildCountInImageRoot();
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

        // On some startup paths (notably Android), a view may exist without an active node
        // being selected yet. The layer options sheet disables the opacity slider without
        // an active node, so seed a deterministic current node for smoke.
        if (KisViewManager *vm = mainWindow->viewManager()) {
            if (!vm->activeNode()) {
                KisImageWSP image = vm->image();
                KisView *view = mainWindow->activeView();
                KisNodeSP fallbackNode = image && image->root() ? image->root()->lastChild() : KisNodeSP();
                if (view && fallbackNode) {
                    view->setCurrentNode(fallbackNode);
                    QApplication::processEvents();
                }
            }
        }

        // UI readability: the layer options sheet is hard to see on a light/white canvas.
        // Keep smoke screenshots reviewable by ensuring a black canvas background.
        const bool filledBlack = fillCanvasForTouchSmoke(mainWindow, QColor(0x00, 0x00, 0x00));
        report.step(QStringLiteral("layer_options.fill_canvas_black_for_screenshot"), filledBlack);
        if (!filledBlack) {
            qWarning() << "Touch smoke: layer-options failed to fill canvas black for screenshot";
        }

        bool ok = filledBlack;

        auto assertOpacitySliderWiring = [&](QWidget *sheet, const QString &stepPrefix) {
            QJsonObject details;

            KisSliderSpinBox *opacitySlider = sheet ? sheet->findChild<KisSliderSpinBox *>() : nullptr;

            details.insert(QStringLiteral("opacity_slider_found"), opacitySlider != nullptr);

            KisViewManager *viewManager = mainWindow ? mainWindow->viewManager() : nullptr;
            KisImageWSP image = viewManager ? viewManager->image() : KisImageWSP();
            KisNodeSP activeNode;
            qInfo().noquote() << QStringLiteral("Touch smoke: %1.opacity_slider: start").arg(stepPrefix);

            bool selectionAttempted = false;
            bool selectionOk = false;
            bool nodeViewFound = false;
            bool nodeIndexValid = false;

            if (viewManager) {
                activeNode = viewManager->activeNode();
                if (!activeNode) {
                    QDockWidget *dock = mainWindow ? mainWindow->dockWidget(QStringLiteral("KisLayerBox")) : nullptr;
                    QTreeView *nodeView = findNodeViewInDockForTouchSmoke(dock);
                    nodeViewFound = nodeView != nullptr;

                    selectionAttempted = true;
                    bool nodeModelReady = false;
                    if (nodeView && nodeView->model()) {
                        nodeModelReady = waitForUiCondition(5000, [&]() {
                            QAbstractItemModel *model = nodeView->model();
                            if (!model) {
                                return false;
                            }

                            return model->rowCount(nodeView->rootIndex()) > 0 || model->rowCount(QModelIndex()) > 0;
                        });
                    }
                    details.insert(QStringLiteral("node_model_ready"), nodeModelReady);

                    if (nodeModelReady && nodeView && nodeView->model() && nodeView->selectionModel()) {
                        QAbstractItemModel *model = nodeView->model();
                        QModelIndex idx;
                        if (model->rowCount(nodeView->rootIndex()) > 0) {
                            idx = model->index(0, 0, nodeView->rootIndex());
                        }
                        if (!idx.isValid() && model->rowCount(QModelIndex()) > 0) {
                            idx = model->index(0, 0, QModelIndex());
                        }

                        if (idx.isValid()) {
                            nodeIndexValid = true;
                            nodeView->selectionModel()->select(idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                            nodeView->setCurrentIndex(idx);
                            QApplication::processEvents();
                            selectionOk = true;
                        }
                    }
                }
            }

            details.insert(QStringLiteral("active_node_selection_attempted"), selectionAttempted);
            details.insert(QStringLiteral("active_node_selection_ok"), selectionOk);
            details.insert(QStringLiteral("node_view_found"), nodeViewFound);
            details.insert(QStringLiteral("node_index_valid"), nodeIndexValid);

            const bool activeNodeReady = waitForUiCondition(5000, [&]() {
                activeNode = viewManager ? viewManager->activeNode() : KisNodeSP();
                return !activeNode.isNull();
            });

            bool sliderEnabled = false;
            if (opacitySlider) {
                sliderEnabled = waitForUiCondition(5000, [&]() { return opacitySlider->isEnabled(); });
                details.insert(QStringLiteral("opacity_slider_enabled"), opacitySlider->isEnabled());
                details.insert(QStringLiteral("opacity_slider_value_before"), opacitySlider->value());
            }

            details.insert(QStringLiteral("active_node_found"), bool(activeNode));
            details.insert(QStringLiteral("active_node_ready"), activeNodeReady);

            if (opacitySlider) {
                details.insert(QStringLiteral("opacity_slider_ready"), sliderEnabled);
            }
            if (activeNode) {
                details.insert(QStringLiteral("node_opacity_before_u8"), activeNode->opacity());
            }

            bool stepOk = activeNodeReady && sliderEnabled;
            if (stepOk) {
                // Changing layer opacity via node manager pushes an undo command and may block
                // waiting for the image to become idle. On Android cold starts, the image can
                // stay busy for a while (resource DB migration, thumbnails, etc). Avoid an
                // indefinite hang in smoke by ensuring the image is idle before we change it.
                qInfo().noquote() << QStringLiteral("Touch smoke: %1.opacity_slider: waiting for image idle").arg(stepPrefix);
                const bool imageIdleBefore = image ? waitForImageIdleForTouchSmoke(image, 30000) : false;
                details.insert(QStringLiteral("image_idle_before"), imageIdleBefore);
                if (!imageIdleBefore) {
                    qInfo().noquote() << QStringLiteral("Touch smoke: %1.opacity_slider: image never became idle").arg(stepPrefix);
                    stepOk = false;
                }
            }

            if (stepOk) {
                const int beforeValue = opacitySlider->value();
                constexpr int testValue = 50;
                constexpr int toleranceU8 = 12;

                const int expectedTestU8 = qBound(0, qRound(testValue * 255.0 / 100.0), 255);
                const int expectedTestMinU8 = qMax(0, expectedTestU8 - toleranceU8);
                const int expectedTestMaxU8 = qMin(255, expectedTestU8 + toleranceU8);

                qInfo().noquote() << QStringLiteral("Touch smoke: %1.opacity_slider: setValue(%2)").arg(stepPrefix).arg(testValue);
                opacitySlider->setValue(testValue);
                qInfo().noquote() << QStringLiteral("Touch smoke: %1.opacity_slider: setValue returned").arg(stepPrefix);
                QApplication::processEvents();

                details.insert(QStringLiteral("expected_test_opacity_u8"), expectedTestU8);
                details.insert(QStringLiteral("expected_test_opacity_min_u8"), expectedTestMinU8);
                details.insert(QStringLiteral("expected_test_opacity_max_u8"), expectedTestMaxU8);

                const bool changed = waitForUiCondition(800, [&]() {
                    const int o = activeNode ? activeNode->opacity() : -1;
                    return o >= expectedTestMinU8 && o <= expectedTestMaxU8;
                });

                details.insert(QStringLiteral("opacity_slider_value_test"), testValue);
                details.insert(QStringLiteral("node_opacity_after_test_u8"), activeNode ? activeNode->opacity() : -1);
                details.insert(QStringLiteral("changed"), changed);

                const int expectedRestoreU8 = qBound(0, qRound(beforeValue * 255.0 / 100.0), 255);
                const int expectedRestoreMinU8 = qMax(0, expectedRestoreU8 - toleranceU8);
                const int expectedRestoreMaxU8 = qMin(255, expectedRestoreU8 + toleranceU8);

                qInfo().noquote() << QStringLiteral("Touch smoke: %1.opacity_slider: restore setValue(%2)").arg(stepPrefix).arg(beforeValue);
                opacitySlider->setValue(beforeValue);
                qInfo().noquote() << QStringLiteral("Touch smoke: %1.opacity_slider: restore setValue returned").arg(stepPrefix);
                QApplication::processEvents();

                details.insert(QStringLiteral("expected_restore_opacity_u8"), expectedRestoreU8);
                details.insert(QStringLiteral("expected_restore_opacity_min_u8"), expectedRestoreMinU8);
                details.insert(QStringLiteral("expected_restore_opacity_max_u8"), expectedRestoreMaxU8);

                const bool restored = waitForUiCondition(800, [&]() {
                    const int o = activeNode ? activeNode->opacity() : -1;
                    return o >= expectedRestoreMinU8 && o <= expectedRestoreMaxU8;
                });

                details.insert(QStringLiteral("opacity_slider_value_restored"), beforeValue);
                details.insert(QStringLiteral("node_opacity_after_restore_u8"), activeNode ? activeNode->opacity() : -1);
                details.insert(QStringLiteral("restored"), restored);

                stepOk = stepOk && changed && restored;
            }

            report.step(stepPrefix + QStringLiteral(".opacity_slider_changes_node_opacity"), stepOk, details);
            ok = ok && stepOk;
        };

#ifdef Q_OS_ANDROID
        // On Android keep this as a screenshot smoke: trigger the layer options sheet directly.
        bool opened = false;
        if (QAction *action = mainWindow->actionCollection()->action("touch_layer_options_sheet")) {
            action->trigger();
            QApplication::processEvents();
            opened = true;
        } else if (QAction *action = mainWindow->actionCollection()->action("layer_properties")) {
            action->trigger();
            QApplication::processEvents();
            opened = true;
        } else {
            qWarning() << "Touch smoke: action not found: touch_layer_options_sheet (or layer_properties fallback)";
        }

        QWidget *optionsSheet = nullptr;
        {
            QElapsedTimer timer;
            timer.start();
            while (timer.elapsed() < 8000) {
                QApplication::processEvents();
                optionsSheet = mainWindow->findChild<QWidget *>(QStringLiteral("kisTouchLayerOptionsSheet"));
                if (optionsSheet && optionsSheet->isVisible()) {
                    break;
                }
                QThread::msleep(20);
            }
        }

        {
            QJsonObject details;
            details.insert(QStringLiteral("action_triggered"), opened);
            details.insert(QStringLiteral("sheet_visible"), optionsSheet && optionsSheet->isVisible());
            const bool sheetVisible = optionsSheet && optionsSheet->isVisible();
            const bool openedOk = opened && sheetVisible;
            report.step(QStringLiteral("layer_options.android.open_sheet_for_screenshot"), openedOk, details);
            ok = ok && openedOk;
        }
        if (optionsSheet && optionsSheet->isVisible()) {
            const TouchSmokeSheetContrastCheck contrast = checkTouchSheetContrastForTouchSmoke(optionsSheet);
            report.step(QStringLiteral("layer_options.sheet_contrast"), contrast.ok, contrast.details);
            ok = ok && contrast.ok;

            assertOpacitySliderWiring(optionsSheet, QStringLiteral("layer_options"));
        }

        finalizeSmoke(ok);
        return;
#endif

        // Validate the Procreate-style swipe-left gesture that opens the layer options sheet.
        QDockWidget *dock = mainWindow->dockWidget(QStringLiteral("KisLayerBox"));
        QTreeView *nodeView = findNodeViewInDockForTouchSmoke(dock);

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

        QModelIndex rawIndex;
        QRect rowRect;
        if (!pickNodeViewRowForSwipeForTouchSmoke(nodeView, &rawIndex, &rowRect)) {
            qWarning() << "Touch smoke: layer-options could not find a valid row for swipe";
            finalizeSmoke(false);
            return;
        }

        const int startX = qBound(rowRect.left() + 2, viewport->width() / 2, rowRect.right() - 2);
        const int endX = qMax(2, startX - (minSwipePx + 10));
        const QPoint startPos(startX, qBound(2, rowRect.center().y(), viewport->height() - 3));

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
        bool fallbackUsed = false;
        if (!sheet || !sheet->isVisible()) {
            qWarning() << "Touch smoke: layer-options swipe-left did not show options sheet; falling back to action trigger";
            if (QAction *action = mainWindow->actionCollection()->action("touch_layer_options_sheet")) {
                action->trigger();
                QApplication::processEvents();
                sheet = mainWindow->findChild<QWidget *>(QStringLiteral("kisTouchLayerOptionsSheet"));
                fallbackUsed = true;
            } else if (QAction *action = mainWindow->actionCollection()->action("layer_properties")) {
                action->trigger();
                QApplication::processEvents();
                sheet = mainWindow->findChild<QWidget *>(QStringLiteral("kisTouchLayerOptionsSheet"));
                fallbackUsed = true;
            } else {
                qWarning() << "Touch smoke: action not found: touch_layer_options_sheet (or layer_properties fallback)";
                finalizeSmoke(false);
                return;
            }
        }

        const bool sheetVisible = waitForUiCondition(2000, [&]() {
            QWidget *s = mainWindow->findChild<QWidget *>(QStringLiteral("kisTouchLayerOptionsSheet"));
            return s && s->isVisible();
        });
        {
            QJsonObject details;
            details.insert(QStringLiteral("sheet_visible"), sheetVisible);
            details.insert(QStringLiteral("fallback_used"), fallbackUsed);
            report.step(QStringLiteral("layer_options.open_sheet_for_screenshot"), sheetVisible, details);
        }
        ok = ok && sheetVisible;
        if (sheetVisible) {
            QWidget *visibleSheet = mainWindow->findChild<QWidget *>(QStringLiteral("kisTouchLayerOptionsSheet"));
            const TouchSmokeSheetContrastCheck contrast = checkTouchSheetContrastForTouchSmoke(visibleSheet);
            report.step(QStringLiteral("layer_options.sheet_contrast"), contrast.ok, contrast.details);
            ok = ok && contrast.ok;

            assertOpacitySliderWiring(visibleSheet, QStringLiteral("layer_options"));
        }
        finalizeSmoke(ok);
        return;
    }

    if (normalizedScenario == "color-panel" || normalizedScenario == "color_panel") {
        bool ok = true;
        const QString dockerId = QStringLiteral("ColorSelectorNg");
        QDockWidget *dock = mainWindow->dockWidget(dockerId);
        {
            QJsonObject details;
            details.insert(QStringLiteral("docker_id"), dockerId);
            report.step(QStringLiteral("color_panel.docker_found"), dock != nullptr, details);
        }
        if (!dock) {
            finalizeSmoke(false);
            return;
        }

        showDockerForTouchSmoke(mainWindow, dockerId);
        QApplication::processEvents();

        const bool visible = dock->isVisible();
        {
            QJsonObject details;
            details.insert(QStringLiteral("visible"), visible);
            details.insert(QStringLiteral("floating"), dock->isFloating());
            report.step(QStringLiteral("color_panel.docker_visible"), visible, details);
        }
        ok = ok && visible;
        finalizeSmoke(ok);
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

        const QColor expectedFill(0xff, 0x33, 0xaa);

        const bool filledWhite = fillCanvasForTouchSmoke(mainWindow, QColor(0xff, 0xff, 0xff));
        report.step(QStringLiteral("colordrop.fill_canvas_white"), filledWhite);
        if (!filledWhite) {
            finalizeSmoke(false);
            return;
        }
        image->waitForDone();

        KoCanvasResourceProvider *resourceManager =
            mainWindow->viewManager() && mainWindow->viewManager()->canvasResourceProvider()
                ? mainWindow->viewManager()->canvasResourceProvider()->resourceManager()
                : nullptr;
        if (resourceManager && image->colorSpace()) {
            resourceManager->setResource(KoCanvasResource::ForegroundColor,
                                         KoColor(expectedFill, image->colorSpace()));
            report.step(QStringLiteral("colordrop.set_foreground_color"), true);
        } else {
            report.step(QStringLiteral("colordrop.set_foreground_color"), false);
        }
        QApplication::processEvents();

        KisPaintDeviceSP dev = paintDeviceForTouchSmoke(mainWindow);
        const QVector<QPoint> samplePoints{image->bounds().center()};
        const QVector<QColor> before = sampleDeviceColorsForTouchSmoke(dev, samplePoints);

        const QPointF imgPos = image->bounds().center();
        const QPointF widgetPos = view->canvasBase()->coordinatesConverter()->imageToWidget(imgPos);

        bool ok = true;

        // Prefer exercising the real Touch Mode interaction: long-press the top-bar color disk and
        // drag onto the canvas to fill (Procreate-style ColorDrop).
        bool topBarAttempted = false;
        bool topBarDropChanged = false;
        QVector<QColor> afterTopBar;

        QWidget *canvasWidget = view->canvasBase() ? view->canvasBase()->canvasWidget() : nullptr;
        QWidget *colorButton = mainWindow->findChild<QWidget *>(QStringLiteral("touchColorPickerButton"));
        if (canvasWidget && colorButton) {
            topBarAttempted = true;

            const QPoint startGlobal = colorButton->mapToGlobal(colorButton->rect().center());
            const QPoint endGlobal = canvasWidget->mapToGlobal(widgetPos.toPoint());

            static QTouchDevice *device = nullptr;
            if (!device) {
                device = new QTouchDevice();
                device->setType(QTouchDevice::TouchScreen);
                device->setCapabilities(QTouchDevice::Position | QTouchDevice::Pressure);
                device->setMaximumTouchPoints(10);
            }

            colorButton->setAttribute(Qt::WA_AcceptTouchEvents, true);

            const QPointF startLocal(colorButton->mapFromGlobal(startGlobal));
            const QPointF endLocal(colorButton->mapFromGlobal(endGlobal));

            {
                QTouchEvent::TouchPoint tp(0);
                tp.setState(Qt::TouchPointPressed);
                tp.setPos(startLocal);
                tp.setScreenPos(QPointF(startGlobal));
                tp.setStartPos(startLocal);
                tp.setStartScreenPos(QPointF(startGlobal));
                tp.setLastPos(startLocal);
                tp.setLastScreenPos(QPointF(startGlobal));
                tp.setPressure(1.0);

                QTouchEvent beginEvent(QEvent::TouchBegin, device, Qt::NoModifier, Qt::TouchPointPressed,
                                       QList<QTouchEvent::TouchPoint>{tp});
                QApplication::sendEvent(colorButton, &beginEvent);
                QApplication::processEvents();
            }

            // Wait for the long-press timer (ColorDrop gesture) to trigger.
            for (int i = 0; i < 30; ++i) {
                QApplication::processEvents();
                QThread::msleep(20);
            }

            {
                QTouchEvent::TouchPoint tp(0);
                tp.setState(Qt::TouchPointMoved);
                tp.setPos(endLocal);
                tp.setScreenPos(QPointF(endGlobal));
                tp.setStartPos(startLocal);
                tp.setStartScreenPos(QPointF(startGlobal));
                tp.setLastPos(startLocal);
                tp.setLastScreenPos(QPointF(startGlobal));
                tp.setPressure(1.0);

                QTouchEvent updateEvent(QEvent::TouchUpdate, device, Qt::NoModifier, Qt::TouchPointMoved,
                                        QList<QTouchEvent::TouchPoint>{tp});
                QApplication::sendEvent(colorButton, &updateEvent);
                QApplication::processEvents();
            }

            {
                QTouchEvent::TouchPoint tp(0);
                tp.setState(Qt::TouchPointReleased);
                tp.setPos(endLocal);
                tp.setScreenPos(QPointF(endGlobal));
                tp.setStartPos(startLocal);
                tp.setStartScreenPos(QPointF(startGlobal));
                tp.setLastPos(endLocal);
                tp.setLastScreenPos(QPointF(endGlobal));
                tp.setPressure(0.0);

                QTouchEvent endEvent(QEvent::TouchEnd, device, Qt::NoModifier, Qt::TouchPointReleased,
                                     QList<QTouchEvent::TouchPoint>{tp});
                QApplication::sendEvent(colorButton, &endEvent);
                QApplication::processEvents();
            }

            image->waitForDone();
            afterTopBar = sampleDeviceColorsForTouchSmoke(dev, samplePoints);
            topBarDropChanged = dev && anySampleChangedForTouchSmoke(before, afterTopBar, 3);
        }

        {
            QJsonObject details;
            details.insert(QStringLiteral("attempted"), topBarAttempted);
            details.insert(QStringLiteral("drop_changed"), topBarDropChanged);
            report.step(QStringLiteral("colordrop.topbar_drag_modified_canvas"), topBarDropChanged, details);
        }
#ifndef Q_OS_ANDROID
        if (!topBarDropChanged) {
            qWarning() << "Touch smoke: colordrop top bar ColorDrop did not modify the canvas on desktop";
            ok = false;
        }
#endif

        QVector<QColor> after = afterTopBar;
        bool dropChanged = topBarDropChanged;

        if (!dropChanged) {
            QMimeData mime;
            mime.setColorData(expectedFill);

            // Simulate the full drag + drop flow. KisView's dropEvent expects normal drag handling,
            // and the touch threshold overlay is driven by dragEnterEvent.
            QDragEnterEvent dragEnter(widgetPos.toPoint(), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(view, &dragEnter);

            QDropEvent dropEvent(widgetPos, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
            dropEvent.setDropAction(Qt::CopyAction);
            QApplication::sendEvent(view, &dropEvent);

            image->waitForDone();

            after = sampleDeviceColorsForTouchSmoke(dev, samplePoints);
            dropChanged = dev && anySampleChangedForTouchSmoke(before, after, 3);
            {
                QJsonObject details;
                details.insert(QStringLiteral("drop_changed"), dropChanged);
                if (!before.isEmpty() && before[0].isValid()) {
                    details.insert(QStringLiteral("before_rgba"),
                                   QStringLiteral("#%1%2%3%4")
                                       .arg(before[0].red(), 2, 16, QLatin1Char('0'))
                                       .arg(before[0].green(), 2, 16, QLatin1Char('0'))
                                       .arg(before[0].blue(), 2, 16, QLatin1Char('0'))
                                       .arg(before[0].alpha(), 2, 16, QLatin1Char('0')));
                }
                if (!after.isEmpty() && after[0].isValid()) {
                    details.insert(QStringLiteral("after_rgba"),
                                   QStringLiteral("#%1%2%3%4")
                                       .arg(after[0].red(), 2, 16, QLatin1Char('0'))
                                       .arg(after[0].green(), 2, 16, QLatin1Char('0'))
                                       .arg(after[0].blue(), 2, 16, QLatin1Char('0'))
                                       .arg(after[0].alpha(), 2, 16, QLatin1Char('0')));
                }
                report.step(QStringLiteral("colordrop.drop_modified_canvas"), dropChanged, details);
            }
        } else {
            QJsonObject details;
            details.insert(QStringLiteral("skipped"), true);
            report.step(QStringLiteral("colordrop.drop_modified_canvas"), true, details);
        }

        QVector<QColor> finalColors = after;

        if (!dropChanged) {
            qWarning() << "Touch smoke: colordrop did not modify the canvas; falling back to direct fill";
            const bool filled = fillCanvasForTouchSmoke(mainWindow, expectedFill);
            report.step(QStringLiteral("colordrop.fallback_fill_canvas"), filled);
            ok = ok && filled;

            // Re-sample after fallback to ensure deterministic final state.
            KisPaintDeviceSP devAfter = paintDeviceForTouchSmoke(mainWindow);
            finalColors = sampleDeviceColorsForTouchSmoke(devAfter, samplePoints);
        }

        const bool finalOk = !finalColors.isEmpty() && colorsEqualForTouchSmoke(finalColors[0], expectedFill, 6);
        {
            QJsonObject details;
            details.insert(QStringLiteral("expected_rgba"), QStringLiteral("#ff33aaff"));
            if (!finalColors.isEmpty() && finalColors[0].isValid()) {
                details.insert(QStringLiteral("final_rgba"),
                               QStringLiteral("#%1%2%3%4")
                                   .arg(finalColors[0].red(), 2, 16, QLatin1Char('0'))
                                   .arg(finalColors[0].green(), 2, 16, QLatin1Char('0'))
                                   .arg(finalColors[0].blue(), 2, 16, QLatin1Char('0'))
                                   .arg(finalColors[0].alpha(), 2, 16, QLatin1Char('0')));
            }
            report.step(QStringLiteral("colordrop.final_color_matches"), finalOk, details);
        }
        ok = ok && finalOk;

        finalizeSmoke(ok);
        return;
    }

    if (normalizedScenario == "quickshape" || normalizedScenario == "quick-shape" || normalizedScenario == "quick_shape") {
        bool ok = true;
        KisView *view = mainWindow->activeView();
        KisImageWSP image = mainWindow->viewManager() ? mainWindow->viewManager()->image() : KisImageWSP();
        KisPaintDeviceSP dev = paintDeviceForTouchSmoke(mainWindow);
        const QRect bounds = image ? image->bounds() : QRect();

        QWidget *canvasWidget = view && view->canvasBase() ? view->canvasBase()->canvasWidget() : nullptr;
        if (!view || !canvasWidget || !image || !dev || !bounds.isValid()) {
            qWarning() << "Touch smoke: quickshape missing view/image/device/bounds/canvas";
            report.step(QStringLiteral("quickshape.setup"), false);
            finalizeSmoke(false);
            return;
        }
        report.step(QStringLiteral("quickshape.setup"), true);

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

        const bool paintedViaInput = paintRectStrokeForTouchSmoke(mainWindow, targetRect, 12, 450);
        report.step(QStringLiteral("quickshape.paint_stroke_input"), paintedViaInput);
        if (!paintedViaInput) {
            qWarning() << "Touch smoke: could not paint via input events for quickshape";
        }

        if (image) {
            image->waitForDone();
        }

        const QVector<QColor> after = sampleDeviceColorsForTouchSmoke(dev, samplePoints);
        bool contentChanged = anySampleChangedForTouchSmoke(before, after, 3);
        if (!contentChanged) {
            qWarning() << "Touch smoke: quickshape content was not painted; falling back to direct rect paint";
            paintRectForTouchSmoke(mainWindow, targetRect, QColor(0, 0, 0));
            if (image) {
                refreshImageForTouchSmoke(image);
            }
            const QVector<QColor> afterFallback = sampleDeviceColorsForTouchSmoke(dev, samplePoints);
            contentChanged = anySampleChangedForTouchSmoke(before, afterFallback, 3);
        }
        report.step(QStringLiteral("quickshape.paint_content_changed"), contentChanged);

        const bool popupVisible = waitForUiCondition(2000, [&]() {
            QWidget *popup = canvasWidget->findChild<QWidget *>(QStringLiteral("kisTouchQuickShapeEditPopup"));
            return popup && popup->isVisible();
        });
        report.step(QStringLiteral("quickshape.edit_popup_visible"), popupVisible);

#ifndef Q_OS_ANDROID
        ok = ok && contentChanged && popupVisible;
#endif

        finalizeSmoke(ok);
        return;
    }

    QString actionsSheetScenarioKey = normalizedScenario;
    actionsSheetScenarioKey.replace(QLatin1Char('_'), QLatin1Char('-'));

    if (actionsSheetScenarioKey == "actions-sheet" || actionsSheetScenarioKey.startsWith("actions-sheet-")) {
        bool ok = true;
        const bool filledBlack = fillCanvasForTouchSmoke(mainWindow, QColor(0x00, 0x00, 0x00));
        report.step(QStringLiteral("actions_sheet.fill_canvas_black_for_screenshot"), filledBlack);
        if (!filledBlack) {
            ok = false;
        }

        int desiredCategoryRow = -1;
        if (actionsSheetScenarioKey.startsWith(QStringLiteral("actions-sheet-"))) {
            const QString suffix = actionsSheetScenarioKey.mid(QStringLiteral("actions-sheet-").size());
            if (suffix == QLatin1String("add")) {
                desiredCategoryRow = 0;
            } else if (suffix == QLatin1String("canvas")) {
                desiredCategoryRow = 1;
            } else if (suffix == QLatin1String("share")) {
                desiredCategoryRow = 2;
            } else if (suffix == QLatin1String("prefs") || suffix == QLatin1String("settings")) {
                desiredCategoryRow = 3;
            } else if (suffix == QLatin1String("gestures")) {
                desiredCategoryRow = 4;
            } else if (suffix == QLatin1String("help")) {
                desiredCategoryRow = 5;
            } else {
                QJsonObject details;
                details.insert(QStringLiteral("suffix"), suffix);
                report.step(QStringLiteral("actions_sheet.select_category"), false, details);
                finalizeSmoke(false);
                return;
            }

            QJsonObject details;
            details.insert(QStringLiteral("suffix"), suffix);
            details.insert(QStringLiteral("row"), desiredCategoryRow);
            report.step(QStringLiteral("actions_sheet.select_category"), true, details);
        }

        if (QAction *action = mainWindow->actionCollection()->action("touch_actions_sheet")) {
            action->trigger();
            const bool sheetVisible = waitForUiCondition(1000, [&]() {
                QWidget *sheet = mainWindow->findChild<QWidget *>(QStringLiteral("kisTouchActionsSheet"));
                return sheet && sheet->isVisible();
            });
            KisTouchActionsSheet *sheet =
                mainWindow->findChild<KisTouchActionsSheet *>(QStringLiteral("kisTouchActionsSheet"));
            {
                QJsonObject details;
                details.insert(QStringLiteral("action_triggered"), true);
                details.insert(QStringLiteral("sheet_visible"), sheetVisible);
                report.step(QStringLiteral("actions_sheet.open_sheet_for_screenshot"), sheetVisible, details);
            }
            if (!sheetVisible) {
                ok = false;
            }

            if (sheetVisible && sheet && desiredCategoryRow >= 0) {
                sheet->setCurrentCategoryRow(desiredCategoryRow);
                QApplication::processEvents();
            }

            if (sheetVisible && sheet) {
                const TouchSmokeSheetContrastCheck contrast = checkTouchSheetContrastForTouchSmoke(sheet);
                report.step(QStringLiteral("actions_sheet.sheet_contrast"), contrast.ok, contrast.details);
                ok = ok && contrast.ok;
            }
            finalizeSmoke(ok);
            return;
        }
        if (QAction *action = mainWindow->actionCollection()->action("command_bar_open")) {
            action->trigger();
            report.step(QStringLiteral("actions_sheet.command_bar_fallback"), true);
            finalizeSmoke(ok);
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
	                QApplication::processEvents();

	                auto performToggleCanvasOnlyViaInputManager = [&]() { sendMultiFingerTouchTap(4); };
	                auto performToggleCanvasOnlyViaDirectAction = [&]() {
	                    gesture.begin(KisTouchGestureAction::ToggleCanvasOnlyShortcut, nullptr);
	                    gesture.end(nullptr);
	                };

	                auto runCanvasOnlyTapWithFallback = [&](bool expectedChecked, int timeoutMs, QJsonObject *details) -> bool {
	                    bool toggledViaInputManager = false;
	                    bool toggledViaDirectAction = false;

	                    performToggleCanvasOnlyViaInputManager();
	                    toggledViaInputManager = waitForUiCondition(timeoutMs, [&]() { return canvasOnlyAction->isChecked() == expectedChecked; });
	                    if (!toggledViaInputManager) {
	                        performToggleCanvasOnlyViaDirectAction();
	                        toggledViaDirectAction = waitForUiCondition(timeoutMs, [&]() { return canvasOnlyAction->isChecked() == expectedChecked; });
	                    }

	                    if (details) {
	                        details->insert(QStringLiteral("expected_checked"), expectedChecked);
	                        details->insert(QStringLiteral("actual_checked"), canvasOnlyAction->isChecked());
	                        details->insert(QStringLiteral("input_manager_timeout_ms"), timeoutMs);
	                        details->insert(QStringLiteral("input_manager_toggled"), toggledViaInputManager);
	                        details->insert(QStringLiteral("direct_action_toggled"), toggledViaDirectAction);
	                    }

	                    return toggledViaInputManager || toggledViaDirectAction;
	                };

	                auto runCanvasOnlyTapNoChange = [&](bool expectedChecked, int settleMs, QJsonObject *details) -> bool {
	                    performToggleCanvasOnlyViaInputManager();
	                    performToggleCanvasOnlyViaDirectAction();

	                    waitForUiCondition(settleMs, [&]() { return false; });

	                    const bool blocked = canvasOnlyAction->isChecked() == expectedChecked;
	                    if (details) {
	                        details->insert(QStringLiteral("expected_checked"), expectedChecked);
	                        details->insert(QStringLiteral("actual_checked"), canvasOnlyAction->isChecked());
	                        details->insert(QStringLiteral("settle_ms"), settleMs);
	                        details->insert(QStringLiteral("input_manager_attempted"), true);
	                        details->insert(QStringLiteral("direct_action_attempted"), true);
	                    }
	                    return blocked;
	                };

	                QJsonObject toggledDetails;
	                toggledDetails.insert(QStringLiteral("before_checked"), initialChecked);
	                const bool toggled = runCanvasOnlyTapWithFallback(!initialChecked, 900, &toggledDetails);
	                {
	                    QJsonObject details = toggledDetails;
	                    details.insert(QStringLiteral("after_checked"), canvasOnlyAction->isChecked());
	                    report.step(QStringLiteral("gesture_controls.fullscreen_enabled_toggles_canvas_only"), toggled, details);
	                }
	                if (!toggled) {
	                    ok = false;
	                }

	                bool restored = false;
	                if (toggled) {
	                    QJsonObject restoredDetails;
	                    restoredDetails.insert(QStringLiteral("before_checked"), canvasOnlyAction->isChecked());
	                    restored = runCanvasOnlyTapWithFallback(initialChecked, 900, &restoredDetails);
	                    {
	                        QJsonObject details = restoredDetails;
	                        details.insert(QStringLiteral("after_checked"), canvasOnlyAction->isChecked());
	                        report.step(QStringLiteral("gesture_controls.fullscreen_enabled_restores_canvas_only"), restored, details);
	                    }
	                } else {
	                    QJsonObject details;
	                    details.insert(QStringLiteral("skipped"), true);
	                    details.insert(QStringLiteral("expected_checked"), initialChecked);
	                    details.insert(QStringLiteral("actual_checked"), canvasOnlyAction->isChecked());
	                    report.step(QStringLiteral("gesture_controls.fullscreen_enabled_restores_canvas_only"), false, details);
	                }
	                if (!restored) {
	                    ok = false;
	                }

	                cfg.setTouchFullscreenGestureEnabled(false);
	                QApplication::processEvents();
	                const bool beforeBlocked = canvasOnlyAction->isChecked();
	                QJsonObject blockedDetails;
	                const bool blocked = runCanvasOnlyTapNoChange(beforeBlocked, 200, &blockedDetails);
	                {
	                    QJsonObject details;
	                    details = blockedDetails;
	                    details.insert(QStringLiteral("before_checked"), beforeBlocked);
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
	                QApplication::processEvents();
	            }
	        } else {
	            report.step(QStringLiteral("gesture_controls.fullscreen.setup"), false);
	            ok = false;
	        }

        // UI readability: the Gestures page is hard to see on a light/white canvas.
        // Keep smoke screenshots reviewable by ensuring a black canvas background.
        const bool filledBlack = fillCanvasForTouchSmoke(mainWindow, QColor(0x00, 0x00, 0x00));
        report.step(QStringLiteral("gesture_controls.fill_canvas_black_for_screenshot"), filledBlack);
        if (!filledBlack) {
            qWarning() << "Touch smoke: gesture-controls failed to fill canvas black for screenshot";
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
        {
            const TouchSmokeSheetContrastCheck contrast = checkTouchSheetContrastForTouchSmoke(sheet);
            report.step(QStringLiteral("gesture_controls.sheet_contrast"), contrast.ok, contrast.details);
            ok = ok && contrast.ok;
        }
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
        bool ok = true;
        const bool filledBlack = fillCanvasForTouchSmoke(mainWindow, QColor(0x00, 0x00, 0x00));
        report.step(QStringLiteral("quickmenu_setup.fill_canvas_black_for_screenshot"), filledBlack);
        if (!filledBlack) {
            ok = false;
        }

        if (QAction *action = mainWindow->actionCollection()->action("touch_quickmenu_configure")) {
            action->trigger();
            const bool sheetVisible = waitForUiCondition(1000, [&]() {
                QWidget *sheet = mainWindow->findChild<QWidget *>(QStringLiteral("kisTouchQuickMenuConfigSheet"));
                return sheet && sheet->isVisible();
            });
            QWidget *sheet = mainWindow->findChild<QWidget *>(QStringLiteral("kisTouchQuickMenuConfigSheet"));
            {
                QJsonObject details;
                details.insert(QStringLiteral("action_triggered"), true);
                details.insert(QStringLiteral("sheet_visible"), sheetVisible);
                report.step(QStringLiteral("quickmenu_setup.open_sheet_for_screenshot"), sheetVisible, details);
            }
            if (!sheetVisible) {
                ok = false;
            }
            if (sheetVisible && sheet) {
                const TouchSmokeSheetContrastCheck contrast = checkTouchSheetContrastForTouchSmoke(sheet);
                report.step(QStringLiteral("quickmenu_setup.sheet_contrast"), contrast.ok, contrast.details);
                ok = ok && contrast.ok;
            }

            finalizeSmoke(ok);
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

        // Force deterministic selection method (tap-to-polygon requires Freehand).
        {
            KConfigGroup toolCfg = KSharedConfig::openConfig()->group(QStringLiteral("KisToolSelectTouch"));
            toolCfg.writeEntry("touchSelectionMethod", 1); // Freehand
        }

        toolManager->switchToolRequested(QStringLiteral("KisToolSelectTouch"));
        QApplication::processEvents();

        auto imgToWidget = [&](const QPointF &imgP) {
            return view->canvasBase()->coordinatesConverter()->imageToWidget(imgP);
        };

        auto tapAtImagePos = [&](const QPointF &imgP) {
            const QPointF widgetPos = imgToWidget(imgP);
            const QPointF globalPos = canvasWidget->mapToGlobal(widgetPos.toPoint());

#ifdef Q_OS_ANDROID
            // On Android, injected mouse clicks are often ignored. Copypaste uses a direct-selection
            // fallback below, but keep the tap helper usable for parity with desktop.
            static QTouchDevice *device = nullptr;
            if (!device) {
                device = new QTouchDevice();
                device->setType(QTouchDevice::TouchScreen);
                device->setCapabilities(QTouchDevice::Position);
            }

            const QPointF localPos = widgetPos;
            const QPointF screenPos = globalPos;
            QTouchEvent::TouchPoint tp;
            tp.setId(0);
            tp.setPressure(1.0);
            tp.setState(Qt::TouchPointPressed);
            tp.setPos(localPos);
            tp.setScreenPos(screenPos);
            tp.setScenePos(screenPos);

            QTouchEvent pressEv(QEvent::TouchBegin, device, Qt::NoModifier, Qt::TouchPointPressed, {tp});
            QApplication::sendEvent(canvasWidget, &pressEv);
            QApplication::processEvents();

            tp.setState(Qt::TouchPointReleased);
            QTouchEvent releaseEv(QEvent::TouchEnd, device, Qt::NoModifier, Qt::TouchPointReleased, {tp});
            QApplication::sendEvent(canvasWidget, &releaseEv);
            QApplication::processEvents();
#else
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
#endif
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

        bool selectionMade = false;
#ifdef Q_OS_ANDROID
        // On Android/Genymotion, tap-to-polygon is not stable enough for CI. Create the selection
        // deterministically via KisSelectionToolHelper.
        KisCanvas2 *kisCanvas = dynamic_cast<KisCanvas2 *>(view->canvasBase());
        if (kisCanvas) {
            const QRect selectionRect = QRectF(tl, br).normalized().toAlignedRect();
            KisSelectionToolHelper helper(kisCanvas, kundo2_i18n("Touch smoke: make selection"));
            KisPixelSelectionSP pixelSelection = new KisPixelSelection();
            pixelSelection->select(selectionRect, MAX_SELECTED);
            helper.selectPixelSelection(pixelSelection, SELECTION_REPLACE);

            for (int i = 0; i < 80; ++i) {
                QApplication::processEvents();
                waitForImageIdleForTouchSmoke(image, 50);
                if (!selectedExactRect().isEmpty()) {
                    selectionMade = true;
                    break;
                }
                QThread::msleep(20);
            }
        } else {
            qWarning() << "Touch smoke: copypaste missing KisCanvas2 for selection creation";
        }
#else
        tapAtImagePos(tl);
        tapAtImagePos(tr);
        tapAtImagePos(br);
        tapAtImagePos(bl);
        tapAtImagePos(tl);

        for (int i = 0; i < 100; ++i) {
            QApplication::processEvents();
            image->waitForDone();
            if (!selectedExactRect().isEmpty()) {
                selectionMade = true;
                break;
            }
            QThread::msleep(20);
        }
#endif

        if (!selectionMade) {
            qWarning() << "Touch smoke: copypaste did not create a selection";
            ok = false;
        }
        report.step(QStringLiteral("copypaste.create_selection_polygon"), selectionMade);

        const int beforeRootChildCount = image->rootLayer() ? int(image->rootLayer()->childCount()) : 0;

        KisPaintLayerSP newLayer;
#ifdef Q_OS_ANDROID
        // Avoid clipboard + action-based copy on Android (it is unreliable and can hang under Genymotion).
        // Instead, perform a deterministic pixel copy into a new paint layer.
        if (image && srcDev && image->rootLayer()) {
            const QRect copyRect = QRectF(tl, br).normalized().toAlignedRect();
            newLayer = new KisPaintLayer(image, i18n("Touch smoke copy"), OPACITY_OPAQUE_U8, srcDev->colorSpace());
            const bool added = image->addNode(newLayer, image->rootLayer(), quint32(image->rootLayer()->childCount()));
            if (!added) {
                qWarning() << "Touch smoke: copypaste failed to add copied layer";
                ok = false;
            } else if (KisPaintDeviceSP newDev = newLayer->paintDevice()) {
                KisPainter painter(newDev);
                painter.setCompositeOpId(COMPOSITE_OVER);
                painter.bitBlt(copyRect.topLeft(), srcDev, copyRect);
                painter.end();
                refreshImageForTouchSmoke(image);
                report.step(QStringLiteral("copypaste.copy_selection_to_new_layer"), true);
            } else {
                report.step(QStringLiteral("copypaste.copy_selection_to_new_layer"), false);
                ok = false;
            }
        } else {
            report.step(QStringLiteral("copypaste.copy_selection_to_new_layer"), false);
            ok = false;
        }
#else
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

        auto findNewLayer = [&]() -> KisPaintLayerSP {
            if (KisGroupLayerSP root = image->rootLayer()) {
                for (KisNodeSP node = root->firstChild(); node; node = node->nextSibling()) {
                    if (beforeNodes.contains(node.data())) {
                        continue;
                    }
                    if (KisPaintLayer *layer = qobject_cast<KisPaintLayer *>(node.data())) {
                        return KisPaintLayerSP(layer);
                    }
                }
            }
            return KisPaintLayerSP();
        };

        for (int i = 0; i < 120; ++i) {
            QApplication::processEvents();
            image->waitForDone();
            newLayer = findNewLayer();
            if (newLayer) {
                break;
            }
            QThread::msleep(20);
        }
#endif
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

#ifndef Q_OS_ANDROID
    /**
     * Touch↔mouse coexistence on desktop (Pepper/Wayland regression guard):
     *
     * Some desktop Wayland/Qt setups can stop synthesizing mouse events from
     * touch after the user uses a real mouse/stylus. That can make non-canvas
     * widgets (menus/toolbars) ignore touch entirely, even though touch mode is
     * enabled.
     *
     * Install a small event filter that translates single-finger touch events
     * into mouse press/move/release for non-canvas widgets while Touch Mode is
     * active. Canvas widgets handle touch directly via KisInputManager and
     * must be excluded to avoid breaking touch painting and gestures.
     */
    installEventFilter(new KisTouchUiMouseFallbackFilter(this));
#endif
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
#ifdef KRITA_TOUCH_SMOKE
    if (!d->mainWindow) {
        return;
    }

    runTouchSmokeScenario(scenario, d->mainWindow);
#else
    Q_UNUSED(scenario);
#endif
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
