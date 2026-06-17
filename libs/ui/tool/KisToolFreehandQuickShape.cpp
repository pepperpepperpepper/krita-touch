/*
 *  SPDX-FileCopyrightText: 2026
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "KisToolFreehandQuickShape.h"

#include <optional>
#include <algorithm>
#include <cmath>
#include <limits>

#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

#include <kis_icon.h>
#include <klocalizedstring.h>
#include <kundo2magicstring.h>

#include <KoPointerEvent.h>
#include <KoViewConverter.h>
#include <KoCanvasBase.h>

#include "kis_config.h"
#include "kis_types.h"
#include "canvas/kis_canvas2.h"
#include <KisViewManager.h>
#include <kis_undo_adapter.h>
#include <brushengine/kis_paint_information.h>
#include <brushengine/KisOptimizedBrushOutline.h>
#include "kis_figure_painting_tool_helper.h"
#include "input/kis_input_manager.h"

#include "kis_tool_freehand.h"

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

KisToolFreehandQuickShape::KisToolFreehandQuickShape(KisToolFreehand *tool)
    : QObject(nullptr)
    , m_tool(tool)
{
}

KisToolFreehandQuickShape::~KisToolFreehandQuickShape() = default;

bool KisToolFreehandQuickShape::isEditActive() const
{
    return m_editActive;
}

void KisToolFreehandQuickShape::resetTracking()
{
    m_tracking = false;
    m_points.clear();
    m_lastRecordedPixelPos = QPointF();
    m_sinceLastMove.invalidate();

    KisCanvas2 *canvas2 = dynamic_cast<KisCanvas2 *>(m_tool->canvas());
    KisViewManager *viewManager = canvas2 ? canvas2->viewManager() : nullptr;
    KisInputManager *inputManager = viewManager ? viewManager->inputManager() : nullptr;
    if (inputManager) {
        inputManager->clearTouchQuickShapePerfectRequest();
    }
}

void KisToolFreehandQuickShape::showEditPopup()
{
    if (m_editActive || !m_lastShape) {
        return;
    }

    QWidget *anchor = m_tool->canvas() ? m_tool->canvas()->canvasWidget() : nullptr;
    if (!anchor) {
        return;
    }

    if (!m_editPopup) {
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

        connect(editButton, &QToolButton::clicked, popup, [this]() { startEdit(); });
        layout->addWidget(editButton);

        m_editPopup = popup;
    }

    QFrame *popup = m_editPopup.data();
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

void KisToolFreehandQuickShape::startEdit()
{
    if (m_editPopup) {
        m_editPopup->hide();
    }

    if (!m_lastShape) {
        return;
    }

    KisImageWSP img = m_tool->image();
    KisNodeSP node = m_tool->currentNode();
    KisCanvas2 *canvas2 = dynamic_cast<KisCanvas2 *>(m_tool->canvas());
    KisViewManager *viewManager = canvas2 ? canvas2->viewManager() : nullptr;
    KisUndoAdapter *undoAdapter = viewManager ? viewManager->undoAdapter() : nullptr;

    if (!img || !node || !undoAdapter) {
        return;
    }

    // Ensure the snap command has landed before we attempt to undo it.
    img->waitForDone();
    undoAdapter->undoLastCommand();

    m_editShape = m_lastShape;
    m_editActive = true;
    m_editHandle = -1;
    m_editDragAll = false;
    m_editMoved = false;
    m_editPendingCommit = false;
    m_editLastPixelPos = QPointF();

    const TouchQuickShapeGeometry &shape = *m_editShape;

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
    m_tool->updateCanvasViewRect(m_tool->pixelToView(pixelRect).normalized().adjusted(-kUpdateMargin,
                                                                                      -kUpdateMargin,
                                                                                      kUpdateMargin,
                                                                                      kUpdateMargin));
}

void KisToolFreehandQuickShape::commitEdit()
{
    if (!m_editActive || !m_editShape) {
        return;
    }

    const TouchQuickShapeGeometry shape = *m_editShape;

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
    const QRectF updateRect = m_tool->pixelToView(pixelRect).normalized().adjusted(-kUpdateMargin,
                                                                                   -kUpdateMargin,
                                                                                   kUpdateMargin,
                                                                                   kUpdateMargin);

    KisImageWSP img = m_tool->image();
    KisNodeSP node = m_tool->currentNode();
    KisCanvas2 *canvas2 = dynamic_cast<KisCanvas2 *>(m_tool->canvas());

    if (img && node && canvas2 && canvas2->viewManager() && canvas2->viewManager()->undoAdapter()) {
        img->waitForDone();

        KisFigurePaintingToolHelper helper(kundo2_i18n("QuickShape"),
                                           img,
                                           node,
                                           m_tool->canvas()->resourceManager(),
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

        m_lastShape = shape;
    }

    m_editActive = false;
    m_editShape.reset();
    m_editHandle = -1;
    m_editDragAll = false;
    m_editMoved = false;
    m_editPendingCommit = false;
    m_editLastPixelPos = QPointF();

    m_tool->updateCanvasViewRect(updateRect);
}

void KisToolFreehandQuickShape::editBegin(KoPointerEvent *event)
{
    if (!m_editShape) {
        event->ignore();
        return;
    }

    event->accept();

    const TouchQuickShapeGeometry &shape = *m_editShape;

    m_editHandle = -1;
    m_editDragAll = false;
    m_editMoved = false;
    m_editPendingCommit = false;
    m_editLastPixelPos = m_tool->convertToPixelCoord(event);

    const QPointF viewPos = m_tool->canvas()->viewConverter()->documentToView(event->point);

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
        const QPointF hp = m_tool->pixelToView(handlePointsPx.at(i));
        const qreal d2 = dist2(viewPos, hp);
        if (d2 < bestDist2) {
            bestDist2 = d2;
            bestHandle = i;
        }
    }

    if (bestHandle >= 0 && bestDist2 <= kHandleHitRadius2) {
        m_editHandle = bestHandle;
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
        const QRectF viewRect = m_tool->pixelToView(shape.rect.normalized()).normalized();
        inside = viewRect.contains(viewPos);
        break;
    }
    case TouchQuickShapeGeometry::Kind::Polygon: {
        if (!shape.polygon.isEmpty()) {
            QPainterPath polyPath;
            polyPath.moveTo(m_tool->pixelToView(shape.polygon.first()));
            for (int i = 1; i < shape.polygon.size(); ++i) {
                polyPath.lineTo(m_tool->pixelToView(shape.polygon.at(i)));
            }
            polyPath.closeSubpath();
            inside = polyPath.contains(viewPos);
        }
        break;
    }
    case TouchQuickShapeGeometry::Kind::Line: {
        const QPointF a = m_tool->pixelToView(shape.lineP0);
        const QPointF b = m_tool->pixelToView(shape.lineP1);
        constexpr qreal kLineHitRadius = 14.0;
        inside = distToSegment2(viewPos, a, b) <= (kLineHitRadius * kLineHitRadius);
        break;
    }
    case TouchQuickShapeGeometry::Kind::None:
    default:
        break;
    }

    if (inside) {
        m_editDragAll = true;
        return;
    }

    // Tap outside the shape commits it. This keeps the touch flow lightweight
    // and avoids needing a separate "Done" UI affordance.
    m_editPendingCommit = true;
}

void KisToolFreehandQuickShape::editContinue(KoPointerEvent *event)
{
    if (!m_editShape) {
        event->ignore();
        return;
    }

    if (m_editHandle < 0 && !m_editDragAll) {
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
        return m_tool->pixelToView(pixelRect).normalized().adjusted(-kMargin, -kMargin, kMargin, kMargin);
    };

    const QRectF oldViewRect = viewRectForShape(*m_editShape);

    const QPointF p = m_tool->convertToPixelCoord(event);
    const QPointF delta = p - m_editLastPixelPos;
    m_editLastPixelPos = p;

    if (std::abs(delta.x()) < 0.01 && std::abs(delta.y()) < 0.01) {
        return;
    }
    m_editMoved = true;
    m_editPendingCommit = false;

    TouchQuickShapeGeometry &shape = *m_editShape;
    if (m_editDragAll) {
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
    } else if (m_editHandle >= 0) {
        switch (shape.kind) {
        case TouchQuickShapeGeometry::Kind::Rect:
        case TouchQuickShapeGeometry::Kind::Ellipse: {
            const QRectF r = shape.rect.normalized();
            QPointF opposite;
            switch (m_editHandle) {
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
            if (m_editHandle >= 0 && m_editHandle < shape.polygon.size()) {
                shape.polygon[m_editHandle] = p;
            }
            break;
        case TouchQuickShapeGeometry::Kind::Line:
            if (m_editHandle == 0) {
                shape.lineP0 = p;
            } else if (m_editHandle == 1) {
                shape.lineP1 = p;
            }
            break;
        case TouchQuickShapeGeometry::Kind::None:
        default:
            break;
        }
    }

    const QRectF newViewRect = viewRectForShape(*m_editShape);
    m_tool->updateCanvasViewRect(oldViewRect.united(newViewRect));
}

void KisToolFreehandQuickShape::editEnd(KoPointerEvent *event)
{
    if (!m_editShape) {
        event->ignore();
        return;
    }

    event->accept();

    if (m_editPendingCommit && !m_editMoved) {
        commitEdit();
        return;
    }

    m_editHandle = -1;
    m_editDragAll = false;
    m_editMoved = false;
    m_editPendingCommit = false;
}

void KisToolFreehandQuickShape::onDeactivate()
{
    if (m_editActive) {
        commitEdit();
    }
    if (m_editPopup) {
        m_editPopup->hide();
    }
    m_lastShape.reset();

    resetTracking();
}

bool KisToolFreehandQuickShape::handleBeginEditRedirect(KoPointerEvent *event)
{
    if (m_editActive) {
        editBegin(event);
        return true;
    }

    if (m_editPopup) {
        m_editPopup->hide();
    }
    m_lastShape.reset();
    return false;
}

void KisToolFreehandQuickShape::startTrackingIfEnabled(KoPointerEvent *event)
{
    resetTracking();

    KisConfig cfg(true);
    if (cfg.touchModeEnabled() && cfg.touchQuickShapeEnabled()) {
        const QPointF p = m_tool->convertToPixelCoord(event);
        m_tracking = true;
        m_points.reserve(128);
        m_points.append(p);
        m_lastRecordedPixelPos = p;
        m_sinceLastMove.start();
    }
}

bool KisToolFreehandQuickShape::handleContinueEditRedirect(KoPointerEvent *event)
{
    if (m_editActive) {
        editContinue(event);
        return true;
    }
    return false;
}

void KisToolFreehandQuickShape::accumulateTrackingPoint(KoPointerEvent *event)
{
    if (!m_tracking) {
        return;
    }

    const QPointF p = m_tool->convertToPixelCoord(event);
    if (QLineF(p, m_lastRecordedPixelPos).length() > 1.0) {
        m_points.append(p);
        m_lastRecordedPixelPos = p;
        m_sinceLastMove.restart();
    }
}

bool KisToolFreehandQuickShape::handleEndEditRedirect(KoPointerEvent *event)
{
    if (m_editActive) {
        editEnd(event);
        return true;
    }
    return false;
}

void KisToolFreehandQuickShape::detectShapeOnEnd(KoPointerEvent *event)
{
    m_pendingDetectedShape.reset();

    if (!m_tracking) {
        return;
    }

    const QPointF p = m_tool->convertToPixelCoord(event);
    if (m_points.isEmpty() || QLineF(p, m_points.last()).length() > 0.01) {
        m_points.append(p);
        m_lastRecordedPixelPos = p;
    }

    bool perfectRequested = false;
    KisCanvas2 *canvas2 = dynamic_cast<KisCanvas2 *>(m_tool->canvas());
    KisViewManager *viewManager = canvas2 ? canvas2->viewManager() : nullptr;
    KisInputManager *inputManager = viewManager ? viewManager->inputManager() : nullptr;
    if (inputManager) {
        perfectRequested = inputManager->takeTouchQuickShapePerfectRequest();
    }

    constexpr qint64 kHoldMs = 350;
    const bool held = m_sinceLastMove.isValid() && m_sinceLastMove.elapsed() >= kHoldMs;
    if (!held) {
        return;
    }

    std::optional<QRectF> quickShapeRect = detectTouchQuickShapeRect(m_points);
    std::optional<QRectF> quickShapeEllipse;
    std::optional<vQPointF> quickShapeTriangle;
    std::optional<vQPointF> quickShapePolygon;
    std::optional<TouchQuickShapeLine> quickShapeLine;
    if (!quickShapeRect) {
        quickShapeEllipse = detectTouchQuickShapeEllipse(m_points);
        if (!quickShapeEllipse) {
            quickShapeTriangle = detectTouchQuickShapeTriangle(m_points);
            if (!quickShapeTriangle) {
                quickShapePolygon = detectTouchQuickShapePolygon(m_points);
                if (!quickShapePolygon) {
                    quickShapeLine = detectTouchQuickShapeLine(m_points);
                }
            }
        }
    }

    if (perfectRequested) {
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

    if (!(quickShapeRect || quickShapeEllipse || quickShapeTriangle || quickShapePolygon || quickShapeLine)) {
        return;
    }

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

    m_pendingDetectedShape = shape;
}

void KisToolFreehandQuickShape::commitDetectedShapeAndShowPopup()
{
    if (m_pendingDetectedShape) {
        KisImageWSP img = m_tool->image();
        KisNodeSP node = m_tool->currentNode();
        KisCanvas2 *canvas2 = dynamic_cast<KisCanvas2 *>(m_tool->canvas());

        if (img && node && canvas2 && canvas2->viewManager() && canvas2->viewManager()->undoAdapter()) {
            const TouchQuickShapeGeometry shape = *m_pendingDetectedShape;

            // Ensure the just-finished stroke has landed before we attempt to undo it.
            img->waitForDone();

            canvas2->viewManager()->undoAdapter()->undoLastCommand();

            KisFigurePaintingToolHelper helper(kundo2_i18n("QuickShape"),
                                               img,
                                               node,
                                               m_tool->canvas()->resourceManager(),
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

            m_lastShape = shape;
            showEditPopup();
        }
    }

    m_pendingDetectedShape.reset();
    resetTracking();
}

void KisToolFreehandQuickShape::paint(QPainter &gc, const KoViewConverter &converter)
{
    Q_UNUSED(converter);

    if (!m_editActive || !m_editShape) {
        return;
    }
    if (!m_tool->canvas() || !m_tool->currentImage()) {
        return;
    }

    const TouchQuickShapeGeometry &shape = *m_editShape;

    QPainterPath path;
    switch (shape.kind) {
    case TouchQuickShapeGeometry::Kind::Rect:
        path.addRect(m_tool->pixelToView(shape.rect.normalized()));
        break;
    case TouchQuickShapeGeometry::Kind::Ellipse:
        path.addEllipse(m_tool->pixelToView(shape.rect.normalized()));
        break;
    case TouchQuickShapeGeometry::Kind::Polygon:
        if (!shape.polygon.isEmpty()) {
            path.moveTo(m_tool->pixelToView(shape.polygon.first()));
            for (int i = 1; i < shape.polygon.size(); ++i) {
                path.lineTo(m_tool->pixelToView(shape.polygon.at(i)));
            }
            path.closeSubpath();
        }
        break;
    case TouchQuickShapeGeometry::Kind::Line:
        path.moveTo(m_tool->pixelToView(shape.lineP0));
        path.lineTo(m_tool->pixelToView(shape.lineP1));
        break;
    case TouchQuickShapeGeometry::Kind::None:
    default:
        return;
    }

    m_tool->paintToolOutline(&gc, path);

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
        const QPointF hp = m_tool->pixelToView(handlePointsPx.at(i));
        const bool active = (m_editHandle == i && !m_editDragAll);

        const QColor fill = active ? QColor(255, 220, 80, 220) : QColor(240, 240, 240, 220);
        const QColor border = QColor(0, 0, 0, 200);
        gc.setPen(QPen(border, 1.0));
        gc.setBrush(fill);
        gc.drawEllipse(hp, kHandleRadius, kHandleRadius);
    }

    gc.restore();
}
