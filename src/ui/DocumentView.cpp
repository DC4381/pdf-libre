// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "DocumentView.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QGestureEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPinchGesture>
#include <QScrollBar>
#include <QScroller>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {
constexpr double kGap = 10.0;    // espace entre les pages, en pixels logiques
constexpr double kMargin = 10.0; // marge latérale minimale
// Au-delà de cette surface, la page n'est plus rendue en entier : on rend
// un aperçu basse résolution et seulement la zone visible en pleine résolution.
constexpr qint64 kMaxFullPixels = 16'000'000;
constexpr qint64 kPreviewPixels = 2'000'000;
constexpr qint64 kCacheBudget = 320ll << 20;
constexpr int kTileGrid = 256;

const double kZoomSteps[] = {0.1, 0.25, 0.5, 0.67, 0.75, 0.9, 1.0, 1.1, 1.25, 1.5, 1.75, 2.0,
                             3.0, 4.0, 6.0, 8.0, 12.0, 16.0, 24.0, 32.0, 48.0, 64.0};

qint64 bytesOf(const QImage &image)
{
    return image.sizeInBytes();
}
} // namespace

DocumentView::DocumentView(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setFrameShape(QFrame::NoFrame);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setAccessibleName(tr("Document"));
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
    viewport()->setAttribute(Qt::WA_AcceptTouchEvents);
    viewport()->grabGesture(Qt::PinchGesture);
    QScroller::grabGesture(viewport(), QScroller::TouchGesture);

    m_requestTimer.setSingleShot(true);
    m_requestTimer.setInterval(8);
    connect(&m_requestTimer, &QTimer::timeout, this, &DocumentView::emitRequests);
    // Les traits tracés à la suite forment un seul dessin.
    m_inkTimer.setSingleShot(true);
    m_inkTimer.setInterval(1200);
    connect(&m_inkTimer, &QTimer::timeout, this, &DocumentView::commitInk);
}

void DocumentView::reset(int pageCount)
{
    m_sizes = QVector<QSizeF>(pageCount, QSizeF(612, 792));
    m_maxPageWidth = 612;
    m_full.clear();
    m_clip.clear();
    m_cacheBytes = 0;
    m_links.clear();
    m_pressedLink = nullptr;
    m_annotations.clear();
    m_selectedAnnot = -1;
    m_selectedAnnotPage = -1;
    m_drafting = false;
    m_inkStrokes.clear();
    m_inkTimer.stop();
    clearSelection();
    clearSearch();
    m_currentPage = 0;
    relayout(PagePoint{0, QPointF(0, 0)}, QPointF(0, 0));
}

void DocumentView::setPageSizes(int first, const QVector<QSizeF> &sizes)
{
    if (sizes.isEmpty() || first < 0 || first + sizes.size() > m_sizes.size())
        return;
    // Premières tailles connues : on suppose que le reste du document a le
    // même format, ce qui est vrai dans l'immense majorité des cas.
    if (first == 0) {
        for (int i = sizes.size(); i < m_sizes.size(); ++i)
            m_sizes[i] = sizes.first();
    }
    bool changed = first == 0;
    for (int i = 0; i < sizes.size(); ++i) {
        if (m_sizes[first + i] != sizes[i]) {
            m_sizes[first + i] = sizes[i];
            changed = true;
        }
    }
    if (!changed)
        return;
    m_maxPageWidth = 0;
    for (const QSizeF &s : std::as_const(m_sizes))
        m_maxPageWidth = std::max(m_maxPageWidth, s.width());
    relayoutKeepingPosition();
}

void DocumentView::addImage(const RenderRequest &request, const QImage &image)
{
    if (request.thumbnail || request.page < 0 || request.page >= m_sizes.size())
        return;
    QHash<int, CachedImage> &target = request.clip.isEmpty() ? m_full : m_clip;
    auto it = target.find(request.page);
    if (it != target.end())
        m_cacheBytes -= bytesOf(it->image);
    target.insert(request.page, CachedImage{request.width, request.clip, image});
    m_cacheBytes += bytesOf(image);
    evictCache();
    viewport()->update();
}

double DocumentView::pixelsPerPoint() const
{
    return m_zoom * logicalDpiX() / 72.0;
}

double DocumentView::computeFitZoom() const
{
    if (m_sizes.isEmpty())
        return m_zoom;
    const double base = logicalDpiX() / 72.0;
    const double availW = std::max(50.0, viewport()->width() - 2 * kMargin);
    const double availH = std::max(50.0, viewport()->height() - 2 * kGap);
    double zoom = m_zoom;
    if (m_zoomMode == ZoomMode::FitWidth) {
        zoom = availW / (m_maxPageWidth * base);
    } else if (m_zoomMode == ZoomMode::FitPage) {
        const QSizeF s = m_sizes.value(m_currentPage, m_sizes.first());
        zoom = std::min(availW / (s.width() * base), availH / (s.height() * base));
    }
    return std::clamp(zoom, kMinZoom, kMaxZoom);
}

