/*
 *  kis_tool_freehand.cc - part of Krita
 *
 *  SPDX-FileCopyrightText: 2003-2007 Boudewijn Rempt <boud@valdyas.org>
 *  SPDX-FileCopyrightText: 2004 Bart Coppens <kde@bartcoppens.be>
 *  SPDX-FileCopyrightText: 2007, 2008, 2010 Cyrille Berger <cberger@cberger.net>
 *  SPDX-FileCopyrightText: 2009 Lukáš Tvrdý <lukast.dev@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kis_tool_freehand.h"
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QPainter>
#include <QRect>
#include <QTimer>
#include <QThreadPool>
#include <QToolButton>
#include <QApplication>
#include <QScreen>
#include <QLineF>

#include <Eigen/Core>

#include <algorithm>
#include <cmath>
#include <limits>

#include <kis_icon.h>
#include <KoPointerEvent.h>
#include <KoViewConverter.h>
#include <KoCanvasController.h>

//pop up palette
#include <kis_canvas_resource_provider.h>

// Krita/image
#include <kis_layer.h>
#include <kis_paint_layer.h>
#include <kis_painter.h>
#include <brushengine/kis_paintop.h>
#include <kis_selection.h>
#include <kis_undo_adapter.h>
#include <brushengine/kis_paintop_preset.h>
#include <brushengine/KisOptimizedBrushOutline.h>


// Krita/ui
#include "kis_abstract_perspective_grid.h"
#include "kis_config.h"
#include "kis_config_notifier.h"
#include "kis_image_config.h"
#include "canvas/kis_canvas2.h"
#include "kis_cursor.h"
#include <KisViewManager.h>
#include <kis_painting_assistants_decoration.h>
#include "kis_painting_information_builder.h"
#include "kis_tool_freehand_helper.h"
#include "strokes/freehand_stroke.h"
#include "kis_tool_utils.h"
#include "kis_figure_painting_tool_helper.h"
#include "input/kis_input_manager.h"

using namespace std::placeholders; // For _1 placeholder

namespace {

struct TouchQuickShapeLine {
    QPointF p0;
    QPointF p1;
};

constexpr qreal kPi = 3.14159265358979323846;

QRectF boundingRectForPoints(const QVector<QPointF> &points)
{
    if (points.isEmpty()) {
        return QRectF();
    }

    qreal minX = points.first().x();
    qreal minY = points.first().y();
    qreal maxX = points.first().x();
    qreal maxY = points.first().y();

    for (const QPointF &p : points) {
        minX = qMin(minX, p.x());
        minY = qMin(minY, p.y());
        maxX = qMax(maxX, p.x());
        maxY = qMax(maxY, p.y());
    }

    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY)).normalized();
}

QRectF squareFromRectPreserveCenter(const QRectF &rect)
{
    const QRectF r = rect.normalized();
    const QPointF c = r.center();
    const qreal side = qMax(r.width(), r.height());
    return QRectF(c.x() - side * 0.5, c.y() - side * 0.5, side, side).normalized();
}

qreal polygonArea2(const vQPointF &poly)
{
    const int n = poly.size();
    if (n < 3) {
        return 0.0;
    }

    qreal area2 = 0.0;
    for (int i = 0; i < n; i++) {
        const QPointF p0 = poly[i];
        const QPointF p1 = poly[(i + 1) % n];
        area2 += p0.x() * p1.y() - p1.x() * p0.y();
    }
    return area2;
}

vQPointF regularPolygonFromPolygonPreserveCentroid(const vQPointF &poly)
{
    const int n = poly.size();
    if (n < 3) {
        return poly;
    }

    QPointF c;
    for (const QPointF &p : poly) {
        c += p;
    }
    c /= qreal(n);

    qreal r = 0.0;
    for (const QPointF &p : poly) {
        r += QLineF(c, p).length();
    }
    r /= qreal(n);
    if (r <= 1e-3) {
        return poly;
    }

    const qreal baseAngle = std::atan2(poly[0].y() - c.y(), poly[0].x() - c.x());
    const qreal step = (2.0 * kPi) / qreal(n);
    const bool ccw = polygonArea2(poly) >= 0.0;

    vQPointF out;
    out.reserve(n);

    for (int i = 0; i < n; i++) {
        const qreal angle = ccw ? (baseAngle + qreal(i) * step) : (baseAngle - qreal(i) * step);
        out.append(QPointF(c.x() + r * std::cos(angle), c.y() + r * std::sin(angle)));
    }

    return out;
}

TouchQuickShapeLine angleSnappedLine45(const TouchQuickShapeLine &line)
{
    const QPointF p0 = line.p0;
    const QPointF p1 = line.p1;
    const QPointF d = p1 - p0;
    const qreal len = std::hypot(d.x(), d.y());
    if (len <= 1e-3) {
        return line;
    }

    const QPointF mid = (p0 + p1) * 0.5;
    const qreal angle = std::atan2(d.y(), d.x());
    const qreal step = kPi / 4.0; // 45 degrees
    const qreal snappedAngle = std::round(angle / step) * step;
    const QPointF dir(std::cos(snappedAngle), std::sin(snappedAngle));
    const QPointF half = dir * (len * 0.5);
    return TouchQuickShapeLine{mid - half, mid + half};
}

qreal polylineLength(const QVector<QPointF> &points)
{
    if (points.size() < 2) {
        return 0.0;
    }

    qreal length = 0.0;
    for (int i = 1; i < points.size(); i++) {
        length += QLineF(points[i - 1], points[i]).length();
    }
    return length;
}

qreal distancePointToSegment(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const qreal len2 = QPointF::dotProduct(ab, ab);
    if (len2 <= 1e-6) {
        return QLineF(p, a).length();
    }

    const qreal t = qBound<qreal>(0.0, QPointF::dotProduct(p - a, ab) / len2, 1.0);
    const QPointF proj = a + t * ab;
    return QLineF(p, proj).length();
}

std::optional<TouchQuickShapeLine> detectTouchQuickShapeLine(const QVector<QPointF> &points)
{
    if (points.size() < 2) {
        return std::nullopt;
    }

    const QPointF p0 = points.first();
    const QPointF p1 = points.last();

    const qreal length = QLineF(p0, p1).length();
    if (length < 80.0) {
        return std::nullopt;
    }

    const qreal tolerance = qMax<qreal>(3.0, length * 0.02);

    for (int i = 1; i < points.size() - 1; i++) {
        if (distancePointToSegment(points[i], p0, p1) > tolerance) {
            return std::nullopt;
        }
    }

    return TouchQuickShapeLine{p0, p1};
}

std::optional<QRectF> detectTouchQuickShapeEllipse(const QVector<QPointF> &points)
{
    if (points.size() < 12) {
        return std::nullopt;
    }

    const QRectF bounds = boundingRectForPoints(points);
    const qreal w = bounds.width();
    const qreal h = bounds.height();

    const qreal minDim = qMin(w, h);
    const qreal maxDim = qMax(w, h);
    if (minDim < 48.0 || maxDim < 96.0) {
        return std::nullopt;
    }

    // Avoid snapping long, thin strokes (likely intended as a line).
    if (maxDim / qMax<qreal>(1.0, minDim) > 3.5) {
        return std::nullopt;
    }

    // Closed-ish stroke: start and end should be near.
    const qreal closureTol = qMax<qreal>(12.0, minDim * 0.25);
    if (QLineF(points.first(), points.last()).length() > closureTol) {
        return std::nullopt;
    }

    const QPointF center = bounds.center();
    const qreal rx = w * 0.5;
    const qreal ry = h * 0.5;
    if (rx < 1.0 || ry < 1.0) {
        return std::nullopt;
    }

    // Ensure the stroke covers most of the ellipse angles, and is not too "boxy".
    constexpr int kBins = 12;
    bool binsSeen[kBins] = {};
    int binsFilled = 0;

    qreal maxNormalizedErr = 0.0;
    qreal sumNormalizedErr = 0.0;

    constexpr qreal kPi = 3.14159265358979323846;

    for (const QPointF &p : points) {
        const qreal dx = (p.x() - center.x()) / rx;
        const qreal dy = (p.y() - center.y()) / ry;
        const qreal r = std::sqrt(dx * dx + dy * dy);

        const qreal err = std::abs(r - 1.0);
        maxNormalizedErr = qMax(maxNormalizedErr, err);
        sumNormalizedErr += err;

        const qreal angle = std::atan2(dy, dx);
        const qreal t = (angle + kPi) / (2.0 * kPi);
        int bin = qBound(0, int(t * kBins), kBins - 1);
        if (!binsSeen[bin]) {
            binsSeen[bin] = true;
            binsFilled++;
        }
    }

    const qreal meanNormalizedErr = sumNormalizedErr / qMax(1, points.size());
    if (maxNormalizedErr > 0.32 || meanNormalizedErr > 0.16) {
        return std::nullopt;
    }

    if (binsFilled < 9) {
        return std::nullopt;
    }

    // Require at least ~1 loop around the shape.
    const qreal pathLen = polylineLength(points);
    const qreal circumference =
        kPi * (3.0 * (rx + ry) - std::sqrt((3.0 * rx + ry) * (rx + 3.0 * ry)));
    if (pathLen < circumference * 0.55) {
        return std::nullopt;
    }

    QRectF rect = bounds;
    // If it's nearly a circle, snap to a true circle while preserving center.
    if (maxDim > 1.0 && std::abs(w - h) / maxDim < 0.12) {
        rect = QRectF(center.x() - maxDim * 0.5, center.y() - maxDim * 0.5, maxDim, maxDim);
    }

    return rect.normalized();
}

std::optional<QRectF> detectTouchQuickShapeRect(const QVector<QPointF> &points)
{
    if (points.size() < 16) {
        return std::nullopt;
    }

    const QRectF bounds = boundingRectForPoints(points);
    const qreal w = bounds.width();
    const qreal h = bounds.height();

    const qreal minDim = qMin(w, h);
    const qreal maxDim = qMax(w, h);
    if (minDim < 48.0 || maxDim < 96.0) {
        return std::nullopt;
    }

    // Closed-ish stroke: start and end should be near.
    const qreal closureTol = qMax<qreal>(16.0, minDim * 0.25);
    if (QLineF(points.first(), points.last()).length() > closureTol) {
        return std::nullopt;
    }

    const qreal tolerance = qMax<qreal>(6.0, minDim * 0.08);

    int interiorCount = 0;
    int edgeCounts[4] = {0, 0, 0, 0}; // left, right, top, bottom

    struct RectRun {
        int edge = -1;
        int count = 0;
    };

    QVector<RectRun> runs;
    runs.reserve(16);

    auto pushEdge = [&runs](int edge) {
        if (!runs.isEmpty() && runs.last().edge == edge) {
            runs.last().count++;
            return;
        }

        RectRun run;
        run.edge = edge;
        run.count = 1;
        runs.append(run);
    };

    for (const QPointF &p : points) {
        const qreal dl = std::abs(p.x() - bounds.left());
        const qreal dr = std::abs(p.x() - bounds.right());
        const qreal dt = std::abs(p.y() - bounds.top());
        const qreal db = std::abs(p.y() - bounds.bottom());

        const qreal minVert = qMin(dl, dr);
        const qreal minHoriz = qMin(dt, db);

        const bool nearVert = minVert <= tolerance;
        const bool nearHoriz = minHoriz <= tolerance;

        if (!nearVert && !nearHoriz) {
            interiorCount++;
            continue;
        }

        // Corner points can jitter between edges. Skip them for edge ordering.
        if (nearVert && nearHoriz) {
            continue;
        }

        int edge = -1;
        if (nearVert && (!nearHoriz || minVert < minHoriz)) {
            edge = (dl <= dr) ? 0 : 1;
        } else {
            edge = (dt <= db) ? 2 : 3;
        }

        edgeCounts[edge]++;
        pushEdge(edge);
    }

    if (interiorCount > points.size() * 0.22) {
        return std::nullopt;
    }

    const int minEdgeCount = qMax(3, int(points.size() * 0.06));
    for (int i = 0; i < 4; i++) {
        if (edgeCounts[i] < minEdgeCount) {
            return std::nullopt;
        }
    }

    if (runs.size() < 4) {
        return std::nullopt;
    }

    if (runs.size() > 1 && runs.first().edge == runs.last().edge) {
        runs.first().count += runs.last().count;
        runs.removeLast();
    }

    // Remove tiny backtracking runs: A-B-A, where B is a 1-2 point jitter run.
    for (int iter = 0; iter < 4; iter++) {
        bool changed = false;
        for (int i = 1; i < runs.size() - 1; i++) {
            if (runs[i].count <= 2 && runs[i - 1].edge == runs[i + 1].edge) {
                runs[i - 1].count += runs[i].count + runs[i + 1].count;
                runs.remove(i); // B
                runs.remove(i); // second A
                changed = true;
                break;
            }
        }
        if (!changed) {
            break;
        }
    }

    if (runs.size() < 4 || runs.size() > 12) {
        return std::nullopt;
    }

    bool edgePresent[4] = {false, false, false, false};
    for (const RectRun &run : runs) {
        edgePresent[run.edge] = true;
    }
    for (int i = 0; i < 4; i++) {
        if (!edgePresent[i]) {
            return std::nullopt;
        }
    }

    auto fitsOrder = [&runs](bool clockwise) {
        auto nextEdge = [clockwise](int edge) {
            if (clockwise) {
                switch (edge) {
                case 2: return 1; // top -> right
                case 1: return 3; // right -> bottom
                case 3: return 0; // bottom -> left
                case 0: return 2; // left -> top
                }
            } else {
                switch (edge) {
                case 2: return 0; // top -> left
                case 0: return 3; // left -> bottom
                case 3: return 1; // bottom -> right
                case 1: return 2; // right -> top
                }
            }
            return edge;
        };

        for (int i = 1; i < runs.size(); i++) {
            const int prev = runs[i - 1].edge;
            const int cur = runs[i].edge;
            if (cur != nextEdge(prev)) {
                return false;
            }
        }
        return true;
    };

    if (!fitsOrder(true) && !fitsOrder(false)) {
        return std::nullopt;
    }

    const qreal pathLen = polylineLength(points);
    const qreal perimeter = 2.0 * (w + h);
    if (perimeter < 1.0 || pathLen < perimeter * 0.65 || pathLen > perimeter * 3.0) {
        return std::nullopt;
    }

    return bounds.normalized();
}

qreal cross2d(const QPointF &o, const QPointF &a, const QPointF &b)
{
    return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x());
}

std::optional<vQPointF> detectTouchQuickShapeTriangle(const QVector<QPointF> &points)
{
    if (points.size() < 16) {
        return std::nullopt;
    }

    const QRectF bounds = boundingRectForPoints(points);
    const qreal w = bounds.width();
    const qreal h = bounds.height();

    const qreal minDim = qMin(w, h);
    const qreal maxDim = qMax(w, h);
    if (minDim < 48.0 || maxDim < 96.0) {
        return std::nullopt;
    }

    // Closed-ish stroke: start and end should be near.
    const qreal closureTol = qMax<qreal>(16.0, minDim * 0.25);
    if (QLineF(points.first(), points.last()).length() > closureTol) {
        return std::nullopt;
    }

    // Pick 3 candidate vertices: farthest from center, farthest from that, then farthest from the AB segment.
    const QPointF boundsCenter = bounds.center();

    QPointF pA = points.first();
    qreal bestDist2 = -1.0;
    for (const QPointF &p : points) {
        const QPointF d = p - boundsCenter;
        const qreal dist2 = QPointF::dotProduct(d, d);
        if (dist2 > bestDist2) {
            bestDist2 = dist2;
            pA = p;
        }
    }

    QPointF pB = points.first();
    bestDist2 = -1.0;
    for (const QPointF &p : points) {
        const QPointF d = p - pA;
        const qreal dist2 = QPointF::dotProduct(d, d);
        if (dist2 > bestDist2) {
            bestDist2 = dist2;
            pB = p;
        }
    }

    QPointF pC = points.first();
    qreal bestDist = -1.0;
    for (const QPointF &p : points) {
        const qreal dist = distancePointToSegment(p, pA, pB);
        if (dist > bestDist) {
            bestDist = dist;
            pC = p;
        }
    }

    const qreal abLen = QLineF(pA, pB).length();
    const qreal acLen = QLineF(pA, pC).length();
    const qreal bcLen = QLineF(pB, pC).length();

    if (abLen < minDim * 0.35 || acLen < minDim * 0.25 || bcLen < minDim * 0.25) {
        return std::nullopt;
    }

    const qreal area2 = std::abs(cross2d(pA, pB, pC));
    const qreal boundsArea = w * h;
    if (area2 < minDim * minDim * 0.12 || boundsArea < 1.0 || (0.5 * area2) / boundsArea < 0.22) {
        return std::nullopt;
    }

    const QPointF centroid = (pA + pB + pC) / 3.0;

    vQPointF tri;
    tri << pA << pB << pC;
    std::sort(tri.begin(), tri.end(), [centroid](const QPointF &p0, const QPointF &p1) {
        return std::atan2(p0.y() - centroid.y(), p0.x() - centroid.x())
            < std::atan2(p1.y() - centroid.y(), p1.x() - centroid.x());
    });

    const qreal tolerance = qMax<qreal>(7.0, minDim * 0.10);

    struct EdgeRun {
        int edge = -1;
        int count = 0;
    };

    QVector<EdgeRun> runs;
    runs.reserve(16);

    int interiorCount = 0;
    int edgeCounts[3] = {0, 0, 0};

    auto pushEdge = [&runs](int edge) {
        if (!runs.isEmpty() && runs.last().edge == edge) {
            runs.last().count++;
            return;
        }

        EdgeRun run;
        run.edge = edge;
        run.count = 1;
        runs.append(run);
    };

    auto distToEdge = [&tri](const QPointF &p, int edge) {
        const QPointF a = tri[edge];
        const QPointF b = tri[(edge + 1) % 3];
        return distancePointToSegment(p, a, b);
    };

    for (const QPointF &p : points) {
        qreal d[3] = {distToEdge(p, 0), distToEdge(p, 1), distToEdge(p, 2)};
        int bestEdge = 0;
        if (d[1] < d[bestEdge]) bestEdge = 1;
        if (d[2] < d[bestEdge]) bestEdge = 2;

        qreal secondBest = std::numeric_limits<qreal>::max();
        for (int i = 0; i < 3; i++) {
            if (i != bestEdge) {
                secondBest = qMin(secondBest, d[i]);
            }
        }

        const qreal best = d[bestEdge];
        if (best > tolerance) {
            interiorCount++;
            continue;
        }

        edgeCounts[bestEdge]++;

        // Corner points can jitter between edges. Skip them for edge ordering.
        if (secondBest <= tolerance && std::abs(secondBest - best) <= tolerance * 0.25) {
            continue;
        }

        pushEdge(bestEdge);
    }

    if (interiorCount > points.size() * 0.25) {
        return std::nullopt;
    }

    const int minEdgeCount = qMax(3, int(points.size() * 0.08));
    for (int i = 0; i < 3; i++) {
        if (edgeCounts[i] < minEdgeCount) {
            return std::nullopt;
        }
    }

    if (runs.size() < 3) {
        return std::nullopt;
    }

    if (runs.size() > 1 && runs.first().edge == runs.last().edge) {
        runs.first().count += runs.last().count;
        runs.removeLast();
    }

    // Remove tiny backtracking runs: A-B-A, where B is a 1-2 point jitter run.
    for (int iter = 0; iter < 4; iter++) {
        bool changed = false;
        for (int i = 1; i < runs.size() - 1; i++) {
            if (runs[i].count <= 2 && runs[i - 1].edge == runs[i + 1].edge) {
                runs[i - 1].count += runs[i].count + runs[i + 1].count;
                runs.remove(i); // B
                runs.remove(i); // second A
                changed = true;
                break;
            }
        }
        if (!changed) {
            break;
        }
    }

    if (runs.size() < 3 || runs.size() > 10) {
        return std::nullopt;
    }

    bool edgePresent[3] = {false, false, false};
    for (const EdgeRun &run : runs) {
        edgePresent[run.edge] = true;
    }
    for (int i = 0; i < 3; i++) {
        if (!edgePresent[i]) {
            return std::nullopt;
        }
    }

    const qreal pathLen = polylineLength(points);
    const qreal perimeter = abLen + bcLen + acLen;
    if (perimeter < 1.0 || pathLen < perimeter * 0.60 || pathLen > perimeter * 4.0) {
        return std::nullopt;
    }

    return tri;
}

std::optional<vQPointF> detectTouchQuickShapePolygon(const QVector<QPointF> &points)
{
    if (points.size() < 24) {
        return std::nullopt;
    }

    const QRectF bounds = boundingRectForPoints(points);
    const qreal w = bounds.width();
    const qreal h = bounds.height();

    const qreal minDim = qMin(w, h);
    const qreal maxDim = qMax(w, h);
    if (minDim < 48.0 || maxDim < 96.0) {
        return std::nullopt;
    }

    // Closed-ish stroke: start and end should be near.
    const qreal closureTol = qMax<qreal>(18.0, minDim * 0.25);
    if (QLineF(points.first(), points.last()).length() > closureTol) {
        return std::nullopt;
    }

    const int n = points.size();

    QVector<qreal> arc;
    arc.resize(n);
    arc[0] = 0.0;
    for (int i = 1; i < n; i++) {
        arc[i] = arc[i - 1] + QLineF(points[i - 1], points[i]).length();
    }
    const qreal closedPathLen = arc.last() + QLineF(points.last(), points.first()).length();
    if (closedPathLen < 1.0) {
        return std::nullopt;
    }

    QVector<qreal> angles;
    angles.resize(n);
    for (int i = 0; i < n; i++) {
        const int prev = (i - 1 + n) % n;
        const int next = (i + 1) % n;

        const QPointF v1 = points[i] - points[prev];
        const QPointF v2 = points[next] - points[i];
        const qreal l1 = std::hypot(v1.x(), v1.y());
        const qreal l2 = std::hypot(v2.x(), v2.y());

        if (l1 < 1e-3 || l2 < 1e-3) {
            angles[i] = 0.0;
            continue;
        }

        const qreal ux1 = v1.x() / l1;
        const qreal uy1 = v1.y() / l1;
        const qreal ux2 = v2.x() / l2;
        const qreal uy2 = v2.y() / l2;

        const qreal cross = ux1 * uy2 - uy1 * ux2;
        const qreal dot = ux1 * ux2 + uy1 * uy2;

        angles[i] = std::atan2(std::abs(cross), dot);
    }

    struct Candidate {
        int index = 0;
        qreal angle = 0.0;
    };

    QVector<Candidate> candidates;
    candidates.reserve(32);

    const qreal minCornerAngle = 0.65; // ~37 deg
    for (int i = 0; i < n; i++) {
        const qreal a = angles[i];
        if (a < minCornerAngle) {
            continue;
        }

        const int prev = (i - 1 + n) % n;
        const int next = (i + 1) % n;
        if (a >= angles[prev] && a > angles[next]) {
            Candidate c;
            c.index = i;
            c.angle = a;
            candidates.append(c);
        }
    }

    if (candidates.size() < 4) {
        return std::nullopt;
    }

    std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
        return a.angle > b.angle;
    });

    const qreal minCornerSpacing = qMax<qreal>(24.0, minDim * 0.22);
    const int maxCorners = 10;

    QVector<int> cornerIdx;
    cornerIdx.reserve(maxCorners);

    for (const Candidate &c : candidates) {
        const qreal cArc = arc[c.index];

        bool tooClose = false;
        for (int selected : cornerIdx) {
            const qreal diff = std::abs(cArc - arc[selected]);
            const qreal cycDist = qMin(diff, closedPathLen - diff);
            if (cycDist < minCornerSpacing) {
                tooClose = true;
                break;
            }
        }
        if (tooClose) {
            continue;
        }

        cornerIdx.append(c.index);
        if (cornerIdx.size() >= maxCorners) {
            break;
        }
    }

    if (cornerIdx.size() < 4 || cornerIdx.size() > maxCorners) {
        return std::nullopt;
    }

    std::sort(cornerIdx.begin(), cornerIdx.end());

    vQPointF poly;
    poly.reserve(cornerIdx.size());
    for (int idx : cornerIdx) {
        poly << points[idx];
    }

    const int m = poly.size();
    qreal perimeter = 0.0;
    for (int i = 0; i < m; i++) {
        perimeter += QLineF(poly[i], poly[(i + 1) % m]).length();
    }

    if (perimeter < 1.0 || closedPathLen < perimeter * 0.60 || closedPathLen > perimeter * 4.0) {
        return std::nullopt;
    }

    const qreal minEdgeLen = qMax<qreal>(18.0, minDim * 0.18);
    for (int i = 0; i < m; i++) {
        if (QLineF(poly[i], poly[(i + 1) % m]).length() < minEdgeLen) {
            return std::nullopt;
        }
    }

    qreal area2 = 0.0;
    for (int i = 0; i < m; i++) {
        const QPointF p0 = poly[i];
        const QPointF p1 = poly[(i + 1) % m];
        area2 += p0.x() * p1.y() - p1.x() * p0.y();
    }
    const qreal area = std::abs(area2) * 0.5;
    const qreal boundsArea = w * h;
    if (area < minDim * minDim * 0.12 || boundsArea < 1.0 || area / boundsArea < 0.20) {
        return std::nullopt;
    }

    const qreal tolerance = qMax<qreal>(8.0, minDim * 0.12);
    int offEdgeCount = 0;

    for (int corner = 0; corner < m; corner++) {
        const int startIdx = cornerIdx[corner];
        const int endIdx = cornerIdx[(corner + 1) % m];
        const QPointF a = poly[corner];
        const QPointF b = poly[(corner + 1) % m];

        int edgePointCount = 0;
        auto visitIndex = [&](int idx) {
            edgePointCount++;
            if (distancePointToSegment(points[idx], a, b) > tolerance) {
                offEdgeCount++;
            }
        };

        if (startIdx <= endIdx) {
            for (int i = startIdx; i <= endIdx; i++) {
                visitIndex(i);
            }
        } else {
            for (int i = startIdx; i < n; i++) {
                visitIndex(i);
            }
            for (int i = 0; i <= endIdx; i++) {
                visitIndex(i);
            }
        }

        if (edgePointCount < 3) {
            return std::nullopt;
        }
    }

    if (offEdgeCount > points.size() * 0.28) {
        return std::nullopt;
    }

    return poly;
}

} // namespace


KisToolFreehand::KisToolFreehand(KoCanvasBase * canvas, const QCursor & cursor,
                                 const KUndo2MagicString &transactionText, bool useSavedSmoothing)
    : KisToolPaint(canvas, cursor),
      m_brushResizeCompressor(200, std::bind(&KisToolFreehand::slotDoResizeBrush, this, _1))
{

    setSupportOutline(true);
    updateMaskSyntheticEventsFromTouch();
    connect(KisConfigNotifier::instance(), SIGNAL(touchPaintingChanged()),
            SLOT(updateMaskSyntheticEventsFromTouch()));

    m_infoBuilder = new KisToolFreehandPaintingInformationBuilder(this);
    m_helper = new KisToolFreehandHelper(m_infoBuilder, canvas->resourceManager(), transactionText,
                                         new KisSmoothingOptions(useSavedSmoothing));

    connect(m_helper, SIGNAL(requestExplicitUpdateOutline()), SLOT(explicitUpdateOutline()));

    connect(qobject_cast<KisCanvas2*>(canvas)->viewManager(), SIGNAL(brushOutlineToggled()), SLOT(explicitUpdateOutline()));

    KisCanvasResourceProvider *provider = qobject_cast<KisCanvas2*>(canvas)->viewManager()->canvasResourceProvider();

    connect(provider, SIGNAL(sigEffectiveCompositeOpChanged()), SLOT(explicitUpdateOutline()));
    connect(provider, SIGNAL(sigEffectiveCompositeOpChanged()), SLOT(resetCursorStyle()));
    connect(provider, SIGNAL(sigPaintOpPresetChanged(KisPaintOpPresetSP)), SLOT(explicitUpdateOutline()));
    connect(provider, SIGNAL(sigPaintOpPresetChanged(KisPaintOpPresetSP)), SLOT(resetCursorStyle()));
}

KisToolFreehand::~KisToolFreehand()
{
    delete m_helper;
    delete m_infoBuilder;
}

void KisToolFreehand::resetTouchQuickShapeTracking()
{
    m_touchQuickShapeTracking = false;
    m_touchQuickShapePoints.clear();
    m_touchQuickShapeLastRecordedPixelPos = QPointF();
    m_touchQuickShapeSinceLastMove.invalidate();

    KisCanvas2 *canvas2 = dynamic_cast<KisCanvas2 *>(canvas());
    KisViewManager *viewManager = canvas2 ? canvas2->viewManager() : nullptr;
    KisInputManager *inputManager = viewManager ? viewManager->inputManager() : nullptr;
    if (inputManager) {
        inputManager->clearTouchQuickShapePerfectRequest();
    }
}

void KisToolFreehand::showTouchQuickShapeEditPopup()
{
    if (m_touchQuickShapeEditActive || !m_touchQuickShapeLastShape) {
        return;
    }

    QWidget *anchor = canvas() ? canvas()->canvasWidget() : nullptr;
    if (!anchor) {
        return;
    }

    if (!m_touchQuickShapeEditPopup) {
        QFrame *popup = new QFrame(anchor);
        popup->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
        popup->setAttribute(Qt::WA_TranslucentBackground, true);
        popup->setObjectName(QStringLiteral("kisTouchQuickShapeEditPopup"));

        popup->setStyleSheet(QStringLiteral(
            "QFrame#kisTouchQuickShapeEditPopup {"
            "  background-color: rgba(30, 30, 30, 230);"
            "  border: 1px solid rgba(255, 255, 255, 40);"
            "  border-radius: 14px;"
            "  color: rgb(240, 240, 240);"
            "}"
            "QToolButton {"
            "  color: rgb(240, 240, 240);"
            "  background: transparent;"
            "  border: 0px;"
            "  padding: 10px 14px;"
            "}"
            "QToolButton:pressed {"
            "  background-color: rgba(255, 255, 255, 30);"
            "  border-radius: 12px;"
            "}"));

        QHBoxLayout *layout = new QHBoxLayout(popup);
        layout->setContentsMargins(10, 10, 10, 10);
        layout->setSpacing(6);

        QToolButton *editButton = new QToolButton(popup);
        editButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        editButton->setIcon(koIcon("document-edit"));
        editButton->setIconSize(QSize(24, 24));
        editButton->setText(i18n("Edit Shape"));
        editButton->setMinimumSize(QSize(180, 56));
        editButton->setAutoRaise(true);
        editButton->setFocusPolicy(Qt::NoFocus);

        connect(editButton, &QToolButton::clicked, popup, [this]() { startTouchQuickShapeEdit(); });
        layout->addWidget(editButton);

        m_touchQuickShapeEditPopup = popup;
    }

    QFrame *popup = m_touchQuickShapeEditPopup.data();
    if (!popup) {
        return;
    }

    popup->adjustSize();

    const int localY = qMax(64, anchor->height() / 8);
    const QPoint globalPos = anchor->mapToGlobal(QPoint(anchor->rect().center().x(), localY));

    QRect desiredRect(
        QPoint(globalPos.x() - popup->width() / 2, globalPos.y() - popup->height() / 2),
        popup->size());

    QScreen *screen = QGuiApplication::screenAt(globalPos);
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen) {
        const QRect avail = screen->availableGeometry();

        if (desiredRect.left() < avail.left()) {
            desiredRect.moveLeft(avail.left());
        }
        if (desiredRect.right() > avail.right()) {
            desiredRect.moveRight(avail.right());
        }
        if (desiredRect.top() < avail.top()) {
            desiredRect.moveTop(avail.top());
        }
        if (desiredRect.bottom() > avail.bottom()) {
            desiredRect.moveBottom(avail.bottom());
        }
    }

    popup->move(desiredRect.topLeft());
    popup->show();
    popup->raise();

    QTimer::singleShot(2500, popup, &QWidget::hide);
}

void KisToolFreehand::startTouchQuickShapeEdit()
{
    if (m_touchQuickShapeEditPopup) {
        m_touchQuickShapeEditPopup->hide();
    }

    if (!m_touchQuickShapeLastShape) {
        return;
    }

    KisImageWSP img = image();
    KisNodeSP node = currentNode();
    KisCanvas2 *canvas2 = dynamic_cast<KisCanvas2 *>(canvas());
    KisViewManager *viewManager = canvas2 ? canvas2->viewManager() : nullptr;
    KisUndoAdapter *undoAdapter = viewManager ? viewManager->undoAdapter() : nullptr;

    if (!img || !node || !undoAdapter) {
        return;
    }

    // Ensure the snap command has landed before we attempt to undo it.
    img->waitForDone();
    undoAdapter->undoLastCommand();

    m_touchQuickShapeEditShape = m_touchQuickShapeLastShape;
    m_touchQuickShapeEditActive = true;
    m_touchQuickShapeEditHandle = -1;
    m_touchQuickShapeEditDragAll = false;
    m_touchQuickShapeEditMoved = false;
    m_touchQuickShapeEditPendingCommit = false;
    m_touchQuickShapeEditLastPixelPos = QPointF();

    const TouchQuickShapeGeometry &shape = *m_touchQuickShapeEditShape;

    QRectF pixelRect;
    switch (shape.kind) {
    case TouchQuickShapeGeometry::Kind::Rect:
    case TouchQuickShapeGeometry::Kind::Ellipse:
        pixelRect = shape.rect.normalized();
        break;
    case TouchQuickShapeGeometry::Kind::Polygon:
        pixelRect = boundingRectForPoints(shape.polygon).normalized();
        break;
    case TouchQuickShapeGeometry::Kind::Line:
        pixelRect = QRectF(shape.lineP0, shape.lineP1).normalized();
        break;
    case TouchQuickShapeGeometry::Kind::None:
    default:
        break;
    }

    constexpr qreal kUpdateMargin = 48.0;
    updateCanvasViewRect(pixelToView(pixelRect).normalized().adjusted(-kUpdateMargin,
                                                                     -kUpdateMargin,
                                                                     kUpdateMargin,
                                                                     kUpdateMargin));
}

void KisToolFreehand::commitTouchQuickShapeEdit()
{
    if (!m_touchQuickShapeEditActive || !m_touchQuickShapeEditShape) {
        return;
    }

    const TouchQuickShapeGeometry shape = *m_touchQuickShapeEditShape;

    QRectF pixelRect;
    switch (shape.kind) {
    case TouchQuickShapeGeometry::Kind::Rect:
    case TouchQuickShapeGeometry::Kind::Ellipse:
        pixelRect = shape.rect.normalized();
        break;
    case TouchQuickShapeGeometry::Kind::Polygon:
        pixelRect = boundingRectForPoints(shape.polygon).normalized();
        break;
    case TouchQuickShapeGeometry::Kind::Line:
        pixelRect = QRectF(shape.lineP0, shape.lineP1).normalized();
        break;
    case TouchQuickShapeGeometry::Kind::None:
    default:
        break;
    }

    constexpr qreal kUpdateMargin = 64.0;
    const QRectF updateRect = pixelToView(pixelRect).normalized().adjusted(-kUpdateMargin,
                                                                           -kUpdateMargin,
                                                                           kUpdateMargin,
                                                                           kUpdateMargin);

    KisImageWSP img = image();
    KisNodeSP node = currentNode();
    KisCanvas2 *canvas2 = dynamic_cast<KisCanvas2 *>(canvas());

    if (img && node && canvas2 && canvas2->viewManager() && canvas2->viewManager()->undoAdapter()) {
        img->waitForDone();

        KisFigurePaintingToolHelper helper(kundo2_i18n("QuickShape"),
                                           img,
                                           node,
                                           canvas()->resourceManager(),
                                           KisToolShapeUtils::StrokeStyleForeground,
                                           KisToolShapeUtils::FillStyleNone);

        switch (shape.kind) {
        case TouchQuickShapeGeometry::Kind::Rect:
            helper.paintRect(shape.rect);
            break;
        case TouchQuickShapeGeometry::Kind::Ellipse:
            helper.paintEllipse(shape.rect);
            break;
        case TouchQuickShapeGeometry::Kind::Polygon:
            helper.paintPolygon(shape.polygon);
            break;
        case TouchQuickShapeGeometry::Kind::Line:
            helper.paintLine(KisPaintInformation(shape.lineP0), KisPaintInformation(shape.lineP1));
            break;
        case TouchQuickShapeGeometry::Kind::None:
        default:
            break;
        }

        m_touchQuickShapeLastShape = shape;
    }

    m_touchQuickShapeEditActive = false;
    m_touchQuickShapeEditShape.reset();
    m_touchQuickShapeEditHandle = -1;
    m_touchQuickShapeEditDragAll = false;
    m_touchQuickShapeEditMoved = false;
    m_touchQuickShapeEditPendingCommit = false;
    m_touchQuickShapeEditLastPixelPos = QPointF();

    updateCanvasViewRect(updateRect);
}

void KisToolFreehand::touchQuickShapeEditBegin(KoPointerEvent *event)
{
    if (!m_touchQuickShapeEditShape) {
        event->ignore();
        return;
    }

    event->accept();

    const TouchQuickShapeGeometry &shape = *m_touchQuickShapeEditShape;

    m_touchQuickShapeEditHandle = -1;
    m_touchQuickShapeEditDragAll = false;
    m_touchQuickShapeEditMoved = false;
    m_touchQuickShapeEditPendingCommit = false;
    m_touchQuickShapeEditLastPixelPos = convertToPixelCoord(event);

    const QPointF viewPos = canvas()->viewConverter()->documentToView(event->point);

    QVector<QPointF> handlePointsPx;
    switch (shape.kind) {
    case TouchQuickShapeGeometry::Kind::Rect:
    case TouchQuickShapeGeometry::Kind::Ellipse: {
        const QRectF r = shape.rect.normalized();
        handlePointsPx << r.topLeft() << r.topRight() << r.bottomRight() << r.bottomLeft();
        break;
    }
    case TouchQuickShapeGeometry::Kind::Polygon:
        handlePointsPx = shape.polygon;
        break;
    case TouchQuickShapeGeometry::Kind::Line:
        handlePointsPx << shape.lineP0 << shape.lineP1;
        break;
    case TouchQuickShapeGeometry::Kind::None:
    default:
        break;
    }

    auto dist2 = [](const QPointF &a, const QPointF &b) {
        const qreal dx = a.x() - b.x();
        const qreal dy = a.y() - b.y();
        return dx * dx + dy * dy;
    };

    constexpr qreal kHandleHitRadius = 18.0;
    const qreal kHandleHitRadius2 = kHandleHitRadius * kHandleHitRadius;

    int bestHandle = -1;
    qreal bestDist2 = std::numeric_limits<qreal>::max();
    for (int i = 0; i < handlePointsPx.size(); ++i) {
        const QPointF hp = pixelToView(handlePointsPx.at(i));
        const qreal d2 = dist2(viewPos, hp);
        if (d2 < bestDist2) {
            bestDist2 = d2;
            bestHandle = i;
        }
    }

    if (bestHandle >= 0 && bestDist2 <= kHandleHitRadius2) {
        m_touchQuickShapeEditHandle = bestHandle;
        return;
    }

    auto distToSegment2 = [](const QPointF &p, const QPointF &a, const QPointF &b) {
        const QPointF ab = b - a;
        const QPointF ap = p - a;

        const qreal ab2 = ab.x() * ab.x() + ab.y() * ab.y();
        if (ab2 <= 1e-6) {
            const qreal dx = ap.x();
            const qreal dy = ap.y();
            return dx * dx + dy * dy;
        }

        const qreal dot = ap.x() * ab.x() + ap.y() * ab.y();
        const qreal t = std::clamp(dot / ab2, 0.0, 1.0);
        const QPointF closest = a + t * ab;

        const QPointF diff = p - closest;
        return diff.x() * diff.x() + diff.y() * diff.y();
    };

    bool inside = false;
    switch (shape.kind) {
    case TouchQuickShapeGeometry::Kind::Rect:
    case TouchQuickShapeGeometry::Kind::Ellipse: {
        const QRectF viewRect = pixelToView(shape.rect.normalized()).normalized();
        inside = viewRect.contains(viewPos);
        break;
    }
    case TouchQuickShapeGeometry::Kind::Polygon: {
        if (!shape.polygon.isEmpty()) {
            QPainterPath polyPath;
            polyPath.moveTo(pixelToView(shape.polygon.first()));
            for (int i = 1; i < shape.polygon.size(); ++i) {
                polyPath.lineTo(pixelToView(shape.polygon.at(i)));
            }
            polyPath.closeSubpath();
            inside = polyPath.contains(viewPos);
        }
        break;
    }
    case TouchQuickShapeGeometry::Kind::Line: {
        const QPointF a = pixelToView(shape.lineP0);
        const QPointF b = pixelToView(shape.lineP1);
        constexpr qreal kLineHitRadius = 14.0;
        inside = distToSegment2(viewPos, a, b) <= (kLineHitRadius * kLineHitRadius);
        break;
    }
    case TouchQuickShapeGeometry::Kind::None:
    default:
        break;
    }

    if (inside) {
        m_touchQuickShapeEditDragAll = true;
        return;
    }

    // Tap outside the shape commits it. This keeps the touch flow lightweight
    // and avoids needing a separate "Done" UI affordance.
    m_touchQuickShapeEditPendingCommit = true;
}

void KisToolFreehand::touchQuickShapeEditContinue(KoPointerEvent *event)
{
    if (!m_touchQuickShapeEditShape) {
        event->ignore();
        return;
    }

    if (m_touchQuickShapeEditHandle < 0 && !m_touchQuickShapeEditDragAll) {
        event->ignore();
        return;
    }

    event->accept();

    auto viewRectForShape = [this](const TouchQuickShapeGeometry &s) {
        QRectF pixelRect;
        switch (s.kind) {
        case TouchQuickShapeGeometry::Kind::Rect:
        case TouchQuickShapeGeometry::Kind::Ellipse:
            pixelRect = s.rect.normalized();
            break;
        case TouchQuickShapeGeometry::Kind::Polygon:
            pixelRect = boundingRectForPoints(s.polygon).normalized();
            break;
        case TouchQuickShapeGeometry::Kind::Line:
            pixelRect = QRectF(s.lineP0, s.lineP1).normalized();
            break;
        case TouchQuickShapeGeometry::Kind::None:
        default:
            break;
        }

        constexpr qreal kMargin = 64.0;
        return pixelToView(pixelRect).normalized().adjusted(-kMargin, -kMargin, kMargin, kMargin);
    };

    const QRectF oldViewRect = viewRectForShape(*m_touchQuickShapeEditShape);

    const QPointF p = convertToPixelCoord(event);
    const QPointF delta = p - m_touchQuickShapeEditLastPixelPos;
    m_touchQuickShapeEditLastPixelPos = p;

    if (std::abs(delta.x()) < 0.01 && std::abs(delta.y()) < 0.01) {
        return;
    }
    m_touchQuickShapeEditMoved = true;
    m_touchQuickShapeEditPendingCommit = false;

    TouchQuickShapeGeometry &shape = *m_touchQuickShapeEditShape;
    if (m_touchQuickShapeEditDragAll) {
        switch (shape.kind) {
        case TouchQuickShapeGeometry::Kind::Rect:
        case TouchQuickShapeGeometry::Kind::Ellipse:
            shape.rect.translate(delta);
            break;
        case TouchQuickShapeGeometry::Kind::Polygon:
            for (QPointF &pt : shape.polygon) {
                pt += delta;
            }
            break;
        case TouchQuickShapeGeometry::Kind::Line:
            shape.lineP0 += delta;
            shape.lineP1 += delta;
            break;
        case TouchQuickShapeGeometry::Kind::None:
        default:
            break;
        }
    } else if (m_touchQuickShapeEditHandle >= 0) {
        switch (shape.kind) {
        case TouchQuickShapeGeometry::Kind::Rect:
        case TouchQuickShapeGeometry::Kind::Ellipse: {
            const QRectF r = shape.rect.normalized();
            QPointF opposite;
            switch (m_touchQuickShapeEditHandle) {
            case 0:
                opposite = r.bottomRight();
                break;
            case 1:
                opposite = r.bottomLeft();
                break;
            case 2:
                opposite = r.topLeft();
                break;
            case 3:
                opposite = r.topRight();
                break;
            default:
                opposite = r.bottomRight();
                break;
            }

            shape.rect = QRectF(p, opposite).normalized();
            break;
        }
        case TouchQuickShapeGeometry::Kind::Polygon:
            if (m_touchQuickShapeEditHandle >= 0 && m_touchQuickShapeEditHandle < shape.polygon.size()) {
                shape.polygon[m_touchQuickShapeEditHandle] = p;
            }
            break;
        case TouchQuickShapeGeometry::Kind::Line:
            if (m_touchQuickShapeEditHandle == 0) {
                shape.lineP0 = p;
            } else if (m_touchQuickShapeEditHandle == 1) {
                shape.lineP1 = p;
            }
            break;
        case TouchQuickShapeGeometry::Kind::None:
        default:
            break;
        }
    }

    const QRectF newViewRect = viewRectForShape(*m_touchQuickShapeEditShape);
    updateCanvasViewRect(oldViewRect.united(newViewRect));
}

void KisToolFreehand::touchQuickShapeEditEnd(KoPointerEvent *event)
{
    if (!m_touchQuickShapeEditShape) {
        event->ignore();
        return;
    }

    event->accept();

    if (m_touchQuickShapeEditPendingCommit && !m_touchQuickShapeEditMoved) {
        commitTouchQuickShapeEdit();
        return;
    }

    m_touchQuickShapeEditHandle = -1;
    m_touchQuickShapeEditDragAll = false;
    m_touchQuickShapeEditMoved = false;
    m_touchQuickShapeEditPendingCommit = false;
}

void KisToolFreehand::mouseMoveEvent(KoPointerEvent *event)
{
    KisToolPaint::mouseMoveEvent(event);
    m_helper->cursorMoved(convertToPixelCoord(event));
}

void KisToolFreehand::paint(QPainter &gc, const KoViewConverter &converter)
{
    KisToolPaint::paint(gc, converter);

    if (!m_touchQuickShapeEditActive || !m_touchQuickShapeEditShape) {
        return;
    }
    if (!canvas() || !currentImage()) {
        return;
    }

    const TouchQuickShapeGeometry &shape = *m_touchQuickShapeEditShape;

    QPainterPath path;
    switch (shape.kind) {
    case TouchQuickShapeGeometry::Kind::Rect:
        path.addRect(pixelToView(shape.rect.normalized()));
        break;
    case TouchQuickShapeGeometry::Kind::Ellipse:
        path.addEllipse(pixelToView(shape.rect.normalized()));
        break;
    case TouchQuickShapeGeometry::Kind::Polygon:
        if (!shape.polygon.isEmpty()) {
            path.moveTo(pixelToView(shape.polygon.first()));
            for (int i = 1; i < shape.polygon.size(); ++i) {
                path.lineTo(pixelToView(shape.polygon.at(i)));
            }
            path.closeSubpath();
        }
        break;
    case TouchQuickShapeGeometry::Kind::Line:
        path.moveTo(pixelToView(shape.lineP0));
        path.lineTo(pixelToView(shape.lineP1));
        break;
    case TouchQuickShapeGeometry::Kind::None:
    default:
        return;
    }

    paintToolOutline(&gc, path);

    QVector<QPointF> handlePointsPx;
    switch (shape.kind) {
    case TouchQuickShapeGeometry::Kind::Rect:
    case TouchQuickShapeGeometry::Kind::Ellipse: {
        const QRectF r = shape.rect.normalized();
        handlePointsPx << r.topLeft() << r.topRight() << r.bottomRight() << r.bottomLeft();
        break;
    }
    case TouchQuickShapeGeometry::Kind::Polygon:
        handlePointsPx = shape.polygon;
        break;
    case TouchQuickShapeGeometry::Kind::Line:
        handlePointsPx << shape.lineP0 << shape.lineP1;
        break;
    case TouchQuickShapeGeometry::Kind::None:
    default:
        break;
    }

    constexpr qreal kHandleRadius = 10.0;
    gc.save();
    gc.setRenderHint(QPainter::Antialiasing, true);

    for (int i = 0; i < handlePointsPx.size(); ++i) {
        const QPointF hp = pixelToView(handlePointsPx.at(i));
        const bool active = (m_touchQuickShapeEditHandle == i && !m_touchQuickShapeEditDragAll);

        const QColor fill = active ? QColor(255, 220, 80, 220) : QColor(240, 240, 240, 220);
        const QColor border = QColor(0, 0, 0, 200);
        gc.setPen(QPen(border, 1.0));
        gc.setBrush(fill);
        gc.drawEllipse(hp, kHandleRadius, kHandleRadius);
    }

    gc.restore();
}

KisSmoothingOptionsSP KisToolFreehand::smoothingOptions() const
{
    return m_helper->smoothingOptions();
}

void KisToolFreehand::resetCursorStyle()
{
    KisConfig cfg(true);

    bool useSeparateEraserCursor = cfg.separateEraserCursor() && isEraser();

    switch (useSeparateEraserCursor ? cfg.eraserCursorStyle() : cfg.newCursorStyle()) {
    case CURSOR_STYLE_NO_CURSOR:
        useCursor(KisCursor::blankCursor());
        break;
    case CURSOR_STYLE_POINTER:
        useCursor(KisCursor::arrowCursor());
        break;
    case CURSOR_STYLE_SMALL_ROUND:
        useCursor(KisCursor::roundCursor());
        break;
    case CURSOR_STYLE_CROSSHAIR:
        useCursor(KisCursor::crossCursor());
        break;
    case CURSOR_STYLE_TRIANGLE_RIGHTHANDED:
        useCursor(KisCursor::triangleRightHandedCursor());
        break;
    case CURSOR_STYLE_TRIANGLE_LEFTHANDED:
        useCursor(KisCursor::triangleLeftHandedCursor());
        break;
    case CURSOR_STYLE_BLACK_PIXEL:
        useCursor(KisCursor::pixelBlackCursor());
        break;
    case CURSOR_STYLE_WHITE_PIXEL:
        useCursor(KisCursor::pixelWhiteCursor());
        break;
    case CURSOR_STYLE_ERASER:
        useCursor(KisCursor::eraserCursor());
        break;
    case CURSOR_STYLE_TOOLICON:
    default:
        KisToolPaint::resetCursorStyle();
        break;
    }
}

KisPaintingInformationBuilder* KisToolFreehand::paintingInformationBuilder() const
{
    return m_infoBuilder;
}

void KisToolFreehand::resetHelper(KisToolFreehandHelper *helper)
{
    delete m_helper;
    m_helper = helper;
}

bool KisToolFreehand::supportsPaintingAssistants() const
{
    return true;
}

int KisToolFreehand::flags() const
{
    return KisTool::FLAG_USES_CUSTOM_COMPOSITEOP|KisTool::FLAG_USES_CUSTOM_PRESET
           |KisTool::FLAG_USES_CUSTOM_SIZE;
}

void KisToolFreehand::activate(const QSet<KoShape*> &shapes)
{
    KisToolPaint::activate(shapes);
}

void KisToolFreehand::deactivate()
{
    if (m_touchQuickShapeEditActive) {
        commitTouchQuickShapeEdit();
    }
    if (m_touchQuickShapeEditPopup) {
        m_touchQuickShapeEditPopup->hide();
    }
    m_touchQuickShapeLastShape.reset();

    resetTouchQuickShapeTracking();

    if (mode() == PAINT_MODE) {
        endStroke();
        setMode(KisTool::HOVER_MODE);
    }
    KisToolPaint::deactivate();
}

void KisToolFreehand::initStroke(KoPointerEvent *event)
{
    m_helper->initPaint(event,
                        convertToPixelCoord(event),
                        image(),
                        currentNode(),
                        image().data());
}

void KisToolFreehand::doStroke(KoPointerEvent *event)
{
    m_helper->paintEvent(event);
}

void KisToolFreehand::endStroke()
{
    m_helper->endPaint();
    bool paintOpIgnoredEvent = currentPaintOpPreset()->settings()->mouseReleaseEvent();
    Q_UNUSED(paintOpIgnoredEvent);
}

bool KisToolFreehand::primaryActionSupportsHiResEvents() const
{
    return true;
}

void KisToolFreehand::beginPrimaryAction(KoPointerEvent *event)
{
    if (m_touchQuickShapeEditActive) {
        touchQuickShapeEditBegin(event);
        return;
    }

    if (m_touchQuickShapeEditPopup) {
        m_touchQuickShapeEditPopup->hide();
    }
    m_touchQuickShapeLastShape.reset();

    // FIXME: workaround for the Duplicate Op
    trySampleByPaintOp(event, SampleFgImage);

    requestUpdateOutline(event->point, event);

    NodePaintAbility paintability = nodePaintAbility();
    // XXX: move this to KisTool and make it work properly for clone layers: for clone layers, the shape paint tools don't work either
    if (!nodeEditable() || paintability != PAINT) {
        if (paintability == KisToolPaint::VECTOR || paintability == KisToolPaint::CLONE){
            KisCanvas2 * kiscanvas = static_cast<KisCanvas2*>(canvas());
            QString message = i18n("The brush tool cannot paint on this layer.  Please select a paint layer or mask.");
            kiscanvas->viewManager()->showFloatingMessage(message, koIcon("object-locked"));
        }
        else if (paintability == MYPAINTBRUSH_UNPAINTABLE) {
            KisCanvas2 * kiscanvas = static_cast<KisCanvas2*>(canvas());
            QString message = i18n("The MyPaint Brush Engine is not available for this colorspace");
            kiscanvas->viewManager()->showFloatingMessage(message, koIcon("object-locked"));
        }
        event->ignore();

        return;
    }

    KIS_SAFE_ASSERT_RECOVER_RETURN(!m_helper->isRunning());

    setMode(KisTool::PAINT_MODE);

    KisCanvas2 *canvas2 = dynamic_cast<KisCanvas2 *>(canvas());
    if (canvas2) {
        canvas2->viewManager()->disableControls();
    }

    resetTouchQuickShapeTracking();
    {
        KisConfig cfg(true);
        if (cfg.touchModeEnabled() && cfg.touchQuickShapeEnabled()) {
            const QPointF p = convertToPixelCoord(event);
            m_touchQuickShapeTracking = true;
            m_touchQuickShapePoints.reserve(128);
            m_touchQuickShapePoints.append(p);
            m_touchQuickShapeLastRecordedPixelPos = p;
            m_touchQuickShapeSinceLastMove.start();
        }
    }

    initStroke(event);
}

void KisToolFreehand::continuePrimaryAction(KoPointerEvent *event)
{
    if (m_touchQuickShapeEditActive) {
        touchQuickShapeEditContinue(event);
        return;
    }

    CHECK_MODE_SANITY_OR_RETURN(KisTool::PAINT_MODE);

    requestUpdateOutline(event->point, event);

    /**
     * Actual painting
     */
    if (m_touchQuickShapeTracking) {
        const QPointF p = convertToPixelCoord(event);
        if (QLineF(p, m_touchQuickShapeLastRecordedPixelPos).length() > 1.0) {
            m_touchQuickShapePoints.append(p);
            m_touchQuickShapeLastRecordedPixelPos = p;
            m_touchQuickShapeSinceLastMove.restart();
        }
    }

    doStroke(event);
}

