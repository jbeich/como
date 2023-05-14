/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "compositor.h"
#include "effects.h"

#include "render/platform.h"

#include <memory>

namespace KWin::render::wayland
{

template<typename Base>
class platform : public render::platform<Base>
{
public:
    using type = platform<Base>;
    using base_t = Base;
    using compositor_t = typename wayland::compositor<platform<Base>>;
    using scene_t = typename compositor_t::scene_t;
    using space_t = typename Base::space_t;

    using window_t = typename scene_t::window_t;
    using buffer_t = buffer_win_integration<typename scene_t::buffer_t>;

    platform(Base& base)
        : render::platform<Base>(base)
    {
    }

    bool requiresCompositing() const override
    {
        return true;
    }

    bool compositingPossible() const override
    {
        return true;
    }

    QString compositingNotPossibleReason() const override
    {
        return {};
    }

    bool openGLCompositingIsBroken() const override
    {
        return false;
    }

    void createOpenGLSafePoint(opengl_safe_point /*safePoint*/) override
    {
    }

    render::outline_visual* create_non_composited_outline(render::outline* /*outline*/)
    {
        // Not possible on Wayland.
        return nullptr;
    }

    virtual gl::backend<gl::scene<type>, type>* get_opengl_backend(compositor_t& compositor) = 0;
    virtual qpainter::backend<qpainter::scene<type>>* get_qpainter_backend(compositor_t& compositor)
        = 0;

    void invertScreen() override
    {
        assert(compositor->effects);
        compositor->effects->invert_screen();
    }

    std::unique_ptr<compositor_t> compositor;

    int output_index{0};
};

}
