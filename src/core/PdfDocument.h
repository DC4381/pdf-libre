// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Types.h"

#include <QByteArray>
#include <QImage>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVector>

struct fz_context;
struct fz_document;

// Lien présent sur une page : vers une autre page ou vers une adresse externe.
struct PdfLink
{
    QRectF rect;   // en points, origine en haut à gauche de la page
    int page = -1; // cible interne, ou -1
    QString uri;   // adresse externe (http, mailto…) si page < 0
};

// Enveloppe RAII d'un document MuPDF.
//
// Une instance n'est utilisée que depuis un seul thread : chaque service
// (rendu, recherche, impression) ouvre sa propre instance, ce qui évite
// tout verrou autour de MuPDF.
class PdfDocument
{
public:
    enum class OpenStatus { Ok, NeedsPassword, WrongPassword, Failed };

    PdfDocument();
    ~PdfDocument();
    PdfDocument(const PdfDocument &) = delete;
    PdfDocument &operator=(const PdfDocument &) = delete;

    OpenStatus open(const QString &path, const QByteArray &password = {});
    bool isOpen() const { return m_doc != nullptr; }
    QString lastError() const { return m_error; }

    int pageCount() const { return m_pageCount; }
    // Taille de la page en points (1/72 de pouce), rotation /Rotate comprise.
    QSizeF pageSize(int index);
    // Rendu de la page à l'échelle `scale` (pixels par point). Si `clip`
    // n'est pas vide, seule cette zone (en pixels à cette échelle) est rendue.
    QImage render(int index, double scale, const QRect &clip = {});
    OutlineList outline();
    // Occurrences de `needle` sur la page, en points. Insensible à la casse.
    QVector<QRectF> search(int index, const QString &needle, int maxHits = 512);
    QString metadata(const char *key);

    enum class SnapMode { Chars, Words, Lines };
    // Zones du texte compris entre les points `a` et `b` (en points).
    // Avec `snap`, les points sont d'abord étendus au mot ou à la ligne.
    QVector<QRectF> selectionRects(int index, QPointF a, QPointF b, SnapMode snap = SnapMode::Chars);
    QString selectionText(int index, QPointF a, QPointF b, SnapMode snap = SnapMode::Chars);
    // Vrai si du texte se trouve sous le point (pour le curseur).
    bool hasTextAt(int index, QPointF point);
    QVector<PdfLink> links(int index);

private:
    struct TextCacheEntry
    {
        int page = -1;
        void *text = nullptr; // fz_stext_page (type anonyme chez MuPDF)
        float originX = 0;
        float originY = 0;
    };
    const TextCacheEntry *textPage(int index);
    void dropTextPages();

    TextCacheEntry m_textCache[4];
    int m_textCacheNext = 0;

    fz_context *m_ctx = nullptr;
    fz_document *m_doc = nullptr;
    int m_pageCount = 0;
    QString m_error;
};