void KisToolFreehand::endPrimaryAction(KoPointerEvent *event)
{
    if (m_touchQuickShapeEditActive) {
        touchQuickShapeEditEnd(event);
        return;
    }

    CHECK_MODE_SANITY_OR_RETURN(KisTool::PAINT_MODE);

    std::optional<QRectF> quickShapeRect;
    std::optional<QRectF> quickShapeEllipse;
    std::optional<vQPointF> quickShapeTriangle;
    std::optional<vQPointF> quickShapePolygon;
    std::optional<TouchQuickShapeLine> quickShapeLine;
    bool quickShapePerfectRequested = false;
    if (m_touchQuickShapeTracking) {
        const QPointF p = convertToPixelCoord(event);
        if (m_touchQuickShapePoints.isEmpty() || QLineF(p, m_touchQuickShapePoints.last()).length() > 0.01) {
            m_touchQuickShapePoints.append(p);
            m_touchQuickShapeLastRecordedPixelPos = p;
        }

        KisCanvas2 *canvas2 = dynamic_cast<KisCanvas2 *>(canvas());
        KisViewManager *viewManager = canvas2 ? canvas2->viewManager() : nullptr;
        KisInputManager *inputManager = viewManager ? viewManager->inputManager() : nullptr;
        if (inputManager) {
            quickShapePerfectRequested = inputManager->takeTouchQuickShapePerfectRequest();
        }

        constexpr qint64 kHoldMs = 350;
        const bool held = m_touchQuickShapeSinceLastMove.isValid() && m_touchQuickShapeSinceLastMove.elapsed() >= kHoldMs;
        if (held) {
            quickShapeRect = detectTouchQuickShapeRect(m_touchQuickShapePoints);
            if (!quickShapeRect) {
                quickShapeEllipse = detectTouchQuickShapeEllipse(m_touchQuickShapePoints);
                if (!quickShapeEllipse) {
                    quickShapeTriangle = detectTouchQuickShapeTriangle(m_touchQuickShapePoints);
                    if (!quickShapeTriangle) {
                        quickShapePolygon = detectTouchQuickShapePolygon(m_touchQuickShapePoints);
                        if (!quickShapePolygon) {
                            quickShapeLine = detectTouchQuickShapeLine(m_touchQuickShapePoints);
                        }
                    }
                }
            }

            if (quickShapePerfectRequested) {
                if (quickShapeRect) {
                    quickShapeRect = squareFromRectPreserveCenter(*quickShapeRect);
                } else if (quickShapeEllipse) {
                    quickShapeEllipse = squareFromRectPreserveCenter(*quickShapeEllipse);
                } else if (quickShapeTriangle) {
                    quickShapeTriangle = regularPolygonFromPolygonPreserveCentroid(*quickShapeTriangle);
                } else if (quickShapePolygon) {
                    quickShapePolygon = regularPolygonFromPolygonPreserveCentroid(*quickShapePolygon);
                } else if (quickShapeLine) {
                    quickShapeLine = angleSnappedLine45(*quickShapeLine);
                }
            }
        }
    }

    endStroke();

    if (m_assistant && static_cast<KisCanvas2*>(canvas())->paintingAssistantsDecoration()) {
        static_cast<KisCanvas2*>(canvas())->paintingAssistantsDecoration()->endStroke();
    }

    KisCanvas2 *canvas2 = dynamic_cast<KisCanvas2 *>(canvas());
    if (canvas2) {
        canvas2->viewManager()->enableControls();
    }

    setMode(KisTool::HOVER_MODE);

    if (quickShapeRect || quickShapeEllipse || quickShapeTriangle || quickShapePolygon || quickShapeLine) {
        KisImageWSP img = image();
        KisNodeSP node = currentNode();
        KisCanvas2 *canvas2 = dynamic_cast<KisCanvas2 *>(canvas());

        if (img && node && canvas2 && canvas2->viewManager() && canvas2->viewManager()->undoAdapter()) {
            TouchQuickShapeGeometry shape;
            if (quickShapeRect) {
                shape.kind = TouchQuickShapeGeometry::Kind::Rect;
                shape.rect = *quickShapeRect;
            } else if (quickShapeEllipse) {
                shape.kind = TouchQuickShapeGeometry::Kind::Ellipse;
                shape.rect = *quickShapeEllipse;
            } else if (quickShapeTriangle) {
                shape.kind = TouchQuickShapeGeometry::Kind::Polygon;
                shape.polygon = *quickShapeTriangle;
            } else if (quickShapePolygon) {
                shape.kind = TouchQuickShapeGeometry::Kind::Polygon;
                shape.polygon = *quickShapePolygon;
            } else if (quickShapeLine) {
                shape.kind = TouchQuickShapeGeometry::Kind::Line;
                shape.lineP0 = quickShapeLine->p0;
                shape.lineP1 = quickShapeLine->p1;
            }

            // Ensure the just-finished stroke has landed before we attempt to undo it.
            img->waitForDone();

            canvas2->viewManager()->undoAdapter()->undoLastCommand();

            KisFigurePaintingToolHelper helper(kundo2_i18n("QuickShape"),
                                               img,
                                               node,
                                               canvas()->resourceManager(),
                                               KisToolShapeUtils::StrokeStyleForeground,
                                               KisToolShapeUtils::FillStyleNone);

            if (quickShapeRect) {
                helper.paintRect(*quickShapeRect);
            } else if (quickShapeEllipse) {
                helper.paintEllipse(*quickShapeEllipse);
            } else if (quickShapeTriangle) {
                helper.paintPolygon(*quickShapeTriangle);
            } else if (quickShapePolygon) {
                helper.paintPolygon(*quickShapePolygon);
            } else if (quickShapeLine) {
                helper.paintLine(KisPaintInformation(quickShapeLine->p0),
                                 KisPaintInformation(quickShapeLine->p1));
            }

            m_touchQuickShapeLastShape = shape;
            showTouchQuickShapeEditPopup();
        }
    }

    resetTouchQuickShapeTracking();
}

