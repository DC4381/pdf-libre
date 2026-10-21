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

private:
    fz_context *m_ctx = nullptr;
    fz_document *m_doc = nullptr;
    int m_pageCount = 0;
    QString m_error;
};
