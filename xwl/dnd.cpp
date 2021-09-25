/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright 2019 Roman Gilg <subdiff@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#include "dnd.h"

#include "drag_wl.h"
#include "drag_x.h"
#include "selection_source.h"

#include "atoms.h"
#include "toplevel.h"
#include "wayland_server.h"
#include "workspace.h"
#include "xwayland.h"

#include <Wrapland/Client/compositor.h>
#include <Wrapland/Client/surface.h>

#include <Wrapland/Server/compositor.h>
#include <Wrapland/Server/data_device.h>
#include <Wrapland/Server/drag_pool.h>
#include <Wrapland/Server/pointer_pool.h>
#include <Wrapland/Server/seat.h>
#include <Wrapland/Server/surface.h>

#include <QMouseEvent>

#include <xcb/xcb.h>

namespace KWin
{
namespace Xwl
{

template<>
void do_handle_xfixes_notify(Dnd* sel, xcb_xfixes_selection_notify_event_t* event)
{
    if (qobject_cast<XToWlDrag*>(sel->m_currentDrag)) {
        // X drag is in progress, rogue X client took over the selection.
        return;
    }
    if (sel->m_currentDrag) {
        // Wl drag is in progress - don't overwrite by rogue X client,
        // get it back instead!
        own_selection(sel, true);
        return;
    }
    create_x11_source(sel, nullptr);
    auto const seat = waylandServer()->seat();
    auto originSurface = seat->pointers().get_focus().surface;
    if (!originSurface) {
        return;
    }
    if (originSurface->client() != waylandServer()->xWaylandConnection()) {
        // focused surface client is not Xwayland - do not allow drag to start
        // TODO: can we make this stronger (window id comparison)?
        return;
    }
    if (!seat->pointers().is_button_pressed(Qt::LeftButton)) {
        // we only allow drags to be started on (left) pointer button being
        // pressed for now
        return;
    }
    create_x11_source(sel, event);
    if (!sel->data.x11_source) {
        return;
    }
    sel->data.srv_device->updateProxy(originSurface);
    sel->m_currentDrag = new XToWlDrag(sel->data.x11_source, sel);
}

template<>
bool handle_client_message(Dnd* sel, xcb_client_message_event_t* event)
{
    for (auto& drag : sel->m_oldDrags) {
        if (drag->handleClientMessage(event)) {
            return true;
        }
    }
    if (sel->m_currentDrag && sel->m_currentDrag->handleClientMessage(event)) {
        return true;
    }
    return false;
}

template<>
void handle_x11_offer_change([[maybe_unused]] Dnd* sel,
                             [[maybe_unused]] QStringList const& added,
                             [[maybe_unused]] QStringList const& removed)
{
    // Handled internally.
}

// version of DnD support in X
const static uint32_t s_version = 5;
uint32_t Dnd::version()
{
    return s_version;
}

Dnd::Dnd(xcb_atom_t atom, srv_data_device* srv_dev, clt_data_device* clt_dev, x11_data const& x11)
{
    data = create_selection_data(atom, srv_dev, clt_dev, x11);

    // TODO(romangg): for window size get current screen size and connect to changes.
    register_x11_selection(this, QSize(8192, 8192));
    register_xfixes(this);

    xcb_connection_t* xcbConn = kwinApp()->x11Connection();
    xcb_change_property(xcbConn,
                        XCB_PROP_MODE_REPLACE,
                        data.window,
                        atoms->xdnd_aware,
                        XCB_ATOM_ATOM,
                        32,
                        1,
                        &s_version);
    xcb_flush(xcbConn);

    QObject::connect(waylandServer()->seat(),
                     &Wrapland::Server::Seat::dragStarted,
                     data.qobject.get(),
                     [this]() { startDrag(); });
    QObject::connect(waylandServer()->seat(),
                     &Wrapland::Server::Seat::dragEnded,
                     data.qobject.get(),
                     [this]() { endDrag(); });

    const auto* comp = waylandServer()->compositor();
    m_surface = waylandServer()->internalCompositor()->createSurface(data.qobject.get());
    m_surface->setInputRegion(nullptr);
    m_surface->commit(Wrapland::Client::Surface::CommitFlag::None);
    auto* dc = new QMetaObject::Connection();
    *dc = QObject::connect(
        comp,
        &Wrapland::Server::Compositor::surfaceCreated,
        data.qobject.get(),
        [this, dc](Wrapland::Server::Surface* si) {
            // TODO: how to make sure that it is the iface of m_surface?
            if (m_surfaceIface || si->client() != waylandServer()->internalConnection()) {
                return;
            }
            QObject::disconnect(*dc);
            delete dc;
            m_surfaceIface = si;
            QObject::connect(
                workspace(), &Workspace::clientActivated, data.qobject.get(), [this](Toplevel* ac) {
                    if (!ac || !ac->inherits("KWin::X11Client")) {
                        return;
                    }
                    auto* surface = ac->surface();
                    if (surface) {
                        surface->setDataProxy(m_surfaceIface);
                    } else {
                        auto* dc = new QMetaObject::Connection();
                        *dc = QObject::connect(
                            ac, &Toplevel::surfaceChanged, data.qobject.get(), [this, ac, dc] {
                                if (auto* surface = ac->surface()) {
                                    surface->setDataProxy(m_surfaceIface);
                                    QObject::disconnect(*dc);
                                    delete dc;
                                }
                            });
                    }
                });
        });
    waylandServer()->dispatch();
}

DragEventReply Dnd::dragMoveFilter(Toplevel* target, const QPoint& pos)
{
    // This filter only is used when a drag is in process.
    Q_ASSERT(m_currentDrag);
    return m_currentDrag->moveFilter(target, pos);
}

void Dnd::startDrag()
{
    auto srv_dev = waylandServer()->seat()->drags().get_source().dev;
    if (srv_dev == data.srv_device) {
        // X to Wl drag, started by us, is in progress.
        Q_ASSERT(m_currentDrag);
        return;
    }

    // There can only ever be one Wl native drag at the same time.
    Q_ASSERT(!m_currentDrag);

    // New Wl to X drag, init drag and Wl source.
    m_currentDrag = new WlToXDrag(this);
    auto source = new WlSource<Wrapland::Server::DataDevice, Wrapland::Server::DataSource>(srv_dev);
    source->setSourceIface(srv_dev->dragSource());
    set_wl_source(this, source);
    own_selection(this, true);
}

void Dnd::endDrag()
{
    Q_ASSERT(m_currentDrag);

    if (m_currentDrag->end()) {
        delete m_currentDrag;
    } else {
        QObject::connect(m_currentDrag, &Drag::finish, data.qobject.get(), [this](auto drag) {
            clearOldDrag(drag);
        });
        m_oldDrags << m_currentDrag;
    }
    m_currentDrag = nullptr;
}

void Dnd::clearOldDrag(Drag* drag)
{
    m_oldDrags.removeOne(drag);
    delete drag;
}

} // namespace Xwl
} // namespace KWin
