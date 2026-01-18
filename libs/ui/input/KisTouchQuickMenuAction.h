/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_TOUCH_QUICKMENU_ACTION_H
#define KIS_TOUCH_QUICKMENU_ACTION_H

#include "kis_abstract_input_action.h"

#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QPointer>
#include <QTimer>

class KisTouchQuickMenuOverlay;
class KisKActionCollection;
class QTouchEvent;

/**
 * Touch hold action to show a Procreate-like QuickMenu overlay.
 *
 * Gesture: One Finger Hold
 * Interaction: hold to open, slide to select, release to activate.
 */
class KisTouchQuickMenuAction : public QObject, public KisAbstractInputAction
{
    Q_OBJECT

public:
    explicit KisTouchQuickMenuAction();
    ~KisTouchQuickMenuAction() override;

    int priority() const override { return 6; }

    void begin(int shortcut, QEvent *event) override;
    void end(QEvent *event) override;
    void inputEvent(QEvent *event) override;

private Q_SLOTS:
    void slotConfigureTimeout();

private:
    QPoint touchGlobalPos(const QTouchEvent *event) const;
    int slotForDelta(const QPointF &delta) const;

private:
    QPointer<KisTouchQuickMenuOverlay> m_overlay;
    QPointer<KisKActionCollection> m_actionCollection;
    QTimer m_configureTimer;
    QPoint m_originGlobalPos;
    QPoint m_lastGlobalPos;
    int m_selectedSlot{-1};
    bool m_configureTriggered{false};
};

#endif // KIS_TOUCH_QUICKMENU_ACTION_H
