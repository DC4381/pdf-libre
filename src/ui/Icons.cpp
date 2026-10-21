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
        case Icons::Kind::Pages:
            painter->drawRoundedRect(QRectF(3, 4, 8, 11), 1.5, 1.5);
            painter->drawRoundedRect(QRectF(13, 4, 8, 11), 1.5, 1.5);
            painter->setPen(QPen(accent, 1.8, Qt::SolidLine, Qt::RoundCap));
            painter->drawLine(QPointF(5, 20), QPointF(19, 20));
            painter->drawLine(QPointF(16, 17), QPointF(19, 20));
            painter->drawLine(QPointF(16, 23), QPointF(19, 20));
            break;
        case Icons::Kind::Cursor: {
            QPainterPath arrow;
            arrow.moveTo(6, 3);
            arrow.lineTo(6, 19);
            arrow.lineTo(10, 15);
            arrow.lineTo(13, 21);
            arrow.lineTo(16, 20);
            arrow.lineTo(13, 14);
            arrow.lineTo(18, 14);
            arrow.closeSubpath();
            painter->setBrush(ink);
            painter->drawPath(arrow);
            break;
        }
        case Icons::Kind::Highlight:
            painter->setBrush(QColor(255, 220, 0));
            painter->setPen(Qt::NoPen);
            painter->drawRect(QRectF(4, 10, 16, 7));
            painter->setPen(pen);
            painter->setBrush(Qt::NoBrush);
            painter->drawLine(QPointF(5, 7), QPointF(19, 7));
            painter->drawLine(QPointF(5, 13.5), QPointF(19, 13.5));
            painter->drawLine(QPointF(5, 20), QPointF(13, 20));
            break;
        case Icons::Kind::Underline:
            painter->drawLine(QPointF(5, 7), QPointF(19, 7));
            painter->drawLine(QPointF(5, 12), QPointF(19, 12));
            painter->setPen(QPen(accent, 2.2));
            painter->drawLine(QPointF(5, 18), QPointF(19, 18));
            break;
        case Icons::Kind::StrikeOut:
            painter->drawLine(QPointF(5, 7), QPointF(19, 7));
            painter->drawLine(QPointF(5, 17), QPointF(19, 17));
            painter->setPen(QPen(accent, 2.2));
            painter->drawLine(QPointF(4, 12), QPointF(20, 12));
            break;
        case Icons::Kind::Note:
            painter->setBrush(QColor(255, 220, 0));
            painter->drawRoundedRect(QRectF(4, 4, 16, 13), 2, 2);
            painter->drawLine(QPointF(8, 17), QPointF(8, 21));
            painter->drawLine(QPointF(8, 21), QPointF(12, 17));
            break;
        case Icons::Kind::Text:
            painter->drawLine(QPointF(5, 5), QPointF(19, 5));
            painter->drawLine(QPointF(12, 5), QPointF(12, 20));
            painter->drawLine(QPointF(9, 20), QPointF(15, 20));
            break;
        case Icons::Kind::Pen: {
            QPainterPath path;
            path.moveTo(4, 19);
            path.cubicTo(7, 8, 11, 20, 14, 10);
            path.cubicTo(16, 5, 18, 14, 20, 8);
            painter->setPen(QPen(accent, 2, Qt::SolidLine, Qt::RoundCap));
            painter->drawPath(path);
            break;
        }
        case Icons::Kind::Line:
            painter->drawLine(QPointF(4, 19), QPointF(20, 5));
            break;
        case Icons::Kind::Arrow:
            painter->drawLine(QPointF(4, 19), QPointF(19, 5));
            painter->drawLine(QPointF(19, 5), QPointF(12, 6));
            painter->drawLine(QPointF(19, 5), QPointF(18, 12));
            break;
        case Icons::Kind::Rectangle:
            painter->drawRect(QRectF(4, 6, 16, 12));
            break;
        case Icons::Kind::Ellipse:
            painter->drawEllipse(QRectF(4, 6, 16, 12));
            break;
        case Icons::Kind::Stamp:
            painter->setPen(QPen(accent, 1.8));
            painter->drawRoundedRect(QRectF(3, 7, 18, 10), 3, 3);
            painter->drawLine(QPointF(7, 12), QPointF(17, 12));
            break;
        case Icons::Kind::Signature: {
            QPainterPath path;
            path.moveTo(3, 17);
            path.cubicTo(6, 4, 9, 20, 11, 11);
            path.cubicTo(12, 7, 13, 16, 15, 12);
            path.cubicTo(16, 9, 17, 15, 21, 11);
            painter->setPen(QPen(QColor(20, 30, 90), 1.8, Qt::SolidLine, Qt::RoundCap));
            painter->drawPath(path);
            painter->setPen(QPen(ink, 1, Qt::DashLine));
            painter->drawLine(QPointF(3, 20), QPointF(21, 20));
            break;
        }
        case Icons::Kind::Undo:
        case Icons::Kind::Redo: {
            const bool redo = m_kind == Icons::Kind::Redo;
            painter->save();
            if (redo) {
                painter->translate(24, 0);
                painter->scale(-1, 1);
            }
            QPainterPath path;
            path.moveTo(7, 10);
            path.cubicTo(12, 6, 19, 8, 19, 15);
            path.cubicTo(19, 19, 15, 20, 11, 20);
            painter->drawPath(path);
            painter->drawLine(QPointF(7, 10), QPointF(11, 6));
            painter->drawLine(QPointF(7, 10), QPointF(11, 14));
            painter->restore();
            break;
        }
        case Icons::Kind::Save:
            painter->drawRoundedRect(QRectF(4, 4, 16, 16), 2, 2);
            painter->drawRect(QRectF(8, 4, 8, 5));
            painter->setBrush(ink);
            painter->drawRect(QRectF(7, 13, 10, 7));
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
