// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "core/PdfDocument.h"
#include "core/Types.h"

#include <QAbstractScrollArea>
#include <QHash>
#include <QImage>
#include <QSizeF>
#include <QTimer>
#include <QVector>

class QMenu;
class QPinchGesture;

// Affichage continu des pages, façon SumatraPDF : défilement vertical,
// rendu progressif des seules pages visibles, mise à l'échelle immédiate de
// l'image existante pendant un zoom, puis remplacement par un rendu net.
class DocumentView : public QAbstractScrollArea
{
    Q_OBJECT
public:
    enum class ZoomMode { FitWidth = 0, FitPage = 1, Custom = 2 };
    Q_ENUM(ZoomMode)
    enum class Tool { Select, Highlight, Underline, StrikeOut, Note, FreeText, Ink, Line, Arrow, Square, Circle, Stamp };
    Q_ENUM(Tool)

    explicit DocumentView(QWidget *parent = nullptr);

    void reset(int pageCount);
    void setPageSizes(int first, const QVector<QSizeF> &sizes);
    void addImage(const RenderRequest &request, const QImage &image);

    int pageCount() const { return m_sizes.size(); }
    int currentPage() const { return m_currentPage; }
    void goToPage(int page, double yPoints = 0.0);
    void nextPage();
    void previousPage();

    ZoomMode zoomMode() const { return m_zoomMode; }
    double zoom() const { return m_zoom; }
    void setZoomMode(ZoomMode mode);
    // `anchor` : point de la fenêtre qui reste fixe pendant le zoom.
    void setZoom(double zoom, QPointF anchor = QPointF(-1, -1));
    void zoomIn();
    void zoomOut();

    // Document ouvert sur le thread de l'interface, réservé au texte
    // (sélection, liens). Peut rester nul : la vue reste alors sans sélection.
    void setTextDocument(PdfDocument *document);
    bool hasSelection() const { return !m_selectionRects.isEmpty(); }
    QString selectedText() const;
    void copySelection();
    void selectAllOnPage(int page);
    void clearSelection();
    bool invertColors() const { return m_invert; }
    void setInvertColors(bool invert);
    // Rectangle de la page dans la fenêtre, en pixels logiques.
    QRectF pageViewportRect(int page) const;

    // --- Annotations ------------------------------------------------------------
    void setTool(Tool tool);
    Tool tool() const { return m_tool; }
    void setAnnotationColor(const QColor &color) { m_annotColor = color; }
    QColor annotationColor() const { return m_annotColor; }
    void setPenWidth(double width) { m_penWidth = width; }
    double penWidth() const { return m_penWidth; }
    // Image à poser avec l'outil Tampon (tampon texte déjà dessiné, signature, logo).
    void setStampImage(const QImage &image, const QString &label);
    void setPageAnnotations(int page, const QVector<AnnotationInfo> &annotations);
    QVector<AnnotationInfo> pageAnnotations(int page) const { return m_annotations.value(page); }
    // Oublie les rendus de ces pages et les redemande.
    void invalidatePages(const QVector<int> &pages);
    int selectedAnnotation() const { return m_selectedAnnot; }
    int selectedAnnotationPage() const { return m_selectedAnnotPage; }
    void selectAnnotation(int page, int id);
    // Termine le dessin en cours (traits accumulés) : envoyé comme une seule annotation.
    void commitInk();

    void setSearchHits(int page, const QVector<QRectF> &hits);
    void setCurrentHit(int page, const QRectF &rect);
    void clearSearch();

