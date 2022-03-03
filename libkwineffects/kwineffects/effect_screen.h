/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwineffects_export.h>

#include <QObject>

namespace KWin
{

/**
 * The EffectScreen class represents a screen used by/for Effect classes.
 */
class KWINEFFECTS_EXPORT EffectScreen : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QRect geometry READ geometry NOTIFY geometryChanged)
    Q_PROPERTY(qreal devicePixelRatio READ devicePixelRatio NOTIFY devicePixelRatioChanged)
    Q_PROPERTY(QString name READ name CONSTANT)

public:
    explicit EffectScreen(QObject* parent = nullptr);

    /**
     * Returns the name of the screen, e.g. "DP-1".
     */
    virtual QString name() const = 0;

    /**
     * Returns the screen's ratio between physical pixels and device-independent pixels.
     */
    virtual qreal devicePixelRatio() const = 0;

    /**
     * Returns the screen's geometry in the device-independent pixels.
     */
    virtual QRect geometry() const = 0;

Q_SIGNALS:
    /**
     * Notifies that the display will be dimmed in @p time ms.
     */
    void aboutToTurnOff(std::chrono::milliseconds time);

    /**
     * Notifies that the output has been turned on and the wake can be decorated.
     */
    void wakeUp();

    /**
     * This signal is emitted when the geometry of this screen changes.
     */
    void geometryChanged();

    /**
     * This signal is emitted when the device pixel ratio of this screen changes.
     */
    void devicePixelRatioChanged();
};

}