void DocumentView::relayout(const PagePoint &anchor, QPointF anchorPos)
{
    const double oldZoom = m_zoom;
    if (m_zoomMode != ZoomMode::Custom)
        m_zoom = computeFitZoom();

    const double ppp = pixelsPerPoint();
    m_tops.resize(m_sizes.size());
    double y = kGap;
    for (int i = 0; i < m_sizes.size(); ++i) {
        m_tops[i] = y;
        y += m_sizes[i].height() * ppp + kGap;
    }
    m_contentHeight = y;
    m_contentWidth = std::max<double>(m_maxPageWidth * ppp + 2 * kMargin, viewport()->width());

    const int vw = viewport()->width();
    const int vh = viewport()->height();
    verticalScrollBar()->setRange(0, std::max(0, int(std::ceil(m_contentHeight)) - vh));
    verticalScrollBar()->setPageStep(vh);
    verticalScrollBar()->setSingleStep(40);
    horizontalScrollBar()->setRange(0, std::max(0, int(std::ceil(m_contentWidth)) - vw));
    horizontalScrollBar()->setPageStep(vw);
    horizontalScrollBar()->setSingleStep(40);

    if (anchor.page >= 0 && anchor.page < m_sizes.size()) {
        const double contentY = m_tops[anchor.page] + anchor.point.y() * ppp;
        const double pageLeft = (m_contentWidth - m_sizes[anchor.page].width() * ppp) / 2;
        const double contentX = pageLeft + anchor.point.x() * ppp;
        verticalScrollBar()->setValue(int(std::lround(contentY - anchorPos.y())));
        horizontalScrollBar()->setValue(int(std::lround(contentX - anchorPos.x())));
    }

    viewport()->update();
    updateCurrentPage();
    scheduleRequests();
    if (!qFuzzyCompare(oldZoom, m_zoom) || m_zoomMode != ZoomMode::Custom)
        emit zoomChanged(m_zoom, m_zoomMode);
}

void DocumentView::relayoutKeepingPosition()
{
    if (m_tops.size() != m_sizes.size() || m_sizes.isEmpty()) {
        relayout(PagePoint{0, QPointF(0, 0)}, QPointF(0, 0));
        return;
    }
    const QPointF pos(viewport()->width() / 2.0, 0);
    relayout(pagePointAt(pos), pos);
}

QRectF DocumentView::pageRect(int page) const
{
    const double ppp = pixelsPerPoint();
    const QSizeF s = m_sizes[page] * ppp;
    const double x = (m_contentWidth - s.width()) / 2 - horizontalScrollBar()->value();
    const double y = m_tops[page] - verticalScrollBar()->value();
    return QRectF(QPointF(x, y), s);
}

int DocumentView::pageAtY(double contentY) const
{
    if (m_tops.isEmpty())
        return -1;
    const auto it = std::upper_bound(m_tops.cbegin(), m_tops.cend(), contentY);
    return std::max(0, int(it - m_tops.cbegin()) - 1);
}

void DocumentView::visibleRange(int &first, int &last) const
{
    const double top = verticalScrollBar()->value();
    first = pageAtY(top);
    last = pageAtY(top + viewport()->height());
}

DocumentView::PagePoint DocumentView::pagePointAt(QPointF pos) const
{
    PagePoint result;
    if (m_tops.isEmpty())
        return result;
    const double ppp = pixelsPerPoint();
    const double contentY = pos.y() + verticalScrollBar()->value();
    result.page = pageAtY(contentY);
    const double pageLeft = (m_contentWidth - m_sizes[result.page].width() * ppp) / 2;
    result.point = QPointF((pos.x() + horizontalScrollBar()->value() - pageLeft) / ppp,
                           (contentY - m_tops[result.page]) / ppp);
    return result;
}

void DocumentView::updateCurrentPage()
{
    if (m_tops.isEmpty())
        return;
    int page = pageAtY(verticalScrollBar()->value() + viewport()->height() / 3.0);
    if (verticalScrollBar()->value() >= verticalScrollBar()->maximum() && verticalScrollBar()->maximum() > 0) {
        int first, last;
        visibleRange(first, last);
        page = std::max(page, last);
    }
    page = std::clamp(page, 0, int(m_sizes.size()) - 1);
    if (page != m_currentPage) {
        m_currentPage = page;
        emit currentPageChanged(page);
    }
}

void DocumentView::scheduleRequests()
{
    if (!m_requestTimer.isActive())
        m_requestTimer.start();
}

void DocumentView::emitRequests()
{
    if (m_tops.isEmpty())
        return;
    const double dpr = devicePixelRatioF();
    const QRectF viewRect = viewport()->rect();
    int first, last;
    visibleRange(first, last);

    QVector<RenderRequest> requests;
    const auto want = [&](int page, bool visible) {
        const QRectF r = pageRect(page);
        const int w = int(std::lround(r.width() * dpr));
        const int h = int(std::lround(r.height() * dpr));
        if (w <= 0 || h <= 0)
            return;
        const auto full = m_full.constFind(page);
        if (qint64(w) * h <= kMaxFullPixels) {
            if (full == m_full.cend() || full->width != w)
                requests.append(RenderRequest{page, w, false, {}});
            return;
        }
        // Fort zoom : aperçu de la page entière, puis zone visible nette.
        const int previewW = int(w * std::sqrt(double(kPreviewPixels) / (double(w) * h)));
        if (full == m_full.cend() || full->width < previewW * 3 / 4)
            requests.append(RenderRequest{page, previewW, false, {}});
        if (!visible)
            return;
        const QRectF vis = r.intersected(viewRect).translated(-r.topLeft());
        if (vis.isEmpty())
            return;
        const QRect needed = QRectF(vis.topLeft() * dpr, vis.size() * dpr).toAlignedRect();
        const auto clip = m_clip.constFind(page);
        if (clip != m_clip.cend() && clip->width == w && clip->clip.contains(needed))
            return;
        // Zone élargie et alignée sur une grille, pour qu'un petit
        // défilement ne déclenche pas un nouveau rendu.
        const int x0 = std::max(0, (needed.left() - kTileGrid / 2) / kTileGrid * kTileGrid);
        const int y0 = std::max(0, (needed.top() - kTileGrid / 2) / kTileGrid * kTileGrid);
        const int x1 = std::min(w, (needed.right() + kTileGrid * 3 / 2) / kTileGrid * kTileGrid);
        const int y1 = std::min(h, (needed.bottom() + kTileGrid * 3 / 2) / kTileGrid * kTileGrid);
        requests.append(RenderRequest{page, w, false, QRect(QPoint(x0, y0), QPoint(x1 - 1, y1 - 1))});
    };

    if (m_currentPage >= first && m_currentPage <= last)
        want(m_currentPage, true);
    for (int i = first; i <= last; ++i) {
        if (i != m_currentPage)
            want(i, true);
    }
    if (first > 0)
        want(first - 1, false);
    if (last + 1 < m_sizes.size())
        want(last + 1, false);
    emit requestsChanged(requests);
}

