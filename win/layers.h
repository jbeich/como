/*
    SPDX-FileCopyrightText: ...

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "focuschain.h"
#include "meta.h"
#include "transient.h"
#include "util.h"

#include "options.h"
#include "virtualdesktops.h"
#include "workspace.h"

#include "win/x11/group.h"
#include "win/x11/window.h"

namespace KWin
{
class Toplevel;

namespace win
{

template<typename Space, typename Window>
void lower_window(Space* space, Window* window)
{
    assert(window->control);

    auto do_lower = [space](auto win) {
        win->control->cancel_auto_raise();

        StackingUpdatesBlocker blocker(space);

        remove_all(space->unconstrained_stacking_order, win);
        space->unconstrained_stacking_order.push_front(win);

        return blocker;
    };
    auto cleanup = [space](auto win) {
        if (win == space->most_recently_raised) {
            space->most_recently_raised = nullptr;
        }
    };

    auto blocker = do_lower(window);

    if (window->isTransient() && window->group()) {
        // Lower also all windows in the group, in reversed stacking order.
        auto const wins = space->ensureStackingOrder(window->group()->members());

        for (auto it = wins.crbegin(); it != wins.crend(); it++) {
            auto gwin = *it;
            if (gwin == static_cast<Toplevel*>(window)) {
                continue;
            }

            assert(gwin->control);
            do_lower(gwin);
            cleanup(gwin);
        }
    }

    cleanup(window);
}

template<typename Space, typename Window>
void raise_window(Space* space, Window* window)
{
    if (!window) {
        return;
    }

    auto prepare = [space](auto window) {
        assert(window->control);
        window->control->cancel_auto_raise();
        return StackingUpdatesBlocker(space);
    };
    auto do_raise = [space](auto window) {
        remove_all(space->unconstrained_stacking_order, window);
        space->unconstrained_stacking_order.push_back(window);

        if (!is_special_window(window)) {
            space->most_recently_raised = static_cast<Toplevel*>(window);
        }
    };

    auto blocker = prepare(window);

    if (window->isTransient()) {
        // Also raise all leads.
        std::vector<Toplevel*> leads;

        for (auto lead : window->transient()->leads()) {
            while (lead) {
                if (!contains(leads, lead)) {
                    leads.push_back(lead);
                }
                lead = lead->transient()->lead();
            }
        }

        auto stacked_leads = space->ensureStackingOrder(leads);

        for (auto lead : stacked_leads) {
            if (!lead->control) {
                // Might be without control, at least on X11 this can happen (latte-dock settings).
                continue;
            }
            auto blocker = prepare(lead);
            do_raise(lead);
        }
    }

    do_raise(window);
}

template<typename Space, typename Window>
void raise_or_lower_client(Space* space, Window* window)
{
    if (!window) {
        return;
    }

    Toplevel* topmost = nullptr;

    if (space->most_recently_raised && contains(space->stacking_order, space->most_recently_raised)
        && space->most_recently_raised->isShown() && window->isOnCurrentDesktop()) {
        topmost = space->most_recently_raised;
    } else {
        topmost = space->topClientOnDesktop(
            window->isOnAllDesktops() ? VirtualDesktopManager::self()->current()
                                      : window->desktop(),
            options->isSeparateScreenFocus() ? window->screen() : -1);
    }

    if (window == topmost) {
        lower_window(space, window);
    } else {
        raise_window(space, window);
    }
}

template<typename Space, typename Window>
void restack(Space* space, Window* window, Toplevel* under, bool force = false)
{
    assert(contains(space->unconstrained_stacking_order, under));

    if (!force && !belong_to_same_client(under, window)) {
        // put in the stacking order below _all_ windows belonging to the active application
        for (auto it = space->unconstrained_stacking_order.crbegin();
             it != space->unconstrained_stacking_order.crend();
             it++) {
            auto other = *it;
            if (other->control && other->layer() == window->layer()
                && belong_to_same_client(under, other)) {
                under = (window == other) ? nullptr : other;
                break;
            }
        }
    }
    if (under) {
        remove_all(space->unconstrained_stacking_order, window);
        auto it = find(space->unconstrained_stacking_order, under);
        space->unconstrained_stacking_order.insert(it, window);
    }

    assert(contains(space->unconstrained_stacking_order, window));
    FocusChain::self()->moveAfterClient(window, under);
    space->updateStackingOrder();
}

template<typename Space, typename Win>
void restack_client_under_active(Space* space, Win* window)
{
    if (!space->active_client || space->active_client == window
        || space->active_client->layer() != window->layer()) {
        raise_window(space, window);
        return;
    }
    restack(space, window, space->active_client);
}

}
}
