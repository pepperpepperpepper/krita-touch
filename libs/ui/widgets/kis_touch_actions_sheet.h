/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_TOUCH_ACTIONS_SHEET_H
#define KIS_TOUCH_ACTIONS_SHEET_H

#include <kritaui_export.h>

#include <QFrame>
#include <QPointer>

class KisKActionCollection;
class QListWidget;
class QPaintEvent;
class QStackedWidget;
class QToolButton;

/**
 * A touch-friendly “Actions sheet” (Procreate wrench menu analog).
 *
 * v1: a simple categorized sheet that maps onto existing Krita actions.
 */
class KRITAUI_EXPORT KisTouchActionsSheet final : public QFrame
{
    Q_OBJECT

public:
    explicit KisTouchActionsSheet(KisKActionCollection *actionCollection, QWidget *parent = nullptr);
    ~KisTouchActionsSheet() override;

    void setActionCollection(KisKActionCollection *actionCollection);
    void openAtGlobalPos(const QPoint &globalPos);
    void setCurrentCategoryRow(int row);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void rebuildUi();
    QWidget *buildCategoryPage(const QList<QPair<QString, QString>> &entries);
    void refitToCurrentCategory(const QPoint &referencePoint);
    void triggerAndClose(const QString &actionId);

private:
    QPointer<KisKActionCollection> m_actionCollection;
    QToolButton *m_closeButton{nullptr};
    QListWidget *m_categories{nullptr};
    QStackedWidget *m_pages{nullptr};
};

#endif // KIS_TOUCH_ACTIONS_SHEET_H
