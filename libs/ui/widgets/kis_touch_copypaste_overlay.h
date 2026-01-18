/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_TOUCH_COPYPASTE_OVERLAY_H
#define KIS_TOUCH_COPYPASTE_OVERLAY_H

#include <kritaui_export.h>

#include <QFrame>
#include <QPointer>

class KisKActionCollection;

/**
 * A touch-friendly, Procreate-inspired copy/paste overlay.
 *
 * This is intentionally minimal and relies on existing Krita actions.
 */
class KRITAUI_EXPORT KisTouchCopyPasteOverlay final : public QFrame
{
    Q_OBJECT

public:
    explicit KisTouchCopyPasteOverlay(KisKActionCollection *actionCollection, QWidget *parent = nullptr);
    ~KisTouchCopyPasteOverlay() override;

    void setActionCollection(KisKActionCollection *actionCollection);
    void openAtGlobalPos(const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void rebuildUi();
    void triggerAndClose(QAction *action);

private:
    QPointer<KisKActionCollection> m_actionCollection;
};

#endif // KIS_TOUCH_COPYPASTE_OVERLAY_H
