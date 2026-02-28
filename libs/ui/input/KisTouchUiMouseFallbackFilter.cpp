/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisTouchUiMouseFallbackFilter.h"

#include <QApplication>
#include <QMouseEvent>
#include <QScopedValueRollback>
#include <QTouchEvent>
#include <QWidget>
#include <QWindow>

#include "kis_config.h"

KisTouchUiMouseFallbackFilter::KisTouchUiMouseFallbackFilter(QObject *parent)
    : QObject(parent)
{
}

bool KisTouchUiMouseFallbackFilter::debugLoggingEnabled()
{
    return qEnvironmentVariableIsSet("KRITA_TOUCH_UI_MOUSE_FALLBACK_DEBUG") &&
           qEnvironmentVariableIntValue("KRITA_TOUCH_UI_MOUSE_FALLBACK_DEBUG") != 0;
}

bool KisTouchUiMouseFallbackFilter::isCanvasWidget(QWidget *w)
{
    return w && (w->inherits("KisOpenGLCanvas2") || w->inherits("KisQPainterCanvas"));
}

bool KisTouchUiMouseFallbackFilter::isInCanvas(QWidget *w)
{
    for (QWidget *p = w; p; p = p->parentWidget()) {
        if (isCanvasWidget(p)) {
            return true;
        }
    }
    return false;
}

bool KisTouchUiMouseFallbackFilter::isTouchNativeWidget(QWidget *w)
{
    // Touch widgets that implement their own touch interactions.
    if (!w) {
        return false;
    }

    if (w->objectName() == QLatin1String("touchColorPickerButton") ||
        w->objectName() == QLatin1String("touchColorDropOverlay")) {
        return true;
    }

    // Keep this generic so we don't depend on widget headers here.
    if (w->inherits("KisTouchColorPickerButton")) {
        return true;
    }

    return false;
}

QWidget *KisTouchUiMouseFallbackFilter::closestCanvasWidget(QWidget *w)
{
    for (QWidget *p = w; p; p = p->parentWidget()) {
        if (isCanvasWidget(p)) {
            return p;
        }
    }
    return nullptr;
}

QWidget *KisTouchUiMouseFallbackFilter::closestTouchNativeWidget(QWidget *w)
{
    for (QWidget *p = w; p; p = p->parentWidget()) {
        if (isTouchNativeWidget(p)) {
            return p;
        }
    }
    return nullptr;
}

void KisTouchUiMouseFallbackFilter::resetEmulation()
{
    m_targetWidget.clear();
    m_touchId = -1;
    m_mode = EmulationMode::None;
}

void KisTouchUiMouseFallbackFilter::sendMouseEvent(QWidget *targetWidget,
                                                   const QPointF &windowPos,
                                                   const QPointF &screenPos,
                                                   Qt::KeyboardModifiers modifiers,
                                                   QEvent::Type mouseType,
                                                   Qt::MouseButton button,
                                                   Qt::MouseButtons buttons) const
{
    if (!targetWidget) {
        return;
    }

    QWidget *windowWidget = targetWidget->window();
    if (!windowWidget) {
        return;
    }

    const QPoint localPoint = targetWidget->mapFrom(windowWidget, windowPos.toPoint());

    QMouseEvent mouseEvent(mouseType,
                           QPointF(localPoint),
                           windowPos,
                           screenPos,
                           button,
                           buttons,
                           modifiers,
                           Qt::MouseEventSynthesizedByQt);
    QApplication::sendEvent(targetWidget, &mouseEvent);
}

