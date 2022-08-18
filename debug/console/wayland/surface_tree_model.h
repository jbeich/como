/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "win/space_qobject.h"
#include "win/x11/stacking.h"

#include <QAbstractItemModel>
#include <Wrapland/Server/buffer.h>
#include <Wrapland/Server/client.h>
#include <Wrapland/Server/subcompositor.h>
#include <Wrapland/Server/surface.h>

namespace KWin::debug
{

template<typename Space>
class surface_tree_model : public QAbstractItemModel
{
public:
    using window_t = typename Space::window_t;

    explicit surface_tree_model(Space& space, QObject* parent = nullptr)
        : QAbstractItemModel(parent)
        , space{space}
    {
        // TODO: it would be nice to not have to reset the model on each change
        auto reset = [this] {
            beginResetModel();
            endResetModel();
        };

        auto const unmangeds = win::x11::get_unmanageds(space);
        for (auto u : unmangeds) {
            if (!u->surface) {
                continue;
            }
            QObject::connect(
                u->surface, &Wrapland::Server::Surface::subsurfaceTreeChanged, this, reset);
        }
        for (auto c : space.windows) {
            if (!c->control || !c->surface) {
                continue;
            }
            QObject::connect(
                c->surface, &Wrapland::Server::Surface::subsurfaceTreeChanged, this, reset);
        }
        QObject::connect(space.qobject.get(),
                         &win::space_qobject::wayland_window_added,
                         this,
                         [this, reset](auto win_id) {
                             auto win = this->space.windows_map.at(win_id);
                             QObject::connect(win->surface,
                                              &Wrapland::Server::Surface::subsurfaceTreeChanged,
                                              this,
                                              reset);
                             reset();
                         });
        QObject::connect(space.qobject.get(),
                         &win::space_qobject::clientAdded,
                         this,
                         [this, reset](auto win_id) {
                             auto win = this->space.windows_map.at(win_id);
                             if (win->surface) {
                                 QObject::connect(win->surface,
                                                  &Wrapland::Server::Surface::subsurfaceTreeChanged,
                                                  this,
                                                  reset);
                             }
                             reset();
                         });
        QObject::connect(space.qobject.get(), &win::space_qobject::clientRemoved, this, reset);
        QObject::connect(space.qobject.get(),
                         &win::space_qobject::unmanagedAdded,
                         this,
                         [this, reset](auto win_id) {
                             auto win = this->space.windows_map.at(win_id);
                             if (win->surface) {
                                 QObject::connect(win->surface,
                                                  &Wrapland::Server::Surface::subsurfaceTreeChanged,
                                                  this,
                                                  reset);
                             }
                             reset();
                         });
        QObject::connect(space.qobject.get(), &win::space_qobject::unmanagedRemoved, this, reset);
    }

    int columnCount(QModelIndex const& parent) const override
    {
        Q_UNUSED(parent)
        return 1;
    }

    QVariant data(QModelIndex const& index, int role) const override
    {
        if (!index.isValid()) {
            return QVariant();
        }
        if (auto surface = static_cast<Wrapland::Server::Surface*>(index.internalPointer())) {
            if (role == Qt::DisplayRole || role == Qt::ToolTipRole) {
                return QStringLiteral("%1 (%2)")
                    .arg(QString::fromStdString(surface->client()->executablePath()))
                    .arg(surface->client()->processId());
            } else if (role == Qt::DecorationRole) {
                if (auto buffer = surface->state().buffer) {
                    if (buffer->shmBuffer()) {
                        return buffer->shmImage()->createQImage().scaled(QSize(64, 64),
                                                                         Qt::KeepAspectRatio);
                    }
                }
            }
        }
        return QVariant();
    }

    QModelIndex index(int row, int column, QModelIndex const& parent) const override
    {
        if (column != 0) {
            // invalid column
            return QModelIndex();
        }

        auto row_u = static_cast<size_t>(row);

        if (parent.isValid()) {
            if (auto surface = static_cast<Wrapland::Server::Surface*>(parent.internalPointer())) {
                const auto& children = surface->state().children;
                if (row_u < children.size()) {
                    return createIndex(row_u, column, children.at(row_u)->surface());
                }
            }
            return QModelIndex();
        }

        // a window
        auto const& allClients = get_windows_with_control(space.windows);
        if (row_u < allClients.size()) {
            // references a client
            return createIndex(row_u, column, allClients.at(row_u)->surface);
        }

        int reference = allClients.size();
        const auto& unmanaged = win::x11::get_unmanageds(space);
        if (row_u < reference + unmanaged.size()) {
            return createIndex(row_u, column, unmanaged.at(row_u - reference)->surface);
        }
        reference += unmanaged.size();

        // not found
        return QModelIndex();
    }

    int rowCount(QModelIndex const& parent) const override
    {
        if (parent.isValid()) {
            if (auto surface = static_cast<Wrapland::Server::Surface*>(parent.internalPointer())) {
                const auto& children = surface->state().children;
                return children.size();
            }
            return 0;
        }

        // toplevel are all windows
        return get_windows_with_control(space.windows).size()
            + win::x11::get_unmanageds(space).size();
    }

    QModelIndex parent(QModelIndex const& child) const override
    {
        if (auto surface = static_cast<Wrapland::Server::Surface*>(child.internalPointer())) {
            const auto& subsurface = surface->subsurface();
            if (!subsurface) {
                // doesn't reference a subsurface, this is a top-level window
                return QModelIndex();
            }
            auto parent = subsurface->parentSurface();
            if (!parent) {
                // something is wrong
                return QModelIndex();
            }
            // is the parent a subsurface itself?
            if (parent->subsurface()) {
                auto grandParent = parent->subsurface()->parentSurface();
                if (!grandParent) {
                    // something is wrong
                    return QModelIndex();
                }
                const auto& children = grandParent->state().children;
                for (size_t row = 0; row < children.size(); row++) {
                    if (children.at(row) == parent->subsurface()) {
                        return createIndex(row, 0, parent);
                    }
                }
                return QModelIndex();
            }
            // not a subsurface, thus it's a true window
            size_t row = 0;
            const auto& allClients = get_windows_with_control(space.windows);
            for (; row < allClients.size(); row++) {
                if (allClients.at(row)->surface == parent) {
                    return createIndex(row, 0, parent);
                }
            }
            row = allClients.size();
            const auto& unmanaged = win::x11::get_unmanageds(space);
            for (size_t i = 0; i < unmanaged.size(); i++) {
                if (unmanaged.at(i)->surface == parent) {
                    return createIndex(row + i, 0, parent);
                }
            }
            row += unmanaged.size();
        }
        return QModelIndex();
    }

private:
    static std::vector<window_t*> get_windows_with_control(std::vector<window_t*>& windows)
    {
        std::vector<window_t*> with_control;
        for (auto win : windows) {
            if (win->control) {
                with_control.push_back(win);
            }
        }
        return with_control;
    }

    Space& space;
};

}
