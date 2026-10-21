// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

// Référence à une page d'un document source, avec sa rotation à appliquer.
// Une page "blanche" a un sourcePath vide et une taille en points.
struct PageRef
{
    QString sourcePath;
    QByteArray password;
    int page = -1;
    int rotation = 0; // 0, 90, 180, 270 : ajouté à la rotation d'origine
    double blankWidth = 595;
    double blankHeight = 842;

    bool isBlank() const { return sourcePath.isEmpty(); }
    bool operator==(const PageRef &o) const
    {
        return sourcePath == o.sourcePath && page == o.page && rotation == o.rotation && blankWidth == o.blankWidth
               && blankHeight == o.blankHeight;
    }
};

// Assemble un nouveau PDF à partir d'une liste de pages venant d'un ou
// plusieurs documents. Un seul moteur pour fusionner, séparer, extraire,
// réordonner, tourner, insérer et supprimer : le résultat est toujours
// reconstruit, jamais modifié "en place", ce qui garantit un fichier propre.
//
// Les signets sont conservés quand toutes les pages proviennent du même
// document : chaque signet est redirigé vers la nouvelle position de sa page,
// et ceux dont la page a été supprimée sont retirés.
class PdfAssembler
{
public:
    struct Result
    {
        bool ok = false;
        QString error;
        int pageCount = 0;
    };

    // Écrit le document assemblé dans `outputPath` (écriture atomique :
    // fichier temporaire puis remplacement).
    static Result assemble(const QVector<PageRef> &pages, const QString &outputPath);

    // Nombre de signatures électroniques présentes dans un document.
    // Toute opération d'assemblage les invalide : à signaler avant d'agir.
    static int signatureCount(const QString &path, const QByteArray &password = {});
};
