// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QMetaType>
#include <QRect>
#include <QString>
#include <vector>

// Entrée de la table des matières (signets) d'un document.
struct OutlineItem
{
    QString title;
    int page = -1;
    bool open = false;
    std::vector<OutlineItem> children;
};
using OutlineList = std::vector<OutlineItem>;

// Demande de rendu adressée au service de rendu.
// `width` est la largeur de la page complète en pixels physiques.
// `clip`, s'il n'est pas vide, limite le rendu à une zone de la page
// (coordonnées en pixels à cette largeur) : utilisé aux forts zooms.
struct RenderRequest
{
    int page = -1;
    int width = 0;
    bool thumbnail = false;
    QRect clip;

    bool operator==(const RenderRequest &o) const
    {
        return page == o.page && width == o.width && thumbnail == o.thumbnail && clip == o.clip;
    }
};

Q_DECLARE_METATYPE(OutlineList)
Q_DECLARE_METATYPE(RenderRequest)