void DocumentView::evictCache()
{
    while (m_cacheBytes > kCacheBudget) {
        int first, last;
        visibleRange(first, last);
        int victim = -1;
        bool fromClip = false;
        int farthest = -1;
        const auto consider = [&](const QHash<int, CachedImage> &map, bool isClip) {
            for (auto it = map.cbegin(); it != map.cend(); ++it) {
                if (it.key() >= first - 1 && it.key() <= last + 1)
                    continue;
                const int distance = std::abs(it.key() - m_currentPage);
                if (distance > farthest) {
                    farthest = distance;
                    victim = it.key();
                    fromClip = isClip;
                }
            }
        };
        consider(m_clip, true);
        consider(m_full, false);
        if (victim < 0)
            return;
        QHash<int, CachedImage> &map = fromClip ? m_clip : m_full;
        m_cacheBytes -= bytesOf(map.value(victim).image);
        map.remove(victim);
    }
}

void DocumentView::goToPage(int page, double yPoints)
{
    if (m_tops.isEmpty())
        return;
    page = std::clamp(page, 0, int(m_sizes.size()) - 1);
    if (m_zoomMode == ZoomMode::FitPage && page != m_currentPage) {
        m_currentPage = page;
        m_zoom = computeFitZoom();
        relayout(PagePoint{page, QPointF(0, 0)}, QPointF(0, kGap));
        horizontalScrollBar()->setValue(horizontalScrollBar()->maximum() / 2);
        emit currentPageChanged(page);
        return;
    }
    verticalScrollBar()->setValue(int(std::lround(m_tops[page] + yPoints * pixelsPerPoint() - kGap)));
    updateCurrentPage();
    if (m_currentPage != page) {
        // Dernières pages : impossible de les amener en haut de la fenêtre.
        m_currentPage = page;
        emit currentPageChanged(page);
    }
}

void DocumentView::nextPage()
{
    goToPage(m_currentPage + 1);
}

void DocumentView::previousPage()
{
    goToPage(m_currentPage - 1);
}

void DocumentView::setZoomMode(ZoomMode mode)
{
    m_zoomMode = mode;
    if (mode == ZoomMode::FitPage) {
        const int page = m_currentPage;
        m_zoom = computeFitZoom();
        relayout(PagePoint{page, QPointF(0, 0)}, QPointF(0, kGap));
    } else {
        relayoutKeepingPosition();
    }
}

void DocumentView::setZoom(double zoom, QPointF anchor)
{
    zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
    if (anchor.x() < 0)
        anchor = QPointF(viewport()->width() / 2.0, viewport()->height() / 2.0);
    const PagePoint point = pagePointAt(anchor);
    m_zoomMode = ZoomMode::Custom;
    m_zoom = zoom;
    relayout(point, anchor);
}

void DocumentView::zoomIn()
{
    for (double step : kZoomSteps) {
        if (step > m_zoom * 1.01) {
            setZoom(step);
            return;
        }
    }
}

void DocumentView::zoomOut()
{
    for (auto it = std::rbegin(kZoomSteps); it != std::rend(kZoomSteps); ++it) {
        if (*it < m_zoom * 0.99) {
            setZoom(*it);
            return;
        }
    }
}

void DocumentView::setSearchHits(int page, const QVector<QRectF> &hits)
{
    if (hits.isEmpty())
        m_hits.remove(page);
    else
        m_hits.insert(page, hits);
    viewport()->update();
}

void DocumentView::setCurrentHit(int page, const QRectF &rect)
{
    m_currentHitPage = page;
    m_currentHit = rect;
    if (page < 0 || page >= m_sizes.size())
        return;
    const double ppp = pixelsPerPoint();
    const QRectF pr = pageRect(page);
    const QRectF hit(pr.left() + rect.left() * ppp, pr.top() + rect.top() * ppp, rect.width() * ppp,
                     rect.height() * ppp);
    const QRectF view = viewport()->rect();
    if (!view.contains(hit)) {
        if (hit.top() < 0 || hit.bottom() > view.height())
            verticalScrollBar()->setValue(int(verticalScrollBar()->value() + hit.top() - view.height() / 3));
        if (hit.left() < 0 || hit.right() > view.width())
            horizontalScrollBar()->setValue(int(horizontalScrollBar()->value() + hit.center().x() - view.width() / 2));
    }
    viewport()->update();
}