    static constexpr double kMinZoom = 0.1;
    static constexpr double kMaxZoom = 64.0;

signals:
    void currentPageChanged(int page);
    void zoomChanged(double zoom, DocumentView::ZoomMode mode);
    void requestsChanged(const QVector<RenderRequest> &requests);
    void selectionChanged(bool hasSelection);
    void externalLinkActivated(const QString &uri);
    void toolChanged(DocumentView::Tool tool);
    void textMarkupRequested(AnnotationKind kind, const QHash<int, QVector<QRectF>> &rectsByPage);
    void noteRequested(int page, QPointF point);
    void freeTextRequested(int page, QRectF rect);
    void inkRequested(int page, const QVector<InkStroke> &strokes, const QColor &color, double width);
    void shapeRequested(AnnotationKind kind, int page, QRectF rect, const QColor &color, double width);
    void stampRequested(int page, QRectF rect, const QImage &image, const QString &label);
    void annotationSelected(int page, int id);
    void annotationDeleteRequested(int page, int id);
    void annotationEditRequested(int page, int id);
    void annotationMoveRequested(int page, int id, QRectF rect);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool viewportEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    struct PagePoint
    {
        int page = -1;
        QPointF point; // en points, dans la page
    };
    struct CachedImage
    {
        int width = 0; // largeur de la page entière, en pixels physiques
        QRect clip;    // vide : page entière
        QImage image;
    };

    double pixelsPerPoint() const;
    double computeFitZoom() const;
    void relayout(const PagePoint &anchor, QPointF anchorPos);
    void relayoutKeepingPosition();
    QRectF pageRect(int page) const; // coordonnées de la fenêtre
    int pageAtY(double contentY) const;
    void visibleRange(int &first, int &last) const;
    PagePoint pagePointAt(QPointF pos) const;
    void updateCurrentPage();
    void scheduleRequests();
    void emitRequests();
    void evictCache();
    void handlePinch(QPinchGesture *gesture);
    void drawPage(QPainter &painter, int page, const QRectF &rect);
    void drawAnnotationOverlay(QPainter &painter, int page, const QRectF &rect);
    const AnnotationInfo *annotationAt(const PagePoint &point) const;
    bool toolPress(QMouseEvent *event, const PagePoint &point, bool onPage);
    bool toolMove(QMouseEvent *event);
    bool toolRelease(QMouseEvent *event);
    void updateSelection();
    const PdfLink *linkAt(const PagePoint &point);
    void updateCursor(QPointF pos);

    QVector<QSizeF> m_sizes;  // en points
    QVector<double> m_tops;   // haut de chaque page, en pixels logiques
    double m_contentWidth = 0;
    double m_contentHeight = 0;
    double m_maxPageWidth = 0; // en points

    ZoomMode m_zoomMode = ZoomMode::FitWidth;
    double m_zoom = 1.0;
    double m_pinchStartZoom = 1.0;
    int m_currentPage = 0;

    QHash<int, CachedImage> m_full;
    QHash<int, CachedImage> m_clip;
    qint64 m_cacheBytes = 0;

    QHash<int, QVector<QRectF>> m_hits;
    int m_currentHitPage = -1;
    QRectF m_currentHit;

    QTimer m_requestTimer;

    // Texte, sélection et liens.
    PdfDocument *m_text = nullptr;
    PagePoint m_selStart;
    PagePoint m_selEnd;
    PdfDocument::SnapMode m_selMode = PdfDocument::SnapMode::Chars;
    bool m_selecting = false;
    bool m_panning = false;
    QPoint m_pressPos;
    QPoint m_panLast;
    qint64 m_lastDoubleClick = 0;
    QHash<int, QVector<QRectF>> m_selectionRects;
    QHash<int, QVector<PdfLink>> m_links;
    const PdfLink *m_pressedLink = nullptr;
    bool m_invert = false;

    // Outils d'annotation.
    Tool m_tool = Tool::Select;
    QColor m_annotColor = QColor(255, 220, 0);
    double m_penWidth = 2.0;
    QImage m_stampImage;
    QString m_stampLabel;
    QHash<int, QVector<AnnotationInfo>> m_annotations;
    int m_selectedAnnot = -1;
    int m_selectedAnnotPage = -1;
    bool m_drafting = false;
    PagePoint m_draftStart;
    PagePoint m_draftEnd;
    QVector<InkStroke> m_inkStrokes;
    int m_inkPage = -1;
    QTimer m_inkTimer;
    bool m_movingAnnot = false;
    QPointF m_moveOffset; // en points : position du clic dans l'annotation
    QRectF m_moveRect;    // rectangle déplacé, en points
};
