// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Annotations.h"
#include "Types.h"

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVector>

struct fz_context;
struct fz_document;
struct pdf_document;
struct pdf_annot;
struct pdf_page;

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

    // --- Annotations (document PDF uniquement) ---------------------------------
    // Active le journal des modifications (annuler/rétablir). À appeler une fois.
    bool enableEditing();
    bool isEditable() const { return m_pdf != nullptr; }
    void setAuthor(const QString &author) { m_author = author; }
    QVector<AnnotationInfo> annotations(int index);
    // Toutes les fonctions d'ajout renvoient l'identifiant de l'annotation, ou -1.
    int addTextMarkup(int index, AnnotationKind kind, const QVector<QRectF> &rects, const QColor &color);
    int addNote(int index, QPointF point, const QString &text, const QColor &color);
    int addFreeText(int index, const QRectF &rect, const QString &text, double fontSize, const QColor &color);
    int addInk(int index, const QVector<InkStroke> &strokes, const QColor &color, double width);
    // Line / Arrow : `rect` donne les deux extrémités (topLeft -> bottomRight).
    int addShape(int index, AnnotationKind kind, const QRectF &rect, const QColor &stroke, const QColor &fill, double width);
    int addStampText(int index, const QRectF &rect, const QString &text, const QColor &color);
    int addStampImage(int index, const QRectF &rect, const QImage &image, const QString &contents = {});
    bool setAnnotationContents(int index, int id, const QString &text);
    bool setAnnotationRect(int index, int id, const QRectF &rect);
    bool deleteAnnotation(int index, int id);
    bool canUndo() const;
    bool canRedo() const;
    // Renvoient la page concernée par l'opération annulée/rétablie, ou -1.
    int undo();
    int redo();
    bool hasUnsavedChanges() const;
    // Enregistre : incrémental si `path` est le fichier ouvert et que le
    // document le permet (préserve les signatures), sinon copie complète.
    bool save(const QString &path);

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

    struct AnnotOp
    {
        int page = -1;
    };
    pdf_annot *findAnnot(pdf_page *page, int id);
    int beginAnnot(int index, const char *name, pdf_page **pageOut);
    int finishAnnot(int index, pdf_page *page, pdf_annot *annot, bool ok, bool setMeta = true);
    void stampMeta(pdf_annot *annot);
    void pageToPdf(pdf_page *page, double &x, double &y) const;

    fz_context *m_ctx = nullptr;
    fz_document *m_doc = nullptr;
    pdf_document *m_pdf = nullptr;
    QString m_path;
    QByteArray m_lastPassword;
    QString m_author;
    bool m_dirty = false;
    QVector<int> m_undoPages;
    QVector<int> m_redoPages;
    int m_pageCount = 0;
    QString m_error;
};
