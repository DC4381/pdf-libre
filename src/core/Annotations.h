// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QColor>
#include <QDateTime>
#include <QMetaType>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

enum class AnnotationKind {
    Highlight,
    Underline,
    StrikeOut,
    Note,
    FreeText,
    Ink,
    Line,
    Arrow,
    Square,
    Circle,
    Stamp,
    Other
};

// Description d'une annotation telle que l'interface la voit. Toutes les
// coordonnées sont en points, origine en haut à gauche de la page affichée.
struct AnnotationInfo
{
    int id = -1; // numéro d'objet PDF : stable tant que le document est ouvert
    int page = -1;
    AnnotationKind kind = AnnotationKind::Other;
    QRectF rect;
    QString contents;
    QString author;
    QDateTime modified;
    QColor color;
};

using InkStroke = QVector<QPointF>;

Q_DECLARE_METATYPE(AnnotationInfo)
Q_DECLARE_METATYPE(QVector<AnnotationInfo>)
