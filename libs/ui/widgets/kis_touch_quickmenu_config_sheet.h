/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_TOUCH_QUICKMENU_CONFIG_SHEET_H
#define KIS_TOUCH_QUICKMENU_CONFIG_SHEET_H

#include <kritaui_export.h>

#include <QFrame>
#include <QPointer>
#include <QStringList>

class KisKActionCollection;
class QButtonGroup;
class QPaintEvent;
class QToolButton;

/**
 * Touch-friendly QuickMenu configuration sheet.
 *
 * Procreate parity target: configurable QuickMenu slots.
 */
class KRITAUI_EXPORT KisTouchQuickMenuConfigSheet final : public QFrame
{
    Q_OBJECT

public:
    explicit KisTouchQuickMenuConfigSheet(KisKActionCollection *actionCollection, QWidget *parent = nullptr);
    ~KisTouchQuickMenuConfigSheet() override;

    void setActionCollection(KisKActionCollection *actionCollection);
    void openAtGlobalPos(const QPoint &globalPos, int initialSlot = -1);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void rebuildUi();
    void reloadFromConfig();
    void persistToConfig();
    void updateSlotButtons();
    void setSelectedSlot(int slot);

private:
    QPointer<KisKActionCollection> m_actionCollection;
    QToolButton *m_closeButton{nullptr};
    QToolButton *m_resetButton{nullptr};
    QButtonGroup *m_slotGroup{nullptr};
    QList<QToolButton *> m_slotButtons;
    int m_selectedSlot{0};
    QStringList m_slotActionIds;
};

#endif // KIS_TOUCH_QUICKMENU_CONFIG_SHEET_H
