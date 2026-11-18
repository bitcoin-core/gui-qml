// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_WINDOWGEOMETRY_H
#define BITCOIN_QML_WINDOWGEOMETRY_H

#include <QRect>
#include <QRectF>
#include <limits>
#include <cmath>
#include <cstddef>
#include <QSize>
#include <algorithm>
#include <span>

namespace windowgeometry {
/** Bounded presentation policy. Screen rectangles come from Qt on GUI. This
 * value function performs no settings, Core, I/O, or platform window effects. */
inline QRect Restore(QRectF saved, QSize minimum, std::span<const QRect> screens, QRect fallback)
{
    if (saved.isEmpty() || !std::isfinite(saved.x()) || !std::isfinite(saved.y()) || !std::isfinite(saved.width()) || !std::isfinite(saved.height())) saved = fallback;
    QRect target;
    double largest{0};
    for (const auto& screen : screens.first(std::min<size_t>(screens.size(), 32))) {
        if (screen.isEmpty()) continue;
        const auto overlap = saved.intersected(QRectF{screen});
        const auto area = overlap.width() * overlap.height();
        if (area > largest) { largest = area; target = screen; }
    }
    if (target.isEmpty()) {
        for (const auto& screen : screens.first(std::min<size_t>(screens.size(), 32))) {
            if (!screen.isEmpty()) { target = screen; break; }
        }
    }
    auto size = saved.size().expandedTo(QSizeF{minimum}).expandedTo(QSizeF{1, 1});
    // Below the supported minimum, keep the minimum and expose the title bar;
    // unavailable display pixels cannot be made visible by a restore policy.
    if (!target.isEmpty()) {
        size.setWidth(std::max(double(minimum.width()), std::min(size.width(), double(target.width()))));
        size.setHeight(std::max(double(minimum.height()), std::min(size.height(), double(target.height()))));
        const auto max_x = double(target.x()) + std::max(0.0, target.width() - size.width());
        const auto max_y = double(target.y()) + std::max(0.0, target.height() - size.height());
        saved.moveLeft(std::clamp(saved.x(), double(target.x()), max_x));
        saved.moveTop(std::clamp(saved.y(), double(target.y()), max_y));
    }
    const int width = int(std::min(size.width(), double(std::numeric_limits<int>::max())));
    const int height = int(std::min(size.height(), double(std::numeric_limits<int>::max())));
    // Keep QRect's endpoint representation in range even without a screen.
    const int x = int(std::clamp(saved.x(), double(std::numeric_limits<int>::min()), double(std::numeric_limits<int>::max() - width)));
    const int y = int(std::clamp(saved.y(), double(std::numeric_limits<int>::min()), double(std::numeric_limits<int>::max() - height)));
    return {x, y, width, height};
}
} // namespace windowgeometry
#endif // BITCOIN_QML_WINDOWGEOMETRY_H
