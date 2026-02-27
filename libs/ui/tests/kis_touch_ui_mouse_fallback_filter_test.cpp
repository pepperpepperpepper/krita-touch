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
    tp.setScreenPos(screenPos);
    tp.setStartPos(localPos);
    tp.setStartScreenPos(screenPos);
    tp.setLastPos(localPos);
    tp.setLastScreenPos(screenPos);
    return {tp};
}

static void sendSingleTouch(QWindow *window, QEvent::Type type, const QPointF &screenPos, Qt::TouchPointState state)
{
    // The filter uses `screenPos` + `QApplication::widgetAt()`; the local pos is irrelevant.
    const QPointF localPos = screenPos;
    QTouchEvent ev(type,
                   testTouchDevice(),
                   Qt::NoModifier,
                   Qt::TouchPointStates(state),
                   singleTouchPoint(localPos, screenPos, state));
    QCoreApplication::sendEvent(window, &ev);
}

class RecordingTouchWidget : public QWidget
{
public:
    int touchBeginCount{0};
    int touchUpdateCount{0};
    int touchEndCount{0};
    int touchCancelCount{0};

protected:
    bool event(QEvent *event) override
    {
        switch (event->type()) {
        case QEvent::TouchBegin:
            touchBeginCount++;
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

SIMPLE_TEST_MAIN(KisTouchUiMouseFallbackFilterTest)
