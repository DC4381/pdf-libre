// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "core/Types.h"

#include <QAbstractScrollArea>
#include <QHash>
#include <QImage>
#include <QSizeF>
#include <QTimer>
#include <QVector>

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

    void setSearchHits(int page, const QVector<QRectF> &hits);
    void setCurrentHit(int page, const QRectF &rect);
    void clearSearch();

    static constexpr double kMinZoom = 0.1;
    static constexpr double kMaxZoom = 64.0;

signals:
    void currentPageChanged(int page);
    void zoomChanged(double zoom, DocumentView::ZoomMode mode);
    void requestsChanged(const QVector<RenderRequest> &requests);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool viewportEvent(QEvent *event) override;

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
};
