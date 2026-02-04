/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_TOUCH_UI_METRICS_H
#define KIS_TOUCH_UI_METRICS_H

#include <algorithm>
#include <limits>

#include <QScreen>
#include <QSize>
#include <QtGlobal>

namespace KisTouchUiMetrics
{

inline qreal scaleForScreen(const QScreen *screen)
{
    if (!screen) {
        return 1.0;
    }

    const QSize avail = screen->availableGeometry().size();
    const int minDim = std::min(avail.width(), avail.height());

    // Touch UI is tuned for tablet-sized viewports. On phones, fixed logical-pixel sizes
    // can become disproportionately large relative to the available screen width/height.
    //
    // Use the shorter available screen dimension as a proxy for "how much UI fits" and
    // scale sizes proportionally.
    //
    // Design target: ~720dp min dimension (small tablet / large phone landscape baseline).
    constexpr qreal kDesignMinDim = 720.0;
    constexpr qreal kMinScale = 0.4;
    constexpr qreal kMaxScale = 1.0;

    const qreal rawScale = minDim > 0 ? qreal(minDim) / kDesignMinDim : 1.0;
    return std::clamp(rawScale, kMinScale, kMaxScale);
}

inline int px(qreal basePx,
              qreal scale,
              int minPx = 1,
              int maxPx = std::numeric_limits<int>::max())
{
    return std::clamp(qRound(basePx * scale), minPx, maxPx);
}

inline QSize pxSize(const QSize &basePx, qreal scale)
{
    return QSize(px(basePx.width(), scale), px(basePx.height(), scale));
}

} // namespace KisTouchUiMetrics

#endif // KIS_TOUCH_UI_METRICS_H
