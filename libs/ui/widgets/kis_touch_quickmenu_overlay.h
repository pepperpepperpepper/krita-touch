/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_TOUCH_QUICKMENU_OVERLAY_H
#define KIS_TOUCH_QUICKMENU_OVERLAY_H

#include <kritaui_export.h>

#include <QFrame>
#include <QPointer>
#include <QStringList>
#include <QVector>

class QAction;
class KisKActionCollection;
class QToolButton;

/**
 * A touch-friendly, Procreate-inspired QuickMenu overlay.
 *
 * This widget is intentionally input-transparent. Selection is expected to be
 * driven by the touch shortcut system (press-hold, slide, release).
 */
class KRITAUI_EXPORT KisTouchQuickMenuOverlay final : public QFrame
{
    Q_OBJECT

public:
    explicit KisTouchQuickMenuOverlay(KisKActionCollection *actionCollection, QWidget *parent = nullptr);
    ~KisTouchQuickMenuOverlay() override;

    void setActionCollection(KisKActionCollection *actionCollection);
    void setSlotActionIds(const QStringList &actionIds);
    QString slotActionId(int slot) const;

    void setHighlightedSlot(int slot);
    int highlightedSlot() const;

    void openAtGlobalPos(const QPoint &globalPos);
    void triggerSlotAndClose(int slot);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void rebuildUi();
    void positionButtons();

private:
    QPointer<KisKActionCollection> m_actionCollection;
    QStringList m_slotActionIds;
    QVector<QToolButton *> m_slotButtons;
    int m_highlightedSlot{-1};
};

#endif // KIS_TOUCH_QUICKMENU_OVERLAY_H