namespace
{

static QWidget *topLevelWidgetForWatched(QObject *watched)
{
    if (QWidget *watchedWidget = qobject_cast<QWidget *>(watched)) {
        return watchedWidget->window();
    }

    QWindow *watchedWindow = qobject_cast<QWindow *>(watched);
    if (!watchedWindow) {
        return nullptr;
    }

    QWindow *rootWindow = watchedWindow;
    while (rootWindow && rootWindow->parent()) {
        rootWindow = rootWindow->parent();
    }

    const auto topLevels = QApplication::topLevelWidgets();
    for (QWidget *tl : topLevels) {
        if (!tl) {
            continue;
        }
        QWindow *handle = tl->windowHandle();
        if (handle == watchedWindow || handle == rootWindow) {
            return tl;
        }
    }

    QWidget *active = QApplication::activeWindow();
    return active ? active->window() : nullptr;
}

static QWidget *resolveWidgetAtTouchPoint(QObject *watched,
                                         const QTouchEvent::TouchPoint &tp,
                                         QWidget **outTopLevelWidget)
{
    const bool debug =
        qEnvironmentVariableIsSet("KRITA_TOUCH_UI_MOUSE_FALLBACK_DEBUG") &&
        qEnvironmentVariableIntValue("KRITA_TOUCH_UI_MOUSE_FALLBACK_DEBUG") != 0;

    if (QWidget *watchedWidget = qobject_cast<QWidget *>(watched)) {
        if (outTopLevelWidget) {
            *outTopLevelWidget = watchedWidget->window();
        }
        if (QWidget *child = watchedWidget->childAt(tp.pos().toPoint())) {
            return child;
        }
        return watchedWidget;
    }

    QWindow *watchedWindow = qobject_cast<QWindow *>(watched);

    QWidget *topLevelWidget = topLevelWidgetForWatched(watched);
    if (outTopLevelWidget) {
        *outTopLevelWidget = topLevelWidget;
    }

    if (topLevelWidget) {
        auto widgetAtWindowPoint = [&](const QPoint &windowPoint) -> QWidget * {
            if (QWidget *child = topLevelWidget->childAt(windowPoint)) {
                return child;
            }
            if (topLevelWidget->rect().contains(windowPoint)) {
                return topLevelWidget;
            }
            return nullptr;
        };

        // When the touch sequence is delivered to a QWindow (e.g. QWidgetWindow on Wayland),
        // TouchPoint::pos() is window-local in QWindow coordinates. Prefer mapping it via
        // QWindow::mapToGlobal() and QWidget::mapFromGlobal() so we don't depend on the
        // event-provided screenPos/scenePos fields (which can be unreliable).
        if (watchedWindow) {
            const QPoint globalFromPos = watchedWindow->mapToGlobal(tp.pos().toPoint());
            const QPoint windowPoint = topLevelWidget->mapFromGlobal(globalFromPos);
            if (QWidget *w = widgetAtWindowPoint(windowPoint)) {
                return w;
            }
        }

        // Fallback: try interpreting pos()/scenePos() directly as window-local points.
        const QPoint posPoint = tp.pos().toPoint();
        const QPoint scenePoint = tp.scenePos().toPoint();
        if (QWidget *w = widgetAtWindowPoint(posPoint)) {
            return w;
        }
        if (scenePoint != posPoint) {
            if (QWidget *w = widgetAtWindowPoint(scenePoint)) {
                return w;
            }
        }
    }

    if (watchedWindow) {
        const QPoint globalFromPos = watchedWindow->mapToGlobal(tp.pos().toPoint());
        if (QWidget *w = QApplication::widgetAt(globalFromPos)) {
            if (outTopLevelWidget) {
                *outTopLevelWidget = w->window();
            }
            return w;
        }
    }

    if (QWidget *w = QApplication::widgetAt(tp.screenPos().toPoint())) {
        if (outTopLevelWidget) {
            *outTopLevelWidget = w->window();
        }
        return w;
    }

    if (debug) {
        qWarning() << "Touch UI mouse fallback: resolve failed"
                   << "watched=" << (watched ? watched->metaObject()->className() : "null")
                   << (watched ? watched->objectName() : QString())
                   << "pos=" << tp.pos()
                   << "scene=" << tp.scenePos()
                   << "screen=" << tp.screenPos()
                   << "topLevelNull=" << (topLevelWidget == nullptr)
                   << "topLevelRect=" << (topLevelWidget ? topLevelWidget->rect() : QRect());
    }

    return nullptr;
}

static QList<QTouchEvent::TouchPoint> mapTouchPointsToWidget(const QList<QTouchEvent::TouchPoint> &srcPoints,
                                                             QWidget *targetWidget,
                                                             QWidget *topLevelWidget,
                                                             QWindow *sourceWindow)
{
    QList<QTouchEvent::TouchPoint> mapped;
    mapped.reserve(srcPoints.size());

    QWidget *windowWidget = topLevelWidget ? topLevelWidget : (targetWidget ? targetWidget->window() : nullptr);

    for (const QTouchEvent::TouchPoint &src : srcPoints) {
        QTouchEvent::TouchPoint tp = src;

        QPointF windowPos;
        QPointF startWindowPos;
        QPointF lastWindowPos;
        QPointF screenPos;
        QPointF startScreenPos;
        QPointF lastScreenPos;

        if (sourceWindow && windowWidget) {
            const QPoint globalPos = sourceWindow->mapToGlobal(src.pos().toPoint());
            const QPoint globalStartPos = sourceWindow->mapToGlobal(src.startPos().toPoint());
            const QPoint globalLastPos = sourceWindow->mapToGlobal(src.lastPos().toPoint());

            windowPos = QPointF(windowWidget->mapFromGlobal(globalPos));
            startWindowPos = QPointF(windowWidget->mapFromGlobal(globalStartPos));
            lastWindowPos = QPointF(windowWidget->mapFromGlobal(globalLastPos));

            screenPos = QPointF(globalPos);
            startScreenPos = QPointF(globalStartPos);
            lastScreenPos = QPointF(globalLastPos);
        } else {
            // QWidget delivery: scenePos is window-local.
            windowPos = src.scenePos();
            startWindowPos = src.startScenePos();
            lastWindowPos = src.lastScenePos();

            if (windowWidget) {
                screenPos = QPointF(windowWidget->mapToGlobal(windowPos.toPoint()));
                startScreenPos = QPointF(windowWidget->mapToGlobal(startWindowPos.toPoint()));
                lastScreenPos = QPointF(windowWidget->mapToGlobal(lastWindowPos.toPoint()));
            } else {
                screenPos = src.screenPos();
                startScreenPos = src.startScreenPos();
                lastScreenPos = src.lastScreenPos();
            }
        }

        auto mapToLocal = [&](const QPointF &windowPos, const QPointF &screenPos) -> QPointF {
            if (windowWidget) {
                return QPointF(targetWidget->mapFrom(windowWidget, windowPos.toPoint()));
            }
            return QPointF(targetWidget->mapFromGlobal(screenPos.toPoint()));
        };

        tp.setPos(mapToLocal(windowPos, screenPos));
        tp.setStartPos(mapToLocal(startWindowPos, startScreenPos));
        tp.setLastPos(mapToLocal(lastWindowPos, lastScreenPos));

        tp.setScenePos(windowPos);
        tp.setStartScenePos(startWindowPos);
        tp.setLastScenePos(lastWindowPos);

        tp.setScreenPos(screenPos);
        tp.setStartScreenPos(startScreenPos);
        tp.setLastScreenPos(lastScreenPos);

        mapped.append(tp);
    }

    return mapped;
}

static void sendMappedTouchEvent(QWidget *targetWidget,
                                 QTouchEvent *srcEvent,
                                 QWidget *topLevelWidget,
                                 QWindow *sourceWindow)
{
    if (!targetWidget || !srcEvent) {
        return;
    }

    const QList<QTouchEvent::TouchPoint> mappedPoints =
        mapTouchPointsToWidget(srcEvent->touchPoints(), targetWidget, topLevelWidget, sourceWindow);

    QTouchEvent mappedEvent(srcEvent->type(),
                            srcEvent->device(),
                            srcEvent->modifiers(),
                            srcEvent->touchPointStates(),
                            mappedPoints);
    mappedEvent.setTimestamp(srcEvent->timestamp());

    if (qEnvironmentVariableIsSet("KRITA_TOUCH_UI_MOUSE_FALLBACK_DEBUG") &&
        qEnvironmentVariableIntValue("KRITA_TOUCH_UI_MOUSE_FALLBACK_DEBUG") != 0) {
        qWarning() << "Touch UI mouse fallback: send mapped touch"
                   << "type=" << srcEvent->type()
                   << "points=" << mappedPoints.size()
                   << "target=" << targetWidget->metaObject()->className()
                   << targetWidget->objectName();
    }

    QApplication::sendEvent(targetWidget, &mappedEvent);
}

static bool watchedIsWidgetInSubtree(QObject *watched, QWidget *targetWidget)
{
    QWidget *watchedWidget = qobject_cast<QWidget *>(watched);
    if (!watchedWidget || !targetWidget) {
        return false;
    }

    if (watchedWidget == targetWidget) {
        return true;
    }

    return targetWidget->isAncestorOf(watchedWidget);
}

}