bool KisToolFreehand::trySampleByPaintOp(KoPointerEvent *event, AlternateAction action)
{
    if (action != SampleFgNode && action != SampleFgImage) return false;

    /**
     * FIXME: we need some better way to implement modifiers
     * for a paintop level. This method is used in DuplicateOp only!
     */
    QPointF pos = adjustPosition(event->point, event->point);
    qreal perspective = calculatePerspective(pos);
    if (!currentPaintOpPreset()) {
        return false;
    }
    KisPaintInformation info(convertToPixelCoord(event->point),
                             m_infoBuilder->pressureToCurve(event->pressure()),
                             event->xTilt(), event->yTilt(),
                             event->rotation(),
                             event->tangentialPressure(),
                             perspective, 0, 0);
    info.setRandomSource(new KisRandomSource());
    info.setPerStrokeRandomSource(new KisPerStrokeRandomSource());

    bool paintOpIgnoredEvent = currentPaintOpPreset()->settings()->mousePressEvent(info,
                                                                                   event->modifiers(),
                                                                                   currentNode());
    // DuplicateOP during the sampling of new source point (origin)
    // is the only paintop that returns "false" here
    return !paintOpIgnoredEvent;
}

void KisToolFreehand::activateAlternateAction(AlternateAction action)
{
    if (action != ChangeSize && action != ChangeSizeSnap) {
        KisToolPaint::activateAlternateAction(action);
        return;
    }

    useCursor(KisCursor::blankCursor());
    setOutlineVisible(true);
}

