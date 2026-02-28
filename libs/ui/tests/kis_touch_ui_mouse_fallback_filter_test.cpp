/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kis_touch_ui_mouse_fallback_filter_test.h"

#include <QApplication>
#include <QCoreApplication>
#include <QPushButton>
#include <QSignalSpy>
#include <QTouchDevice>
#include <QTouchEvent>
#include <QWidget>
#include <QWindow>

#include "input/KisTouchUiMouseFallbackFilter.h"
#include "kis_config.h"

namespace
{

QTouchDevice *testTouchDevice()
{
    static QTouchDevice *device = []() {
        auto *d = new QTouchDevice;
        d->setType(QTouchDevice::TouchScreen);
        d->setCapabilities(QTouchDevice::Position);
        return d;
    }();
    return device;
}

static QList<QTouchEvent::TouchPoint> singleTouchPoint(const QPointF &localPos, const QPointF &screenPos, Qt::TouchPointState state)
{
    QTouchEvent::TouchPoint tp(0);
    tp.setState(state);
    tp.setPos(localPos);
    tp.setScenePos(localPos);
    tp.setScreenPos(screenPos);
    tp.setStartPos(localPos);
    tp.setStartScenePos(localPos);
    tp.setStartScreenPos(screenPos);
    tp.setLastPos(localPos);
    tp.setLastScenePos(localPos);
    tp.setLastScreenPos(screenPos);
    return {tp};
}

static QList<QTouchEvent::TouchPoint> singleTouchPointCustomPosScene(const QPointF &pos,
                                                                     const QPointF &scenePos,
                                                                     const QPointF &screenPos,
                                                                     Qt::TouchPointState state)
{
    QTouchEvent::TouchPoint tp(0);
    tp.setState(state);
    tp.setPos(pos);
    tp.setScenePos(scenePos);
    tp.setScreenPos(screenPos);
    tp.setStartPos(pos);
    tp.setStartScenePos(scenePos);
    tp.setStartScreenPos(screenPos);
    tp.setLastPos(pos);
    tp.setLastScenePos(scenePos);
    tp.setLastScreenPos(screenPos);
    return {tp};
}

static void sendSingleTouch(QWindow *window, QEvent::Type type, const QPointF &screenPos, Qt::TouchPointState state)
{
    // The filter may use `scenePos()` (window-local) to resolve widgets, so populate it.
    const QPoint localPoint = window->mapFromGlobal(screenPos.toPoint());
    const QPointF localPos(localPoint);
    QTouchEvent ev(type,
                   testTouchDevice(),
                   Qt::NoModifier,
                   Qt::TouchPointStates(state),
                   singleTouchPoint(localPos, screenPos, state));
    QCoreApplication::sendEvent(window, &ev);
}

static void sendSingleTouchCustom(QWindow *window,
                                  QEvent::Type type,
                                  const QPointF &scenePos,
                                  const QPointF &screenPos,
                                  Qt::TouchPointState state)
{
    QTouchEvent ev(type,
                   testTouchDevice(),
                   Qt::NoModifier,
                   Qt::TouchPointStates(state),
                   singleTouchPoint(scenePos, screenPos, state));
    QCoreApplication::sendEvent(window, &ev);
}

static void sendSingleTouchCustomPosScene(QWindow *window,
                                          QEvent::Type type,
                                          const QPointF &pos,
                                          const QPointF &scenePos,
                                          const QPointF &screenPos,
                                          Qt::TouchPointState state)
{
    QTouchEvent ev(type,
                   testTouchDevice(),
                   Qt::NoModifier,
                   Qt::TouchPointStates(state),
                   singleTouchPointCustomPosScene(pos, scenePos, screenPos, state));
    QCoreApplication::sendEvent(window, &ev);
}

class RecordingTouchWidget : public QWidget
{
public:
    explicit RecordingTouchWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
    }

