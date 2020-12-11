/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "deco.h"
#include "effects.h"
#include "shadow.h"

namespace KWin::win
{

template<typename Win>
auto scene_window(Win* win)
{
    auto eff_win = win->effectWindow();
    return eff_win ? eff_win->sceneWindow() : nullptr;
}

/**
 * Returns the pointer to the window's shadow. A shadow is only available if Compositing is enabled
 * and on X11 if the corresponding X window has the shadow property set.
 *
 * @returns The shadow belonging to @param win, @c null if there's no shadow.
 */
template<typename Win>
auto shadow(Win* win)
{
    auto sc_win = scene_window(win);
    return sc_win ? sc_win->shadow() : nullptr;
}

template<typename Win>
bool shaded(Win* win)
{
    return win->shadeMode() == shade::normal;
}

/**
 * Returns the area that win occupies from the point of view of the user.
 */
template<typename Win>
QRect visible_rect(Win* win)
{
    // There's no strict order between frame geometry and buffer geometry.
    auto rect = win->frameGeometry() | win->bufferGeometry();

    if (shadow(win) && !shadow(win)->shadowRegion().isEmpty()) {
        rect |= shadow(win)->shadowRegion().boundingRect().translated(win->pos());
    }

    return rect;
}

template<typename Win>
QRegion content_render_region(Win* win)
{
    if (win->control() && shaded(win)) {
        return QRegion();
    }

    auto const shape = win->render_region();
    auto clipping = QRect(QPoint(0, 0), win->bufferGeometry().size());

    if (win->has_in_content_deco) {
        auto const tl_offset = QPoint(left_border(win), top_border(win));
        auto const br_offset = -QPoint(right_border(win), bottom_border(win));

        clipping = QRect(tl_offset, clipping.bottomRight() + br_offset);
    }

    return shape & clipping;
}

/**
 * Updates the shadow associated with @param win.
 * Call this method when the windowing system notifies a change or compositing is started.
 */
template<typename Win>
auto update_shadow(Win* win)
{
    // Old & new shadow region
    QRect dirty_rect;

    auto const old_visible_rect = visible_rect(win);

    if (auto shdw = shadow(win)) {
        dirty_rect = shdw->shadowRegion().boundingRect();
        if (!shdw->updateShadow()) {
            scene_window(win)->updateShadow(nullptr);
        }
        Q_EMIT win->shadowChanged();
    } else if (win->effectWindow()) {
        Shadow::createShadow(win);
    }

    if (auto shdw = shadow(win)) {
        dirty_rect |= shdw->shadowRegion().boundingRect();
    }

    if (old_visible_rect != visible_rect(win)) {
        Q_EMIT win->paddingChanged(win, old_visible_rect);
    }

    if (dirty_rect.isValid()) {
        dirty_rect.translate(win->pos());
        win->addLayerRepaint(dirty_rect);
    }
}

}
