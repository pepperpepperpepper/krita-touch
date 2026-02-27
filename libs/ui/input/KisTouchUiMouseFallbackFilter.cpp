/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisTouchUiMouseFallbackFilter.h"

#include <QApplication>
#include <QMouseEvent>
#include <QTouchEvent>
#include <QWidget>

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
                                                   const QPointF &screenPos,
                                                   Qt::KeyboardModifiers modifiers,
                                                   QEvent::Type mouseType,
                                                   Qt::MouseButton button,
                                                   Qt::MouseButtons buttons) const
{
    if (!targetWidget) {
        return;
    }

    const QPoint globalPoint = screenPos.toPoint();
    const QPoint localPoint = targetWidget->mapFromGlobal(globalPoint);
    const QPoint windowPoint = targetWidget->window()->mapFromGlobal(globalPoint);

    QMouseEvent mouseEvent(mouseType,
                           QPointF(localPoint),
                           QPointF(windowPoint),
                           screenPos,
                           button,
                           buttons,
                           modifiers,
                           Qt::MouseEventSynthesizedByQt);
    QApplication::sendEvent(targetWidget, &mouseEvent);
}

namespace
{

static QList<QTouchEvent::TouchPoint> mapTouchPointsToWidget(const QList<QTouchEvent::TouchPoint> &srcPoints, QWidget *targetWidget)
{
    QList<QTouchEvent::TouchPoint> mapped;
    mapped.reserve(srcPoints.size());

    for (const QTouchEvent::TouchPoint &src : srcPoints) {
        QTouchEvent::TouchPoint tp = src;

        const QPoint globalPoint = src.screenPos().toPoint();
        const QPoint localPoint = targetWidget->mapFromGlobal(globalPoint);
        tp.setPos(QPointF(localPoint));

        const QPoint globalStartPoint = src.startScreenPos().toPoint();
        const QPoint localStartPoint = targetWidget->mapFromGlobal(globalStartPoint);
        tp.setStartPos(QPointF(localStartPoint));

        const QPoint globalLastPoint = src.lastScreenPos().toPoint();
        const QPoint localLastPoint = targetWidget->mapFromGlobal(globalLastPoint);
        tp.setLastPos(QPointF(localLastPoint));

        mapped.append(tp);
    }

    return mapped;
}

static void sendMappedTouchEvent(QWidget *targetWidget, QTouchEvent *srcEvent)
{
    if (!targetWidget || !srcEvent) {
        return;
    }

    const QList<QTouchEvent::TouchPoint> mappedPoints =
        mapTouchPointsToWidget(srcEvent->touchPoints(), targetWidget);

    QTouchEvent mappedEvent(srcEvent->type(),
                            srcEvent->device(),
                            srcEvent->modifiers(),
                            srcEvent->touchPointStates(),
                            mappedPoints);
    mappedEvent.setTimestamp(srcEvent->timestamp());

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

    if (!KisConfig(true).touchModeEnabled()) {
        resetEmulation();
        return false;
    }

    QTouchEvent *touchEvent = dynamic_cast<QTouchEvent *>(event);
    if (!touchEvent) {
        resetEmulation();
        return false;
    }

    if (type == QEvent::TouchBegin) {
        resetEmulation();

        const QList<QTouchEvent::TouchPoint> points = touchEvent->touchPoints();
        if (points.isEmpty()) {
            return false;
        }

        const QTouchEvent::TouchPoint &tp = points.first();
        const int tpId = tp.id();
        const QPointF screenPos = tp.screenPos();

        QWidget *targetWidget = QApplication::widgetAt(screenPos.toPoint());

        // If we can't resolve a widget, don't interfere.
        if (!targetWidget) {
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
                               << "screen=" << screenPos;
                }

                sendMappedTouchEvent(targetWidget, touchEvent);
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
                       << "screen=" << screenPos;
        }

        sendMouseEvent(targetWidget,
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
        sendMappedTouchEvent(m_targetWidget.data(), touchEvent);
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
    const QPointF screenPos = tp.screenPos();

    if (m_touchId >= 0 && tpId != m_touchId) {
        return false;
    }

    if (type == QEvent::TouchUpdate) {
        sendMouseEvent(m_targetWidget.data(),
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
                   screenPos,
                   touchEvent->modifiers(),
                   QEvent::MouseButtonRelease,
                   Qt::LeftButton,
                   Qt::NoButton);
    resetEmulation();
    event->accept();
    return true;
}
