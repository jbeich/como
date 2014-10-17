/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2013 Martin Gräßlin <mgraesslin@kde.org>

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
// own
#include "wayland_backend.h"
// KWin
#include "cursor.h"
#include "input.h"
#include "main.h"
#include <KWayland/Client/buffer.h>
#include <KWayland/Client/compositor.h>
#include <KWayland/Client/connection_thread.h>
#include <KWayland/Client/event_queue.h>
#include <KWayland/Client/fullscreen_shell.h>
#include <KWayland/Client/keyboard.h>
#include <KWayland/Client/output.h>
#include <KWayland/Client/pointer.h>
#include <KWayland/Client/region.h>
#include <KWayland/Client/registry.h>
#include <KWayland/Client/seat.h>
#include <KWayland/Client/shell.h>
#include <KWayland/Client/shm_pool.h>
#include <KWayland/Client/subcompositor.h>
#include <KWayland/Client/subsurface.h>
#include <KWayland/Client/surface.h>
// Qt
#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QDebug>
#include <QThread>
// xcb
#include <xcb/xtest.h>
#include <xcb/xfixes.h>
// Wayland
#include <wayland-client-protocol.h>
#include <wayland-cursor.h>

namespace KWin
{
namespace Wayland
{

using namespace KWayland::Client;

CursorData::CursorData()
    : m_valid(init())
{
}

CursorData::~CursorData()
{
}

bool CursorData::init()
{
    QScopedPointer<xcb_xfixes_get_cursor_image_reply_t, QScopedPointerPodDeleter> cursor(
        xcb_xfixes_get_cursor_image_reply(connection(),
                                          xcb_xfixes_get_cursor_image_unchecked(connection()),
                                          NULL));
    if (cursor.isNull()) {
        return false;
    }

    QImage cursorImage((uchar *) xcb_xfixes_get_cursor_image_cursor_image(cursor.data()), cursor->width, cursor->height,
                       QImage::Format_ARGB32_Premultiplied);
    if (cursorImage.isNull()) {
        return false;
    }
    // the backend for the cursorImage is destroyed once the xcb cursor goes out of scope
    // because of that we create a copy
    m_cursor = cursorImage.copy();

    m_hotSpot = QPoint(cursor->xhot, cursor->yhot);
    return true;
}

X11CursorTracker::X11CursorTracker(WaylandBackend *backend, QObject* parent)
    : QObject(parent)
    , m_backend(backend)
    , m_lastX11Cursor(0)
{
    Cursor::self()->startCursorTracking();
    connect(Cursor::self(), SIGNAL(cursorChanged(uint32_t)), SLOT(cursorChanged(uint32_t)));
}

X11CursorTracker::~X11CursorTracker()
{
    if (Cursor::self()) {
        // Cursor might have been destroyed before Wayland backend gets destroyed
        Cursor::self()->stopCursorTracking();
    }
}

void X11CursorTracker::cursorChanged(uint32_t serial)
{
    if (m_lastX11Cursor == serial) {
        // not changed;
        return;
    }
    m_lastX11Cursor = serial;
    QHash<uint32_t, CursorData>::iterator it = m_cursors.find(serial);
    if (it != m_cursors.end()) {
        installCursor(it.value());
        return;
    }
    ShmPool *pool = m_backend->shmPool();
    if (!pool->isValid()) {
        return;
    }
    CursorData cursor;
    if (cursor.isValid()) {
        // TODO: discard unused cursors after some time?
        m_cursors.insert(serial, cursor);
    }
    installCursor(cursor);
}

void X11CursorTracker::installCursor(const CursorData& cursor)
{
    const QImage &cursorImage = cursor.cursor();
    auto buffer = m_backend->shmPool()->createBuffer(cursorImage);
    if (!buffer) {
        return;
    }
    emit cursorImageChanged(buffer, cursorImage.size(), cursor.hotSpot());
}

void X11CursorTracker::resetCursor()
{
    QHash<uint32_t, CursorData>::iterator it = m_cursors.find(m_lastX11Cursor);
    if (it != m_cursors.end()) {
        installCursor(it.value());
    }
}

WaylandSeat::WaylandSeat(wl_seat *seat, WaylandBackend *backend)
    : QObject(NULL)
    , m_seat(new Seat(this))
    , m_pointer(NULL)
    , m_keyboard(NULL)
    , m_cursor(NULL)
    , m_theme(new WaylandCursorTheme(backend, this))
    , m_enteredSerial(0)
    , m_backend(backend)
    , m_installCursor(false)
{
    m_seat->setup(seat);
    connect(m_seat, &Seat::hasKeyboardChanged, this,
        [this](bool hasKeyboard) {
            if (hasKeyboard) {
                m_keyboard = m_seat->createKeyboard(this);
                connect(m_keyboard, &Keyboard::keyChanged, this,
                    [this](quint32 key, Keyboard::KeyState state, quint32 time) {
                        auto toState = [state] {
                            switch (state) {
                            case Keyboard::KeyState::Pressed:
                                return InputRedirection::KeyboardKeyPressed;
                            case Keyboard::KeyState::Released:
                                return InputRedirection::KeyboardKeyReleased;
                            }
                            abort();
                        };
                        input()->processKeyboardKey(key, toState(), time);
                    }
                );
                connect(m_keyboard, &Keyboard::modifiersChanged, this,
                    [this](quint32 depressed, quint32 latched, quint32 locked, quint32 group) {
                        input()->processKeyboardModifiers(depressed, latched, locked, group);
                    }
                );
                connect(m_keyboard, &Keyboard::keymapChanged, this,
                    [this](int fd, quint32 size) {
                        input()->processKeymapChange(fd, size);
                    }
                );
            } else {
                destroyKeyboard();
            }
        }
    );
    connect(m_seat, &Seat::hasPointerChanged, this,
        [this](bool hasPointer) {
            if (hasPointer && !m_pointer) {
                m_pointer = m_seat->createPointer(this);
                connect(m_pointer, &Pointer::entered, this,
                    [this](quint32 serial) {
                        m_enteredSerial = serial;
                        if (!m_installCursor) {
                            // explicitly hide cursor
                            wl_pointer_set_cursor(*m_pointer, m_enteredSerial, nullptr, 0, 0);
                        }
                    }
                );
                connect(m_pointer, &Pointer::motion, this,
                    [this](const QPointF &relativeToSurface, quint32 time) {
                        input()->processPointerMotion(relativeToSurface.toPoint(), time);
                    }
                );
                connect(m_pointer, &Pointer::buttonStateChanged, this,
                    [this](quint32 serial, quint32 time, quint32 button, Pointer::ButtonState state) {
                        Q_UNUSED(serial)
                        auto toState = [state] {
                            switch (state) {
                            case Pointer::ButtonState::Pressed:
                                return InputRedirection::PointerButtonPressed;
                            case Pointer::ButtonState::Released:
                                return InputRedirection::PointerButtonReleased;
                            }
                            abort();
                        };
                        input()->processPointerButton(button, toState(), time);
                    }
                );
                connect(m_pointer, &Pointer::axisChanged, this,
                    [this](quint32 time, Pointer::Axis axis, qreal delta) {
                        auto toAxis = [axis] {
                            switch (axis) {
                            case Pointer::Axis::Horizontal:
                                return InputRedirection::PointerAxisHorizontal;
                            case Pointer::Axis::Vertical:
                                return InputRedirection::PointerAxisVertical;
                            }
                            abort();
                        };
                        input()->processPointerAxis(toAxis(), delta, time);
                    }
                );
                connect(m_backend->cursorTracker(), &X11CursorTracker::cursorImageChanged, this,
                    [this](Buffer::Ptr image, const QSize &size, const QPoint &hotspot) {
                        if (image.isNull()) {
                            return;
                        }
                        installCursorImage(image.toStrongRef()->buffer(), size, hotspot);
                    }
                );
            } else {
                destroyPointer();
            }
        }
    );
}

WaylandSeat::~WaylandSeat()
{
    destroyPointer();
    destroyKeyboard();
}

void WaylandSeat::destroyPointer()
{
    delete m_pointer;
    m_pointer = nullptr;
}

void WaylandSeat::destroyKeyboard()
{
    delete m_keyboard;
    m_keyboard = nullptr;
}

void WaylandSeat::installCursorImage(wl_buffer *image, const QSize &size, const QPoint &hotSpot)
{
    if (!m_installCursor) {
        return;
    }
    if (!m_pointer || !m_pointer->isValid()) {
        return;
    }
    if (!m_cursor) {
        m_cursor = m_backend->compositor()->createSurface(this);
    }
    if (!m_cursor || !m_cursor->isValid()) {
        return;
    }
    wl_pointer_set_cursor(*m_pointer, m_enteredSerial, *m_cursor, hotSpot.x(), hotSpot.y());
    m_cursor->attachBuffer(image);
    m_cursor->damage(QRect(QPoint(0,0), size));
    m_cursor->commit(Surface::CommitFlag::None);
}

void WaylandSeat::installCursorImage(Qt::CursorShape shape)
{
    wl_cursor_image *image = m_theme->get(shape);
    if (!image) {
        return;
    }
    installCursorImage(wl_cursor_image_get_buffer(image),
                       QSize(image->width, image->height),
                       QPoint(image->hotspot_x, image->hotspot_y));
}

void WaylandSeat::setInstallCursor(bool install)
{
    // TODO: remove, add?
    m_installCursor = install;
}

WaylandCursorTheme::WaylandCursorTheme(WaylandBackend *backend, QObject *parent)
    : QObject(parent)
    , m_theme(nullptr)
    , m_backend(backend)
{
}

WaylandCursorTheme::~WaylandCursorTheme()
{
    destroyTheme();
}

void WaylandCursorTheme::loadTheme()
{
    Cursor *c = Cursor::self();
    if (!m_theme) {
        // so far the theme had not been created, this means we need to start tracking theme changes
        connect(c, &Cursor::themeChanged, this, &WaylandCursorTheme::loadTheme);
    } else {
        destroyTheme();
    }
    m_theme = wl_cursor_theme_load(c->themeName().toUtf8().constData(),
                                   c->themeSize(), m_backend->shmPool()->shm());
}

void WaylandCursorTheme::destroyTheme()
{
    if (!m_theme) {
        return;
    }
    wl_cursor_theme_destroy(m_theme);
    m_theme = nullptr;
}

wl_cursor_image *WaylandCursorTheme::get(Qt::CursorShape shape)
{
    if (!m_theme) {
        loadTheme();
    }
    wl_cursor *c = wl_cursor_theme_get_cursor(m_theme, Cursor::self()->cursorName(shape).constData());
    if (!c || c->image_count <= 0) {
        return nullptr;
    }
    return c->images[0];
}

WaylandCursor::WaylandCursor(Surface *parentSurface, WaylandBackend *backend)
    : QObject(backend)
    , m_backend(backend)
    , m_theme(new WaylandCursorTheme(backend, this))
{
    auto surface = backend->compositor()->createSurface(this);
    m_subSurface = backend->subCompositor()->createSubSurface(QPointer<Surface>(surface), QPointer<Surface>(parentSurface), this);

    connect(m_backend->cursorTracker(), &X11CursorTracker::cursorImageChanged, this,
        [this](Buffer::Ptr image, const QSize &size, const QPoint &hotspot) {
            if (image.isNull()) {
                return;
            }
            setCursorImage(image.toStrongRef()->buffer(), size, hotspot);
        }
    );
    connect(Cursor::self(), &Cursor::posChanged, this,
        [this](const QPoint &pos) {
            m_subSurface->setPosition(pos - m_hotSpot);
            QPointer<Surface> parent = m_subSurface->parentSurface();
            if (parent.isNull()) {
                return;
            }
            parent->commit(Surface::CommitFlag::None);
        }
    );

    // install a default cursor image:
    setCursorImage(Qt::ArrowCursor);
}

void WaylandCursor::setHotSpot(const QPoint &pos)
{
    if (m_hotSpot == pos) {
        return;
    }
    m_hotSpot = pos;
    emit hotSpotChanged(m_hotSpot);
}

void WaylandCursor::setCursorImage(wl_buffer *image, const QSize &size, const QPoint &hotspot)
{
    QPointer<Surface> cursor = m_subSurface->surface();
    if (cursor.isNull()) {
        return;
    }
    cursor->attachBuffer(image);
    cursor->damage(QRect(QPoint(0,0), size));
    cursor->setInputRegion(m_backend->compositor()->createRegion(QRegion()).get());
    cursor->commit(Surface::CommitFlag::None);
    setHotSpot(hotspot);
    m_subSurface->setPosition(Cursor::pos() - m_hotSpot);
    QPointer<Surface> parent = m_subSurface->parentSurface();
    if (parent.isNull()) {
        return;
    }
    parent->commit(Surface::CommitFlag::None);
}

void WaylandCursor::setCursorImage(Qt::CursorShape shape)
{
    wl_cursor_image *image = m_theme->get(shape);
    if (!image) {
        return;
    }
    setCursorImage(wl_cursor_image_get_buffer(image),
                   QSize(image->width, image->height),
                   QPoint(image->hotspot_x, image->hotspot_y));
}

WaylandBackend *WaylandBackend::s_self = 0;
WaylandBackend *WaylandBackend::create(QObject *parent)
{
    Q_ASSERT(!s_self);
    s_self = new WaylandBackend(parent);
    return s_self;
}

WaylandBackend::WaylandBackend(QObject *parent)
    : QObject(parent)
    , m_display(nullptr)
    , m_eventQueue(new EventQueue(this))
    , m_registry(new Registry(this))
    , m_compositor(new Compositor(this))
    , m_shell(new Shell(this))
    , m_surface(nullptr)
    , m_shellSurface(NULL)
    , m_seat()
    , m_shm(new ShmPool(this))
    , m_cursorTracker()
    , m_connectionThreadObject(nullptr)
    , m_connectionThread(nullptr)
    , m_fullscreenShell(new FullscreenShell(this))
    , m_subCompositor(new SubCompositor(this))
    , m_cursor(nullptr)
{
    connect(this, &WaylandBackend::shellSurfaceSizeChanged, this, &WaylandBackend::checkBackendReady);
    connect(m_registry, &Registry::compositorAnnounced, this,
        [this](quint32 name) {
            m_compositor->setup(m_registry->bindCompositor(name, 1));
        }
    );
    connect(m_registry, &Registry::shellAnnounced, this,
        [this](quint32 name) {
            m_shell->setup(m_registry->bindShell(name, 1));
        }
    );
    connect(m_registry, &Registry::outputAnnounced, this,
        [this](quint32 name) {
            Output *output = new Output(this);
            output->setup(m_registry->bindOutput(name, 2));
            m_outputs.append(output);
            connect(output, &Output::changed, this, &WaylandBackend::outputsChanged);
        }
    );
    connect(m_registry, &Registry::seatAnnounced, this,
        [this](quint32 name) {
            if (Application::usesLibinput()) {
                return;
            }
            m_seat.reset(new WaylandSeat(m_registry->bindSeat(name, 2), this));
        }
    );
    connect(m_registry, &Registry::shmAnnounced, this,
        [this](quint32 name) {
            m_shm->setup(m_registry->bindShm(name, 1));
        }
    );
    connect(m_registry, &Registry::fullscreenShellAnnounced, this,
        [this](quint32 name, quint32 version) {
            m_fullscreenShell->setup(m_registry->bindFullscreenShell(name, version));
        }
    );
    connect(m_registry, &Registry::subCompositorAnnounced, this,
        [this](quint32 name, quint32 version) {
            m_subCompositor->setup(m_registry->bindSubCompositor(name, version));
        }
    );
    connect(m_registry, &Registry::interfacesAnnounced, this, &WaylandBackend::createSurface);
    initConnection();
}

WaylandBackend::~WaylandBackend()
{
    destroyOutputs();
    if (m_shellSurface) {
        m_shellSurface->release();
    }
    m_fullscreenShell->release();
    if (m_surface) {
        m_surface->release();
    }
    m_shell->release();
    m_compositor->release();
    m_registry->release();
    m_seat.reset();
    m_shm->release();
    m_eventQueue->release();

    m_connectionThreadObject->deleteLater();
    m_connectionThread->quit();
    m_connectionThread->wait();

    qDebug() << "Destroyed Wayland display";
    s_self = NULL;
}

void WaylandBackend::destroyOutputs()
{
    qDeleteAll(m_outputs);
    m_outputs.clear();
}

void WaylandBackend::initConnection()
{
    m_connectionThreadObject = new ConnectionThread(nullptr);
    connect(m_connectionThreadObject, &ConnectionThread::connected, this,
        [this]() {
            // create the event queue for the main gui thread
            m_display = m_connectionThreadObject->display();
            m_eventQueue->setup(m_connectionThreadObject);
            m_registry->setEventQueue(m_eventQueue);
            // setup registry
            m_registry->create(m_display);
            m_registry->setup();
            m_cursorTracker.reset(new X11CursorTracker(this, this));
        },
        Qt::QueuedConnection);
    connect(m_connectionThreadObject, &ConnectionThread::connectionDied, this,
        [this]() {
            emit systemCompositorDied();
            m_cursorTracker.reset();
            m_seat.reset();
            m_shm->destroy();
            destroyOutputs();
            if (m_shellSurface) {
                m_shellSurface->destroy();
                delete m_shellSurface;
                m_shellSurface = nullptr;
            }
            m_fullscreenShell->destroy();
            if (m_surface) {
                m_surface->destroy();
                delete m_surface;
                m_surface = nullptr;
            }
            if (m_shell) {
                m_shell->destroy();
            }
            m_compositor->destroy();
            m_registry->destroy();
            m_eventQueue->destroy();
            if (m_display) {
                m_display = nullptr;
            }
        },
        Qt::QueuedConnection);
    connect(m_connectionThreadObject, &ConnectionThread::failed, this, &WaylandBackend::connectionFailed, Qt::QueuedConnection);

    m_connectionThread = new QThread(this);
    m_connectionThreadObject->moveToThread(m_connectionThread);
    m_connectionThread->start();

    m_connectionThreadObject->initConnection();
}

void WaylandBackend::installCursorImage(Qt::CursorShape shape)
{
    if (!m_seat.isNull() && m_seat->isInstallCursor()) {
        m_seat->installCursorImage(shape);
    } else if (m_cursor) {
        m_cursor->setCursorImage(shape);
    }
}

void WaylandBackend::createSurface()
{
    m_surface = m_compositor->createSurface(this);
    if (!m_surface || !m_surface->isValid()) {
        qCritical() << "Creating Wayland Surface failed";
        return;
    }
    if (m_subCompositor->isValid()) {
        // we have a sub compositor - let's use it for mouse cursor
        m_cursor = new WaylandCursor(m_surface, this);
    } else {
        // no sub-compositor - use the seat for setting the cursor image
        if (m_seat) {
            m_seat->setInstallCursor(true);
        }
    }
    if (m_fullscreenShell->isValid()) {
        Output *o = m_outputs.first();
        m_fullscreenShell->present(m_surface, o);
        if (o->pixelSize().isValid()) {
            emit shellSurfaceSizeChanged(o->pixelSize());
        }
        connect(o, &Output::changed, this,
            [this, o]() {
                if (o->pixelSize().isValid()) {
                    emit shellSurfaceSizeChanged(o->pixelSize());
                }
            }
        );
    } else if (m_shell->isValid()) {
        // map the surface as fullscreen
        m_shellSurface = m_shell->createSurface(m_surface, this);
        m_shellSurface->setFullscreen();
        connect(m_shellSurface, &ShellSurface::pinged, m_cursorTracker.data(), &X11CursorTracker::resetCursor);
        connect(m_shellSurface, &ShellSurface::sizeChanged, this, &WaylandBackend::shellSurfaceSizeChanged);
    }
}

QSize WaylandBackend::shellSurfaceSize() const
{
    if (m_shellSurface) {
        return m_shellSurface->size();
    }
    if (m_fullscreenShell->isValid()) {
        return m_outputs.first()->pixelSize();
    }
    return QSize();
}

void WaylandBackend::checkBackendReady()
{
    if (!shellSurfaceSize().isValid()) {
        return;
    }
    disconnect(this, &WaylandBackend::shellSurfaceSizeChanged, this, &WaylandBackend::checkBackendReady);
    emit backendReady();
}

}

} // KWin
