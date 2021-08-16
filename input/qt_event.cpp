/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "qt_event.h"

#include "keyboard_redirect.h"
#include "pointer_redirect.h"
#include <main.h>

#include <QHash>
#include <linux/input.h>

namespace KWin::input
{

QHash<uint32_t, Qt::MouseButton> const button_map = {
    {BTN_LEFT, Qt::LeftButton},
    {BTN_MIDDLE, Qt::MiddleButton},
    {BTN_RIGHT, Qt::RightButton},
    // in QtWayland mapped like that
    {BTN_SIDE, Qt::ExtraButton1},
    // in QtWayland mapped like that
    {BTN_EXTRA, Qt::ExtraButton2},
    {BTN_BACK, Qt::BackButton},
    {BTN_FORWARD, Qt::ForwardButton},
    {BTN_TASK, Qt::TaskButton},
    // mapped like that in QtWayland
    {0x118, Qt::ExtraButton6},
    {0x119, Qt::ExtraButton7},
    {0x11a, Qt::ExtraButton8},
    {0x11b, Qt::ExtraButton9},
    {0x11c, Qt::ExtraButton10},
    {0x11d, Qt::ExtraButton11},
    {0x11e, Qt::ExtraButton12},
    {0x11f, Qt::ExtraButton13},
};

Qt::MouseButton button_to_qt_mouse_button(uint32_t button)
{
    // all other values get mapped to ExtraButton24
    // this is actually incorrect but doesn't matter in our usage
    // KWin internally doesn't use these high extra buttons anyway
    // it's only needed for recognizing whether buttons are pressed
    // if multiple buttons are mapped to the value the evaluation whether
    // buttons are pressed is correct and that's all we care about.
    return button_map.value(button, Qt::ExtraButton24);
}

uint32_t qt_mouse_button_to_button(Qt::MouseButton button)
{
    return button_map.key(button);
}

QMouseEvent get_qt_mouse_event(QEvent::Type type, QPointF const& pos, Qt::MouseButton button)
{
    auto buttons = kwinApp()->input->redirect->pointer()->buttons();
    auto modifiers = kwinApp()->input->redirect->keyboard()->modifiers();

    return QMouseEvent(type, pos, pos, button, buttons, modifiers);
}

QMouseEvent get_qt_mouse_button_event(uint32_t key, button_state state)
{
    auto type = state == button_state::pressed ? QMouseEvent::MouseButtonPress
                                               : QMouseEvent::MouseButtonRelease;
    auto pos = kwinApp()->input->redirect->pointer()->pos();
    auto button = button_to_qt_mouse_button(key);

    return get_qt_mouse_event(type, pos, button);
}

QMouseEvent get_qt_mouse_motion_absolute_event(QPointF const& pos)
{
    return get_qt_mouse_event(QMouseEvent::MouseMove, pos, Qt::NoButton);
}

QMouseEvent button_to_qt_event(button_event const& event)
{
    return get_qt_mouse_button_event(event.key, event.state);
}

QMouseEvent motion_to_qt_event([[maybe_unused]] motion_event const& event)
{
    auto pos = kwinApp()->input->redirect->pointer()->pos();
    return get_qt_mouse_event(QMouseEvent::MouseMove, pos, Qt::NoButton);
}

QMouseEvent motion_absolute_to_qt_event(motion_absolute_event const& event)
{
    return get_qt_mouse_motion_absolute_event(event.pos);
}

}
