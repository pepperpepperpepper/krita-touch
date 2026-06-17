/*
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisTouchTransformGesture.h"

#include <QLineF>
#include <QMetaObject>
#include <QObject>
#include <QTouchEvent>

#include <KoToolManager.h>

#include <kis_canvas2.h>
#include <kis_config.h>

#include "kis_input_manager.h"

void KisTouchTransformGesture::reset()
{
    m_tool.clear();
    m_active = false;
}

bool KisTouchTransformGesture::maybeBegin(KisInputManager *inputManager, QTouchEvent *touchEvent)
{
    reset();

    if (!inputManager || !touchEvent || touchEvent->touchPoints().count() <= 1) {
        return false;
    }

    KisConfig cfg(true);
    if (!cfg.touchModeEnabled()) {
        return false;
    }

    KoToolManager *toolManager = KoToolManager::instance();
    if (!toolManager || toolManager->activeToolId() != QStringLiteral("KisToolTransform")) {
        return false;
    }

    QObject *toolObj = dynamic_cast<QObject *>(toolManager->toolById(inputManager->canvas(), toolManager->activeToolId()));
    if (!toolObj) {
        return false;
    }

    const QPointF p0 = touchEvent->touchPoints().at(0).pos();
    const QPointF p1 = touchEvent->touchPoints().at(1).pos();

    auto hitTestWidgetPoint = [&](const QPointF &widgetPoint, bool &hitOut) -> bool {
        hitOut = false;
        return QMetaObject::invokeMethod(toolObj, "touchTransformHitTest", Qt::DirectConnection,
                                         Q_RETURN_ARG(bool, hitOut),
                                         Q_ARG(QPointF, widgetPoint));
    };

    const QPointF centerWidget = (p0 + p1) * 0.5;

    bool hit0 = false;
    bool hit1 = false;
    bool hitCenter = false;
    const bool canHit0 = hitTestWidgetPoint(p0, hit0);
    const bool canHit1 = hitTestWidgetPoint(p1, hit1);
    const bool canHitCenter = hitTestWidgetPoint(centerWidget, hitCenter);

    int hits = 0;
    if (canHit0 && hit0) {
        ++hits;
    }
    if (canHit1 && hit1) {
        ++hits;
    }
    if (canHitCenter && hitCenter) {
        ++hits;
    }

    // Require at least two "inside bounds" confirmations so that pinch gestures
    // around the selection (both fingers outside, center inside) still navigate
    // the canvas rather than transforming content.
    if (hits < 2) {
        return false;
    }

    bool began = false;
    const bool invokedBegin =
        QMetaObject::invokeMethod(toolObj, "touchTransformGestureBegin", Qt::DirectConnection,
                                  Q_RETURN_ARG(bool, began),
                                  Q_ARG(QPointF, p0),
                                  Q_ARG(QPointF, p1));

    if (invokedBegin && began) {
        m_tool = toolObj;
        m_active = true;
        return true;
    }

    return false;
}

bool KisTouchTransformGesture::isActive() const
{
    return m_active && m_tool;
}

bool KisTouchTransformGesture::handleUpdate(QTouchEvent *touchEvent)
{
    if (!isActive()) {
        return false;
    }

    if (touchEvent && touchEvent->touchPoints().count() > 1) {
        const QTouchEvent::TouchPoint tp0 = touchEvent->touchPoints().at(0);
        const QTouchEvent::TouchPoint tp1 = touchEvent->touchPoints().at(1);

        if (tp0.state() != Qt::TouchPointReleased && tp1.state() != Qt::TouchPointReleased) {
            const QPointF p0 = tp0.pos();
            const QPointF p1 = tp1.pos();

            if ((p0 - p1).manhattanLength() >= 10) {
                QMetaObject::invokeMethod(m_tool, "touchTransformGestureUpdate", Qt::DirectConnection,
                                          Q_ARG(QPointF, p0),
                                          Q_ARG(QPointF, p1));
            }
        }
    }

    // Active: the gesture owns this event regardless of whether a move was sent.
    return true;
}

bool KisTouchTransformGesture::end()
{
    const bool wasActive = isActive();
    if (wasActive) {
        QMetaObject::invokeMethod(m_tool, "touchTransformGestureEnd", Qt::DirectConnection);
    }
    m_tool.clear();
    m_active = false;
    return wasActive;
}
