/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kis_touch_color_picker_button.h"

#include <kis_icon.h>

#include <KoCanvasResourceProvider.h>
#include <KoCanvasResourcesIds.h>

#include <klocalizedstring.h>

#include <QApplication>
#include <QDropEvent>
#include <QDockWidget>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QTouchEvent>

namespace {

constexpr int kLongPressDelayMs = 400;
constexpr int kTouchSlopSquared = 16 * 16;
constexpr int kColorDropOverlaySizePx = 44;

QString rgbaString(const QColor &c)
{
    if (!c.isValid()) {
        return QString();
    }

    return QStringLiteral("#%1%2%3%4")
        .arg(c.red(), 2, 16, QLatin1Char('0'))
        .arg(c.green(), 2, 16, QLatin1Char('0'))
        .arg(c.blue(), 2, 16, QLatin1Char('0'))
        .arg(c.alpha(), 2, 16, QLatin1Char('0'));
}

QPixmap makeColorDiskPixmap(const QColor &color, int sizePx, qreal devicePixelRatio)
{
    const int scaledSizePx = qMax(1, qRound(sizePx * devicePixelRatio));
    QPixmap pixmap(scaledSizePx, scaledSizePx);
    pixmap.setDevicePixelRatio(devicePixelRatio);
    pixmap.fill(Qt::transparent);

    if (!color.isValid() || sizePx <= 0) {
        return pixmap;
    }

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r(0.5, 0.5, sizePx - 1.0, sizePx - 1.0);
    const QPainterPath diskPath = [&]() {
        QPainterPath path;
        path.addEllipse(r);
        return path;
    }();

    p.save();
    p.setClipPath(diskPath);

    if (color.alpha() < 255) {
        // Checkerboard background for transparent colors.
        const int tile = qMax(2, sizePx / 6);
        QPixmap check(tile * 2, tile * 2);
        check.fill(QColor(220, 220, 220));
        QPainter cp(&check);
        cp.fillRect(0, 0, tile, tile, QColor(180, 180, 180));
        cp.fillRect(tile, tile, tile, tile, QColor(180, 180, 180));
        cp.end();
        p.fillRect(r, QBrush(check));
    }

    p.fillRect(r, color);
    p.restore();

    // Border ring that reads on both light/dark backgrounds.
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 0, 0, 110), 1.0));
    p.drawEllipse(r);
    p.setPen(QPen(QColor(255, 255, 255, 90), 1.0));
    p.drawEllipse(r.adjusted(1.0, 1.0, -1.0, -1.0));

    return pixmap;
}

} // namespace

KisTouchColorPickerButton::KisTouchColorPickerButton(QWidget *parent)
    : QToolButton(parent)
{
    setAutoRaise(true);
    setToolButtonStyle(Qt::ToolButtonIconOnly);
    setFocusPolicy(Qt::NoFocus);
    setCheckable(false);
    setAttribute(Qt::WA_AcceptTouchEvents, true);

    setObjectName(QStringLiteral("touchColorPickerButton"));
    setToolTip(i18n("Color"));

    m_longPressTimer.setSingleShot(true);
    m_longPressTimer.setInterval(kLongPressDelayMs);
    connect(&m_longPressTimer, &QTimer::timeout, this, &KisTouchColorPickerButton::slotLongPressTriggered);

    connect(this, &QToolButton::clicked, this, &KisTouchColorPickerButton::slotClicked);

    // Default/fallback icon (until we have a foreground color).
    setIcon(KisIconUtils::loadIcon(QStringLiteral("extended_color_selector")));
}

KisTouchColorPickerButton::~KisTouchColorPickerButton()
{
    QObject::disconnect(m_resourceChangedConnection);
    m_resourceChangedConnection = QMetaObject::Connection();
}

void KisTouchColorPickerButton::setColorDock(QDockWidget *dock)
{
    m_colorDock = dock;
}

void KisTouchColorPickerButton::setResourceManager(KoCanvasResourceProvider *resourceManager)
{
    if (m_resourceManager == resourceManager) {
        return;
    }

    QObject::disconnect(m_resourceChangedConnection);
    m_resourceChangedConnection = QMetaObject::Connection();
    m_resourceManager = resourceManager;

    if (!m_resourceManager) {
        m_hasValidColor = false;
        refreshIcon();
        return;
    }

    const KoColor fg = m_resourceManager->resource(KoCanvasResource::ForegroundColor).value<KoColor>();
    setColor(fg);

    m_resourceChangedConnection =
        connect(m_resourceManager, &KoCanvasResourceProvider::canvasResourceChanged,
                this, &KisTouchColorPickerButton::slotResourceChanged);
}

QColor KisTouchColorPickerButton::currentColor() const
{
    return m_hasValidColor ? m_color.toQColor() : QColor();
}

void KisTouchColorPickerButton::refreshIcon()
{
    updateDiskIcon();
}

