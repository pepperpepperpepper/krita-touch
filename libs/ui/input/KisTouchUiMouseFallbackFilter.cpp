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

void KisTouchUiMouseFallbackFilter::resetEmulation()
{
    m_targetWidget.clear();
    m_touchId = -1;
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

    const QList<QTouchEvent::TouchPoint> points = touchEvent->touchPoints();
    if (points.size() != 1) {
        resetEmulation();
        return false;
    }

    const QTouchEvent::TouchPoint &tp = points.first();
    const int tpId = tp.id();
    const QPointF screenPos = tp.screenPos();

    if (type == QEvent::TouchBegin) {
        QWidget *targetWidget = QApplication::widgetAt(screenPos.toPoint());

        // If we can't resolve a widget, don't interfere.
        if (!targetWidget) {
            resetEmulation();
            return false;
        }

        if (isInCanvas(targetWidget) || isTouchNativeWidget(targetWidget)) {
            resetEmulation();
            return false;
        }

        m_targetWidget = targetWidget;
        m_touchId = tpId;

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

    if (m_targetWidget.isNull() || (m_touchId >= 0 && tpId != m_touchId)) {
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

