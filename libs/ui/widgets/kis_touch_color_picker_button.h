/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_TOUCH_COLOR_PICKER_BUTTON_H
#define KIS_TOUCH_COLOR_PICKER_BUTTON_H

#include <kritaui_export.h>

#include <KoColor.h>

#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <QToolButton>

class KoCanvasResourceProvider;
class QDockWidget;
class QMouseEvent;
class QVariant;

/**
 * Procreate-style color picker button for Touch Mode.
 *
 * - Shows the current foreground color as a disk.
 * - Tap: toggles the color panel.
 * - Touch+hold+drag: starts a color drag (ColorDrop) that can be dropped on the canvas to fill.
 */
class KRITAUI_EXPORT KisTouchColorPickerButton final : public QToolButton
{
    Q_OBJECT

public:
    explicit KisTouchColorPickerButton(QWidget *parent = nullptr);
    ~KisTouchColorPickerButton() override;

    void setColorDock(QDockWidget *dock);
    void setResourceManager(KoCanvasResourceProvider *resourceManager);

    QColor currentColor() const;

public Q_SLOTS:
    void refreshIcon();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void setColor(const KoColor &color);
    void updateDiskIcon();
    void startColorDrag();
    bool dragDistanceReached(const QPoint &pos) const;

private Q_SLOTS:
    void slotResourceChanged(int key, const QVariant &value);
    void slotLongPressTriggered();
    void slotClicked();

private:
    QPointer<QDockWidget> m_colorDock;
    QPointer<KoCanvasResourceProvider> m_resourceManager;
    QMetaObject::Connection m_resourceChangedConnection;

    QTimer m_longPressTimer;
    QPoint m_pressPos;
    bool m_longPressActive {false};
    bool m_dragInProgress {false};
    bool m_suppressClick {false};

    bool m_hasValidColor {false};
    KoColor m_color;
};

#endif // KIS_TOUCH_COLOR_PICKER_BUTTON_H