bool KisTouchColorPickerButton::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::TouchBegin: {
        QTouchEvent *touchEvent = static_cast<QTouchEvent *>(event);
        if (touchEvent->touchPoints().isEmpty()) {
            event->accept();
            return true;
        }

        const QTouchEvent::TouchPoint &pt = touchEvent->touchPoints().first();
        m_pressPos = pt.pos().toPoint();
        m_lastGlobalPos = pt.screenPos().toPoint();
        m_pressActive = true;
        m_longPressActive = false;
        m_colorDropActive = false;
        m_suppressClick = false;
        m_longPressTimer.start();
        setDown(true);
        event->accept();
        return true;
    }
    case QEvent::TouchUpdate: {
        QTouchEvent *touchEvent = static_cast<QTouchEvent *>(event);
        if (!m_pressActive || touchEvent->touchPoints().isEmpty()) {
            event->accept();
            return true;
        }

        const QTouchEvent::TouchPoint &pt = touchEvent->touchPoints().first();
        const QPoint localPos = pt.pos().toPoint();
        const QPoint globalPos = pt.screenPos().toPoint();
        m_lastGlobalPos = globalPos;

        const QPoint delta = localPos - m_pressPos;
        const int deltaSquared = delta.x() * delta.x() + delta.y() * delta.y();
        if (!m_longPressActive && m_longPressTimer.isActive() && deltaSquared > kTouchSlopSquared) {
            m_longPressTimer.stop();
        }

        if (m_colorDropActive) {
            updateColorDropDrag(globalPos);
        }

        event->accept();
        return true;
    }
    case QEvent::TouchEnd: {
        QTouchEvent *touchEvent = static_cast<QTouchEvent *>(event);
        const QPoint globalPos =
            (!touchEvent->touchPoints().isEmpty()) ? touchEvent->touchPoints().first().screenPos().toPoint() : m_lastGlobalPos;
        m_lastGlobalPos = globalPos;

        m_longPressTimer.stop();

        if (m_colorDropActive) {
            endColorDropDrag(globalPos, false);
            m_pressActive = false;
            setDown(false);
            event->accept();
            return true;
        }

        const bool longPressed = m_longPressActive;
        m_pressActive = false;
        m_longPressActive = false;
        setDown(false);

        // Procreate-style: long press is for ColorDrop, not for toggling the panel.
        if (!longPressed) {
            slotClicked();
        }

        event->accept();
        return true;
    }
    case QEvent::TouchCancel: {
        m_longPressTimer.stop();
        endColorDropDrag(m_lastGlobalPos, true);
        m_pressActive = false;
        m_longPressActive = false;
        setDown(false);
        event->accept();
        return true;
    }
    default:
        break;
    }

    return QToolButton::event(event);
}

void KisTouchColorPickerButton::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressPos = event->pos();
        m_lastGlobalPos = event->globalPos();
        m_pressActive = true;
        m_longPressActive = false;
        m_colorDropActive = false;
        m_suppressClick = false;
        m_longPressTimer.start();
    }

    QToolButton::mousePressEvent(event);
}

void KisTouchColorPickerButton::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton)) {
        QToolButton::mouseMoveEvent(event);
        return;
    }

    m_lastGlobalPos = event->globalPos();
    const QPoint delta = event->pos() - m_pressPos;
    const int deltaSquared = delta.x() * delta.x() + delta.y() * delta.y();

    if (!m_longPressActive && m_longPressTimer.isActive() && deltaSquared > kTouchSlopSquared) {
        m_longPressTimer.stop();
    }

    if (m_colorDropActive) {
        updateColorDropDrag(event->globalPos());
        event->accept();
        return;
    }

    QToolButton::mouseMoveEvent(event);
}

void KisTouchColorPickerButton::mouseReleaseEvent(QMouseEvent *event)
{
    m_longPressTimer.stop();

    if (m_colorDropActive) {
        endColorDropDrag(event->globalPos(), false);
        m_pressActive = false;
        setDown(false);
        event->accept();
        return;
    }

    m_pressActive = false;
    QToolButton::mouseReleaseEvent(event);
}

void KisTouchColorPickerButton::leaveEvent(QEvent *event)
{
    if (m_longPressTimer.isActive() && !m_longPressActive) {
        m_longPressTimer.stop();
    }
    QToolButton::leaveEvent(event);
}

void KisTouchColorPickerButton::setColor(const KoColor &color)
{
    m_color = color;
    m_hasValidColor = true;
    updateDiskIcon();
}

void KisTouchColorPickerButton::updateDiskIcon()
{
    if (!m_hasValidColor) {
        setProperty("touchColorRgba", QVariant());
        setIcon(KisIconUtils::loadIcon(QStringLiteral("extended_color_selector")));
        return;
    }

    const QColor c = m_color.toQColor();
    if (!c.isValid()) {
        setProperty("touchColorRgba", QVariant());
        setIcon(KisIconUtils::loadIcon(QStringLiteral("extended_color_selector")));
        return;
    }

    setProperty("touchColorRgba", rgbaString(c));

    const int sizePx = qMax(18, iconSize().width());
    const qreal dpr = devicePixelRatioF();
    const QPixmap pix = makeColorDiskPixmap(c, sizePx, dpr);

    QIcon icon;
    icon.addPixmap(pix);
    setIcon(icon);
}

