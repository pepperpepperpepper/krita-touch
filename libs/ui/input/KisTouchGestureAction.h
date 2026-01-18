/*
 * SPDX-FileCopyrightText: 2022 Sharaf Zaman <shzam@sdf.org>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef __KISTOUCHGESTUREACTION_H_
#define __KISTOUCHGESTUREACTION_H_

#include "kis_abstract_input_action.h"

#include <QPointF>

class KisTouchGestureAction : public KisAbstractInputAction
{
public:
    enum Shortcut {
        UndoActionShortcut,
        RedoActionShortcut,
        ToggleCanvasOnlyShortcut,
        ToggleEraserMode,
        ResetDisplay,
        PreviousPresetShortcut,
        ColorSampler,
        Deselect,
        NextLayer,
        PreviousLayer,
        FreehandBrush,
        KisToolSelectContiguous,
        KisToolMove,
        KisToolTransform,
        ToggleEraserPreset,
        CopyPasteOverlay,
    };

    KisTouchGestureAction();

    void begin(int shortcut, QEvent *event) override;
    void inputEvent(QEvent *event) override;
    void end(QEvent *event) override;

    int priority() const override;

private:
    int m_shortcut{-1};
    bool m_triggeredThisGesture{false};

    // Used for classifying 3-finger drag gestures into "swipe down" (clipboard)
    // vs "scrub" (clear layer). Stored in global coordinates, averaged across points.
    QPointF m_gestureStartPos;
    QPointF m_gestureLastPos;
    qreal m_gestureAccumAbsDx{0.0};
    qreal m_gestureAccumAbsDy{0.0};
    int m_gestureLastXSign{0};
    int m_gestureXDirectionChanges{0};
};

#endif // __KISTOUCHGESTUREACTION_H_