void DocumentView::clearSearch()
{
    m_hits.clear();
    m_currentHitPage = -1;
    viewport()->update();
}

void DocumentView::drawPage(QPainter &painter, int page, const QRectF &rect)
{
    painter.fillRect(rect.translated(1.5, 1.5), QColor(0, 0, 0, 50));
    painter.fillRect(rect, Qt::white);

    const auto full = m_full.constFind(page);
    if (full != m_full.cend())
        painter.drawImage(rect, full->image);
    const auto clip = m_clip.constFind(page);
    if (clip != m_clip.cend() && clip->width > 0) {
        const double scale = rect.width() / clip->width;
        const QRectF target(rect.left() + clip->clip.x() * scale, rect.top() + clip->clip.y() * scale,
                            clip->clip.width() * scale, clip->clip.height() * scale);
        painter.drawImage(target, clip->image);
    }

    if (m_invert) {
        // Inversion des couleurs sans copie d'image : différence avec du blanc.
        painter.setCompositionMode(QPainter::CompositionMode_Difference);
        painter.fillRect(rect, Qt::white);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    }

    const double ppp = pixelsPerPoint();
    const auto toView = [&](const QRectF &r) {
        return QRectF(rect.left() + r.left() * ppp, rect.top() + r.top() * ppp, r.width() * ppp, r.height() * ppp);
    };
    const auto selection = m_selectionRects.constFind(page);
    if (selection != m_selectionRects.cend()) {
        const QColor color = palette().color(QPalette::Highlight);
        for (const QRectF &r : *selection)
            painter.fillRect(toView(r), QColor(color.red(), color.green(), color.blue(), 90));
    }
    const auto hits = m_hits.constFind(page);
    if (hits != m_hits.cend()) {
        for (const QRectF &r : *hits)
            painter.fillRect(toView(r), QColor(255, 220, 0, 90));
    }
    if (m_currentHitPage == page) {
        const QRectF r = toView(m_currentHit).adjusted(-1, -1, 1, 1);
        painter.fillRect(r, QColor(255, 140, 0, 110));
        painter.setPen(QPen(QColor(220, 90, 0), 1.5));
        painter.drawRect(r);
    }
    drawAnnotationOverlay(painter, page, rect);
}

void DocumentView::paintEvent(QPaintEvent *)
{
    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), m_invert ? QColor(40, 40, 40) : palette().color(QPalette::Mid));
    if (m_tops.isEmpty())
        return;
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    int first, last;
    visibleRange(first, last);
    for (int i = first; i <= last; ++i)
        drawPage(painter, i, pageRect(i));
}

void DocumentView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    relayoutKeepingPosition();
}

void DocumentView::scrollContentsBy(int, int)
{
    viewport()->update();
    updateCurrentPage();
    scheduleRequests();
}

void DocumentView::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        const double steps = event->angleDelta().y() / 120.0;
        if (steps != 0)
            setZoom(m_zoom * std::pow(1.15, steps), event->position());
        event->accept();
        return;
    }
    QAbstractScrollArea::wheelEvent(event);
}

void DocumentView::keyPressEvent(QKeyEvent *event)
{
    QScrollBar *v = verticalScrollBar();
    const int line = 48;
    const int pageStep = std::max(40, viewport()->height() - 48);
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    switch (event->key()) {
    case Qt::Key_Down:
    case Qt::Key_J:
        v->setValue(v->value() + line);
        break;
    case Qt::Key_Up:
    case Qt::Key_K:
        v->setValue(v->value() - line);
        break;
    case Qt::Key_Space:
        v->setValue(v->value() + (shift ? -pageStep : pageStep));
        break;
    case Qt::Key_PageDown:
        if (m_zoomMode == ZoomMode::FitPage)
            nextPage();
        else
            v->setValue(v->value() + pageStep);
        break;
    case Qt::Key_PageUp:
        if (m_zoomMode == ZoomMode::FitPage)
            previousPage();
        else
            v->setValue(v->value() - pageStep);
        break;
    case Qt::Key_N:
        nextPage();
        break;
    case Qt::Key_P:
        previousPage();
        break;
    case Qt::Key_Right:
        if (horizontalScrollBar()->maximum() > 0)
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() + line);
        else
            nextPage();
        break;
    case Qt::Key_Left:
        if (horizontalScrollBar()->maximum() > 0)
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() - line);
        else
            previousPage();
        break;
    case Qt::Key_Home:
        goToPage(0);
        break;
    case Qt::Key_End:
        goToPage(int(m_sizes.size()) - 1);
        break;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        zoomIn();
        break;
    case Qt::Key_Minus:
        zoomOut();
        break;
    case Qt::Key_Escape:
        if (m_drafting || !m_inkStrokes.isEmpty()) {
            m_drafting = false;
            m_inkStrokes.clear();
            m_inkTimer.stop();
            viewport()->update();
        } else if (m_selectedAnnot >= 0) {
            selectAnnotation(-1, -1);
        } else if (hasSelection()) {
            clearSelection();
        } else if (m_tool != Tool::Select) {
            setTool(Tool::Select);
        } else {
            QAbstractScrollArea::keyPressEvent(event);
            return;
        }
        break;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        if (m_selectedAnnot >= 0)
            emit annotationDeleteRequested(m_selectedAnnotPage, m_selectedAnnot);
        break;
    default:
        if (event->matches(QKeySequence::Copy) && hasSelection()) {
            copySelection();
            break;
        }
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }
    event->accept();
}

