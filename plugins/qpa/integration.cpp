/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2015 Martin Gräßlin <mgraesslin@kde.org>
Copyright (C) 2019 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

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
#include "integration.h"

#include "base/app_singleton.h"
#include "base/platform.h"
#include "base/singleton_interface.h"
#include "backingstore.h"
#include "offscreensurface.h"
#include "render/singleton_interface.h"
#include "screen.h"
#include "sharingplatformcontext.h"
#include "window.h"
#include "../../main.h"

#include <QCoreApplication>
#include <QtConcurrentRun>

#include <qpa/qplatformnativeinterface.h>
#include <qpa/qplatformwindow.h>
#include <qpa/qwindowsysteminterface.h>

#include <QtEventDispatcherSupport/private/qunixeventdispatcher_qpa_p.h>
#include <QtFontDatabaseSupport/private/qgenericunixfontdatabase_p.h>
#include <QtThemeSupport/private/qgenericunixthemes_p.h>

namespace KWin
{

namespace QPA
{

Integration::Integration()
    : QObject()
    , QPlatformIntegration()
    , m_fontDb(new QGenericUnixFontDatabase())
    , m_nativeInterface(new QPlatformNativeInterface())
    , m_services(new QGenericUnixServices())
{
}

Integration::~Integration()
{
    for (QPlatformScreen *platformScreen : qAsConst(m_screens)) {
        QWindowSystemInterface::handleScreenRemoved(platformScreen);
    }
}

QVector<Screen*> Integration::screens() const
{
    return m_screens;
}

bool Integration::hasCapability(Capability cap) const
{
    switch (cap) {
    case ThreadedPixmaps:
        return true;
    case OpenGL:
        return true;
    case ThreadedOpenGL:
        return false;
    case BufferQueueingOpenGL:
        return false;
    case MultipleWindows:
    case NonFullScreenWindows:
        return true;
    case RasterGLSurface:
        return false;
    default:
        return QPlatformIntegration::hasCapability(cap);
    }
}

void Integration::initialize()
{
    assert(base::singleton_interface::app_singleton);
    connect(base::singleton_interface::app_singleton,
            &base::app_singleton::platform_created,
            this,
            [this] {
                assert(base::singleton_interface::platform);
                QObject::connect(base::singleton_interface::platform,
                                 &base::platform::topology_changed,
                                 this,
                                 &Integration::initScreens);
                initScreens();
            });

    QPlatformIntegration::initialize();
    auto dummyScreen = new Screen(nullptr, this);
    QWindowSystemInterface::handleScreenAdded(dummyScreen);
    m_screens << dummyScreen;
}

QAbstractEventDispatcher *Integration::createEventDispatcher() const
{
    return new QUnixEventDispatcherQPA;
}

QPlatformBackingStore *Integration::createPlatformBackingStore(QWindow *window) const
{
    return new BackingStore(window);
}

QPlatformWindow *Integration::createPlatformWindow(QWindow *window) const
{
    return new Window(window);
}

QPlatformOffscreenSurface *Integration::createPlatformOffscreenSurface(QOffscreenSurface *surface) const
{
    return new OffscreenSurface(surface);
}

QPlatformFontDatabase *Integration::fontDatabase() const
{
    return m_fontDb.get();
}

QPlatformTheme *Integration::createPlatformTheme(const QString &name) const
{
    return QGenericUnixTheme::createUnixTheme(name);
}

QStringList Integration::themeNames() const
{
    if (qEnvironmentVariableIsSet("KDE_FULL_SESSION")) {
        return QStringList({QStringLiteral("kde")});
    }
    return QStringList({QLatin1String(QGenericUnixTheme::name)});
}

QPlatformOpenGLContext* Integration::createPlatformOpenGLContext(QOpenGLContext* context) const
{
    assert(render::singleton_interface::supports_surfaceless_context);
    if (render::singleton_interface::supports_surfaceless_context()) {
        return new SharingPlatformContext(context);
    }
    return nullptr;
}

void Integration::initScreens()
{
    auto const outputs = base::singleton_interface::platform->get_outputs();
    QVector<Screen*> newScreens;

    newScreens.reserve(std::max<size_t>(outputs.size(), 1));

    for (auto output : outputs) {
        auto screen = new Screen(output, this);
        QWindowSystemInterface::handleScreenAdded(screen);
        newScreens << screen;
    }

    if (newScreens.isEmpty()) {
        auto dummyScreen = new Screen(nullptr, this);
        QWindowSystemInterface::handleScreenAdded(dummyScreen);
        newScreens << dummyScreen;
    }

    while (!m_screens.isEmpty()) {
        QWindowSystemInterface::handleScreenRemoved(m_screens.takeLast());
    }

    m_screens = newScreens;
}

QPlatformNativeInterface *Integration::nativeInterface() const
{
    return m_nativeInterface.data();
}

QPlatformServices *Integration::services() const
{
    return m_services.data();
}

}
}