    int touchBeginCount{0};
    int touchUpdateCount{0};
    int touchEndCount{0};
    int touchCancelCount{0};
    bool hasLastBeginPos{false};
    QPointF lastBeginPos;

protected:
    bool event(QEvent *event) override
    {
        switch (event->type()) {
        case QEvent::TouchBegin:
            touchBeginCount++;
            if (auto *touchEvent = dynamic_cast<QTouchEvent *>(event)) {
                const auto points = touchEvent->touchPoints();
                if (!points.isEmpty()) {
                    hasLastBeginPos = true;
                    lastBeginPos = points.first().pos();
                }
            }
            break;
        case QEvent::TouchUpdate:
            touchUpdateCount++;
            break;
        case QEvent::TouchEnd:
            touchEndCount++;
            break;
        case QEvent::TouchCancel:
            touchCancelCount++;
            break;
        default:
            break;
        }

        if (event->type() == QEvent::TouchBegin ||
            event->type() == QEvent::TouchUpdate ||
            event->type() == QEvent::TouchEnd ||
            event->type() == QEvent::TouchCancel) {
            // Touch event sequences are only delivered to widgets that accept TouchBegin.
            event->accept();
            return true;
        }

        return QWidget::event(event);
    }
};

}

void KisTouchUiMouseFallbackFilterTest::testTouchOnWindowClicksButton()
{
    KisConfig cfg(false);
    cfg.setTouchModeEnabled(true);

    auto *filter = new KisTouchUiMouseFallbackFilter(qApp);
    qApp->installEventFilter(filter);

    QWidget window;
    window.resize(320, 240);

    QPushButton button(QStringLiteral("Button"), &window);
    button.resize(160, 80);
    button.move(20, 20);

    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QSignalSpy clickedSpy(&button, &QPushButton::clicked);
    QVERIFY(clickedSpy.isValid());

    // Exercise the QWindow path (Wayland can deliver touch to the window rather than widgets).
    window.winId();
    QWindow *windowHandle = window.windowHandle();
    QVERIFY(windowHandle);

    const QPoint globalPos = button.mapToGlobal(button.rect().center());
    QCOMPARE(QApplication::widgetAt(globalPos), &button);

    const QPointF screenPos(globalPos);
    sendSingleTouch(windowHandle, QEvent::TouchBegin, screenPos, Qt::TouchPointPressed);
    QApplication::processEvents();
    sendSingleTouch(windowHandle, QEvent::TouchEnd, screenPos, Qt::TouchPointReleased);
    QApplication::processEvents();

    QVERIFY(clickedSpy.count() >= 1);

    qApp->removeEventFilter(filter);
    filter->deleteLater();
}

void KisTouchUiMouseFallbackFilterTest::testTouchOnWindowForwardsTouchToTouchNativeWidget()
{
    KisConfig cfg(false);
    cfg.setTouchModeEnabled(true);

    auto *filter = new KisTouchUiMouseFallbackFilter(qApp);
    qApp->installEventFilter(filter);

    QWidget window;
    window.resize(320, 240);

    RecordingTouchWidget touchNative(&window);
    touchNative.setObjectName(QStringLiteral("touchColorPickerButton"));
    touchNative.setAttribute(Qt::WA_AcceptTouchEvents, true);
    touchNative.resize(160, 80);
    touchNative.move(20, 20);

    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.winId();
    QWindow *windowHandle = window.windowHandle();
    QVERIFY(windowHandle);

    const QPoint globalPos = touchNative.mapToGlobal(touchNative.rect().center());
    QCOMPARE(QApplication::widgetAt(globalPos), &touchNative);

    const QPointF screenPos(globalPos);
    sendSingleTouch(windowHandle, QEvent::TouchBegin, screenPos, Qt::TouchPointPressed);
    QApplication::processEvents();
    sendSingleTouch(windowHandle, QEvent::TouchEnd, screenPos, Qt::TouchPointReleased);
    QApplication::processEvents();

    QVERIFY(touchNative.touchBeginCount >= 1);
    QVERIFY(touchNative.touchEndCount >= 1);

    qApp->removeEventFilter(filter);
    filter->deleteLater();
}

