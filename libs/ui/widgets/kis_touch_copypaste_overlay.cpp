/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kis_touch_copypaste_overlay.h"

#include <QAction>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QScreen>
#include <QToolButton>

#include <KisMainWindow.h>
#include <KisPart.h>
#include <KisViewManager.h>
#include <kactioncollection.h>
#include <klocalizedstring.h>
#include <kis_canvas2.h>
#include <kis_tool_proxy.h>

#include "kis_touch_ui_metrics.h"

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

bool currentCanvasHasSelection()
{
    KisMainWindow *mainWindow = KisPart::instance() ? KisPart::instance()->currentMainwindow() : nullptr;
    KisViewManager *viewManager = mainWindow ? mainWindow->viewManager() : nullptr;
    KisCanvas2 *canvas = viewManager ? viewManager->canvasBase() : nullptr;
    return canvas && canvas->toolProxy() && canvas->toolProxy()->hasSelection();
}

} // namespace

KisTouchCopyPasteOverlay::KisTouchCopyPasteOverlay(KisKActionCollection *actionCollection, QWidget *parent)
    : QFrame(parent)
    , m_actionCollection(actionCollection)
{
    const qreal scale = KisTouchUiMetrics::scaleForScreen(QGuiApplication::primaryScreen());
    const int overlayRadiusPx = KisTouchUiMetrics::px(14.0, scale);
    const int buttonPaddingPx = KisTouchUiMetrics::px(10.0, scale);
    const int pressedRadiusPx = KisTouchUiMetrics::px(12.0, scale);

    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setObjectName(QStringLiteral("kisTouchCopyPasteOverlay"));

    setStyleSheet(QStringLiteral(
        "QFrame#kisTouchCopyPasteOverlay {"
        "  background-color: rgba(30, 30, 30, 230);"
        "  border: 1px solid rgba(255, 255, 255, 40);"
        "  border-radius: %1px;"
        "  color: rgb(240, 240, 240);"
        "}"
        "QToolButton {"
        "  color: rgb(240, 240, 240);"
        "  background: transparent;"
        "  border: 0px;"
        "  padding: %2px;"
        "}"
        "QToolButton:pressed {"
        "  background-color: rgba(255, 255, 255, 30);"
        "  border-radius: %3px;"
        "}")
                      .arg(overlayRadiusPx)
                      .arg(buttonPaddingPx)
                      .arg(pressedRadiusPx));

    rebuildUi();
}

KisTouchCopyPasteOverlay::~KisTouchCopyPasteOverlay() = default;

void KisTouchCopyPasteOverlay::setActionCollection(KisKActionCollection *actionCollection)
{
    if (m_actionCollection == actionCollection) {
        return;
    }

    m_actionCollection = actionCollection;
    rebuildUi();
}

void KisTouchCopyPasteOverlay::openAtGlobalPos(const QPoint &globalPos)
{
    rebuildUi();

    adjustSize();

    const QRect desiredRect(
        QPoint(globalPos.x() - width() / 2, globalPos.y() - height() / 2),
        size());
    const QRect finalRect = clampToScreen(desiredRect, globalPos);

    move(finalRect.topLeft());
    show();
    raise();
}

void KisTouchCopyPasteOverlay::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    const qreal scale = KisTouchUiMetrics::scaleForScreen(QGuiApplication::primaryScreen());

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = KisTouchUiMetrics::px(14.0, scale);

    const QColor bg(30, 30, 30, 230);
    const QColor border(255, 255, 255, 40);

    p.setPen(QPen(border, 1.0));
    p.setBrush(bg);
    p.drawRoundedRect(r, radius, radius);
}

