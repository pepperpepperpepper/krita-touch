/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisTouchQuickMenuAction.h"

#include <cmath>

#include <QAction>
#include <QTouchEvent>

#include <klocalizedstring.h>

#include <KisMainWindow.h>
#include <KisViewManager.h>
#include <kactioncollection.h>
#include <kis_config.h>
#include <kis_canvas2.h>
#include "kis_input_manager.h"

#include "widgets/kis_touch_quickmenu_overlay.h"

namespace {

constexpr int kConfigureDelayMs = 650;
constexpr int kConfigureMotionRestartThresholdPx = 6;

qreal angularDistanceDeg(qreal a, qreal b)
{
    qreal diff = std::fmod(std::abs(a - b), 360.0);
    if (diff > 180.0) {
        diff = 360.0 - diff;
    }
    return diff;
}

} // namespace

KisTouchQuickMenuAction::KisTouchQuickMenuAction()
    : KisAbstractInputAction("Touch QuickMenu")
{
    setName(i18n("Touch QuickMenu"));
    setDescription(i18n("Show a touch-friendly QuickMenu overlay (hold, slide, release)."));

    m_configureTimer.setSingleShot(true);
    connect(&m_configureTimer, &QTimer::timeout, this, &KisTouchQuickMenuAction::slotConfigureTimeout);
}

KisTouchQuickMenuAction::~KisTouchQuickMenuAction() = default;

void KisTouchQuickMenuAction::begin(int shortcut, QEvent *event)
{
    Q_UNUSED(shortcut)

    m_configureTimer.stop();
    m_configureTriggered = false;
    m_actionCollection = nullptr;

    if (!KisConfig(true).touchQuickMenuEnabled()) {
        return;
    }

    const QTouchEvent *touchEvent = dynamic_cast<const QTouchEvent *>(event);
    if (!touchEvent) {
        return;
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (touchEvent->points().isEmpty()) {
        return;
    }
#else
    if (touchEvent->touchPoints().isEmpty()) {
        return;
    }
#endif

    KisCanvas2 *canvas = inputManager() ? inputManager()->canvas() : nullptr;
    KisViewManager *viewManager = canvas ? canvas->viewManager() : nullptr;
    KisMainWindow *mainWindow = viewManager ? viewManager->mainWindow() : nullptr;
    KisKActionCollection *actionCollection = mainWindow ? mainWindow->actionCollection() : nullptr;

    if (!mainWindow || !actionCollection) {
        return;
    }

    m_actionCollection = actionCollection;
    m_originGlobalPos = touchGlobalPos(touchEvent);
    if (m_originGlobalPos.isNull()) {
        return;
    }
    m_selectedSlot = -1;
    m_lastGlobalPos = m_originGlobalPos;

    if (!m_overlay || m_overlay->parentWidget() != mainWindow) {
        if (m_overlay) {
            m_overlay->hide();
            m_overlay->deleteLater();
        }
        m_overlay = new KisTouchQuickMenuOverlay(actionCollection, mainWindow);
    } else {
        m_overlay->setActionCollection(actionCollection);
    }

    m_overlay->setSlotActionIds(KisConfig(true).touchQuickMenuActionIds());
    m_overlay->setHighlightedSlot(-1);
    m_overlay->openAtGlobalPos(m_originGlobalPos);
}

void KisTouchQuickMenuAction::inputEvent(QEvent *event)
{
    const QTouchEvent *touchEvent = dynamic_cast<const QTouchEvent *>(event);
    if (!touchEvent) {
        KisAbstractInputAction::inputEvent(event);
        return;
    }

    if (!m_overlay) {
        return;
    }

    if (m_configureTriggered) {
        return;
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (touchEvent->points().isEmpty()) {
        return;
    }
#else
    if (touchEvent->touchPoints().isEmpty()) {
        return;
    }
#endif

    const QPoint currentGlobalPos = touchGlobalPos(touchEvent);
    if (currentGlobalPos.isNull()) {
        return;
    }
    const QPointF delta = currentGlobalPos - m_originGlobalPos;
    const int slot = slotForDelta(delta);

    if (slot != m_selectedSlot) {
        m_selectedSlot = slot;
        m_overlay->setHighlightedSlot(slot);
    }

    if (slot < 0) {
        m_configureTimer.stop();
        m_lastGlobalPos = currentGlobalPos;
        return;
    }

    const QPoint drift = currentGlobalPos - m_lastGlobalPos;
    if (drift.manhattanLength() >= kConfigureMotionRestartThresholdPx) {
        m_configureTimer.start(kConfigureDelayMs);
        m_lastGlobalPos = currentGlobalPos;
    } else if (!m_configureTimer.isActive()) {
        m_configureTimer.start(kConfigureDelayMs);
    }
}

void KisTouchQuickMenuAction::end(QEvent *event)
{
    Q_UNUSED(event)

    m_configureTimer.stop();

    if (m_overlay) {
        if (!m_configureTriggered && m_selectedSlot >= 0) {
            m_overlay->triggerSlotAndClose(m_selectedSlot);
        } else {
            m_overlay->hide();
        }
        m_overlay->setHighlightedSlot(-1);
    }

    m_selectedSlot = -1;
    m_originGlobalPos = QPoint();
    m_lastGlobalPos = QPoint();
    m_actionCollection = nullptr;
    m_configureTriggered = false;
}

void KisTouchQuickMenuAction::slotConfigureTimeout()
{
    if (m_configureTriggered) {
        return;
    }
    if (!m_overlay || m_selectedSlot < 0) {
        return;
    }

    KisKActionCollection *actionCollection = m_actionCollection;
    if (!actionCollection) {
        return;
    }

    QAction *configureAction = actionCollection->action(QStringLiteral("touch_quickmenu_configure"));
    if (!configureAction) {
        return;
    }

    configureAction->setData(m_selectedSlot);
    configureAction->trigger();
    m_configureTriggered = true;

    m_overlay->hide();
    m_overlay->setHighlightedSlot(-1);
}

QPoint KisTouchQuickMenuAction::touchGlobalPos(const QTouchEvent *event) const
{
    if (!event) {
        return QPoint();
    }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QList<QEventPoint> &points = event->points();
    if (points.isEmpty()) {
        return QPoint();
    }
    return points.constFirst().globalPosition().toPoint();
#else
    const QList<QTouchEvent::TouchPoint> &points = event->touchPoints();
    if (points.isEmpty()) {
        return QPoint();
    }
    return points.constFirst().screenPos().toPoint();
#endif
}

int KisTouchQuickMenuAction::slotForDelta(const QPointF &delta) const
{
    constexpr qreal kMinRadiusPx = 50.0;
    constexpr qreal kPi = 3.14159265358979323846;

    const qreal dx = delta.x();
    const qreal dy = delta.y();
    const qreal r = std::hypot(dx, dy);
    if (r < kMinRadiusPx) {
        return -1;
    }

    qreal deg = std::atan2(dy, dx) * 180.0 / kPi;
    if (deg < 0.0) {
        deg += 360.0;
    }

    // Slot order matches KisTouchQuickMenuOverlay layout.
    const qreal centersDeg[6] = {270.0, 330.0, 30.0, 90.0, 150.0, 210.0};

    int bestSlot = 0;
    qreal bestDist = angularDistanceDeg(deg, centersDeg[0]);
    for (int i = 1; i < 6; ++i) {
        const qreal dist = angularDistanceDeg(deg, centersDeg[i]);
        if (dist < bestDist) {
            bestDist = dist;
            bestSlot = i;
        }
    }

    return bestSlot;
}
