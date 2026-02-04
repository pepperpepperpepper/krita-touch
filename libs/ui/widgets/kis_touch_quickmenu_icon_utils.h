/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_TOUCH_QUICKMENU_ICON_UTILS_H
#define KIS_TOUCH_QUICKMENU_ICON_UTILS_H

#include <QAction>
#include <QGuiApplication>
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QRect>
#include <QSize>
#include <QScreen>
#include <QString>
#include <QtGlobal>

#include <kis_icon_utils.h>

namespace KisTouchQuickMenuIconUtils
{

struct IconSpec {
    QString iconNameOverride;
    qreal contentScale = 0.82; // 1.0 means "fill the square", lower adds padding.
};

inline const QHash<QString, IconSpec> &quickMenuIconSpecs()
{
    // Touch-only overrides so the QuickMenu can present a consistent icon set
    // without changing global action icons or desktop behavior.
    //
    // The contentScale is applied after trimming transparent margins; it helps
    // keep "full-frame" tool icons (e.g. transform) visually balanced against
    // lighter-weight action icons (e.g. undo/redo).
    static const QHash<QString, IconSpec> s_specs{
        {QStringLiteral("edit_undo"), {QStringLiteral("edit-undo"), 0.82}},
        {QStringLiteral("edit_redo"), {QStringLiteral("edit-redo"), 0.82}},
        {QStringLiteral("deselect"), {QStringLiteral("select-clear"), 0.82}},
        {QStringLiteral("KisToolSelectTouch"), {QStringLiteral("tool_outline_selection"), 0.80}},
        {QStringLiteral("KisToolTransform"), {QStringLiteral("krita_tool_transform"), 0.78}},
        // Global action currently uses a generic document icon; QuickMenu needs a
        // "canvas only" specific glyph without affecting the rest of the UI.
        {QStringLiteral("view_show_canvas_only"), {QStringLiteral("config-canvas-only"), 0.80}},
    };
    return s_specs;
}

inline QRect alphaBounds(const QImage &image, int alphaThreshold)
{
    const int width = image.width();
    const int height = image.height();

    int minX = width;
    int minY = height;
    int maxX = -1;
    int maxY = -1;

    for (int y = 0; y < height; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < width; ++x) {
            if (qAlpha(line[x]) > alphaThreshold) {
                minX = qMin(minX, x);
                minY = qMin(minY, y);
                maxX = qMax(maxX, x);
                maxY = qMax(maxY, y);
            }
        }
    }

    if (maxX < minX || maxY < minY) {
        return QRect();
    }

    return QRect(minX, minY, (maxX - minX) + 1, (maxY - minY) + 1);
}

inline QIcon normalizeIcon(const QIcon &icon,
                           const QSize &logicalSize,
                           qreal contentScale,
                           qreal devicePixelRatio,
                           int alphaThreshold = 2)
{
    if (icon.isNull() || logicalSize.isEmpty()) {
        return icon;
    }

    const qreal dpr = devicePixelRatio > 0.0 ? devicePixelRatio : 1.0;
    const QSize deviceSize(qMax(1, qRound(logicalSize.width() * dpr)),
                           qMax(1, qRound(logicalSize.height() * dpr)));

    QImage rendered(deviceSize, QImage::Format_ARGB32_Premultiplied);
    rendered.fill(Qt::transparent);
    {
        QPainter p(&rendered);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        // Render at device pixel resolution to keep the normalized icon crisp on HiDPI.
        icon.paint(&p, QRect(QPoint(0, 0), deviceSize), Qt::AlignCenter, QIcon::Normal, QIcon::Off);
    }

    // Analyze in device pixels for best results on HiDPI.
    const QImage renderedDevice = rendered.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QRect bounds = alphaBounds(renderedDevice, alphaThreshold);
    if (bounds.isEmpty()) {
        return icon;
    }

    // Keep some padding after trimming margins.
    const int availW = qMax(1, qRound(deviceSize.width() * contentScale));
    const int availH = qMax(1, qRound(deviceSize.height() * contentScale));

    const qreal scale = qMin(availW / qreal(bounds.width()), availH / qreal(bounds.height()));
    if (!(scale > 0.0)) {
        return icon;
    }

    const QSize scaledSize(qMax(1, qRound(bounds.width() * scale)),
                           qMax(1, qRound(bounds.height() * scale)));
    const QRect destRect((deviceSize.width() - scaledSize.width()) / 2,
                         (deviceSize.height() - scaledSize.height()) / 2,
                         scaledSize.width(),
                         scaledSize.height());

    QImage normalized(deviceSize, QImage::Format_ARGB32_Premultiplied);
    normalized.fill(Qt::transparent);
    {
        QPainter p(&normalized);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawImage(destRect, renderedDevice, bounds);
    }

    QPixmap pixmap = QPixmap::fromImage(normalized);
    pixmap.setDevicePixelRatio(dpr);
    return QIcon(pixmap);
}

inline QIcon iconForActionId(const QString &actionId, const QAction *action, const QSize &logicalSize)
{
    QIcon baseIcon;
    qreal contentScale = 0.82;

    const auto &specs = quickMenuIconSpecs();
    const auto it = specs.constFind(actionId);
    if (it != specs.constEnd()) {
        contentScale = it->contentScale;
        if (!it->iconNameOverride.isEmpty()) {
            baseIcon = KisIconUtils::loadIcon(it->iconNameOverride);
        }
    }

    if (baseIcon.isNull() && action) {
        baseIcon = action->icon();
    }

    if (baseIcon.isNull()) {
        return QIcon();
    }

    QScreen *screen = QGuiApplication::primaryScreen();
    const qreal dpr = screen ? screen->devicePixelRatio() : 1.0;

    static QHash<QString, QIcon> s_normalizedCache;
    const QString cacheKey = QStringLiteral("%1:%2x%3:%4:%5")
                                 .arg(QString::number(baseIcon.cacheKey()),
                                      QString::number(logicalSize.width()),
                                      QString::number(logicalSize.height()),
                                      QString::number(contentScale, 'f', 3),
                                      QString::number(dpr, 'f', 3));

    const auto cached = s_normalizedCache.constFind(cacheKey);
    if (cached != s_normalizedCache.constEnd()) {
        return cached.value();
    }

    const QIcon normalized = normalizeIcon(baseIcon, logicalSize, contentScale, dpr);
    s_normalizedCache.insert(cacheKey, normalized);
    return normalized;
}

} // namespace KisTouchQuickMenuIconUtils

#endif // KIS_TOUCH_QUICKMENU_ICON_UTILS_H
