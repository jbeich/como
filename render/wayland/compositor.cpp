/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "compositor.h"

#include "output.h"
#include "presentation.h"
#include "utils.h"

#include "base/platform.h"
#include "base/wayland/output.h"
#include "render/cursor.h"
#include "render/gl/scene.h"
#include "render/platform.h"
#include "render/qpainter/scene.h"
#include "render/scene.h"
#include "wayland_server.h"
#include "win/scene.h"
#include "workspace.h"

#include "wayland_logging.h"

namespace KWin::render::wayland
{

void compositor::addRepaint(QRegion const& region)
{
    if (locked) {
        return;
    }
    for (auto& [key, output] : outputs) {
        output->add_repaint(region);
    }
}

void compositor::check_idle()
{
    for (auto& [key, output] : outputs) {
        if (!output->idle) {
            return;
        }
    }
    scene()->idle();
}

compositor::compositor(render::platform& platform)
    : render::compositor(platform)
    , presentation(new render::wayland::presentation(this))
{
    if (!presentation->init_clock(platform.clockId())) {
        qCCritical(KWIN_WL) << "Presentation clock failed. Exit.";
        qApp->quit();
    }

    // For now we use the software cursor as our wlroots backend does not support yet a hardware
    // cursor.
    software_cursor->set_enabled(true);

    connect(kwinApp(),
            &Application::x11ConnectionAboutToBeDestroyed,
            this,
            &compositor::destroyCompositorSelection);

    for (auto output : platform.base.get_outputs()) {
        auto wl_out = static_cast<base::wayland::output*>(output);
        outputs.emplace(wl_out, new render::wayland::output(wl_out, this));
    }

    connect(&platform.base, &base::platform::output_added, this, [this](auto output) {
        auto wl_out = static_cast<base::wayland::output*>(output);
        outputs.emplace(wl_out, new render::wayland::output(wl_out, this));
    });

    connect(&platform.base, &base::platform::output_removed, this, [this](auto output) {
        for (auto it = outputs.begin(); it != outputs.end(); ++it) {
            if (it->first == output) {
                outputs.erase(it);
                break;
            }
        }
        if (auto workspace = Workspace::self()) {
            for (auto& win : workspace->windows()) {
                remove_all(win->repaint_outputs, output);
            }
        }
    });

    connect(workspace(), &Workspace::destroyed, this, [this] {
        for (auto& [key, output] : outputs) {
            output->delay_timer.stop();
        }
    });

    start();
}

compositor::~compositor() = default;

void compositor::schedule_repaint(Toplevel* window)
{
    if (locked) {
        return;
    }

    for (auto& [base, output] : outputs) {
        if (!win::visible_rect(window).intersected(base->geometry()).isEmpty()) {
            output->set_delay_timer();
        }
    }
}

void compositor::schedule_frame_callback(Toplevel* window)
{
    if (locked) {
        return;
    }

    auto max_out = static_cast<base::wayland::output*>(max_coverage_output(window));
    if (!max_out) {
        return;
    }

    outputs[max_out]->request_frame(window);
}

void compositor::toggleCompositing()
{
    // For the shortcut. Not possible on Wayland because we always composite.
}

bool compositor::is_locked() const
{
    return locked > 0;
}

void compositor::lock()
{
    locked++;
}

void compositor::unlock()
{
    assert(locked > 0);
    locked--;

    if (!locked) {
        addRepaintFull();
    }
}

void compositor::start()
{
    if (!render::compositor::setupStart()) {
        // Internal setup failed, abort.
        return;
    }

    if (Workspace::self()) {
        startupWithWorkspace();
    } else {
        connect(kwinApp(), &Application::workspaceCreated, this, &compositor::startupWithWorkspace);
    }
}

render::scene* compositor::create_scene(QVector<CompositingType> const& support)
{
    for (auto type : support) {
        if (type == OpenGLCompositing) {
            qCDebug(KWIN_WL) << "Creating OpenGL scene.";
            return gl::create_scene(this);
        }
        if (type == QPainterCompositing) {
            qCDebug(KWIN_WL) << "Creating QPainter scene.";
            return qpainter::create_scene();
        }
    }
    return nullptr;
}

std::deque<Toplevel*> compositor::performCompositing()
{
    for (auto& [output, render_output] : outputs) {
        render_output->run();
    }

    return std::deque<Toplevel*>();
}

}
