/*
 * This file is part of the KDE project
 * SPDX-FileCopyrightText: 2019 Sharaf Zaman <sharafzaz121@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QApplication>
#include <QAction>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QPointer>
#include <QTouchEvent>

#include <klocalizedstring.h>
#include <kis_canvas_controller.h>
#include <kis_canvas2.h>
#include <kis_config.h>
#include <KisViewManager.h>
#include <kactioncollection.h>
#include <kis_algebra_2d.h>
#include <KoToolManager.h>

#include "kis_zoom_and_rotate_action.h"
#include "kis_input_manager.h"
#include <KoViewTransformStillPoint.h>

class KisZoomAndRotateAction::Private {
public:
    Private() {}

    int shortcutIndex {0};
    QPointF lastPosition {0, 0};
    float lastDistance {0.0};
    qreal previousAngle {0.0};
    qreal initialReferenceAngle {0.0};
    qreal accumRotationAngle {0.0};

    KoViewTransformStillPoint actionStillPoint;

    QPointer<QObject> touchTransformTool;
    bool touchTransformActive {false};

    QElapsedTimer quickPinchTimer;
    bool quickPinchCandidate {false};
    float quickPinchStartDistance {0.0f};
    float quickPinchLastDistance {0.0f};
    qreal quickPinchAccumAbsRotation {0.0};
};

KisZoomAndRotateAction::KisZoomAndRotateAction()
    : KisAbstractInputAction ("Zoom and Rotate Canvas")
    , d(new Private)
{
    setName(i18n("Zoom and Rotate Canvas"));
    QHash<QString, int> shortcuts;
    shortcuts.insert(i18n("Rotate Mode"), ContinuousRotateMode);
    shortcuts.insert(i18n("Discrete Rotate Mode"), DiscreteRotateMode);
    setShortcutIndexes(shortcuts);
}

KisZoomAndRotateAction::~KisZoomAndRotateAction()
{
}

int KisZoomAndRotateAction::priority() const
{
    return 5;
}

void KisZoomAndRotateAction::activate(int shortcut)
{
    Q_UNUSED(shortcut);
}

void KisZoomAndRotateAction::deactivate(int shortcut)
{
    Q_UNUSED(shortcut);
}

void KisZoomAndRotateAction::begin(int shortcut, QEvent *event)
{
    QTouchEvent *touchEvent = dynamic_cast<QTouchEvent *>(event);

    d->touchTransformTool.clear();
    d->touchTransformActive = false;

    d->quickPinchCandidate = false;
    d->quickPinchStartDistance = 0.0f;
    d->quickPinchLastDistance = 0.0f;
    d->quickPinchAccumAbsRotation = 0.0;
    d->quickPinchTimer.invalidate();

    if (touchEvent && touchEvent->touchPoints().size() > 0) {
        d->shortcutIndex = shortcut;
        d->lastPosition = touchEvent->touchPoints().at(0).pos();
        d->lastDistance = 0;
        d->previousAngle = 0;
        d->initialReferenceAngle = 0;
        d->accumRotationAngle = 0;
        d->actionStillPoint = inputManager()->canvas()->coordinatesConverter()->makeWidgetStillPoint(d->lastPosition);

        if (touchEvent->touchPoints().size() > 1) {
            KisConfig cfg(true);
            if (cfg.touchModeEnabled()) {
                d->quickPinchCandidate = cfg.touchQuickPinchToFitEnabled();
                if (d->quickPinchCandidate) {
                    const QPointF p0 = touchEvent->touchPoints().at(0).pos();
                    const QPointF p1 = touchEvent->touchPoints().at(1).pos();
                    d->quickPinchStartDistance = QLineF(p0, p1).length();
                    d->quickPinchLastDistance = d->quickPinchStartDistance;
                    d->quickPinchTimer.start();
                }

                KoToolManager *toolManager = KoToolManager::instance();
                if (toolManager && toolManager->activeToolId() == QStringLiteral("KisToolTransform")) {
                    QObject *toolObj = dynamic_cast<QObject *>(toolManager->toolById(inputManager()->canvas(), toolManager->activeToolId()));

                    if (toolObj) {
                        const QPointF p0 = touchEvent->touchPoints().at(0).pos();
                        const QPointF p1 = touchEvent->touchPoints().at(1).pos();
                        const QPointF centerWidget = (p0 + p1) * 0.5;

                        bool hit = false;
                        const bool canHitTest = QMetaObject::invokeMethod(toolObj, "touchTransformHitTest", Qt::DirectConnection,
                                                                         Q_RETURN_ARG(bool, hit),
                                                                         Q_ARG(QPointF, centerWidget));

                        if (canHitTest && hit) {
                            bool began = false;
                            const bool invokedBegin =
                                QMetaObject::invokeMethod(toolObj, "touchTransformGestureBegin", Qt::DirectConnection,
                                                          Q_RETURN_ARG(bool, began),
                                                          Q_ARG(QPointF, p0),
                                                          Q_ARG(QPointF, p1));

                            if (invokedBegin && began) {
                                d->touchTransformTool = toolObj;
                                d->touchTransformActive = true;
                                d->quickPinchCandidate = false;
                            }
                        }
                    }
                }
            }
        }
    }
}

void KisZoomAndRotateAction::cursorMovedAbsolute(const QPointF &, const QPointF &)
{
}

qreal angleForSnapping(qreal angle)
{
    if (angle < 0) {
        return std::fmod(angle - 2, 45) + 2;
    } else {
        return std::fmod(angle + 2, 45) - 2;
    }
}

void KisZoomAndRotateAction::inputEvent(QEvent *event)
{
    switch (event->type()) {
    case QEvent::TouchUpdate: {
        QTouchEvent *tevent = dynamic_cast<QTouchEvent *>(event);
        if (tevent && tevent->touchPoints().size() > 1) {

            if (d->touchTransformActive && d->touchTransformTool) {
                const QPointF p0 = tevent->touchPoints().at(0).pos();
                const QPointF p1 = tevent->touchPoints().at(1).pos();

                QMetaObject::invokeMethod(d->touchTransformTool, "touchTransformGestureUpdate", Qt::DirectConnection,
                                          Q_ARG(QPointF, p0),
                                          Q_ARG(QPointF, p1));

                return;
            }

            const QPointF p0 = tevent->touchPoints().at(0).pos();
            const QPointF p1 = tevent->touchPoints().at(1).pos();

            const qreal rotationAngle = canvasRotationAngle(p0, p1);
            const float dist = QLineF(p0, p1).length();
            const float scaleDelta = qFuzzyCompare(1.0f, 1.0f + d->lastDistance) ? 1.f : dist / d->lastDistance;

            KisCanvas2 *canvas = inputManager()->canvas();
            KisCanvasController *controller = static_cast<KisCanvasController *>(canvas->canvasController());
            const qreal newZoom = canvas->viewConverter()->zoom() * scaleDelta;
            KoViewTransformStillPoint adjustedStillPoint = d->actionStillPoint;
            adjustedStillPoint.second = p0;
            controller->setZoom(KoZoomMode::ZOOM_CONSTANT, newZoom, adjustedStillPoint);
            const KisConfig cfg(true);
            const bool rotateWithPinch = !cfg.touchModeEnabled() || cfg.touchRotateWithPinchEnabled();
            if (rotateWithPinch) {
                controller->rotateCanvas(rotationAngle, adjustedStillPoint);
            }

            if (d->quickPinchCandidate) {
                d->quickPinchLastDistance = dist;
                if (rotateWithPinch) {
                    d->quickPinchAccumAbsRotation += std::abs(rotationAngle);
                }
            }

            d->lastPosition = p0;
            d->lastDistance = dist;

            return;
        }
    }
    default:
        break;
    }
    KisAbstractInputAction::inputEvent(event);
}

void KisZoomAndRotateAction::end(QEvent *event)
{
    Q_UNUSED(event);

    if (d->touchTransformActive && d->touchTransformTool) {
        QMetaObject::invokeMethod(d->touchTransformTool, "touchTransformGestureEnd", Qt::DirectConnection);
    }

    if (d->quickPinchCandidate && d->quickPinchTimer.isValid()) {
        const KisConfig cfg(true);
        if (cfg.touchModeEnabled() && cfg.touchQuickPinchToFitEnabled()) {
            const qint64 elapsedMs = d->quickPinchTimer.elapsed();
            constexpr qint64 kQuickPinchMaxMs = 250;
            constexpr float kQuickPinchMinScale = 1.25f;
            constexpr qreal kQuickPinchMaxAbsRotationDeg = 10.0;

            const float startDist = d->quickPinchStartDistance;
            const float endDist = d->quickPinchLastDistance > 0.0f ? d->quickPinchLastDistance : d->lastDistance;

            float scaleMagnitude = 1.0f;
            if (startDist > 0.0f && endDist > 0.0f) {
                const float ratio = endDist / startDist;
                scaleMagnitude = ratio >= 1.0f ? ratio : (1.0f / ratio);
            }

            const bool timeOk = elapsedMs >= 0 && elapsedMs <= kQuickPinchMaxMs;
            const bool scaleOk = scaleMagnitude >= kQuickPinchMinScale;
            const bool rotationOk = d->quickPinchAccumAbsRotation <= kQuickPinchMaxAbsRotationDeg;

            if (timeOk && scaleOk && rotationOk) {
                KisCanvas2 *canvas = inputManager()->canvas();
                KisViewManager *viewManager = canvas ? canvas->viewManager() : nullptr;
                KisKActionCollection *actionCollection = viewManager ? viewManager->actionCollection() : nullptr;
                if (QAction *toggleZoomToFit = actionCollection ? actionCollection->action(QStringLiteral("toggle_zoom_to_fit")) : nullptr) {
                    toggleZoomToFit->trigger();
                }
            }
        }
    }

    d->touchTransformActive = false;
    d->touchTransformTool.clear();
}

KisInputActionGroup KisZoomAndRotateAction::inputActionGroup(int shortcut) const
{
    Q_UNUSED(shortcut);
    return ViewTransformActionGroup;
}

qreal KisZoomAndRotateAction::canvasRotationAngle(QPointF p0, QPointF p1)
{
    const QPointF slope = p1 - p0;
    const qreal currentAngle = std::atan2(slope.y(), slope.x());

    switch (d->shortcutIndex) {
    case ContinuousRotateMode: {
        if (!d->previousAngle) {
            d->previousAngle = currentAngle;
            return 0;
        }
        qreal rotationAngle = (180 / M_PI) * (currentAngle - d->previousAngle);
        d->previousAngle = currentAngle;

        KisCanvas2 *canvas = inputManager()->canvas();
        KisCanvasController *controller = static_cast<KisCanvasController *>(canvas->canvasController());
        const qreal canvasAnglePostRotation = controller->rotation() + rotationAngle;
        const qreal snapDelta = angleForSnapping(canvasAnglePostRotation);
        // we snap the canvas to an angle that is a multiple of 45
        if (abs(snapDelta) <= 2 && abs(d->accumRotationAngle) <= 2) {
            // accumulate the relative angle of finger from the point when we started snapping
            d->accumRotationAngle += rotationAngle;
            rotationAngle = rotationAngle - snapDelta;
        } else {
            // snap the canvas out using the accumulated angle
            rotationAngle += d->accumRotationAngle;
            d->accumRotationAngle = 0;
        }

        return rotationAngle;
    }
    case DiscreteRotateMode: {
        if (!d->initialReferenceAngle) {
            d->initialReferenceAngle = currentAngle;
            return 0;
        }
        qreal rotationAngle = 0;
        const qreal relativeAngle = (180 / M_PI) * (currentAngle - d->initialReferenceAngle);
        const qreal rotationThreshold = 15;

        // if the canvas is moved in either direction with an angle greater than the threshold, we rotate the canvas in
        // that direction by 15°.
        if (std::abs(relativeAngle) >= rotationThreshold && std::abs(relativeAngle) <= (360 - rotationThreshold)) {
            // set reference as currentAngle to check if we go beyond the threshold next time
            d->initialReferenceAngle = currentAngle;

            if (std::abs(relativeAngle) <= 180) {
                rotationAngle = KisAlgebra2D::copysign(15.0, relativeAngle);
            } else {
                // if we're over 180, it means the canvas has to be rotated in the opposite direction of the current
                // angle. E.g if the relative angle is +341° then we move the canvas by -15° (because the actual effect
                // is 341 - 360 = -19°  on the original theta).
                rotationAngle = KisAlgebra2D::copysign(15.0, -relativeAngle);
            }
        }
        return rotationAngle;
    }
    default:
        qWarning() << "KisZoomAndRotateAction: Unrecognized shortcut" << d->shortcutIndex;
        return 0;
    }
}