bool DocumentView::viewportEvent(QEvent *event)
{
    if (event->type() == QEvent::Gesture) {
        auto *gestureEvent = static_cast<QGestureEvent *>(event);
        if (auto *pinch = static_cast<QPinchGesture *>(gestureEvent->gesture(Qt::PinchGesture))) {
            handlePinch(pinch);
            gestureEvent->accept(pinch);
            return true;
        }
    }
    return QAbstractScrollArea::viewportEvent(event);
}

void DocumentView::handlePinch(QPinchGesture *gesture)
{
    if (gesture->state() == Qt::GestureStarted)
        m_pinchStartZoom = m_zoom;
    if (gesture->changeFlags() & QPinchGesture::ScaleFactorChanged) {
        const QPointF center = viewport()->mapFromGlobal(gesture->centerPoint());
        setZoom(m_pinchStartZoom * gesture->totalScaleFactor(), center);
    }
}

// --- Texte, sélection, liens, déplacement ------------------------------------

void DocumentView::setTextDocument(PdfDocument *document)
{
    m_text = document;
    m_links.clear();
}

QRectF DocumentView::pageViewportRect(int page) const
{
    if (page < 0 || page >= m_tops.size())
        return {};
    return pageRect(page);
}

void DocumentView::setInvertColors(bool invert)
{
    if (m_invert == invert)
        return;
    m_invert = invert;
    viewport()->update();
}

void DocumentView::clearSelection()
{
    const bool had = hasSelection();
    m_selectionRects.clear();
    m_selStart = {};
    m_selEnd = {};
    m_selecting = false;
    if (had) {
        emit selectionChanged(false);
        viewport()->update();
    }
}

// Recalcule les zones sélectionnées entre m_selStart et m_selEnd, qui peuvent
// se trouver sur des pages différentes.
void DocumentView::updateSelection()
{
    m_selectionRects.clear();
    if (m_text && m_selStart.page >= 0 && m_selEnd.page >= 0) {
        PagePoint first = m_selStart;
        PagePoint last = m_selEnd;
        if (last.page < first.page)
            std::swap(first, last);
        for (int page = first.page; page <= last.page; ++page) {
            const QSizeF size = m_sizes[page];
            const QPointF a = page == first.page ? first.point : QPointF(0, 0);
            const QPointF b = page == last.page ? last.point : QPointF(size.width(), size.height());
            const QVector<QRectF> rects = m_text->selectionRects(page, a, b, m_selMode);
            if (!rects.isEmpty())
                m_selectionRects.insert(page, rects);
        }
    }
    emit selectionChanged(hasSelection());
    viewport()->update();
}

QString DocumentView::selectedText() const
{
    if (!m_text || m_selectionRects.isEmpty())
        return {};
    PagePoint first = m_selStart;
    PagePoint last = m_selEnd;
    if (last.page < first.page)
        std::swap(first, last);
    QStringList parts;
    for (int page = first.page; page <= last.page; ++page) {
        if (!m_selectionRects.contains(page))
            continue;
        const QSizeF size = m_sizes[page];
        const QPointF a = page == first.page ? first.point : QPointF(0, 0);
        const QPointF b = page == last.page ? last.point : QPointF(size.width(), size.height());
        const QString text = m_text->selectionText(page, a, b, m_selMode).trimmed();
        if (!text.isEmpty())
            parts.append(text);
    }
    return parts.join(QStringLiteral("\n\n"));
}

void DocumentView::copySelection()
{
    const QString text = selectedText();
    if (!text.isEmpty())
        QApplication::clipboard()->setText(text);
}

void DocumentView::selectAllOnPage(int page)
{
    if (!m_text || page < 0 || page >= m_sizes.size())
        return;
    const QSizeF size = m_sizes[page];
    m_selMode = PdfDocument::SnapMode::Chars;
    m_selStart = PagePoint{page, QPointF(0, 0)};
    m_selEnd = PagePoint{page, QPointF(size.width(), size.height())};
    updateSelection();
}

const PdfLink *DocumentView::linkAt(const PagePoint &point)
{
    if (!m_text || point.page < 0)
        return nullptr;
    auto it = m_links.find(point.page);
    if (it == m_links.end())
        it = m_links.insert(point.page, m_text->links(point.page));
    for (const PdfLink &link : *it) {
        if (link.rect.contains(point.point))
            return &link;
    }
    return nullptr;
}

