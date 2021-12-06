/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "wlr_includes.h"

#include "base/backend/wlroots.h"
#include "base/utils.h"
#include "platform.h"

#include <Wrapland/Server/drm_lease_v1.h>
#include <variant>

struct gbm_device;

namespace KWin
{

namespace render::backend::wlroots
{

class egl_backend;
class output;

class KWIN_EXPORT backend : public Platform
{
    Q_OBJECT
public:
    base::backend::wlroots& base;
    egl_backend* egl{nullptr};
    render::compositor* compositor{nullptr};

    QVector<output*> all_outputs;
    QVector<output*> enabled_outputs;

#if HAVE_WLR_OUTPUT_INIT_RENDER
    wlr_renderer* renderer{nullptr};
    wlr_allocator* allocator{nullptr};
#endif

    explicit backend(base::backend::wlroots& base);
    ~backend() override;

    gl::backend* createOpenGLBackend(render::compositor* compositor) override;
    void createEffectsHandler(render::compositor* compositor, render::scene* scene) override;

    void init();

    Outputs outputs() const override;
    Outputs enabledOutputs() const override;

    void enableOutput(output* output, bool enable);

    QVector<CompositingType> supportedCompositors() const override;

    QString supportInformation() const override;

protected:
    clockid_t clockId() const override;

private:
    void init_drm_leasing();
    void process_drm_leased(Wrapland::Server::drm_lease_v1* lease);

    clockid_t m_clockId;
    base::event_receiver<backend> new_output;
};

}
}