void KisTouchColorPickerButton::beginColorDropDrag(const QPoint &globalPos)
{
    if (m_colorDropActive) {
        return;
    }

    if (!m_hasValidColor) {
        return;
    }

    const QColor c = m_color.toQColor();
    if (!c.isValid()) {
        return;
    }

    m_colorDropActive = true;
    m_suppressClick = true;
    m_lastGlobalPos = globalPos;
    ensureColorDropOverlay();
    if (m_colorDropOverlay) {
        const QPixmap pix = makeColorDiskPixmap(c, kColorDropOverlaySizePx, m_colorDropOverlay->devicePixelRatioF());
        m_colorDropOverlay->setPixmap(pix);
    }
    updateColorDropDrag(globalPos);
}

void KisTouchColorPickerButton::updateColorDropDrag(const QPoint &globalPos)
{
    if (!m_colorDropActive) {
        return;
    }

    m_lastGlobalPos = globalPos;
    if (!m_colorDropOverlay) {
        return;
    }

    QWidget *anchorWindow = window();
    if (!anchorWindow) {
        anchorWindow = this;
    }

    const QPoint localPos = anchorWindow->mapFromGlobal(globalPos);
    const QSize size = m_colorDropOverlay->size();
    const QPoint desiredTopLeft = localPos - QPoint(size.width() / 2, size.height() / 2);
    m_colorDropOverlay->move(desiredTopLeft);
    m_colorDropOverlay->raise();
    if (!m_colorDropOverlay->isVisible()) {
        m_colorDropOverlay->show();
    }
}

void KisTouchColorPickerButton::endColorDropDrag(const QPoint &globalPos, bool canceled)
{
    if (!m_colorDropActive) {
        hideColorDropOverlay();
        return;
    }

    m_colorDropActive = false;
    m_longPressActive = false;

    hideColorDropOverlay();

    if (!canceled) {
        (void)performColorDropAtGlobalPos(globalPos);
    }
}

bool KisTouchColorPickerButton::performColorDropAtGlobalPos(const QPoint &globalPos) const
{
    if (!m_hasValidColor) {
        return false;
    }

    const QColor c = m_color.toQColor();
    if (!c.isValid()) {
        return false;
    }

    QWidget *targetWidget = QApplication::widgetAt(globalPos);
    QWidget *viewWidget = nullptr;
    for (QWidget *w = targetWidget; w; w = w->parentWidget()) {
        if (w->inherits("KisView")) {
            viewWidget = w;
            break;
        }
    }
    if (!viewWidget) {
        return false;
    }

    const QPoint viewPos = viewWidget->mapFromGlobal(globalPos);
    if (!viewWidget->rect().contains(viewPos)) {
        return false;
    }

    QMimeData mime;
    mime.setColorData(c);
    mime.setText(c.name(QColor::HexArgb));

    QDragEnterEvent enterEvent(viewPos, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewWidget, &enterEvent);

    QDropEvent dropEvent(viewPos, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    dropEvent.setDropAction(Qt::CopyAction);
    QApplication::sendEvent(viewWidget, &dropEvent);

    return true;
}

void KisTouchColorPickerButton::ensureColorDropOverlay()
{
    if (m_colorDropOverlay) {
        return;
    }

    QWidget *anchorWindow = window();
    if (!anchorWindow) {
        anchorWindow = this;
    }

    auto *overlay = new QLabel(anchorWindow);
    overlay->setObjectName(QStringLiteral("touchColorDropOverlay"));
    overlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    overlay->setAttribute(Qt::WA_ShowWithoutActivating, true);
    overlay->setAlignment(Qt::AlignCenter);
    overlay->setFixedSize(kColorDropOverlaySizePx, kColorDropOverlaySizePx);

    if (m_hasValidColor) {
        const QColor c = m_color.toQColor();
        const QPixmap pix = makeColorDiskPixmap(c, kColorDropOverlaySizePx, overlay->devicePixelRatioF());
        overlay->setPixmap(pix);
    }

    overlay->hide();
    m_colorDropOverlay = overlay;
}

void KisTouchColorPickerButton::hideColorDropOverlay()
{
    if (m_colorDropOverlay) {
        m_colorDropOverlay->hide();
    }
}

void KisTouchColorPickerButton::slotResourceChanged(int key, const QVariant &value)
{
    if (key != KoCanvasResource::ForegroundColor) {
        return;
    }

    setColor(value.value<KoColor>());
}

void KisTouchColorPickerButton::slotLongPressTriggered()
{
    m_longPressActive = true;
    m_suppressClick = true;

    if (m_pressActive && !m_colorDropActive) {
        const QPoint globalPos = m_lastGlobalPos.isNull() ? mapToGlobal(m_pressPos) : m_lastGlobalPos;
        beginColorDropDrag(globalPos);
        setDown(false);
    }
}

void KisTouchColorPickerButton::slotClicked()
{
    if (m_suppressClick) {
        m_suppressClick = false;
        return;
    }

    if (!m_colorDock) {
        return;
    }

    if (m_colorDock->isVisible()) {
        m_colorDock->hide();
        return;
    }

    m_colorDock->show();
    m_colorDock->raise();
}
