/*
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_TOUCH_TRANSFORM_GESTURE_H
#define KIS_TOUCH_TRANSFORM_GESTURE_H

#include <QPointer>

class KisInputManager;
class QObject;
class QTouchEvent;

/**
 * Shared "let the active Transform tool consume a two-finger gesture" passthrough.
 *
 * This logic was copy-pasted into KisZoomAction, KisRotateCanvasAction and
 * KisZoomAndRotateAction: when KisToolTransform is the active tool and a
 * two-finger gesture lands on (enough of) the transform handles, the gesture
 * should drive the transform tool instead of navigating the canvas. Each of the
 * three navigation actions detects this in begin(), forwards moves in
 * inputEvent() and finishes it in end(), via QMetaObject::invokeMethod on the
 * tool (touchTransformHitTest / touchTransformGestureBegin/Update/End, defined
 * on KisToolTransform).
 *
 * This helper owns the small bit of state ({tool, active}) and the three steps.
 * It deliberately returns plain bools and never alters the caller's control flow
 * itself: each action keeps its own surrounding behavior (early-returning,
 * falling through to its zoom/rotate math, clearing quick-pinch state, ...) by
 * branching on those bools. That is what makes the extraction behavior-preserving.
 *
 * Not a QObject; no signals/slots — just shared state + logic.
 */
class KisTouchTransformGesture
{
public:
    /// Drop any in-progress gesture state WITHOUT notifying the tool. Call at the
    /// start of an action's begin() (replaces the manual tool.clear()/active=false).
    void reset();

    /// Hit-test the two touch points and their midpoint against the transform
    /// handles; if at least two land inside, begin a touch-transform gesture on
    /// KisToolTransform. Always reset()s first. Returns true iff a gesture became
    /// active. A no-op returning false unless touch mode is enabled, the event has
    /// >= 2 touch points, and KisToolTransform is the active tool.
    bool maybeBegin(KisInputManager *inputManager, QTouchEvent *touchEvent);

    /// True while a touch-transform gesture is in progress.
    bool isActive() const;

    /// When active, forward the two-finger move to the tool. A released point, or
    /// points closer than 10px apart, are swallowed (no update is sent). Returns
    /// true iff a gesture is active — in which case the caller should early-return,
    /// because the gesture has consumed the event.
    bool handleUpdate(QTouchEvent *touchEvent);

    /// When active, end the gesture on the tool and clear state. Returns the prior
    /// active state, so the caller can branch (e.g. skip endCanvasRotation()).
    bool end();

private:
    QPointer<QObject> m_tool;
    bool m_active {false};
};

#endif // KIS_TOUCH_TRANSFORM_GESTURE_H