void DocumentView::updateCursor(QPointF pos)
{
    if (m_panning) {
        viewport()->setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (m_tool != Tool::Select) {
        const bool textTool = m_tool == Tool::Highlight || m_tool == Tool::Underline || m_tool == Tool::StrikeOut;
        viewport()->setCursor(textTool ? Qt::IBeamCursor : Qt::CrossCursor);
        return;
    }
    const PagePoint point = pagePointAt(pos);
    if (annotationAt(point)) {
        viewport()->setCursor(Qt::SizeAllCursor);
        return;
    }
    if (!pageRect(point.page).contains(pos)) {
        viewport()->setCursor(Qt::ArrowCursor);
    } else if (linkAt(point)) {
        viewport()->setCursor(Qt::PointingHandCursor);
    } else if (m_text && m_text->hasTextAt(point.page, point.point)) {
        viewport()->setCursor(Qt::IBeamCursor);
    } else {
        viewport()->setCursor(Qt::OpenHandCursor);
    }
}

void DocumentView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || m_tops.isEmpty()) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }
    setFocus(Qt::MouseFocusReason);
    m_pressPos = event->pos();
    const PagePoint point = pagePointAt(event->position());
    const bool onPage = pageRect(point.page).contains(event->position());
    if (toolPress(event, point, onPage))
        return;
    m_pressedLink = onPage ? linkAt(point) : nullptr;

    // Un clic simple après un double-clic rapproché sélectionne la ligne.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool triple = now - m_lastDoubleClick < QApplication::doubleClickInterval();
    if (triple) {
        m_lastDoubleClick = 0;
        m_selMode = PdfDocument::SnapMode::Lines;
        m_selStart = m_selEnd = point;
        updateSelection();
        event->accept();
        return;
    }

    if (onPage && m_text && !m_pressedLink && m_text->hasTextAt(point.page, point.point)) {
        clearSelection();
        m_selMode = PdfDocument::SnapMode::Chars;
        m_selStart = m_selEnd = point;
        m_selecting = true;
    } else if (!m_pressedLink) {
        // Hors du texte : on déplace le document, comme SumatraPDF.
        clearSelection();
        m_panning = true;
        m_panLast = event->pos();
        viewport()->setCursor(Qt::ClosedHandCursor);
    }
    event->accept();
}

void DocumentView::mouseMoveEvent(QMouseEvent *event)
{
    if (toolMove(event))
        return;
    if (m_panning) {
        const QPoint delta = event->pos() - m_panLast;
        m_panLast = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }
    if (m_selecting) {
        // Défilement automatique quand la souris sort de la fenêtre.
        const int h = viewport()->height();
        if (event->pos().y() < 0)
            verticalScrollBar()->setValue(verticalScrollBar()->value() + std::max(-40, event->pos().y() / 2));
        else if (event->pos().y() > h)
            verticalScrollBar()->setValue(verticalScrollBar()->value() + std::min(40, (event->pos().y() - h) / 2));
        m_selEnd = pagePointAt(event->position());
        updateSelection();
        event->accept();
        return;
    }
    updateCursor(event->position());
    QAbstractScrollArea::mouseMoveEvent(event);
}

void DocumentView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mouseReleaseEvent(event);
        return;
    }
    const bool moved = (event->pos() - m_pressPos).manhattanLength() > QApplication::startDragDistance();
    if (toolRelease(event))
        return;
    if (m_pressedLink && !moved) {
        const PdfLink link = *m_pressedLink;
        m_pressedLink = nullptr;
        if (link.page >= 0)
            goToPage(link.page);
        else
            emit externalLinkActivated(link.uri);
    }
    m_pressedLink = nullptr;
    if (m_selecting) {
        m_selecting = false;
        if (!moved && m_selMode == PdfDocument::SnapMode::Chars)
            clearSelection();
    }
    m_panning = false;
    updateCursor(event->position());
    event->accept();
}

void DocumentView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_text) {
        QAbstractScrollArea::mouseDoubleClickEvent(event);
        return;
    }
    const PagePoint point = pagePointAt(event->position());
    if (m_tool == Tool::Select) {
        if (const AnnotationInfo *info = annotationAt(point)) {
            selectAnnotation(info->page, info->id);
            emit annotationEditRequested(info->page, info->id);
            event->accept();
            return;
        }
    }
    if (m_tool != Tool::Select)
        return;
    if (!pageRect(point.page).contains(event->position()))
        return;
    m_lastDoubleClick = QDateTime::currentMSecsSinceEpoch();
    m_selMode = PdfDocument::SnapMode::Words;
    m_selStart = m_selEnd = point;
    m_selecting = false;
    updateSelection();
    event->accept();
}

void DocumentView::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    const PagePoint hit = pagePointAt(event->pos());
    if (const AnnotationInfo *info = annotationAt(hit)) {
        const int page = info->page;
        const int id = info->id;
        selectAnnotation(page, id);
        menu.addAction(tr("Modifier le commentaire…"), this, [this, page, id] { emit annotationEditRequested(page, id); });
        menu.addAction(tr("Supprimer l'annotation"), this, [this, page, id] { emit annotationDeleteRequested(page, id); });
        menu.addSeparator();
    }
    QAction *copy = menu.addAction(tr("Copier"), this, &DocumentView::copySelection);
    copy->setShortcut(QKeySequence::Copy);
    copy->setEnabled(hasSelection());
    const PagePoint point = pagePointAt(event->pos());
    if (point.page >= 0) {
        menu.addAction(tr("Sélectionner toute la page"), this, [this, point] { selectAllOnPage(point.page); });
        if (const PdfLink *link = linkAt(point)) {
            if (link->page < 0) {
                const QString uri = link->uri;
                menu.addAction(tr("Copier l'adresse du lien"), this, [uri] { QApplication::clipboard()->setText(uri); });
            }
        }
    }
    menu.exec(event->globalPos());
}

// --- Outils d'annotation ----------------------------------------------------

void DocumentView::setTool(Tool tool)
{
    if (m_tool == tool)
        return;
    commitInk();
    m_drafting = false;
    m_tool = tool;
    if (tool != Tool::Select) {
        clearSelection();
        selectAnnotation(-1, -1);
    }
    updateCursor(viewport()->mapFromGlobal(QCursor::pos()));
    emit toolChanged(tool);
    viewport()->update();
}

