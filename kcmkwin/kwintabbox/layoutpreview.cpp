/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2009, 2011 Martin Gräßlin <mgraesslin@kde.org>

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
#include "layoutpreview.h"

#include <QApplication>
#include <QDebug>
#include <QQmlEngine>
#include <QQmlContext>
#include <QScreen>
#include <QStandardPaths>
#include <KConfigGroup>
#include <KDesktopFile>
#include <KLocalizedString>
#include <KApplicationTrader>

namespace KWin
{
namespace TabBox
{

LayoutPreview::LayoutPreview(const QString &path, bool showDesktopThumbnail, QObject *parent)
    : QObject(parent)
    , m_item(nullptr)
{
    QQmlEngine *engine = new QQmlEngine(this);
    QQmlComponent *component = new QQmlComponent(engine, this);
    qmlRegisterType<WindowThumbnailItem>("org.kde.kwin", 2, 0, "ThumbnailItem");
    qmlRegisterType<SwitcherItem>("org.kde.kwin", 2, 0, "Switcher");
    qmlRegisterType<QAbstractItemModel>();
    component->loadUrl(QUrl::fromLocalFile(path));
    if (component->isError()) {
        qDebug() << component->errorString();
    }
    QObject *item = component->create();
    auto findSwitcher = [item]() -> SwitcherItem* {
        if (!item) {
            return nullptr;
        }
        if (SwitcherItem *i = qobject_cast<SwitcherItem*>(item)) {
            return i;
        } else if (QQuickWindow *w = qobject_cast<QQuickWindow*>(item)) {
            return w->contentItem()->findChild<SwitcherItem*>();
        }
        return item->findChild<SwitcherItem*>();
    };
    if (SwitcherItem *switcher = findSwitcher()) {
        m_item = switcher;
        static_cast<ExampleClientModel *>(switcher->model())->showDesktopThumbnail(showDesktopThumbnail);
        switcher->setVisible(true);
    }
    auto findWindow = [item]() -> QQuickWindow* {
        if (!item) {
            return nullptr;
        }
        if (QQuickWindow *w = qobject_cast<QQuickWindow*>(item)) {
            return w;
        }
        return item->findChild<QQuickWindow*>();
    };
    if (QQuickWindow *w = findWindow()) {
        w->setKeyboardGrabEnabled(true);
        w->setMouseGrabEnabled(true);
        w->installEventFilter(this);
    }
}

LayoutPreview::~LayoutPreview()
{
}

bool LayoutPreview::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape ||
                keyEvent->key() == Qt::Key_Return ||
                keyEvent->key() == Qt::Key_Enter ||
                keyEvent->key() == Qt::Key_Space) {
            object->deleteLater();
            deleteLater();
        }
        if (m_item && keyEvent->key() == Qt::Key_Tab) {
            m_item->incrementIndex();
        }
        if (m_item && keyEvent->key() == Qt::Key_Backtab) {
            m_item->decrementIndex();
        }
    } else if (event->type() == QEvent::MouseButtonPress) {
        if (QWindow *w = qobject_cast<QWindow*>(object)) {
            if (!w->geometry().contains(static_cast<QMouseEvent*>(event)->globalPos())) {
                object->deleteLater();
                deleteLater();
            }
        }
    }
    return QObject::eventFilter(object, event);
}

ExampleClientModel::ExampleClientModel (QObject* parent)
    : QAbstractListModel (parent)
{
    init();
}

ExampleClientModel::~ExampleClientModel()
{
}

void ExampleClientModel::init()
{
    if (const auto s = KApplicationTrader::preferredService(QStringLiteral("inode/directory"))) {
        m_thumbnails << ThumbnailInfo{ WindowThumbnailItem::Dolphin, s->name(), s->icon() };
    }
    if (const auto s = KApplicationTrader::preferredService(QStringLiteral("text/html"))) {
        m_thumbnails << ThumbnailInfo{ WindowThumbnailItem::Konqueror, s->name(), s->icon() };
    }
    if (const auto s = KApplicationTrader::preferredService(QStringLiteral("message/rfc822"))) {
        m_thumbnails << ThumbnailInfo{ WindowThumbnailItem::KMail, s->name(), s->icon() };
    }
    if (const auto s = KService::serviceByDesktopName(QStringLiteral("kdesystemsettings"))) {
        m_thumbnails << ThumbnailInfo{ WindowThumbnailItem::Systemsettings, s->name(), s->icon() };
    }
}

void ExampleClientModel::showDesktopThumbnail(bool showDesktop)
{
    const ThumbnailInfo desktopThumbnail = ThumbnailInfo { WindowThumbnailItem::Desktop, i18n("Show Desktop"), QStringLiteral("desktop") };
    const int desktopIndex = m_thumbnails.indexOf(desktopThumbnail);
    if (showDesktop == (desktopIndex >= 0)) {
        return;
    }

    Q_EMIT beginResetModel();
    if (showDesktop) {
        m_thumbnails << desktopThumbnail;
    } else {
        m_thumbnails.removeAt(desktopIndex);
    }
    Q_EMIT endResetModel();
}

QVariant ExampleClientModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount()) {
        return QVariant();
    }

    const ThumbnailInfo &item = m_thumbnails.at(index.row());

    switch (role) {
    case Qt::DisplayRole:
    case CaptionRole:
        return item.caption;
    case MinimizedRole:
        return false;
    case DesktopNameRole:
        return i18nc("An example Desktop Name", "Desktop 1");
    case IconRole:
        return item.icon;
    case WindowIdRole:
        return item.wId;
    }
    return QVariant();
}

QString ExampleClientModel::longestCaption() const
{
    QString caption;
    for (const auto &item : m_thumbnails) {
        if (item.caption.size() > caption.size()) {
            caption = item.caption;
        }
    }
    return caption;
}

int ExampleClientModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_thumbnails.size();
}

QHash<int, QByteArray> ExampleClientModel::roleNames() const
{
    return {
        { CaptionRole, QByteArrayLiteral("caption") },
        { MinimizedRole, QByteArrayLiteral("minimized") },
        { DesktopNameRole, QByteArrayLiteral("desktopName") },
        { IconRole, QByteArrayLiteral("icon") },
        { WindowIdRole, QByteArrayLiteral("windowId") },
    };
}

SwitcherItem::SwitcherItem(QObject *parent)
    : QObject(parent)
    , m_model(new ExampleClientModel(this))
    , m_item(nullptr)
    , m_currentIndex(0)
    , m_visible(false)
{
}

SwitcherItem::~SwitcherItem()
{
}

void SwitcherItem::setVisible(bool visible)
{
    if (m_visible == visible) {
        return;
    }
    m_visible = visible;
    Q_EMIT visibleChanged();
}

void SwitcherItem::setItem(QObject *item)
{
    m_item = item;
    Q_EMIT itemChanged();
}

void SwitcherItem::setCurrentIndex(int index)
{
    if (m_currentIndex == index) {
        return;
    }
    m_currentIndex = index;
    Q_EMIT currentIndexChanged(m_currentIndex);
}

QRect SwitcherItem::screenGeometry() const
{
    const QScreen *primaryScreen = qApp->primaryScreen();
    return primaryScreen->geometry();
}

void SwitcherItem::incrementIndex()
{
    setCurrentIndex((m_currentIndex + 1) % m_model->rowCount());
}

void SwitcherItem::decrementIndex()
{
    int index = m_currentIndex -1;
    if (index < 0) {
        index = m_model->rowCount() -1;
    }
    setCurrentIndex(index);
}

} // namespace KWin
} // namespace TabBox

