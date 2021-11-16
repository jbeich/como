/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "types.h"

#include <kwin_export.h>

#include <Wrapland/Client/appmenu.h>
#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/event_queue.h>
#include <Wrapland/Client/idleinhibit.h>
#include <Wrapland/Client/layer_shell_v1.h>
#include <Wrapland/Client/output.h>
#include <Wrapland/Client/plasmashell.h>
#include <Wrapland/Client/plasmawindowmanagement.h>
#include <Wrapland/Client/pointerconstraints.h>
#include <Wrapland/Client/registry.h>
#include <Wrapland/Client/seat.h>
#include <Wrapland/Client/shadow.h>
#include <Wrapland/Client/shm_pool.h>
#include <Wrapland/Client/subcompositor.h>
#include <Wrapland/Client/xdg_activation_v1.h>
#include <Wrapland/Client/xdg_shell.h>
#include <Wrapland/Client/xdgdecoration.h>

#include <memory>
#include <vector>

namespace KWin::Test
{

class KWIN_EXPORT client
{
public:
    Wrapland::Client::ConnectionThread* connection{nullptr};
    std::unique_ptr<QThread> thread;
    std::unique_ptr<Wrapland::Client::EventQueue> queue;
    std::unique_ptr<Wrapland::Client::Registry> registry;

    struct {
        std::unique_ptr<Wrapland::Client::Compositor> compositor;
        std::unique_ptr<Wrapland::Client::LayerShellV1> layer_shell;
        std::unique_ptr<Wrapland::Client::SubCompositor> subcompositor;
        std::unique_ptr<Wrapland::Client::ShadowManager> shadow_manager;
        std::unique_ptr<Wrapland::Client::XdgShell> xdg_shell;
        std::unique_ptr<Wrapland::Client::ShmPool> shm;
        std::unique_ptr<Wrapland::Client::Seat> seat;
        std::unique_ptr<Wrapland::Client::PlasmaShell> plasma_shell;
        std::unique_ptr<Wrapland::Client::PlasmaWindowManagement> window_management;
        std::unique_ptr<Wrapland::Client::PointerConstraints> pointer_constraints;
        std::vector<std::unique_ptr<Wrapland::Client::Output>> outputs;
        std::unique_ptr<Wrapland::Client::IdleInhibitManager> idle_inhibit;
        std::unique_ptr<Wrapland::Client::AppMenuManager> app_menu;
        std::unique_ptr<Wrapland::Client::XdgActivationV1> xdg_activation;
        std::unique_ptr<Wrapland::Client::XdgDecorationManager> xdg_decoration;
    } interfaces;

    client() = default;
    explicit client(global_selection globals);
    client(client const&) = delete;
    client& operator=(client const&) = delete;
    client(client&& other) noexcept;
    client& operator=(client&& other) noexcept;
    ~client();

private:
    QMetaObject::Connection output_announced;
    std::vector<QMetaObject::Connection> output_removals;

    void connect_outputs();
    QMetaObject::Connection output_removal_connection(Wrapland::Client::Output* output);
    void cleanup();
};

}