void KisToolFreehand::deactivateAlternateAction(AlternateAction action)
{
    if (action != ChangeSize && action != ChangeSizeSnap) {
        KisToolPaint::deactivateAlternateAction(action);
        return;
    }

    resetCursorStyle();
    setOutlineVisible(false);
}

void KisToolFreehand::beginAlternateAction(KoPointerEvent *event, AlternateAction action)
{
    if (trySampleByPaintOp(event, action)) {
        m_paintopBasedSamplingInAction = true;
        return;
    }

    if (action != ChangeSize && action != ChangeSizeSnap) {
        KisToolPaint::beginAlternateAction(event, action);
        return;
    }

    setMode(GESTURE_MODE);
    m_initialGestureDocPoint = event->point;
    m_initialGestureGlobalPoint = QCursor::pos();

    m_lastDocumentPoint = event->point;
    m_lastPaintOpSize = currentPaintOpPreset()->settings()->paintOpSize();

    m_beginAlternateActionEvent = event->deepCopyEvent();
    requestUpdateOutline(m_initialGestureDocPoint, &m_beginAlternateActionEvent->event);
}

void KisToolFreehand::continueAlternateAction(KoPointerEvent *event, AlternateAction action)
{
    if (trySampleByPaintOp(event, action) || m_paintopBasedSamplingInAction) return;

    if (action != ChangeSize && action != ChangeSizeSnap) {
        KisToolPaint::continueAlternateAction(event, action);
        return;
    }

    QPointF lastWidgetPosition = convertDocumentToWidget(m_lastDocumentPoint);
    QPointF actualWidgetPosition = convertDocumentToWidget(event->point);

    QPointF offset = actualWidgetPosition - lastWidgetPosition;

    KisCanvas2 *canvas2 = dynamic_cast<KisCanvas2 *>(canvas());
    KIS_SAFE_ASSERT_RECOVER_RETURN(canvas2);
    QRect screenRect = QGuiApplication::primaryScreen()->availableVirtualGeometry();

    qreal scaleX = 0;
    qreal scaleY = 0;
    canvas2->coordinatesConverter()->imageScale(&scaleX, &scaleY);

    const qreal maxBrushSize = KisImageConfig(true).maxBrushSize();
    const qreal effectiveMaxDragSize = 0.5 * screenRect.width();
    const qreal effectiveMaxBrushSize = qMin(maxBrushSize, effectiveMaxDragSize / scaleX);

    const qreal scaleCoeff = effectiveMaxBrushSize / effectiveMaxDragSize;
    const qreal sizeDiff = scaleCoeff * offset.x() ;

    if (qAbs(sizeDiff) > 0.01) {
        KisPaintOpSettingsSP settings = currentPaintOpPreset()->settings();

        qreal newSize = m_lastPaintOpSize + sizeDiff;

        if (action == ChangeSizeSnap) {
            newSize = qMax(qRound(newSize), 1);
        }

        newSize = qBound(0.01, newSize, maxBrushSize);

        settings->setPaintOpSize(newSize);

        requestUpdateOutline(
            m_initialGestureDocPoint,
            m_beginAlternateActionEvent.has_value() ? &m_beginAlternateActionEvent->event : nullptr);
        //m_brushResizeCompressor.start(newSize);

        m_lastDocumentPoint = event->point;
        m_lastPaintOpSize = newSize;
    }
}