void DocumentView::setStampImage(const QImage &image, const QString &label)
{
    m_stampImage = image;
    m_stampLabel = label;
}

void DocumentView::setPageAnnotations(int page, const QVector<AnnotationInfo> &annotations)
{
    if (annotations.isEmpty())
        m_annotations.remove(page);
    else
        m_annotations.insert(page, annotations);
    if (m_selectedAnnotPage == page) {
        bool stillThere = false;
        for (const AnnotationInfo &info : annotations)
            stillThere = stillThere || info.id == m_selectedAnnot;
        if (!stillThere)
            selectAnnotation(-1, -1);
    }
    viewport()->update();
}

void DocumentView::invalidatePages(const QVector<int> &pages)
{
    for (int page : pages) {
        for (QHash<int, CachedImage> *map : {&m_full, &m_clip}) {
            const auto it = map->find(page);
            if (it != map->end()) {
                m_cacheBytes -= bytesOf(it->image);
                map->erase(it);
            }
        }
    }
    viewport()->update();
    scheduleRequests();
}

void DocumentView::selectAnnotation(int page, int id)
{
    if (m_selectedAnnot == id && m_selectedAnnotPage == page)
        return;
    m_selectedAnnot = id;
    m_selectedAnnotPage = id >= 0 ? page : -1;
    emit annotationSelected(m_selectedAnnotPage, m_selectedAnnot);
    viewport()->update();
}

const AnnotationInfo *DocumentView::annotationAt(const PagePoint &point) const
{
    const auto it = m_annotations.constFind(point.page);
    if (it == m_annotations.cend())
        return nullptr;
    // Les notes sont petites : zone de clic élargie. Dernière dessinée = au-dessus.
    for (auto a = it->crbegin(); a != it->crend(); ++a) {
        const QRectF zone = a->rect.adjusted(-3, -3, 3, 3);
        if (zone.contains(point.point))
            return &*a;
    }
    return nullptr;
}

void DocumentView::commitInk()
{
    m_inkTimer.stop();
    if (m_inkStrokes.isEmpty())
        return;
    const QVector<InkStroke> strokes = m_inkStrokes;
    const int page = m_inkPage;
    m_inkStrokes.clear();
    m_inkPage = -1;
    emit inkRequested(page, strokes, m_annotColor, m_penWidth);
}

bool DocumentView::toolPress(QMouseEvent *event, const PagePoint &point, bool onPage)
{
    if (m_tool == Tool::Select) {
        if (const AnnotationInfo *info = annotationAt(point)) {
            selectAnnotation(info->page, info->id);
            m_movingAnnot = true;
            m_moveRect = info->rect;
            m_moveOffset = point.point - info->rect.topLeft();
            event->accept();
            return true;
        }
        if (m_selectedAnnot >= 0)
            selectAnnotation(-1, -1);
        return false;
    }
    if (!onPage) {
        event->accept();
        return true;
    }
    switch (m_tool) {
    case Tool::Highlight:
    case Tool::Underline:
    case Tool::StrikeOut:
        if (m_text && m_text->hasTextAt(point.page, point.point)) {
            clearSelection();
            m_selMode = PdfDocument::SnapMode::Chars;
            m_selStart = m_selEnd = point;
            m_selecting = true;
        }
        break;
    case Tool::Ink:
        if (m_inkPage >= 0 && m_inkPage != point.page)
            commitInk();
        m_inkTimer.stop();
        m_inkPage = point.page;
        m_inkStrokes.append(InkStroke{point.point});
        m_drafting = true;
        break;
    default:
        m_drafting = true;
        m_draftStart = m_draftEnd = point;
        break;
    }
    event->accept();
    return true;
}

bool DocumentView::toolMove(QMouseEvent *event)
{
    if (m_movingAnnot) {
        const PagePoint point = pagePointAt(event->position());
        if (point.page == m_selectedAnnotPage)
            m_moveRect.moveTopLeft(point.point - m_moveOffset);
        viewport()->update();
        event->accept();
        return true;
    }
    if (m_tool == Tool::Select)
        return false;
    if (m_selecting)
        return false; // sélection de texte : traitement normal
    if (!m_drafting) {
        updateCursor(event->position());
        event->accept();
        return true;
    }
    const PagePoint point = pagePointAt(event->position());
    if (m_tool == Tool::Ink) {
        if (point.page == m_inkPage && !m_inkStrokes.isEmpty())
            m_inkStrokes.last().append(point.point);
    } else {
        // Le brouillon reste sur la page de départ.
        if (point.page == m_draftStart.page)
            m_draftEnd = point;
    }
    viewport()->update();
    event->accept();
    return true;
}

