/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "wlr_includes.h"

#include "base/logging.h"
#include "wayland_logging.h"

#include <QImage>
#include <memory>

struct wlr_renderer;

namespace KWin::render::backend::wlroots
{

template<typename Output>
class qpainter_output
{
public:
    qpainter_output(Output& output, wlr_renderer* renderer)
        : output{output}
        , renderer{renderer}
    {
    }

    qpainter_output(qpainter_output const&) = delete;
    qpainter_output& operator=(qpainter_output const&) = delete;
    qpainter_output(qpainter_output&& other) noexcept = default;
    qpainter_output& operator=(qpainter_output&& other) noexcept = default;

    void begin_render()
    {
        auto native_out = static_cast<base::backend::wlroots::output&>(output.base).native;

        auto const size = output.base.geometry().size();

        wlr_output_attach_render(native_out, nullptr);
        wlr_renderer_begin(renderer, size.width(), size.height());

        if (!buffer || size != buffer->size()) {
            auto current_pixman_image = wlr_pixman_renderer_get_current_image(renderer);
            auto pixman_format = pixman_image_get_format(current_pixman_image);
            buffer = std::make_unique<QImage>(size, pixman_to_qt_image_format(pixman_format));
            if (buffer->isNull()) {
                // TODO(romangg): handle oom
                buffer.reset();
                return;
            }
            buffer->fill(Qt::gray);
        }
    }

    void present(QRegion const& /*damage*/)
    {
        auto buffer_bits = buffer->constBits();
        auto pixman_data = pixman_image_get_data(wlr_pixman_renderer_get_current_image(renderer));

        memcpy(pixman_data, buffer_bits, buffer->width() * buffer->height() * 4);

        auto& base = static_cast<base::backend::wlroots::output&>(output.base);
        output.swap_pending = true;

        if (!base.native->enabled) {
            wlr_output_enable(base.native, true);
        }

        if (!wlr_output_test(base.native)) {
            qCWarning(KWIN_CORE) << "Atomic output test failed on present.";
            wlr_output_rollback(base.native);
            return;
        }
        if (!wlr_output_commit(base.native)) {
            qCWarning(KWIN_CORE) << "Atomic output commit failed on present.";
            return;
        }
    }

    Output& output;
    wlr_renderer* renderer;

    std::unique_ptr<QImage> buffer;

private:
    QImage::Format pixman_to_qt_image_format(pixman_format_code_t format)
    {
        switch (format) {
        case PIXMAN_a8r8g8b8:
            return QImage::Format_ARGB32_Premultiplied;
        case PIXMAN_x8r8g8b8:
            return QImage::Format_RGB32;
        case PIXMAN_r8g8b8a8:
            return QImage::Format_RGBA8888_Premultiplied;
        case PIXMAN_r8g8b8x8:
            return QImage::Format_RGBX8888;
        case PIXMAN_r8g8b8:
            return QImage::Format_RGB888;
        case PIXMAN_b8g8r8:
            return QImage::Format_BGR888;
        default:
            return QImage::Format_RGBA8888;
        }
    }
};

}
