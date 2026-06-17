/*
 *  SPDX-FileCopyrightText: 2026
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_TOOL_FREEHAND_QUICKSHAPE_H_
#define KIS_TOOL_FREEHAND_QUICKSHAPE_H_

#include <optional>

#include <QObject>
#include <QElapsedTimer>
#include <QPointer>
#include <QPointF>
#include <QRectF>
#include <QVector>

class KisToolFreehand;
class KoPointerEvent;
class KoViewConverter;
class QFrame;
class QPainter;

/**
 * Touch "QuickShape" controller, extracted out of KisToolFreehand.
 *
 * Owns all QuickShape state and the detect/snap/edit/commit flow. The tool
 * delegates to it from its primary-action and paint hooks; the controller
 * reaches the tool's protected coordinate/canvas helpers through the
 * KisToolFreehand* back-pointer (which is why KisToolFreehand grants this
 * class friendship). m_tool MUST stay typed KisToolFreehand* — calling the
 * protected KisTool base methods through an upcast KisTool or KisToolPaint
 * base pointer would be ill-formed.
 */
class KisToolFreehandQuickShape : public QObject
{
    Q_OBJECT

public:
    explicit KisToolFreehandQuickShape(KisToolFreehand *tool);
    ~KisToolFreehandQuickShape() override;

    struct TouchQuickShapeGeometry {
        enum class Kind {
            None,
            Rect,
            Ellipse,
            Polygon,
            Line,
        };

        Kind kind{Kind::None};
        QRectF rect;
        QVector<QPointF> polygon;
        QPointF lineP0;
        QPointF lineP1;
    };

    /// True while the user is editing a just-snapped shape (handles visible).
    bool isEditActive() const;

    /// deactivate() hook: commit any in-progress edit, hide popup, drop state.
    void onDeactivate();

    /// beginPrimaryAction(): if an edit is active, route to editBegin and return
    /// true (caller must early-return); otherwise hide the popup, drop the last
    /// shape, and return false so normal painting proceeds.
    bool handleBeginEditRedirect(KoPointerEvent *event);
    /// beginPrimaryAction(): reset tracking, then start QuickShape tracking if
    /// touch mode + QuickShape are enabled.
    void startTrackingIfEnabled(KoPointerEvent *event);

    /// continuePrimaryAction(): route to editContinue when editing; return true
    /// if handled (caller must early-return).
    bool handleContinueEditRedirect(KoPointerEvent *event);
    /// continuePrimaryAction(): record a tracking sample (gated internally).
    void accumulateTrackingPoint(KoPointerEvent *event);

    /// endPrimaryAction(): route to editEnd when editing; return true if handled.
    bool handleEndEditRedirect(KoPointerEvent *event);
    /// endPrimaryAction() phase 1 (BEFORE endStroke): record the final point,
    /// consume the perfect-shape request, run the hold-gated detection cascade,
    /// and stash the detected geometry for the commit phase.
    void detectShapeOnEnd(KoPointerEvent *event);
    /// endPrimaryAction() phase 2 (AFTER setMode HOVER): if a shape was detected,
    /// undo the freehand stroke and repaint it as the clean shape, then show the
    /// edit popup. Always resets tracking at the tail.
    void commitDetectedShapeAndShowPopup();

    /// paint() hook: draws the edit overlay + handles. No-op unless editing.
    void paint(QPainter &gc, const KoViewConverter &converter);

private:
    void showEditPopup();
    void startEdit();
    void commitEdit();

    void editBegin(KoPointerEvent *event);
    void editContinue(KoPointerEvent *event);
    void editEnd(KoPointerEvent *event);

    void resetTracking();

private:
    KisToolFreehand *m_tool; // non-owning back-pointer; MUST stay typed KisToolFreehand*

    QVector<QPointF> m_points;
    QPointF m_lastRecordedPixelPos;
    QElapsedTimer m_sinceLastMove;
    bool m_tracking{false};

    std::optional<TouchQuickShapeGeometry> m_lastShape;
    std::optional<TouchQuickShapeGeometry> m_editShape;
    bool m_editActive{false};
    int m_editHandle{-1};
    bool m_editDragAll{false};
    bool m_editMoved{false};
    bool m_editPendingCommit{false};
    QPointF m_editLastPixelPos;
    QPointer<QFrame> m_editPopup;

    // transient: written by detectShapeOnEnd, consumed by commitDetectedShapeAndShowPopup
    std::optional<TouchQuickShapeGeometry> m_pendingDetectedShape;
};

#endif // KIS_TOOL_FREEHAND_QUICKSHAPE_H_