void KisToolFreehand::endAlternateAction(KoPointerEvent *event, AlternateAction action)
{
    if (trySampleByPaintOp(event, action) || m_paintopBasedSamplingInAction) {
        m_paintopBasedSamplingInAction = false;
        return;
    }

    if (action != ChangeSize && action != ChangeSizeSnap) {
        KisToolPaint::endAlternateAction(event, action);
        return;
    }

    KisToolUtils::setCursorPos(m_initialGestureGlobalPoint);
    requestUpdateOutline(m_initialGestureDocPoint, 0);

    setMode(HOVER_MODE);

    m_beginAlternateActionEvent.reset();
}

bool KisToolFreehand::wantsAutoScroll() const
{
    return false;
}

void KisToolFreehand::setAssistant(bool assistant)
{
    m_assistant = assistant;
}

void KisToolFreehand::setOnlyOneAssistantSnap(bool assistant)
{
    m_only_one_assistant = assistant;
}

void KisToolFreehand::setSnapEraser(bool assistant)
{
    m_eraser_snapping = assistant;
}

void KisToolFreehand::slotDoResizeBrush(qreal newSize)
{
    KisPaintOpSettingsSP settings = currentPaintOpPreset()->settings();

    settings->setPaintOpSize(newSize);
    requestUpdateOutline(m_initialGestureDocPoint, 0);

}