void KisTouchUiMouseFallbackFilterTest::testTouchOnWindowForwardedTouchUsesWindowCoordsForMapping()
{
    KisConfig cfg(false);
    cfg.setTouchModeEnabled(true);

    auto *filter = new KisTouchUiMouseFallbackFilter(qApp);
    qApp->installEventFilter(filter);

    QWidget window;
    window.resize(320, 240);

    RecordingTouchWidget touchNative(&window);
    touchNative.setObjectName(QStringLiteral("touchColorPickerButton"));
    touchNative.setAttribute(Qt::WA_AcceptTouchEvents, true);
    touchNative.resize(160, 80);
    touchNative.move(20, 20);

    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.winId();
    QWindow *windowHandle = window.windowHandle();
    QVERIFY(windowHandle);

    const QPoint globalPos = touchNative.mapToGlobal(touchNative.rect().center());
    QCOMPARE(QApplication::widgetAt(globalPos), &touchNative);

    const QPoint windowPos = windowHandle->mapFromGlobal(globalPos);
    const QPoint expectedLocal = touchNative.mapFrom(&window, windowPos);

    // Deliberately send a bogus screenPos (window-local) to ensure the forwarding path
    // maps via window-local coords (scenePos) rather than mapFromGlobal(screenPos).
    const QPointF scenePos(windowPos);
    const QPointF bogusScreenPos(windowPos);

    sendSingleTouchCustom(windowHandle, QEvent::TouchBegin, scenePos, bogusScreenPos, Qt::TouchPointPressed);
    QApplication::processEvents();
    sendSingleTouchCustom(windowHandle, QEvent::TouchEnd, scenePos, bogusScreenPos, Qt::TouchPointReleased);
    QApplication::processEvents();

    QVERIFY(touchNative.touchBeginCount >= 1);
    QVERIFY(touchNative.touchEndCount >= 1);
    QVERIFY(touchNative.hasLastBeginPos);
    QCOMPARE(touchNative.lastBeginPos, QPointF(expectedLocal));

    qApp->removeEventFilter(filter);
    filter->deleteLater();
}

void KisTouchUiMouseFallbackFilterTest::testTouchOnWindowForwardedTouchUsesPosWhenScenePosIsBogus()
{
    KisConfig cfg(false);
    cfg.setTouchModeEnabled(true);
    QVERIFY(KisConfig(true).touchModeEnabled());

    auto *filter = new KisTouchUiMouseFallbackFilter(qApp);
    qApp->installEventFilter(filter);

    QWidget window;
    window.resize(320, 240);

    RecordingTouchWidget touchNative(&window);
    touchNative.setObjectName(QStringLiteral("touchColorPickerButton"));
    touchNative.setAttribute(Qt::WA_AcceptTouchEvents, true);
    touchNative.resize(160, 80);
    touchNative.move(20, 20);

    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.winId();
    QWindow *windowHandle = window.windowHandle();
    QVERIFY(windowHandle);

    const QPoint globalPos = touchNative.mapToGlobal(touchNative.rect().center());
    QCOMPARE(QApplication::widgetAt(globalPos), &touchNative);

    const QPoint windowPos = windowHandle->mapFromGlobal(globalPos);
    const QPoint expectedLocal = touchNative.mapFrom(&window, windowPos);
    QVERIFY(window.rect().contains(windowPos));
    QCOMPARE(window.childAt(windowPos), &touchNative);

    QWidget *foundTopLevel = nullptr;
    for (QWidget *tl : QApplication::topLevelWidgets()) {
        if (!tl) {
            continue;
        }
        if (tl->windowHandle() == windowHandle) {
            foundTopLevel = tl;
            break;
        }
    }
    QCOMPARE(foundTopLevel, &window);

    // Simulate a failure mode where the touch sequence is delivered to the window handle and
    // TouchPoint::scenePos() is bogus while TouchPoint::pos() is correct window-local.
    const QPointF pos(windowPos);
    const QPointF bogusScenePos(windowPos + QPoint(1000, 1000));
    const QPointF bogusScreenPos(99999.0, 99999.0);

    const QPoint globalFromPos = windowHandle->mapToGlobal(windowPos);
    QCOMPARE(QApplication::widgetAt(globalFromPos), &touchNative);

    sendSingleTouchCustomPosScene(windowHandle, QEvent::TouchBegin, pos, bogusScenePos, bogusScreenPos, Qt::TouchPointPressed);
    QApplication::processEvents();
    sendSingleTouchCustomPosScene(windowHandle, QEvent::TouchEnd, pos, bogusScenePos, bogusScreenPos, Qt::TouchPointReleased);
    QApplication::processEvents();

    QVERIFY(touchNative.touchBeginCount >= 1);
    QVERIFY(touchNative.touchEndCount >= 1);
    QVERIFY(touchNative.hasLastBeginPos);
    QCOMPARE(touchNative.lastBeginPos, QPointF(expectedLocal));

    qApp->removeEventFilter(filter);
    filter->deleteLater();
}

