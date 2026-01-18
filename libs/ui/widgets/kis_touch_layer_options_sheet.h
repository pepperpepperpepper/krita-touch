/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_TOUCH_LAYER_OPTIONS_SHEET_H
#define KIS_TOUCH_LAYER_OPTIONS_SHEET_H

#include <kritaui_export.h>

#include <QFrame>
#include <QPointer>

class KisKActionCollection;
class KisSliderSpinBox;
class QToolButton;

/**
 * A touch-friendly “Layer options sheet” (Procreate layer options analog).
 *
 * v1: a compact sheet that maps onto existing Krita layer actions.
 */
class KRITAUI_EXPORT KisTouchLayerOptionsSheet final : public QFrame
{
    Q_OBJECT

public:
    explicit KisTouchLayerOptionsSheet(KisKActionCollection *actionCollection, QWidget *parent = nullptr);
    ~KisTouchLayerOptionsSheet() override;

    void setActionCollection(KisKActionCollection *actionCollection);
    void openAtGlobalPos(const QPoint &globalPos);

private:
    void rebuildUi();
    QToolButton *buildActionButton(QWidget *parent,
                                   const QString &actionId,
                                   const QString &labelOverride,
                                   const QString &fallbackIconName);
    void triggerAndClose(const QString &actionId);

private:
    QPointer<KisKActionCollection> m_actionCollection;
    QToolButton *m_closeButton{nullptr};
    KisSliderSpinBox *m_opacitySlider{nullptr};
};

#endif // KIS_TOUCH_LAYER_OPTIONS_SHEET_H
