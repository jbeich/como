/*
    SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#ifndef KWIN_MAIN_X11_H
#define KWIN_MAIN_X11_H
#include "main.h"

#include "base/x11/platform.h"
#include "render/backend/x11/platform.h"

#include <QApplication>
#include <memory>

namespace KWin
{

namespace win::x11
{
template<typename Space>
class xcb_event_filter;
}

class KWinSelectionOwner;

class ApplicationX11 : public QApplication
{
    Q_OBJECT
public:
    ApplicationX11(int &argc, char **argv);
    ~ApplicationX11() override;

    void start();

    void setReplace(bool replace);
    void notifyKSplash();

    static int crashes;

private Q_SLOTS:
    void lostSelection();

private:
    void crashChecking();
    void setupCrashHandler();

    static void crashHandler(int signal);

    base::x11::platform base;

    QScopedPointer<KWinSelectionOwner> owner;
    std::unique_ptr<win::x11::xcb_event_filter<base::x11::platform::space_t>> event_filter;
    bool m_replace;
};

}

#endif
