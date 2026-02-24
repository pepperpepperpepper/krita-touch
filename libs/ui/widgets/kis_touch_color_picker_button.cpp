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
#include <QDrag>
#include <QDockWidget>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace {

constexpr int kLongPressDelayMs = 400;
constexpr int kTouchSlopSquared = 16 * 16;

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

void KisTouchColorPickerButton::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressPos = event->pos();
        m_longPressActive = false;
        m_dragInProgress = false;
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

    const QPoint delta = event->pos() - m_pressPos;
    const int deltaSquared = delta.x() * delta.x() + delta.y() * delta.y();

    if (!m_longPressActive && m_longPressTimer.isActive() && deltaSquared > kTouchSlopSquared) {
        m_longPressTimer.stop();
    }

    if (m_longPressActive && !m_dragInProgress && dragDistanceReached(event->pos())) {
        m_dragInProgress = true;
        m_suppressClick = true;
        m_longPressTimer.stop();
        startColorDrag();
        setDown(false);
        event->accept();
        return;
    }

    QToolButton::mouseMoveEvent(event);
}

void KisTouchColorPickerButton::mouseReleaseEvent(QMouseEvent *event)
{
    m_longPressTimer.stop();

    if (m_dragInProgress) {
        m_dragInProgress = false;
        setDown(false);
        event->accept();
        return;
    }

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

bool KisTouchColorPickerButton::dragDistanceReached(const QPoint &pos) const
{
    const int dist = (pos - m_pressPos).manhattanLength();
    return dist >= QApplication::startDragDistance();
}

void KisTouchColorPickerButton::startColorDrag()
{
    if (!m_hasValidColor) {
        return;
    }

    const QColor c = m_color.toQColor();
    if (!c.isValid()) {
        return;
    }

    auto *drag = new QDrag(this);
    auto *mime = new QMimeData;
    mime->setColorData(c);
    mime->setText(c.name(QColor::HexArgb));
    drag->setMimeData(mime);

    const int pixSizePx = qMax(32, iconSize().width());
    const QPixmap pix = makeColorDiskPixmap(c, pixSizePx, devicePixelRatioF());
    drag->setPixmap(pix);
    drag->setHotSpot(QPoint(pixSizePx / 2, pixSizePx / 2));

    drag->exec(Qt::CopyAction);
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
