/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_TOUCH_UI_MOUSE_FALLBACK_FILTER_H
#define KIS_TOUCH_UI_MOUSE_FALLBACK_FILTER_H

#include <QObject>
#include <QPointer>

class QEvent;
class QPointF;
class QWidget;

/**
 * Desktop Touch Mode fallback: translate single-finger touch events into mouse
 * press/move/release for non-canvas widgets.
 *
 * Motivation:
 * - Some Wayland/Qt setups can stop synthesizing mouse events from touch after a
 *   real mouse/stylus interaction, causing menus/toolbars to ignore touch.
 *
 * Notes:
 * - Canvas widgets must be excluded to avoid breaking touch painting/gestures.
 * - Enable debug logging via `KRITA_TOUCH_UI_MOUSE_FALLBACK_DEBUG=1`.
 */
class KisTouchUiMouseFallbackFilter : public QObject
{
    Q_OBJECT

public:
    explicit KisTouchUiMouseFallbackFilter(QObject *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    static bool debugLoggingEnabled();

    static bool isCanvasWidget(QWidget *w);
    static bool isInCanvas(QWidget *w);
    static bool isTouchNativeWidget(QWidget *w);

    void resetEmulation();

    void sendMouseEvent(QWidget *targetWidget,
                        const QPointF &screenPos,
                        Qt::KeyboardModifiers modifiers,
                        QEvent::Type mouseType,
                        Qt::MouseButton button,
                        Qt::MouseButtons buttons) const;

private:
    QPointer<QWidget> m_targetWidget;
    int m_touchId{-1};
};

#endif // KIS_TOUCH_UI_MOUSE_FALLBACK_FILTER_H
