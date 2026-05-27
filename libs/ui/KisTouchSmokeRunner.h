/*
 * SPDX-FileCopyrightText: 1998, 1999 Torben Weis <weis@kde.org>
 * SPDX-FileCopyrightText: 2012 Boudewijn Rempt <boud@valdyas.org>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef KIS_TOUCH_SMOKE_RUNNER_H
#define KIS_TOUCH_SMOKE_RUNNER_H

#include <QString>

class KisMainWindow;

void runTouchSmokeScenario(const QString &scenario, KisMainWindow *mainWindow);

#endif /* KIS_TOUCH_SMOKE_RUNNER_H */