bool KisTouchUiMouseFallbackFilter::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type type = event ? event->type() : QEvent::None;
    if (type != QEvent::TouchBegin &&
        type != QEvent::TouchUpdate &&
        type != QEvent::TouchEnd &&
        type != QEvent::TouchCancel) {
        return false;
    }

    // We forward touch events by sending a mapped QTouchEvent directly to the widget that
    // needs it (canvas or touch-native widgets). That send path will re-enter this filter;
    // ignore forwarded events to avoid resetting state mid-sequence or recursively forwarding.
    if (m_forwardingDepth > 0) {
        if (debugLoggingEnabled()) {
            qWarning() << "Touch UI mouse fallback: skip forwarded"
                       << "watched=" << (watched ? watched->metaObject()->className() : "null")
                       << (watched ? watched->objectName() : QString())
                       << "type=" << type;
        }
        return false;
    }

    if (!KisConfig(true).touchModeEnabled()) {
        resetEmulation();
        return false;
    }

    QTouchEvent *touchEvent = dynamic_cast<QTouchEvent *>(event);
    if (!touchEvent) {
        resetEmulation();
        return false;
    }

    if (debugLoggingEnabled() && type != QEvent::TouchBegin) {
        qWarning() << "Touch UI mouse fallback: touch"
                   << "watched=" << (watched ? watched->metaObject()->className() : "null")
                   << (watched ? watched->objectName() : QString())
                   << "type=" << type
                   << "mode=" << static_cast<int>(m_mode)
                   << "targetNull=" << m_targetWidget.isNull();
    }

    if (type == QEvent::TouchBegin) {
        resetEmulation();

        const QList<QTouchEvent::TouchPoint> points = touchEvent->touchPoints();
        if (points.isEmpty()) {
            return false;
        }

        const QTouchEvent::TouchPoint &tp = points.first();
        const int tpId = tp.id();
        const bool watchedIsWindow = qobject_cast<QWindow *>(watched) != nullptr;
        QWindow *sourceWindow = watchedIsWindow ? qobject_cast<QWindow *>(watched) : nullptr;

        QWidget *topLevelWidget = nullptr;
        QWidget *targetWidget = resolveWidgetAtTouchPoint(watched, tp, &topLevelWidget);

        const QPointF screenPos = [&]() -> QPointF {
            if (sourceWindow) {
                return QPointF(sourceWindow->mapToGlobal(tp.pos().toPoint()));
            }
            if (topLevelWidget) {
                return QPointF(topLevelWidget->mapToGlobal(tp.scenePos().toPoint()));
            }
            return tp.screenPos();
        }();

        const QPointF windowPos = [&]() -> QPointF {
            if (sourceWindow) {
                const QPoint global = sourceWindow->mapToGlobal(tp.pos().toPoint());
                if (topLevelWidget) {
                    return QPointF(topLevelWidget->mapFromGlobal(global));
                }
                return tp.pos();
            }
            return tp.scenePos();
        }();

        if (debugLoggingEnabled()) {
            qWarning() << "Touch UI mouse fallback: raw begin"
                       << "watched=" << (watched ? watched->metaObject()->className() : "null")
                       << (watched ? watched->objectName() : QString())
                       << "points=" << points.size()
                       << "id=" << tpId
                       << "pos=" << tp.pos()
                       << "scene=" << tp.scenePos()
                       << "screen=" << tp.screenPos()
                       << "mappedWindow=" << windowPos
                       << "mappedScreen=" << screenPos;
        }

        // If we can't resolve a widget, don't interfere.
        if (!targetWidget) {
            if (debugLoggingEnabled()) {
                qWarning() << "Touch UI mouse fallback: begin no target"
                           << "watched=" << (watched ? watched->metaObject()->className() : "null")
                           << (watched ? watched->objectName() : QString())
                           << "pos=" << tp.pos()
                           << "scene=" << tp.scenePos()
                           << "screen=" << tp.screenPos()
                           << "topLevelNull=" << (topLevelWidget == nullptr)
                           << "topLevelRect=" << (topLevelWidget ? topLevelWidget->rect() : QRect());
            }
            resetEmulation();
            return false;
        }

        if (isInCanvas(targetWidget)) {
            QWidget *canvasWidget = closestCanvasWidget(targetWidget);
            if (canvasWidget) {
                targetWidget = canvasWidget;
            }
        } else if (isTouchNativeWidget(targetWidget)) {
            QWidget *nativeWidget = closestTouchNativeWidget(targetWidget);
            if (nativeWidget) {
                targetWidget = nativeWidget;
            }
        }

        if (isInCanvas(targetWidget) || isTouchNativeWidget(targetWidget)) {
            // Touch painting and touch-native widgets require real touch events.
            //
            // On Wayland/Qt, touch can arrive on a QWindow (QWidgetWindow) instead of the actual
            // widget hierarchy; in that case the canvas/widgets never see the event. Retarget
            // the touch sequence to the widget under the touch to keep touch painting stable.
            if (!watchedIsWidgetInSubtree(watched, targetWidget)) {
                m_targetWidget = targetWidget;
                m_mode = EmulationMode::TouchForward;

                if (debugLoggingEnabled()) {
                    qWarning() << "Touch UI mouse fallback: forward begin"
                               << "watched=" << (watched ? watched->metaObject()->className() : "null")
                               << (watched ? watched->objectName() : QString())
                               << "target=" << targetWidget->metaObject()->className()
                               << targetWidget->objectName()
                               << "window=" << windowPos
                               << "screen=" << screenPos;
                }

                const QScopedValueRollback<int> forwardingGuard(m_forwardingDepth, m_forwardingDepth + 1);
                sendMappedTouchEvent(targetWidget, touchEvent, topLevelWidget, sourceWindow);
                event->accept();
                return true;
            }

            resetEmulation();
            return false;
        }

        if (points.size() != 1) {
            resetEmulation();
            return false;
        }

        m_targetWidget = targetWidget;
        m_touchId = tpId;
        m_mode = EmulationMode::Mouse;

        if (debugLoggingEnabled()) {
            qWarning() << "Touch UI mouse fallback: begin"
                       << "watched=" << (watched ? watched->metaObject()->className() : "null")
                       << (watched ? watched->objectName() : QString())
                       << "target=" << targetWidget->metaObject()->className()
                       << targetWidget->objectName()
                       << "window=" << windowPos
                       << "screen=" << screenPos;
        }

        sendMouseEvent(targetWidget,
                       windowPos,
                       screenPos,
                       touchEvent->modifiers(),
                       QEvent::MouseButtonPress,
                       Qt::LeftButton,
                       Qt::LeftButton);
        event->accept();
        return true;
    }

    if (m_mode == EmulationMode::None) {
        return false;
    }

    if (m_targetWidget.isNull()) {
        resetEmulation();
        return false;
    }

    if (m_mode == EmulationMode::TouchForward) {
        const QScopedValueRollback<int> forwardingGuard(m_forwardingDepth, m_forwardingDepth + 1);
        sendMappedTouchEvent(m_targetWidget.data(),
                             touchEvent,
                             m_targetWidget->window(),
                             qobject_cast<QWindow *>(watched));
        if (type == QEvent::TouchEnd || type == QEvent::TouchCancel) {
            resetEmulation();
        }
        event->accept();
        return true;
    }

    // Mouse emulation mode: single-finger only.
    const QList<QTouchEvent::TouchPoint> points = touchEvent->touchPoints();
    if (points.size() != 1) {
        resetEmulation();
        return false;
    }

    const QTouchEvent::TouchPoint &tp = points.first();
    const int tpId = tp.id();
    QWindow *sourceWindow = qobject_cast<QWindow *>(watched);
    const QPointF screenPos = [&]() -> QPointF {
        if (sourceWindow) {
            return QPointF(sourceWindow->mapToGlobal(tp.pos().toPoint()));
        }
        QWidget *topLevelWidget = m_targetWidget ? m_targetWidget->window() : nullptr;
        if (topLevelWidget) {
            return QPointF(topLevelWidget->mapToGlobal(tp.scenePos().toPoint()));
        }
        return tp.screenPos();
    }();

    const QPointF windowPos = [&]() -> QPointF {
        if (sourceWindow) {
            const QPoint global = sourceWindow->mapToGlobal(tp.pos().toPoint());
            QWidget *topLevelWidget = m_targetWidget ? m_targetWidget->window() : nullptr;
            if (topLevelWidget) {
                return QPointF(topLevelWidget->mapFromGlobal(global));
            }
            return tp.pos();
        }
        return tp.scenePos();
    }();

    if (m_touchId >= 0 && tpId != m_touchId) {
        return false;
    }

    if (type == QEvent::TouchUpdate) {
        sendMouseEvent(m_targetWidget.data(),
                       windowPos,
                       screenPos,
                       touchEvent->modifiers(),
                       QEvent::MouseMove,
                       Qt::NoButton,
                       Qt::LeftButton);
        event->accept();
        return true;
    }

    // TouchEnd / TouchCancel.
    sendMouseEvent(m_targetWidget.data(),
                   windowPos,
                   screenPos,
                   touchEvent->modifiers(),
                   QEvent::MouseButtonRelease,
                   Qt::LeftButton,
                   Qt::NoButton);
    resetEmulation();
    event->accept();
    return true;
}
