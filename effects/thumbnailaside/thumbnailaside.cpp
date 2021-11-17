/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2007 Lubos Lunak <l.lunak@kde.org>
Copyright (C) 2007 Christian Nitschkowski <christian.nitschkowski@kdemail.net>

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

#include "thumbnailaside.h"
// KConfigSkeleton
#include "thumbnailasideconfig.h"

#include <KGlobalAccel>
#include <KLocalizedString>

#include <QAction>
#include <QMatrix4x4>

namespace KWin
{

ThumbnailAsideEffect::ThumbnailAsideEffect()
{
    initConfig<ThumbnailAsideConfig>();
    QAction* a = new QAction(this);
    a->setObjectName(QStringLiteral("ToggleCurrentThumbnail"));
    a->setText(i18n("Toggle Thumbnail for Current Window"));
    KGlobalAccel::self()->setDefaultShortcut(a, QList<QKeySequence>() << Qt::META + Qt::CTRL + Qt::Key_T);
    KGlobalAccel::self()->setShortcut(a, QList<QKeySequence>() << Qt::META + Qt::CTRL + Qt::Key_T);
    effects->registerGlobalShortcut(Qt::META + Qt::CTRL + Qt::Key_T, a);
    connect(a, &QAction::triggered, this, &ThumbnailAsideEffect::toggleCurrentThumbnail);

    connect(effects, &EffectsHandler::windowClosed, this, &ThumbnailAsideEffect::slotWindowClosed);
    connect(effects, &EffectsHandler::windowGeometryShapeChanged, this, &ThumbnailAsideEffect::slotWindowGeometryShapeChanged);
    connect(effects, &EffectsHandler::windowDamaged, this, &ThumbnailAsideEffect::slotWindowDamaged);
    connect(effects, &EffectsHandler::screenLockingChanged, this, &ThumbnailAsideEffect::repaintAll);
    reconfigure(ReconfigureAll);
}

void ThumbnailAsideEffect::reconfigure(ReconfigureFlags)
{
    ThumbnailAsideConfig::self()->read();
    maxwidth = ThumbnailAsideConfig::maxWidth();
    spacing = ThumbnailAsideConfig::spacing();
    opacity = ThumbnailAsideConfig::opacity()/100.0;
    screen = ThumbnailAsideConfig::screen(); // Xinerama screen TODO add gui option
    arrange();
}

void ThumbnailAsideEffect::paintScreen(int mask, const QRegion &region, ScreenPaintData& data)
{
    painted = QRegion();
    effects->paintScreen(mask, region, data);

    const QMatrix4x4 projectionMatrix = data.projectionMatrix();
    for (auto const& d : qAsConst(windows)) {
        if (painted.intersects(d.rect)) {
            WindowPaintData data(d.window, projectionMatrix);
            data.multiplyOpacity(opacity);
            QRect region;
            setPositionTransformations(data, region, d.window, d.rect, Qt::KeepAspectRatio);
            effects->drawWindow(d.window, PAINT_WINDOW_OPAQUE | PAINT_WINDOW_TRANSLUCENT | PAINT_WINDOW_TRANSFORMED | PAINT_WINDOW_LANCZOS,
                                region, data);
        }
    }
}

void ThumbnailAsideEffect::paintWindow(EffectWindow *w, int mask, QRegion region, WindowPaintData &data)
{
    effects->paintWindow(w, mask, region, data);
    painted |= region;
}

void ThumbnailAsideEffect::slotWindowDamaged(EffectWindow* w, QRegion const&)
{
    for (auto const& d : qAsConst(windows)) {
        if (d.window == w)
            effects->addRepaint(d.rect);
    }
}

void ThumbnailAsideEffect::slotWindowGeometryShapeChanged(EffectWindow* w, const QRect& old)
{
    for (auto const& d : qAsConst(windows)) {
        if (d.window == w) {
            if (w->size() == old.size())
                effects->addRepaint(d.rect);
            else
                arrange();
            return;
        }
    }
}

void ThumbnailAsideEffect::slotWindowClosed(EffectWindow* w)
{
    removeThumbnail(w);
}

void ThumbnailAsideEffect::toggleCurrentThumbnail()
{
    EffectWindow* active = effects->activeWindow();
    if (active == nullptr)
        return;
    if (windows.contains(active))
        removeThumbnail(active);
    else
        addThumbnail(active);
}

void ThumbnailAsideEffect::addThumbnail(EffectWindow* w)
{
    repaintAll(); // repaint old areas
    Data d;
    d.window = w;
    d.index = windows.count();
    windows[ w ] = d;
    arrange();
}

void ThumbnailAsideEffect::removeThumbnail(EffectWindow* w)
{
    if (!windows.contains(w))
        return;
    repaintAll(); // repaint old areas
    int index = windows[ w ].index;
    windows.remove(w);
    for (QHash< EffectWindow*, Data >::Iterator it = windows.begin();
            it != windows.end();
            ++it) {
        Data& d = *it;
        if (d.index > index)
            --d.index;
    }
    arrange();
}

void ThumbnailAsideEffect::arrange()
{
    if (windows.size() == 0)
        return;
    int height = 0;
    QVector< int > pos(windows.size());
    int mwidth = 0;
    for (auto const& d : qAsConst(windows)) {
        height += d.window->height();
        mwidth = qMax(mwidth, d.window->width());
        pos[ d.index ] = d.window->height();
    }
    QRect area = effects->clientArea(MaximizeArea, screen, effects->currentDesktop());
    double scale = area.height() / double(height);
    scale = qMin(scale, maxwidth / double(mwidth));    // don't be wider than maxwidth pixels
    int add = 0;
    for (int i = 0;
            i < windows.size();
            ++i) {
        pos[ i ] = int(pos[ i ] * scale);
        pos[ i ] += spacing + add; // compute offset of each item
        add = pos[ i ];
    }
    for (QHash< EffectWindow*, Data >::Iterator it = windows.begin();
            it != windows.end();
            ++it) {
        Data& d = *it;
        int width = int(d.window->width() * scale);
        d.rect = QRect(area.right() - width, area.bottom() - pos[ d.index ], width, int(d.window->height() * scale));
    }
    repaintAll();
}

void ThumbnailAsideEffect::repaintAll()
{
    for (auto const& d : qAsConst(windows)) {
        effects->addRepaint(d.rect);
    }
}

bool ThumbnailAsideEffect::isActive() const
{
    return !windows.isEmpty() && !effects->isScreenLocked();
}

} // namespace

