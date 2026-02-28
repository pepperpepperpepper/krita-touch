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
    void testTouchOnWindowForwardedTouchUsesPosWhenScenePosIsBogus();
    void testTouchOnWindowForwardedTouchUsesScreenPosWhenPosIsBogus();
};

#endif // KIS_TOUCH_UI_MOUSE_FALLBACK_FILTER_TEST_H
