/*
    SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "deco_renderer.h"

#include "win/deco/client_impl.h"
#include "win/x11/window.h"

#include "kwinglobals.h"

#include <KDecoration2/DecoratedClient>
#include <KDecoration2/Decoration>
#include <QTimer>

namespace KWin::render::backend::x11
{

deco_renderer::deco_renderer(win::deco::client_impl* client)
    : renderer(client)
    , m_scheduleTimer(new QTimer(this))
    , m_gc(XCB_NONE)
{
    // delay any rendering to end of event cycle to catch multiple updates per cycle
    m_scheduleTimer->setSingleShot(true);
    m_scheduleTimer->setInterval(0);
    connect(m_scheduleTimer, &QTimer::timeout, this, &deco_renderer::render);
    connect(this,
            &renderer::renderScheduled,
            m_scheduleTimer,
            static_cast<void (QTimer::*)()>(&QTimer::start));
}

deco_renderer::~deco_renderer()
{
    if (m_gc != XCB_NONE) {
        xcb_free_gc(connection(), m_gc);
    }
}

void deco_renderer::reparent()
{
    if (m_scheduleTimer->isActive()) {
        m_scheduleTimer->stop();
    }
    disconnect(m_scheduleTimer, &QTimer::timeout, this, &deco_renderer::render);
    disconnect(this,
               &renderer::renderScheduled,
               m_scheduleTimer,
               static_cast<void (QTimer::*)()>(&QTimer::start));
    renderer::reparent();
}

void deco_renderer::render()
{
    if (!client()) {
        return;
    }
    const QRegion scheduled = getScheduled();
    if (scheduled.isEmpty()) {
        return;
    }
    xcb_connection_t* c = connection();
    if (m_gc == XCB_NONE) {
        m_gc = xcb_generate_id(c);
        xcb_create_gc(c, m_gc, client()->client()->frameId(), 0, nullptr);
    }

    QRect left, top, right, bottom;
    client()->client()->layoutDecorationRects(left, top, right, bottom);

    const QRect geometry = scheduled.boundingRect();
    left = left.intersected(geometry);
    top = top.intersected(geometry);
    right = right.intersected(geometry);
    bottom = bottom.intersected(geometry);

    auto renderPart = [this, c](const QRect& geo) {
        if (!geo.isValid()) {
            return;
        }
        QImage image = renderToImage(geo);
        xcb_put_image(c,
                      XCB_IMAGE_FORMAT_Z_PIXMAP,
                      client()->client()->frameId(),
                      m_gc,
                      image.width(),
                      image.height(),
                      geo.x(),
                      geo.y(),
                      0,
                      client()->client()->depth(),
                      image.sizeInBytes(),
                      image.constBits());
    };
    renderPart(left);
    renderPart(top);
    renderPart(right);
    renderPart(bottom);

    xcb_flush(c);
    resetImageSizesDirty();
}

}
