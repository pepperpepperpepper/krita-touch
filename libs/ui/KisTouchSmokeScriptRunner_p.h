/*
 * This file is part of the Krita touch fork.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KISTOUCHSMOKESCRIPTRUNNER_P_H
#define KISTOUCHSMOKESCRIPTRUNNER_P_H

#include <functional>

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QPointF>
#include <QString>
#include <QVector>

#include <kis_types.h>

class KisConfig;
class KisMainWindow;
class KisView;
class QTouchDevice;
class QWidget;

namespace KisTouchSmokeScriptRunnerDetail {

QString jsonTypeName(const QJsonValue &v);

QTouchDevice *touchDevice();

bool waitForUiCondition(int timeoutMs, const std::function<bool()> &condition);
bool waitForImageCondition(KisImageWSP image, int timeoutMs, const std::function<bool()> &condition);

bool sleepWithEvents(int ms);
bool sleepWithImageEvents(KisImageWSP image, int ms);

bool setInputProfile(const QString &profileName, QString *errorOut);
bool setConfigBool(KisConfig &cfg, const QString &key, bool value, QJsonObject *details, QString *errorOut);

bool getCanvasContext(KisMainWindow *mainWindow, KisView **viewOut, QWidget **canvasWidgetOut, QString *errorOut);
bool resolveWidgetPos(KisMainWindow *mainWindow, const QJsonObject &posObj, QPointF *posOut, QString *errorOut);

bool assertActiveToolMaskSyntheticEvents(KisMainWindow *mainWindow, bool expected, int timeoutMs, QJsonObject *details, QString *errorOut);

bool overlayVisible(KisMainWindow *mainWindow, const QString &objectName);
bool hideOverlay(KisMainWindow *mainWindow, const QString &objectName, QJsonObject *details);
bool waitOverlayVisible(KisMainWindow *mainWindow,
                        const QString &objectName,
                        bool expectedVisible,
                        int timeoutMs,
                        QJsonObject *details,
                        QString *errorOut);

int touchShortcutFromString(const QString &shortcutName);

bool actionEnsureChecked(KisMainWindow *mainWindow, const QString &actionId, bool expectedChecked, int timeoutMs, QJsonObject *details, QString *errorOut);

bool actionTriggerWaitLayerCountDelta(KisMainWindow *mainWindow,
                                      const QString &actionId,
                                      int delta,
                                      int timeoutMs,
                                      QJsonObject *details,
                                      QString *errorOut);

struct TouchDragPathPoints {
    QVector<QVector<QPointF>> localPoints; // [step][finger]
    QVector<QVector<QPointF>> globalPoints; // [step][finger]
};

bool buildTouchHoldPoints(KisMainWindow *mainWindow,
                          int fingerCount,
                          const QJsonObject &posObj,
                          QWidget **canvasWidgetOut,
                          QVector<QPointF> *localPointsOut,
                          QVector<QPointF> *globalPointsOut,
                          QString *errorOut);

bool sendMultiFingerTouchTap(KisMainWindow *mainWindow, int fingerCount, const QJsonObject &posObj, QString *errorOut);

bool buildTouchDragPathPoints(KisMainWindow *mainWindow,
                              int fingerCount,
                              const QJsonArray &pathArray,
                              TouchDragPathPoints *out,
                              QString *errorOut);

void sendTouchDragPath(QWidget *canvasWidget, const TouchDragPathPoints &pathPoints, int stepMs, int holdMsAtEnd, QJsonObject *details);

void performTouchDragPathViaGestureAction(int shortcut, const TouchDragPathPoints &pathPoints, QJsonObject *details);

bool performTouchGestureShortcut(int shortcut, QJsonObject *details, QString *errorOut);

bool waitForLayerCountDelta(KisImageWSP image,
                            int delta,
                            int timeoutMs,
                            QJsonObject *details,
                            QString *errorOut);

bool paintRectForTouchScript(KisMainWindow *mainWindow, const QJsonObject &rectObj, const QColor &color, qreal strokePx, QJsonObject *details, QString *errorOut);

bool waitForPixelAlphaInRange(KisMainWindow *mainWindow,
                              const QJsonObject &posObj,
                              int minAlpha,
                              int maxAlpha,
                              int timeoutMs,
                              QJsonObject *details,
                              QString *errorOut);

bool touchTapCheckableActionWithFallback(KisMainWindow *mainWindow,
                                        int fingerCount,
                                        const QJsonObject &posObj,
                                        const QString &actionId,
                                        bool expectedChecked,
                                        int timeoutMs,
                                        int fallbackShortcut,
                                        QJsonObject *details,
                                        QString *errorOut);

bool touchTapCheckableActionNoChange(KisMainWindow *mainWindow,
                                    int fingerCount,
                                    const QJsonObject &posObj,
                                    const QString &actionId,
                                    bool expectedChecked,
                                    int settleMs,
                                    int directShortcut,
                                    QJsonObject *details,
                                    QString *errorOut);

bool touchTapLayerCountWithFallback(KisMainWindow *mainWindow,
                                   int fingerCount,
                                   const QJsonObject &posObj,
                                   int delta,
                                   int timeoutMs,
                                   int fallbackShortcut,
                                   bool requireInputManager,
                                   QJsonObject *details,
                                   QString *errorOut);

bool touchTapLayerCountNoChange(KisMainWindow *mainWindow,
                               int fingerCount,
                               const QJsonObject &posObj,
                               int settleMs,
                               int directShortcut,
                               QJsonObject *details,
                               QString *errorOut);

bool touchDragPathWaitOverlayVisibleWithFallback(KisMainWindow *mainWindow,
                                                int fingerCount,
                                                const QJsonArray &pathArray,
                                                const QString &overlayObjectName,
                                                int timeoutMs,
                                                int fallbackShortcut,
                                                int stepMs,
                                                bool requireInputManager,
                                                QJsonObject *details,
                                                QString *errorOut);

bool touchHoldWaitOverlayVisibleWithFallback(KisMainWindow *mainWindow,
                                            int fingerCount,
                                            const QJsonObject &posObj,
                                            const QString &overlayObjectName,
                                            int timeoutMs,
                                            const QString &fallbackActionName,
                                            bool requireInputManager,
                                            QJsonObject *details,
                                            QString *errorOut);

bool touchDragPathOverlayNoChange(KisMainWindow *mainWindow,
                                 int fingerCount,
                                 const QJsonArray &pathArray,
                                 const QString &overlayObjectName,
                                 int settleMs,
                                 int directShortcut,
                                 int stepMs,
                                 QJsonObject *details,
                                 QString *errorOut);

bool touchHoldOverlayNoChange(KisMainWindow *mainWindow,
                              int fingerCount,
                              const QJsonObject &posObj,
                              const QString &overlayObjectName,
                              int settleMs,
                              const QString &directActionName,
                              QJsonObject *details,
                              QString *errorOut);

bool touchDragPathWaitPixelAlphaWithFallback(KisMainWindow *mainWindow,
                                            int fingerCount,
                                            const QJsonArray &pathArray,
                                            const QJsonObject &samplePosObj,
                                            int minAlpha,
                                            int maxAlpha,
                                            int timeoutMs,
                                            int fallbackShortcut,
                                            int stepMs,
                                            bool requireInputManager,
                                            QJsonObject *details,
                                            QString *errorOut);

bool touchDragPathPixelAlphaNoChange(KisMainWindow *mainWindow,
                                     int fingerCount,
                                     const QJsonArray &pathArray,
                                     const QJsonObject &samplePosObj,
                                     int minAlpha,
                                     int maxAlpha,
                                     int settleMs,
                                     int directShortcut,
                                     int stepMs,
                                     QJsonObject *details,
                                     QString *errorOut);

bool mouseDragPathWaitPixelAlphaRange(KisMainWindow *mainWindow,
                                      const QJsonArray &pathArray,
                                      const QString &mouseSourceName,
                                      const QJsonObject &samplePosObj,
                                      int minAlpha,
                                      int maxAlpha,
                                      int timeoutMs,
                                      int stepMs,
                                      int holdMsAtEnd,
                                      QJsonObject *details,
                                      QString *errorOut);

bool mouseDragPathPixelAlphaNoChange(KisMainWindow *mainWindow,
                                     const QJsonArray &pathArray,
                                     const QString &mouseSourceName,
                                     const QJsonObject &samplePosObj,
                                     int minAlpha,
                                     int maxAlpha,
                                     int settleMs,
                                     int stepMs,
                                     int holdMsAtEnd,
                                     QJsonObject *details,
                                     QString *errorOut);

bool toolProxyStrokePathWaitPixelAlphaRange(KisMainWindow *mainWindow,
                                           const QJsonArray &pathArray,
                                           const QString &mouseSourceName,
                                           const QJsonObject &samplePosObj,
                                           int minAlpha,
                                           int maxAlpha,
                                           int timeoutMs,
                                           int stepMs,
                                           int holdMsAtEnd,
                                           const QJsonObject &androidFallbackPaintRectObj,
                                           const QColor &androidFallbackColor,
                                           qreal androidFallbackStrokePx,
                                           bool allowNoPaintOnAndroid,
                                           QJsonObject *details,
                                           QString *errorOut);

bool toolProxyStrokePathPixelAlphaNoChange(KisMainWindow *mainWindow,
                                          const QJsonArray &pathArray,
                                          const QString &mouseSourceName,
                                          const QJsonObject &samplePosObj,
                                          int minAlpha,
                                          int maxAlpha,
                                          int settleMs,
                                          int stepMs,
                                          int holdMsAtEnd,
                                          QJsonObject *details,
                                          QString *errorOut);

bool paintDragPathWaitPixelAlphaRange(KisMainWindow *mainWindow,
                                      const QJsonArray &pathArray,
                                      const QString &mouseSourceName,
                                      const QJsonObject &samplePosObj,
                                      int minAlpha,
                                      int maxAlpha,
                                      int timeoutMs,
                                      int stepMs,
                                      int holdMsAtEnd,
                                      bool allowNoPaintOnAndroid,
                                      QJsonObject *details,
                                      QString *errorOut);

bool paintDragPathPixelAlphaNoChange(KisMainWindow *mainWindow,
                                     const QJsonArray &pathArray,
                                     const QString &mouseSourceName,
                                     const QJsonObject &samplePosObj,
                                     int minAlpha,
                                     int maxAlpha,
                                     int settleMs,
                                     int stepMs,
                                     int holdMsAtEnd,
                                     QJsonObject *details,
                                     QString *errorOut);

bool touchPinchRotateWaitCanvasTransform(KisMainWindow *mainWindow,
                                        const QJsonObject &centerObj,
                                        qreal radiusStartFrac,
                                        qreal radiusEndFrac,
                                        qreal rotationDeg,
                                        qreal startAngleDeg,
                                        int steps,
                                        int timeoutMs,
                                        int stepMs,
                                        bool expectRotation,
                                        qreal minZoomRatioChange,
                                        qreal minAbsRotationDeg,
                                        qreal maxAbsRotationDeg,
                                        bool forceDirectAction,
                                        QJsonObject *details,
                                        QString *errorOut);

bool loadJsonObject(const QString &path, QJsonObject *out, QString *errorOut);
QString scriptIdFromScenarioSpec(const QString &scenarioSpec);

} // namespace KisTouchSmokeScriptRunnerDetail

#endif // KISTOUCHSMOKESCRIPTRUNNER_P_H
