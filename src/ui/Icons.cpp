// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Icons.h"

#include <QApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>

namespace {

class VectorIconEngine : public QIconEngine
{
public:
    explicit VectorIconEngine(Icons::Kind kind)
        : m_kind(kind)
    {
    }

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override
    {
        const QPalette palette = QApplication::palette();
        QColor ink = palette.color(mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active, QPalette::WindowText);
        const QColor accent = mode == QIcon::Disabled ? ink : QColor(196, 43, 28);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        // Repère 24 × 24 centré dans le rectangle demandé.
        const double side = std::min(rect.width(), rect.height());
        painter->translate(rect.center().x() + 0.5 - side / 2, rect.center().y() + 0.5 - side / 2);
        painter->scale(side / 24.0, side / 24.0);
        QPen pen(ink, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);

        switch (m_kind) {
        case Icons::Kind::Open: {
            QPainterPath folder;
            folder.moveTo(3, 7);
            folder.lineTo(3, 19);
            folder.lineTo(19, 19);
            folder.lineTo(21, 11);
            folder.lineTo(7, 11);
            folder.lineTo(5, 19);
            folder.moveTo(3, 7);
            folder.lineTo(3, 5);
            folder.lineTo(9, 5);
            folder.lineTo(11, 7);
            folder.lineTo(18, 7);
            folder.lineTo(18, 11);
            painter->drawPath(folder);
            break;
        }
        case Icons::Kind::Print:
            painter->drawRect(QRectF(7, 3, 10, 5));
            painter->drawRoundedRect(QRectF(3, 8, 18, 9), 2, 2);
            painter->setBrush(palette.color(QPalette::Base));
            painter->drawRect(QRectF(7, 13, 10, 8));
            painter->setPen(QPen(accent, 1.6, Qt::SolidLine, Qt::RoundCap));
            painter->drawLine(QPointF(9, 16), QPointF(15, 16));
            painter->drawLine(QPointF(9, 18.5), QPointF(13, 18.5));
            break;
        case Icons::Kind::Find:
            painter->drawEllipse(QPointF(10, 10), 6, 6);
            painter->setPen(QPen(accent, 2.6, Qt::SolidLine, Qt::RoundCap));
            painter->drawLine(QPointF(14.5, 14.5), QPointF(20.5, 20.5));
            break;
        case Icons::Kind::PreviousPage:
            painter->drawPolyline(QPolygonF({QPointF(6, 15), QPointF(12, 9), QPointF(18, 15)}));
            break;
        case Icons::Kind::NextPage:
            painter->drawPolyline(QPolygonF({QPointF(6, 9), QPointF(12, 15), QPointF(18, 9)}));
            break;
        case Icons::Kind::Sidebar:
            painter->drawRoundedRect(QRectF(3, 4, 18, 16), 2, 2);
            painter->drawLine(QPointF(9, 4), QPointF(9, 20));
            break;
        case Icons::Kind::ZoomIn:
        case Icons::Kind::ZoomOut:
            painter->drawEllipse(QPointF(10, 10), 6, 6);
            painter->drawLine(QPointF(14.5, 14.5), QPointF(20.5, 20.5));
            painter->drawLine(QPointF(7, 10), QPointF(13, 10));
            if (m_kind == Icons::Kind::ZoomIn)
                painter->drawLine(QPointF(10, 7), QPointF(10, 13));
            break;
        }
        painter->restore();
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        QPixmap pixmap(size * scale);
        pixmap.setDevicePixelRatio(scale);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        paint(&painter, QRect(QPoint(0, 0), size), mode, state);
        return pixmap;
    }

    QIconEngine *clone() const override { return new VectorIconEngine(m_kind); }

private:
    Icons::Kind m_kind;
};

} // namespace

QIcon Icons::make(Kind kind)
{
    return QIcon(new VectorIconEngine(kind));
}