void KisTouchCopyPasteOverlay::rebuildUi()
{
    const qreal scale = KisTouchUiMetrics::scaleForScreen(QGuiApplication::primaryScreen());
    const int layoutMarginPx = KisTouchUiMetrics::px(10.0, scale);
    const int layoutSpacingPx = KisTouchUiMetrics::px(6.0, scale);
    const int buttonIconPx = KisTouchUiMetrics::px(36.0, scale, 20);
    const int buttonMinWidthPx = KisTouchUiMetrics::px(96.0, scale, 48);
    const int buttonMinHeightPx = KisTouchUiMetrics::px(92.0, scale, 48);
    const QSize buttonMinSize(buttonMinWidthPx, buttonMinHeightPx);

    QHBoxLayout *layout = qobject_cast<QHBoxLayout *>(this->layout());
    if (!layout) {
        // If a layout wasn't set (or got replaced), create a fresh one.
        layout = new QHBoxLayout(this);
    } else {
        // Clear existing items/widgets so repeated calls don't stack overlays.
        QLayoutItem *item = nullptr;
        while ((item = layout->takeAt(0))) {
            if (QWidget *w = item->widget()) {
                delete w;
            }
            delete item;
        }
    }

    layout->setContentsMargins(layoutMarginPx, layoutMarginPx, layoutMarginPx, layoutMarginPx);
    layout->setSpacing(layoutSpacingPx);

    if (!m_actionCollection) {
        QLabel *label = new QLabel(i18n("No action collection"), this);
        label->setStyleSheet(QStringLiteral("color: white;"));
        layout->addWidget(label);
        return;
    }

    struct Entry {
        const char *actionId;
        const char *fallbackText;
        bool isDuplicate{false};
    };

    // Procreate-like: a small clipboard sheet. Keep labels short.
    const Entry entries[] = {
        {"edit_cut", QT_TR_NOOP("Cut")},
        {"edit_copy", QT_TR_NOOP("Copy")},
        {"edit_paste", QT_TR_NOOP("Paste")},
        {"copy_merged", QT_TR_NOOP("Copy\nAll")},
        {"__touch_duplicate", QT_TR_NOOP("Duplicate"), true},
        {"copy_selection_to_new_layer", QT_TR_NOOP("Copy\nPaste")},
        {"cut_selection_to_new_layer", QT_TR_NOOP("Cut\nPaste")},
    };

    for (const Entry &entry : entries) {
        QAction *action = nullptr;
        QAction *selectionDuplicateAction = nullptr;
        QAction *layerDuplicateAction = nullptr;

        if (entry.isDuplicate) {
            selectionDuplicateAction = m_actionCollection->action(QStringLiteral("copy_selection_to_new_layer"));
            layerDuplicateAction = m_actionCollection->action(QStringLiteral("duplicatelayer"));
            action = layerDuplicateAction ? layerDuplicateAction : selectionDuplicateAction;
            if (!action) {
                continue;
            }
        } else {
            action = m_actionCollection->action(QString::fromLatin1(entry.actionId));
            if (!action) {
                continue;
            }
        }

        QToolButton *button = new QToolButton(this);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setIcon(action->icon());
        button->setIconSize(QSize(buttonIconPx, buttonIconPx));

        button->setText(i18n(entry.fallbackText));
        button->setToolTip(stripAmpersands(action->text()));

        button->setMinimumSize(buttonMinSize);
        button->setAutoRaise(true);

        if (entry.isDuplicate) {
            connect(button, &QToolButton::clicked, this, [this, selectionDuplicateAction, layerDuplicateAction]() {
                const bool hasSelection = currentCanvasHasSelection();
                QAction *preferredAction = nullptr;

                if (hasSelection && selectionDuplicateAction) {
                    preferredAction = selectionDuplicateAction;
                } else if (layerDuplicateAction) {
                    preferredAction = layerDuplicateAction;
                } else {
                    preferredAction = selectionDuplicateAction;
                }

                triggerAndClose(preferredAction);
            });
        } else {
            connect(button, &QToolButton::clicked, this, [this, action]() { triggerAndClose(action); });
        }
        layout->addWidget(button);
    }
}

void KisTouchCopyPasteOverlay::triggerAndClose(QAction *action)
{
    // Release the Qt::Popup mouse/touch grab (via hide()) BEFORE triggering, so an
    // action that opens a modal dialog isn't left fighting the still-active grab.
    hide();
    if (action) {
        action->trigger();
    }
}
