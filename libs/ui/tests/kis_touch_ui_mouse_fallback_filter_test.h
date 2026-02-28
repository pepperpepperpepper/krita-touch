/*
 * SPDX-FileCopyrightText: 2026
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef KIS_TOUCH_UI_MOUSE_FALLBACK_FILTER_TEST_H
#define KIS_TOUCH_UI_MOUSE_FALLBACK_FILTER_TEST_H

#include <simpletest.h>

class KisTouchUiMouseFallbackFilterTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void testTouchOnWindowClicksButton();
    void testTouchOnWindowForwardsTouchToTouchNativeWidget();
    void testTouchOnWindowForwardedTouchUsesWindowCoordsForMapping();
    void testTouchOnWindowForwardedTouchUsesWindowCoordsWhenScreenPosLooksPlausible();
    void testTouchOnWindowForwardedTouchUsesPosWhenScenePosIsBogus();
    void testTouchOnWindowForwardedTouchUsesScreenPosWhenPosIsBogus();
    void testTouchOnWindowForwardedTouchUsesScreenPosWhenPosIsWrongButInside();
};

#endif // KIS_TOUCH_UI_MOUSE_FALLBACK_FILTER_TEST_H