bool DocumentView::toolRelease(QMouseEvent *event)
{
    if (m_movingAnnot) {
        m_movingAnnot = false;
        const bool moved = (event->pos() - m_pressPos).manhattanLength() > QApplication::startDragDistance();
        if (moved && m_selectedAnnot >= 0)
            emit annotationMoveRequested(m_selectedAnnotPage, m_selectedAnnot, m_moveRect);
        viewport()->update();
        event->accept();
        return true;
    }
    if (m_tool == Tool::Select)
        return false;

    const bool textTool = m_tool == Tool::Highlight || m_tool == Tool::Underline || m_tool == Tool::StrikeOut;
    if (textTool) {
        if (m_selecting) {
            m_selecting = false;
            if (hasSelection()) {
                const AnnotationKind kind = m_tool == Tool::Underline ? AnnotationKind::Underline
                                            : m_tool == Tool::StrikeOut ? AnnotationKind::StrikeOut
                                                                        : AnnotationKind::Highlight;
                emit textMarkupRequested(kind, m_selectionRects);
            }
            clearSelection();
        }
        event->accept();
        return true;
    }
    if (!m_drafting) {
        event->accept();
        return true;
    }
    m_drafting = false;
    if (m_tool == Tool::Ink) {
        if (!m_inkStrokes.isEmpty() && m_inkStrokes.last().size() < 2)
            m_inkStrokes.last().append(m_inkStrokes.last().first() + QPointF(0.5, 0.5)); // point simple
        m_inkTimer.start();
        event->accept();
        return true;
    }

    const int page = m_draftStart.page;
    const QPointF a = m_draftStart.point;
    const QPointF b = m_draftEnd.point;
    const bool dragged = (event->pos() - m_pressPos).manhattanLength() > QApplication::startDragDistance();
    const QRectF box = QRectF(a, b).normalized();
    switch (m_tool) {
    case Tool::Note:
        emit noteRequested(page, a);
        break;
    case Tool::FreeText:
        emit freeTextRequested(page, dragged && box.width() > 10 ? box : QRectF(a, QSizeF(220, 28)));
        break;
    case Tool::Line:
    case Tool::Arrow:
        if (dragged)
            emit shapeRequested(m_tool == Tool::Arrow ? AnnotationKind::Arrow : AnnotationKind::Line, page, QRectF(a, b),
                                m_annotColor, m_penWidth);
        break;
    case Tool::Square:
    case Tool::Circle:
        if (dragged && box.width() > 2 && box.height() > 2)
            emit shapeRequested(m_tool == Tool::Circle ? AnnotationKind::Circle : AnnotationKind::Square, page, box, m_annotColor,
                                m_penWidth);
        break;
    case Tool::Stamp: {
        if (m_stampImage.isNull())
            break;
        const double aspect = double(m_stampImage.height()) / std::max(1, m_stampImage.width());
        QRectF rect;
        if (dragged && box.width() > 10)
            rect = QRectF(box.topLeft(), QSizeF(box.width(), box.width() * aspect));
        else
            rect = QRectF(a - QPointF(80, 80 * aspect), QSizeF(160, 160 * aspect));
        emit stampRequested(page, rect, m_stampImage, m_stampLabel);
        break;
    }
    default:
        break;
    }
    viewport()->update();
    event->accept();
    return true;
}

void DocumentView::drawAnnotationOverlay(QPainter &painter, int page, const QRectF &rect)
{
    const double ppp = pixelsPerPoint();
    const auto toView = [&](const QPointF &p) { return QPointF(rect.left() + p.x() * ppp, rect.top() + p.y() * ppp); };
    const auto toViewRect = [&](const QRectF &r) { return QRectF(toView(r.topLeft()), toView(r.bottomRight())).normalized(); };
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Annotation sélectionnée (ou en cours de déplacement).
    if (m_selectedAnnot >= 0 && m_selectedAnnotPage == page) {
        for (const AnnotationInfo &info : m_annotations.value(page)) {
            if (info.id != m_selectedAnnot)
                continue;
            const QRectF r = toViewRect(m_movingAnnot ? m_moveRect : info.rect).adjusted(-2, -2, 2, 2);
            painter.setPen(QPen(palette().color(QPalette::Highlight), 1.5, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(r);
        }
    }

    // Dessin à main levée en attente.
    if (m_inkPage == page && !m_inkStrokes.isEmpty()) {
        QPen pen(m_annotColor, m_penWidth * ppp, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        for (const InkStroke &stroke : m_inkStrokes) {
            QPainterPath path;
            path.moveTo(toView(stroke.first()));
            for (int i = 1; i < stroke.size(); ++i)
                path.lineTo(toView(stroke[i]));
            painter.drawPath(path);
        }
    }

    // Brouillon de forme, de texte ou de tampon.
    if (m_drafting && m_draftStart.page == page && m_tool != Tool::Ink) {
        const QPointF a = toView(m_draftStart.point);
        const QPointF b = toView(m_draftEnd.point);
        QPen pen(m_annotColor, std::max(1.0, m_penWidth * ppp));
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        switch (m_tool) {
        case Tool::Line:
        case Tool::Arrow:
            painter.drawLine(a, b);
            break;
        case Tool::Circle:
            painter.drawEllipse(QRectF(a, b).normalized());
            break;
        case Tool::Square:
            painter.drawRect(QRectF(a, b).normalized());
            break;
        case Tool::FreeText:
            painter.setPen(QPen(palette().color(QPalette::Highlight), 1, Qt::DashLine));
            painter.drawRect(QRectF(a, b).normalized());
            break;
        case Tool::Stamp:
            if (!m_stampImage.isNull()) {
                const double aspect = double(m_stampImage.height()) / std::max(1, m_stampImage.width());
                const QRectF box = QRectF(a, b).normalized();
                const QRectF target = box.width() > 10 ? QRectF(box.topLeft(), QSizeF(box.width(), box.width() * aspect))
                                                       : QRectF(a - QPointF(80, 80 * aspect) * ppp, QSizeF(160, 160 * aspect) * ppp);
                painter.setOpacity(0.7);
                painter.drawImage(target, m_stampImage);
                painter.setOpacity(1.0);
            }
            break;
        default:
            break;
        }
    }
}
