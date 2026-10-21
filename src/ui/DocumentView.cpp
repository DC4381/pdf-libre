// SPDX-License-Identifier: AGPL-3.0-or-later
#include "DocumentView.h"

#include <QGestureEvent>
#include <QKeyEvent>
#include <QPainter>
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
}

void DocumentView::reset(int pageCount)
{
    m_sizes = QVector<QSizeF>(pageCount, QSizeF(612, 792));
    m_maxPageWidth = 612;
    m_full.clear();
    m_clip.clear();
    m_cacheBytes = 0;
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

    const double ppp = pixelsPerPoint();
    const auto toView = [&](const QRectF &r) {
        return QRectF(rect.left() + r.left() * ppp, rect.top() + r.top() * ppp, r.width() * ppp, r.height() * ppp);
    };
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
}

void DocumentView::paintEvent(QPaintEvent *)
{
    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), palette().color(QPalette::Mid));
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
    default:
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
