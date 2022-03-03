/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "startupfeedback.h"

#include <kwineffects/effect_plugin_factory.h>

namespace KWin
{

KWIN_EFFECT_FACTORY_SUPPORTED(StartupFeedbackEffect,
                              "metadata.json.stripped",
                              return StartupFeedbackEffect::supported();)

} // namespace KWin

#include "main.moc"