QPointF KisToolFreehand::adjustPosition(const QPointF& point, const QPointF& strokeBegin)
{
    if (m_assistant && static_cast<KisCanvas2*>(canvas())->paintingAssistantsDecoration()) {
        KisCanvas2* c = static_cast<KisCanvas2*>(canvas());
        c->paintingAssistantsDecoration()->setOnlyOneAssistantSnap(m_only_one_assistant);
        c->paintingAssistantsDecoration()->setEraserSnap(m_eraser_snapping);
        QPointF ap = c->paintingAssistantsDecoration()->adjustPosition(point, strokeBegin);
        QPointF fp = (1.0 - m_magnetism) * point + m_magnetism * ap;
        // Report the final position back to the assistant so the guides
        // can follow the brush
        c->paintingAssistantsDecoration()->setAdjustedBrushPosition(fp);
        return fp;
    }
    return point;
}

qreal KisToolFreehand::calculatePerspective(const QPointF &documentPoint)
{
    qreal perspective = 1.0;
    Q_FOREACH (const KisPaintingAssistantSP assistant, static_cast<KisCanvas2*>(canvas())->paintingAssistantsDecoration()->assistants()) {
        QPointer<KisAbstractPerspectiveGrid> grid = dynamic_cast<KisAbstractPerspectiveGrid*>(assistant.data());
        if (grid && grid->isActive() && grid->contains(documentPoint)) {
            perspective = grid->distance(documentPoint);
            break;
        }
    }
    return perspective;
}

void KisToolFreehand::updateMaskSyntheticEventsFromTouch()
{
    setMaskSyntheticEvents(KisConfig(true).disableTouchOnCanvas());
}

void KisToolFreehand::explicitUpdateOutline()
{
    requestUpdateOutline(m_outlineDocPoint, 0);
}

KisOptimizedBrushOutline KisToolFreehand::getOutlinePath(const QPointF &documentPos,
                                             const KoPointerEvent *event,
                                             KisPaintOpSettings::OutlineMode outlineMode)
{
    if (currentPaintOpPreset())
        return m_helper->paintOpOutline(convertToPixelCoord(documentPos),
                                        event,
                                        currentPaintOpPreset()->settings(),
                                        outlineMode);
    else
        return KisOptimizedBrushOutline();
}