void KisTouchUiMouseFallbackFilterTest::testTouchOnWindowForwardedTouchUsesScreenPosWhenPosIsBogus()
{
    KisConfig cfg(false);
    cfg.setTouchModeEnabled(true);
    QVERIFY(KisConfig(true).touchModeEnabled());

    auto *filter = new KisTouchUiMouseFallbackFilter(qApp);
    qApp->installEventFilter(filter);

    QWidget window;
    window.resize(320, 240);

    RecordingTouchWidget touchNative(&window);
    touchNative.setObjectName(QStringLiteral("touchColorPickerButton"));
    touchNative.setAttribute(Qt::WA_AcceptTouchEvents, true);
    touchNative.resize(160, 80);
    touchNative.move(20, 20);

    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.winId();
    QWindow *windowHandle = window.windowHandle();
    QVERIFY(windowHandle);

    const QPoint globalPos = touchNative.mapToGlobal(touchNative.rect().center());
    QCOMPARE(QApplication::widgetAt(globalPos), &touchNative);

    const QPoint windowPos = windowHandle->mapFromGlobal(globalPos);
    const QPoint expectedLocal = touchNative.mapFrom(&window, windowPos);
    QVERIFY(window.rect().contains(windowPos));
    QCOMPARE(window.childAt(windowPos), &touchNative);

    // Simulate a failure mode where the touch sequence is delivered to the window handle and
    // TouchPoint::pos() is bogus while TouchPoint::screenPos() is correct global.
    const QPointF bogusPos(windowPos + QPoint(1000, 1000));
    const QPointF bogusScenePos(bogusPos);
    const QPointF screenPos(globalPos);

    sendSingleTouchCustomPosScene(windowHandle, QEvent::TouchBegin, bogusPos, bogusScenePos, screenPos, Qt::TouchPointPressed);
    QApplication::processEvents();
    sendSingleTouchCustomPosScene(windowHandle, QEvent::TouchEnd, bogusPos, bogusScenePos, screenPos, Qt::TouchPointReleased);
    QApplication::processEvents();

    QVERIFY(touchNative.touchBeginCount >= 1);
    QVERIFY(touchNative.touchEndCount >= 1);
    QVERIFY(touchNative.hasLastBeginPos);
    QCOMPARE(touchNative.lastBeginPos, QPointF(expectedLocal));

    qApp->removeEventFilter(filter);
    filter->deleteLater();
}

SIMPLE_TEST_MAIN(KisTouchUiMouseFallbackFilterTest)
