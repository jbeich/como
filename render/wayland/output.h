/*
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwin_export.h>

#include <QBasicTimer>
#include <QRegion>
#include <QTimer>

#include <chrono>
#include <deque>
#include <map>

namespace Wrapland
{
namespace Server
{
class Surface;
}
}

namespace KWin
{
class AbstractWaylandOutput;
class Toplevel;

namespace render::wayland
{
class compositor;

class KWIN_EXPORT output : public QObject
{
    int index;
    wayland::compositor* compositor{nullptr};

    ulong msc{0};

    // Compositing delay.
    std::chrono::nanoseconds delay;
    std::chrono::nanoseconds last_paint_durations[2]{};
    int paint_periods{0};

    QRegion repaints_region;

    bool prepare_repaint(Toplevel* win);
    bool prepare_run(QRegion& repaints, std::deque<Toplevel*>& windows);
    void retard_next_run();

    void update_paint_periods(std::chrono::nanoseconds duration);
    std::chrono::nanoseconds refresh_length() const;

    void timerEvent(QTimerEvent* event) override;

public:
    AbstractWaylandOutput* base;
    std::map<uint32_t, Wrapland::Server::Surface*> assigned_surfaces;

    bool idle{true};
    bool swap_pending{false};
    QBasicTimer delay_timer;
    QBasicTimer frame_timer;

    output(AbstractWaylandOutput* base, wayland::compositor* compositor);

    void add_repaint(QRegion const& region);
    void set_delay_timer();
    void request_frame(Toplevel* window);

    std::deque<Toplevel*> run();
    void dry_run();

    void swapped(unsigned int sec, unsigned int usec);
};

}
}
