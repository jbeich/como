/*
    SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "keyboard_layout_switching.h"

#include "spies/keyboard_layout.h"
#include "toplevel.h"
#include "virtualdesktops.h"
#include "workspace.h"
#include "xkb.h"

#include "win/control.h"
#include "win/net.h"
#include "win/util.h"

namespace KWin
{

namespace input::keyboard_layout_switching
{

policy::policy(input::xkb* xkb, keyboard_layout_spy* layout, KConfigGroup const& config)
    : QObject(layout)
    , config(config)
    , xkb(xkb)
    , layout(layout)
{
    QObject::connect(layout, &keyboard_layout_spy::layoutsReconfigured, this, &policy::clear_cache);
    QObject::connect(
        layout, &keyboard_layout_spy::layoutChanged, this, &policy::handle_layout_change);
}

policy::~policy() = default;

void policy::set_layout(uint index)
{
    auto const previous_layout = xkb->currentLayout();
    xkb->switchToLayout(index);
    auto const current_layout = xkb->currentLayout();

    if (previous_layout != current_layout) {
        Q_EMIT layout->layoutChanged(current_layout);
    }
}

policy* policy::create(input::xkb* xkb,
                       keyboard_layout_spy* layout,
                       KConfigGroup const& config,
                       QString const& policy)
{
    if (policy.toLower() == QStringLiteral("desktop")) {
        return new virtual_desktop_policy(xkb, layout, config);
    }
    if (policy.toLower() == QStringLiteral("window")) {
        return new window_policy(xkb, layout);
    }
    if (policy.toLower() == QStringLiteral("winclass")) {
        return new application_policy(xkb, layout, config);
    }
    return new global_policy(xkb, layout, config);
}

char const policy::default_layout_entry_key_prefix[] = "LayoutDefault";

QString const policy::default_layout_entry_key() const
{
    return QLatin1String(default_layout_entry_key_prefix) % name() % QLatin1Char('_');
}

void policy::clear_layouts()
{
    auto const layout_entries = config.keyList().filter(default_layout_entry_key_prefix);
    for (const auto& entry : layout_entries) {
        config.deleteEntry(entry);
    }
}

QString const global_policy::default_layout_entry_key() const
{
    return QLatin1String(default_layout_entry_key_prefix) % name();
}

global_policy::global_policy(input::xkb* xkb,
                             keyboard_layout_spy* _layout,
                             KConfigGroup const& config)
    : policy(xkb, _layout, config)
{
    QObject::connect(workspace()->sessionManager(),
                     &SessionManager::prepareSessionSaveRequested,
                     this,
                     [this, xkb] {
                         clear_layouts();
                         if (auto const layout = xkb->currentLayout()) {
                             this->config.writeEntry(default_layout_entry_key(), layout);
                         }
                     });

    QObject::connect(
        workspace()->sessionManager(), &SessionManager::loadSessionRequested, this, [this, xkb] {
            if (xkb->numberOfLayouts() > 1) {
                set_layout(this->config.readEntry(default_layout_entry_key(), 0));
            }
        });
}

virtual_desktop_policy::virtual_desktop_policy(input::xkb* xkb,
                                               keyboard_layout_spy* layout,
                                               KConfigGroup const& config)
    : policy(xkb, layout, config)
{
    QObject::connect(VirtualDesktopManager::self(),
                     &VirtualDesktopManager::currentChanged,
                     this,
                     &virtual_desktop_policy::handle_desktop_change);

    QObject::connect(
        workspace()->sessionManager(), &SessionManager::prepareSessionSaveRequested, this, [this] {
            clear_layouts();

            for (auto i = layouts.constBegin(); i != layouts.constEnd(); ++i) {
                if (uint const layout = *i) {
                    this->config.writeEntry(
                        default_layout_entry_key()
                            % QLatin1String(QByteArray::number(i.key()->x11DesktopNumber())),
                        layout);
                }
            }
        });

    QObject::connect(
        workspace()->sessionManager(), &SessionManager::loadSessionRequested, this, [this, xkb] {
            if (xkb->numberOfLayouts() > 1) {
                auto const& desktops = VirtualDesktopManager::self()->desktops();

                for (KWin::VirtualDesktop* const desktop : desktops) {
                    uint const layout = this->config.readEntry(
                        default_layout_entry_key()
                            % QLatin1String(QByteArray::number(desktop->x11DesktopNumber())),
                        0u);

                    if (layout) {
                        layouts.insert(desktop, layout);
                        QObject::connect(desktop,
                                         &VirtualDesktop::aboutToBeDestroyed,
                                         this,
                                         [this, desktop] { layouts.remove(desktop); });
                    }
                }

                handle_desktop_change();
            }
        });
}

void virtual_desktop_policy::clear_cache()
{
    layouts.clear();
}

namespace
{
template<typename T, typename U>
quint32 getLayout(T const& layouts, U const& reference)
{
    auto it = layouts.constFind(reference);
    if (it == layouts.constEnd()) {
        return 0;
    } else {
        return it.value();
    }
}
}

void virtual_desktop_policy::handle_desktop_change()
{
    if (auto desktop = VirtualDesktopManager::self()->currentDesktop()) {
        set_layout(getLayout(layouts, desktop));
    }
}

void virtual_desktop_policy::handle_layout_change(uint index)
{
    auto desktop = VirtualDesktopManager::self()->currentDesktop();
    if (!desktop) {
        return;
    }

    auto it = layouts.find(desktop);

    if (it == layouts.end()) {
        layouts.insert(desktop, index);
        QObject::connect(desktop, &VirtualDesktop::aboutToBeDestroyed, this, [this, desktop] {
            layouts.remove(desktop);
        });
    } else {
        if (it.value() == index) {
            return;
        }
        it.value() = index;
    }
}

window_policy::window_policy(input::xkb* xkb, keyboard_layout_spy* layout)
    : policy(xkb, layout)
{
    QObject::connect(workspace(), &Workspace::clientActivated, this, [this](auto window) {
        if (!window) {
            return;
        }

        // Ignore some special types.
        if (win::is_desktop(window) || win::is_dock(window)) {
            return;
        }

        set_layout(getLayout(layouts, window));
    });
}

void window_policy::clear_cache()
{
    layouts.clear();
}

void window_policy::handle_layout_change(uint index)
{
    auto window = workspace()->activeClient();
    if (!window) {
        return;
    }

    // Ignore some special types.
    if (win::is_desktop(window) || win::is_dock(window)) {
        return;
    }

    auto it = layouts.find(window);

    if (it == layouts.end()) {
        layouts.insert(window, index);
        QObject::connect(
            window, &Toplevel::windowClosed, this, [this, window] { layouts.remove(window); });
    } else {
        if (it.value() == index) {
            return;
        }
        it.value() = index;
    }
}

application_policy::application_policy(input::xkb* xkb,
                                       keyboard_layout_spy* layout,
                                       KConfigGroup const& config)
    : policy(xkb, layout, config)
{
    QObject::connect(workspace(),
                     &Workspace::clientActivated,
                     this,
                     &application_policy::handle_client_activated);

    QObject::connect(
        workspace()->sessionManager(), &SessionManager::prepareSessionSaveRequested, this, [this] {
            clear_layouts();

            for (auto i = layouts.constBegin(); i != layouts.constEnd(); ++i) {
                if (auto const layout = *i) {
                    auto const desktopFileName = i.key()->control->desktop_file_name();
                    if (!desktopFileName.isEmpty()) {
                        this->config.writeEntry(
                            default_layout_entry_key() % QLatin1String(desktopFileName), layout);
                    }
                }
            }
        });

    QObject::connect(
        workspace()->sessionManager(), &SessionManager::loadSessionRequested, this, [this, xkb] {
            if (xkb->numberOfLayouts() > 1) {
                auto const keyPrefix = default_layout_entry_key();
                auto const keyList = this->config.keyList().filter(keyPrefix);
                for (auto const& key : keyList) {
                    restored_layouts.insert(key.midRef(keyPrefix.size()).toLatin1(),
                                            this->config.readEntry(key, 0));
                }
            }
            restored_layouts.squeeze();
        });
}

void application_policy::handle_client_activated(Toplevel* window)
{
    if (!window) {
        return;
    }

    // Ignore some special types.
    if (win::is_desktop(window) || win::is_dock(window)) {
        return;
    }

    auto it = layouts.constFind(window);
    if (it != layouts.constEnd()) {
        set_layout(it.value());
        return;
    }

    for (it = layouts.constBegin(); it != layouts.constEnd(); it++) {
        if (win::belong_to_same_client(window, it.key())) {
            auto const layout = it.value();
            set_layout(layout);
            handle_layout_change(layout);
            return;
        }
    }
    set_layout(restored_layouts.take(window->control->desktop_file_name()));
    if (auto const index = xkb->currentLayout()) {
        handle_layout_change(index);
    }
}

void application_policy::clear_cache()
{
    layouts.clear();
}

void application_policy::handle_layout_change(uint index)
{
    auto window = workspace()->activeClient();
    if (!window) {
        return;
    }

    // Ignore some special types.
    if (win::is_desktop(window) || win::is_dock(window)) {
        return;
    }

    auto it = layouts.find(window);

    if (it == layouts.end()) {
        layouts.insert(window, index);
        QObject::connect(
            window, &Toplevel::windowClosed, this, [this, window] { layouts.remove(window); });
    } else {
        if (it.value() == index) {
            return;
        }
        it.value() = index;
    }

    // Update all layouts for the application.
    for (it = layouts.begin(); it != layouts.end(); it++) {
        if (win::belong_to_same_client(it.key(), window)) {
            it.value() = index;
        }
    }
}

}
}
