/*
 * SPDX-FileCopyrightText: 2022 Sharaf Zaman <shzam@sdf.org>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "KisTouchGestureAction.h"

#include <cmath>

#include <KisMainWindow.h>
#include <KisPart.h>
#include <QAction>
#include <QTouchEvent>
#include <kactioncollection.h>
#include <kis_config.h>
#include <kis_debug.h>

namespace {

QPointF averageDeltaFromStart(const QTouchEvent *event, int *outCount = nullptr)
{
    QPointF result;
    int count = 0;

    if (!event) {
        if (outCount) {
            *outCount = 0;
        }
        return result;
    }

    Q_FOREACH (const QTouchEvent::TouchPoint &point, event->touchPoints()) {
        const QPointF delta = point.pos() - point.startPos();
        result += delta;
        count++;
    }

    if (outCount) {
        *outCount = count;
    }

    return count > 0 ? (result / count) : QPointF();
}

QPointF averagePos(const QTouchEvent *event, int *outCount = nullptr)
{
    QPointF result;
    int count = 0;

    if (!event) {
        if (outCount) {
            *outCount = 0;
        }
        return result;
    }

    Q_FOREACH (const QTouchEvent::TouchPoint &point, event->touchPoints()) {
        result += point.pos();
        count++;
    }

    if (outCount) {
        *outCount = count;
    }

    return count > 0 ? (result / count) : QPointF();
}

} // namespace

KisTouchGestureAction::KisTouchGestureAction()
    : KisAbstractInputAction("Touch Gestures")
{
    setName(i18n("Touch Gestures"));
    setDescription(i18n("The Touch Gestures actions launch a single action for the specified gesture"));

    QHash<QString, int> shortcuts;
    shortcuts.insert(i18n("Undo"), UndoActionShortcut);
    shortcuts.insert(i18n("Redo"), RedoActionShortcut);
    shortcuts.insert(i18n("Toggle Canvas Only Mode"), ToggleCanvasOnlyShortcut);
    shortcuts.insert(i18n("Toggle Eraser"), ToggleEraserMode);
    shortcuts.insert(i18n("Toggle Eraser Preset"), ToggleEraserPreset);
    shortcuts.insert(i18n("Reset Display"), ResetDisplay);
    shortcuts.insert(i18n("Toggle Previous Brush Preset"), PreviousPresetShortcut);
    shortcuts.insert(i18n("Color Sampler"), ColorSampler);
    shortcuts.insert(i18n("Deselect"), Deselect);
    shortcuts.insert(i18n("Activate Next Layer"), NextLayer);
    shortcuts.insert(i18n("Activate Previous Layer"), PreviousLayer);
    shortcuts.insert(i18n("Activate Freehand Brush Tool"), FreehandBrush);
    shortcuts.insert(i18n("Freehand Selection Tool"), KisToolSelectContiguous);
    shortcuts.insert(i18n("Activate Move Tool"), KisToolMove);
    shortcuts.insert(i18n("Activate Transform Tool"), KisToolTransform);
    shortcuts.insert(i18n("Copy/Paste / Clear Layer"), CopyPasteOverlay);
    setShortcutIndexes(shortcuts);
}

void KisTouchGestureAction::begin(int shortcut, QEvent *event)
{
    m_shortcut = shortcut;
    m_triggeredThisGesture = false;

    m_gestureStartPos = QPointF();
    m_gestureLastPos = QPointF();
    m_gestureAccumAbsDx = 0.0;
    m_gestureAccumAbsDy = 0.0;
    m_gestureLastXSign = 0;
    m_gestureXDirectionChanges = 0;

    if (m_shortcut == CopyPasteOverlay) {
        if (const QTouchEvent *touchEvent = dynamic_cast<const QTouchEvent *>(event)) {
            int pointCount = 0;
            const QPointF start = averagePos(touchEvent, &pointCount);
            if (pointCount > 0) {
                m_gestureStartPos = start;
                m_gestureLastPos = start;
            }
        }
    }
}

void KisTouchGestureAction::inputEvent(QEvent *event)
{
    if (m_shortcut != CopyPasteOverlay || m_triggeredThisGesture) {
        KisAbstractInputAction::inputEvent(event);
        return;
    }

    const QTouchEvent *touchEvent = dynamic_cast<const QTouchEvent *>(event);
    if (!touchEvent) {
        KisAbstractInputAction::inputEvent(event);
        return;
    }

    int pointCount = 0;
    const QPointF pos = averagePos(touchEvent, &pointCount);
    if (pointCount <= 0) {
        return;
    }

    if (m_gestureLastPos.isNull()) {
        m_gestureStartPos = pos;
        m_gestureLastPos = pos;
        return;
    }

    const QPointF delta = pos - m_gestureLastPos;
    m_gestureAccumAbsDx += std::abs(delta.x());
    m_gestureAccumAbsDy += std::abs(delta.y());

    constexpr qreal kMinTrackStepPx = 3.0;
    if (std::abs(delta.x()) > std::abs(delta.y()) && std::abs(delta.x()) >= kMinTrackStepPx) {
        const int sign = delta.x() > 0.0 ? 1 : -1;
        if (m_gestureLastXSign != 0 && sign != m_gestureLastXSign) {
            m_gestureXDirectionChanges += 1;
        }
        m_gestureLastXSign = sign;
    }

    m_gestureLastPos = pos;
}

void KisTouchGestureAction::end(QEvent *event)
{
    Q_UNUSED(event)

    if (m_triggeredThisGesture) {
        return;
    }

    KisConfig cfg(true);

    QString actionName;
    switch (m_shortcut) {
    case UndoActionShortcut:
        if (!cfg.touchUndoRedoGesturesEnabled()) {
            return;
        }
        actionName = QStringLiteral("edit_undo");
        break;
    case RedoActionShortcut:
        if (!cfg.touchUndoRedoGesturesEnabled()) {
            return;
        }
        actionName = QStringLiteral("edit_redo");
        break;
    case ToggleCanvasOnlyShortcut:
        if (!cfg.touchFullscreenGestureEnabled()) {
            return;
        }
        actionName = QStringLiteral("view_show_canvas_only");
        break;
    case ToggleEraserMode:
        actionName = QStringLiteral("erase_action");
        break;
    case ResetDisplay:
        actionName = QStringLiteral("reset_display");
        break;
    case PreviousPresetShortcut:
        actionName = QStringLiteral("previous_preset");
        break;
    case ColorSampler:
        actionName = QStringLiteral("KritaSelected/KisToolColorSampler");
        break;
    case Deselect:
        actionName = QStringLiteral("deselect");
        break;
    case NextLayer:
        actionName = QStringLiteral("activateNextLayer");
        break;
    case PreviousLayer:
        actionName = QStringLiteral("activatePreviousLayer");
        break;
    case FreehandBrush:
        actionName = QStringLiteral("KritaShape/KisToolBrush");
        break;
    case KisToolSelectContiguous:
        actionName = QStringLiteral("KisToolSelectOutline");
        break;
    case KisToolMove:
        actionName = QStringLiteral("KritaTransform/KisToolMove");
        break;
    case KisToolTransform:
        actionName = QStringLiteral("KisToolTransform");
        break;
    case ToggleEraserPreset:
        actionName = QStringLiteral("eraser_preset_action");
        break;
    case CopyPasteOverlay:
        if (!cfg.touchClipboardGestureEnabled() && !cfg.touchClearLayerGestureEnabled()) {
            return;
        }
        // Procreate uses a 3-finger swipe down for the clipboard menu, and a 3-finger
        // side-to-side "scrub" motion to clear the current layer. Krita's gesture
        // recognizer doesn't encode direction, so we classify by movement.
        constexpr qreal kMinSwipePx = 40.0;
        constexpr qreal kDominance = 1.2;
        constexpr qreal kMinScrubAbsPx = 160.0;
        constexpr int kMinScrubDirectionChanges = 1;

        QPointF netDelta;
        if (!m_gestureStartPos.isNull() && !m_gestureLastPos.isNull()) {
            netDelta = m_gestureLastPos - m_gestureStartPos;
        } else if (const QTouchEvent *touchEvent = dynamic_cast<const QTouchEvent *>(event)) {
            netDelta = averageDeltaFromStart(touchEvent);
        }

        const qreal dx = netDelta.x();
        const qreal dy = netDelta.y();
        const qreal absDx = std::abs(dx);
        const qreal absDy = std::abs(dy);

        const bool mostlyVertical = absDy >= absDx * kDominance;
        const bool mostlyHorizontal = absDx >= absDy * kDominance;

        if (cfg.touchClipboardGestureEnabled() && mostlyVertical && dy >= kMinSwipePx) {
            actionName = QStringLiteral("touch_copypaste_overlay");
            break;
        }

        if (cfg.touchClearLayerGestureEnabled()) {
            const bool scrubDistanceOk = m_gestureAccumAbsDx >= kMinScrubAbsPx &&
                m_gestureAccumAbsDx >= m_gestureAccumAbsDy * kDominance;
            const bool scrubDirectionOk = m_gestureXDirectionChanges >= kMinScrubDirectionChanges;

            if (scrubDistanceOk && scrubDirectionOk) {
                actionName = QStringLiteral("clear");
                break;
            }

            // Fallback: if we couldn't track intermediate motion (e.g. missing updates),
            // allow a strong horizontal swipe.
            if (m_gestureAccumAbsDx <= 0.0 && mostlyHorizontal && absDx >= kMinScrubAbsPx) {
                actionName = QStringLiteral("clear");
                break;
            }
        }

        return;
    }

    if (actionName.isEmpty()) {
        qWarning("KisTouchGestureAction: Unhandled shortcut %d", m_shortcut);
    } else {
        KisKActionCollection *actionCollection = KisPart::instance()->currentMainwindow()->actionCollection();
        QAction *action = actionCollection->action(actionName);
        if (action) {
            m_triggeredThisGesture = true;
            action->trigger();
        } else {
            qWarning("KisTouchGestureAction: unable to find action '%s' for shortcut %d",
                     qUtf8Printable(actionName),
                     m_shortcut);
        }
    }
}

int KisTouchGestureAction::priority() const
{
    return 6;
}
