/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kis_touch_quickmenu_overlay.h"

#include <cmath>

#include <QAction>
#include <QGuiApplication>
#include <QLabel>
#include <QPainter>
#include <QResizeEvent>
#include <QScreen>
#include <QToolButton>

#include <kactioncollection.h>
#include <klocalizedstring.h>

namespace {

QString stripAmpersands(QString text)
{
    text.remove(QLatin1Char('&'));
    return text.trimmed();
}

QRect clampToScreen(const QRect &desired, const QPoint &referencePoint)
{
    QScreen *screen = QGuiApplication::screenAt(referencePoint);
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return desired;
    }

    const QRect avail = screen->availableGeometry();
    QRect clamped = desired;

    if (clamped.left() < avail.left()) {
        clamped.moveLeft(avail.left());
    }
    if (clamped.right() > avail.right()) {
        clamped.moveRight(avail.right());
    }
    if (clamped.top() < avail.top()) {
        clamped.moveTop(avail.top());
    }
    if (clamped.bottom() > avail.bottom()) {
        clamped.moveBottom(avail.bottom());
    }

    return clamped;
}

QStringList defaultQuickMenuActionIds()
{
    // v1 defaults: keep these action IDs stable to make future customization
    // (configurable slots) straightforward.
    return QStringList{
        QStringLiteral("edit_undo"),
        QStringLiteral("edit_redo"),
        QStringLiteral("KisToolSelectTouch"),
        QStringLiteral("KisToolTransform"),
        QStringLiteral("deselect"),
        QStringLiteral("view_show_canvas_only"),
    };
}

QString quickMenuSlotLabel(const QString &actionId, const QAction *action)
{
    if (actionId == QLatin1String("edit_undo")) {
        return i18n("Undo");
    }
    if (actionId == QLatin1String("edit_redo")) {
        return i18n("Redo");
    }
    if (actionId == QLatin1String("KisToolSelectTouch")) {
        return i18n("Select");
    }
    if (actionId == QLatin1String("KisToolTransform")) {
        return i18n("Transform");
    }
    if (actionId == QLatin1String("deselect")) {
        return i18n("Deselect");
    }
    if (actionId == QLatin1String("view_show_canvas_only")) {
        return i18n("Canvas");
    }

    return action ? stripAmpersands(action->text()) : i18n("…");
}

} // namespace

KisTouchQuickMenuOverlay::KisTouchQuickMenuOverlay(KisKActionCollection *actionCollection, QWidget *parent)
    : QFrame(parent)
    , m_actionCollection(actionCollection)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::ToolTip | Qt::WindowTransparentForInput);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setObjectName(QStringLiteral("kisTouchQuickMenuOverlay"));

    setFixedSize(QSize(360, 360));

    setStyleSheet(QStringLiteral(
        "QFrame#kisTouchQuickMenuOverlay {"
        "  background-color: rgba(30, 30, 30, 230);"
        "  border: 1px solid rgba(255, 255, 255, 40);"
        "  border-radius: 180px;"
        "}"
        "QToolButton {"
        "  color: palette(window-text);"
        "  background: transparent;"
        "  border: 0px;"
        "  padding: 6px;"
        "}"
        "QToolButton:checked {"
        "  background-color: rgba(255, 255, 255, 30);"
        "  border-radius: 12px;"
        "}"));

    m_slotActionIds = defaultQuickMenuActionIds();
    rebuildUi();
}

KisTouchQuickMenuOverlay::~KisTouchQuickMenuOverlay() = default;

void KisTouchQuickMenuOverlay::setActionCollection(KisKActionCollection *actionCollection)
{
    if (m_actionCollection == actionCollection) {
        return;
    }

    m_actionCollection = actionCollection;
    rebuildUi();
}

void KisTouchQuickMenuOverlay::setSlotActionIds(const QStringList &actionIds)
{
    if (actionIds == m_slotActionIds) {
        return;
    }

    m_slotActionIds = actionIds;
    while (m_slotActionIds.size() < 6) {
        m_slotActionIds.append(QString());
    }
    if (m_slotActionIds.size() > 6) {
        m_slotActionIds = m_slotActionIds.mid(0, 6);
    }
    rebuildUi();
}

QString KisTouchQuickMenuOverlay::slotActionId(int slot) const
{
    if (slot < 0 || slot >= m_slotActionIds.size()) {
        return QString();
    }
    return m_slotActionIds.at(slot);
}

void KisTouchQuickMenuOverlay::setHighlightedSlot(int slot)
{
    if (slot == m_highlightedSlot) {
        return;
    }

    if (m_highlightedSlot >= 0 && m_highlightedSlot < m_slotButtons.size()) {
        m_slotButtons[m_highlightedSlot]->setChecked(false);
    }

    m_highlightedSlot = slot;

    if (m_highlightedSlot >= 0 && m_highlightedSlot < m_slotButtons.size()) {
        m_slotButtons[m_highlightedSlot]->setChecked(true);
    }
}

int KisTouchQuickMenuOverlay::highlightedSlot() const
{
    return m_highlightedSlot;
}

void KisTouchQuickMenuOverlay::openAtGlobalPos(const QPoint &globalPos)
{
    rebuildUi();

    const QRect desiredRect(
        QPoint(globalPos.x() - width() / 2, globalPos.y() - height() / 2),
        size());
    const QRect finalRect = clampToScreen(desiredRect, globalPos);

    move(finalRect.topLeft());
    show();
    raise();
}

void KisTouchQuickMenuOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const QColor bg(30, 30, 30, 230);
    const QColor border(255, 255, 255, 40);

    p.setPen(QPen(border, 1.0));
    p.setBrush(bg);
    p.drawEllipse(r);
}

void KisTouchQuickMenuOverlay::triggerSlotAndClose(int slot)
{
    if (slot < 0 || slot >= m_slotActionIds.size()) {
        hide();
        return;
    }

    QAction *action = m_actionCollection ? m_actionCollection->action(m_slotActionIds.at(slot)) : nullptr;
    if (action) {
        action->trigger();
    }
    hide();
}

void KisTouchQuickMenuOverlay::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    positionButtons();
}

void KisTouchQuickMenuOverlay::rebuildUi()
{
    if (m_slotButtons.isEmpty()) {
        // Create the buttons once and only update their labels/icons.
        for (int i = 0; i < 6; ++i) {
            QToolButton *button = new QToolButton(this);
            button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            button->setIconSize(QSize(40, 40));
            button->setMinimumSize(QSize(92, 92));
            button->setCheckable(true);
            button->setAutoRaise(true);
            button->setFocusPolicy(Qt::NoFocus);
            m_slotButtons.append(button);
        }
    }

    if (!m_actionCollection) {
        // Minimal fallback: keep empty but stable.
        for (QToolButton *button : m_slotButtons) {
            button->setText(i18n("…"));
            button->setToolTip(i18n("No action collection"));
            button->setIcon(QIcon());
        }
        positionButtons();
        return;
    }

    for (int i = 0; i < m_slotButtons.size(); ++i) {
        QToolButton *button = m_slotButtons[i];
        const QString actionId = slotActionId(i);
        QAction *action = m_actionCollection->action(actionId);

        if (action) {
            button->setIcon(action->icon());
            button->setText(quickMenuSlotLabel(actionId, action));
            button->setToolTip(stripAmpersands(action->text()));
        } else {
            button->setIcon(QIcon());
            button->setText(i18n("…"));
            button->setToolTip(i18n("Missing action: %1", actionId));
        }
    }

    positionButtons();
}

void KisTouchQuickMenuOverlay::positionButtons()
{
    if (m_slotButtons.size() != 6) {
        return;
    }

    constexpr qreal kRadius = 120.0;
    constexpr qreal kPi = 3.14159265358979323846;
    const QPoint center = rect().center();

    const qreal anglesDeg[6] = {
        270.0, // top
        330.0, // top-right
        30.0,  // bottom-right
        90.0,  // bottom
        150.0, // bottom-left
        210.0, // top-left
    };

    for (int i = 0; i < 6; ++i) {
        QToolButton *button = m_slotButtons[i];
        const qreal rad = anglesDeg[i] * kPi / 180.0;

        const QPointF posF(center.x() + std::cos(rad) * kRadius,
                           center.y() + std::sin(rad) * kRadius);
        const QSize size = button->minimumSize();
        const QPoint topLeft(qRound(posF.x() - size.width() / 2.0),
                             qRound(posF.y() - size.height() / 2.0));
        button->setGeometry(QRect(topLeft, size));
    }
}
