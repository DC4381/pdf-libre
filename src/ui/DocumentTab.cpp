// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "DocumentTab.h"

#include "SearchBar.h"
#include "ThumbnailModel.h"
#include "app/Settings.h"
#include "core/PdfDocument.h"
#include "core/RenderService.h"
#include "core/SearchService.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QFileInfo>
#include <QInputDialog>
#include <QHeaderView>
#include <QMessageBox>
#include <QUrl>
#include <QInputDialog>
#include <QLineEdit>
#include <QListView>
#include <QSplitter>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kPageRole = Qt::UserRole + 1;
}

DocumentTab::DocumentTab(const QString &path, QWidget *parent)
    : QWidget(parent)
    , m_path(QFileInfo(path).absoluteFilePath())
    , m_searchBar(new SearchBar(this))
    , m_splitter(new QSplitter(Qt::Horizontal, this))
    , m_sidebar(new QTabWidget(m_splitter))
    , m_thumbnails(new QListView(m_sidebar))
    , m_outline(new QTreeWidget(m_sidebar))
    , m_comments(new QTreeWidget(m_sidebar))
    , m_view(new DocumentView(m_splitter))
    , m_thumbModel(new ThumbnailModel(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_searchBar);
    layout->addWidget(m_splitter, 1);
    m_searchBar->hide();

    m_thumbnails->setModel(m_thumbModel);
    m_thumbnails->setViewMode(QListView::IconMode);
    m_thumbnails->setFlow(QListView::TopToBottom);
    m_thumbnails->setWrapping(false);
    m_thumbnails->setMovement(QListView::Static);
    m_thumbnails->setResizeMode(QListView::Adjust);
    m_thumbnails->setUniformItemSizes(true);
    const QSize thumb = m_thumbModel->thumbnailSize();
    m_thumbnails->setIconSize(thumb + QSize(2, 2));
    m_thumbnails->setGridSize(QSize(thumb.width() + 24, thumb.height() + 34));
    m_thumbnails->setAccessibleName(tr("Miniatures des pages"));

    m_outline->setHeaderHidden(true);
    m_outline->setAccessibleName(tr("Signets du document"));
    m_outline->header()->setStretchLastSection(true);

    m_comments->setHeaderLabels({tr("Page"), tr("Auteur"), tr("Commentaire")});
    m_comments->setRootIsDecorated(false);
    m_comments->setAccessibleName(tr("Commentaires du document"));
    m_comments->header()->setStretchLastSection(true);

    m_sidebar->addTab(m_thumbnails, tr("Miniatures"));
    m_sidebar->addTab(m_outline, tr("Signets"));
    m_sidebar->addTab(m_comments, tr("Commentaires"));
    m_sidebar->setTabVisible(2, false);
    m_sidebar->setDocumentMode(true);
    m_sidebar->setMinimumWidth(150);

    m_splitter->addWidget(m_sidebar);
    m_splitter->addWidget(m_view);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({190, 900});
    m_splitter->setChildrenCollapsible(false);
    setSidebarVisible(Settings::instance().sidebarVisible());
    setFocusProxy(m_view);

    m_pushTimer.setSingleShot(true);
    m_pushTimer.setInterval(0);
    connect(&m_pushTimer, &QTimer::timeout, this, &DocumentTab::pushRequests);

    connect(m_view, &DocumentView::requestsChanged, this, [this](const QVector<RenderRequest> &requests) {
        m_urgent = requests;
        pushRequests();
    });
    connect(m_view, &DocumentView::currentPageChanged, this, [this](int page) {
        const QModelIndex index = m_thumbModel->index(page);
        m_thumbnails->setCurrentIndex(index);
        if (m_sidebar->isVisible())
            m_thumbnails->scrollTo(index);
        emit pageChanged(page, m_pageCount);
    });
    connect(m_view, &DocumentView::zoomChanged, this, &DocumentTab::zoomChanged);
    connect(m_view, &DocumentView::externalLinkActivated, this, [this](const QString &uri) {
        // Un PDF reçu peut pointer n'importe où : confirmation avant d'ouvrir.
        const QUrl url(uri);
        const QString scheme = url.scheme().toLower();
        if (scheme != QLatin1String("http") && scheme != QLatin1String("https") && scheme != QLatin1String("mailto")) {
            QMessageBox::information(this, tr("Lien"), tr("Ce type de lien n'est pas pris en charge :\n%1").arg(uri));
            return;
        }
        const auto answer = QMessageBox::question(this, tr("Ouvrir le lien"),
                                                  tr("Ce document veut ouvrir :\n\n%1\n\nContinuer ?").arg(uri),
                                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer == QMessageBox::Yes)
            QDesktopServices::openUrl(url);
    });
    connect(m_thumbModel, &ThumbnailModel::thumbnailNeeded, this, [this](int page) {
        m_thumbQueue.prepend(page);
        m_pushTimer.start();
    });
    connect(m_thumbnails, &QListView::clicked, this, [this](const QModelIndex &index) {
        m_view->goToPage(index.row());
    });
    connect(m_thumbnails, &QListView::activated, this, [this](const QModelIndex &index) {
        m_view->goToPage(index.row());
        m_view->setFocus();
    });
    connect(m_outline, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) {
        const int page = item->data(0, kPageRole).toInt();
        if (page >= 0)
            m_view->goToPage(page);
    });
    connect(m_comments, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) {
        const int page = item->data(0, kPageRole).toInt();
        const int id = item->data(0, kPageRole + 1).toInt();
        m_view->goToPage(page);
        m_view->selectAnnotation(page, id);
    });
    connect(m_comments, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
        const int page = item->data(0, kPageRole).toInt();
        const int id = item->data(0, kPageRole + 1).toInt();
        m_view->goToPage(page);
        m_view->selectAnnotation(page, id);
        editAnnotation(page, id, QString());
    });
    connect(m_view, &DocumentView::annotationSelected, this, [this](int page, int id) {
        for (int i = 0; i < m_comments->topLevelItemCount(); ++i) {
            QTreeWidgetItem *item = m_comments->topLevelItem(i);
            if (item->data(0, kPageRole).toInt() == page && item->data(0, kPageRole + 1).toInt() == id) {
                m_comments->setCurrentItem(item);
                return;
            }
        }
        m_comments->setCurrentItem(nullptr);
    });

    // Outils d'annotation : la vue décrit le geste, l'onglet l'applique.
    connect(m_view, &DocumentView::textMarkupRequested, this, [this](AnnotationKind kind, const QHash<int, QVector<QRectF>> &byPage) {
        if (!m_render)
            return;
        const QColor color = m_view->annotationColor();
        m_render->edit([kind, byPage, color](PdfDocument &doc) {
            QVector<int> pages;
            for (auto it = byPage.cbegin(); it != byPage.cend(); ++it) {
                if (doc.addTextMarkup(it.key(), kind, it.value(), color) > 0)
                    pages.append(it.key());
            }
            return pages;
        });
    });
    connect(m_view, &DocumentView::noteRequested, this, [this](int page, QPointF point) {
        bool ok = false;
        const QString text = QInputDialog::getMultiLineText(this, tr("Nouvelle note"), tr("Texte de la note :"), {}, &ok);
        if (ok && !text.trimmed().isEmpty())
            addNote(page, point, text);
    });
    connect(m_view, &DocumentView::freeTextRequested, this, [this](int page, QRectF rect) {
        bool ok = false;
        const QString text = QInputDialog::getMultiLineText(this, tr("Ajouter du texte"), tr("Texte à poser sur la page :"), {}, &ok);
        if (ok && !text.trimmed().isEmpty())
            addFreeText(page, rect, text, 12);
    });
    connect(m_view, &DocumentView::inkRequested, this, [this](int page, const QVector<InkStroke> &strokes, const QColor &color, double width) {
        if (!m_render)
            return;
        m_render->edit([page, strokes, color, width](PdfDocument &doc) {
            return doc.addInk(page, strokes, color, width) > 0 ? QVector<int>{page} : QVector<int>{};
        });
    });
    connect(m_view, &DocumentView::shapeRequested, this, [this](AnnotationKind kind, int page, QRectF rect, const QColor &color, double width) {
        if (!m_render)
            return;
        m_render->edit([kind, page, rect, color, width](PdfDocument &doc) {
            return doc.addShape(page, kind, rect, color, QColor(), width) > 0 ? QVector<int>{page} : QVector<int>{};
        });
    });
    connect(m_view, &DocumentView::stampRequested, this, &DocumentTab::addStamp);
    connect(m_view, &DocumentView::annotationDeleteRequested, this, &DocumentTab::deleteAnnotation);
    connect(m_view, &DocumentView::annotationEditRequested, this, [this](int page, int id) { editAnnotation(page, id, QString()); });
    connect(m_view, &DocumentView::annotationMoveRequested, this, [this](int page, int id, QRectF rect) {
        if (!m_render)
            return;
        m_render->edit([page, id, rect](PdfDocument &doc) {
            return doc.setAnnotationRect(page, id, rect) ? QVector<int>{page} : QVector<int>{};
        });
    });

    connect(m_outline, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        const int page = item->data(0, kPageRole).toInt();
        if (page >= 0) {
            m_view->goToPage(page);
            m_view->setFocus();
        }
    });

    connect(m_searchBar, &SearchBar::searchRequested, this, &DocumentTab::startSearch);
    connect(m_searchBar, &SearchBar::nextRequested, this, &DocumentTab::findNext);
    connect(m_searchBar, &SearchBar::previousRequested, this, &DocumentTab::findPrevious);
    connect(m_searchBar, &SearchBar::closed, this, [this] {
        m_searchBar->hide();
        if (m_search)
            m_search->cancel();
        m_hits.clear();
        m_currentHit = {};
        m_view->clearSearch();
        m_view->setFocus();
    });

    // Rechargement automatique quand un autre programme modifie le fichier.
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(500);
    connect(&m_reloadTimer, &QTimer::timeout, this, &DocumentTab::reload);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] {
        if (QDateTime::currentMSecsSinceEpoch() < m_ignoreChangesUntil) {
            if (!m_watcher.files().contains(m_path))
                m_watcher.addPath(m_path);
            return; // modification faite par nous-mêmes
        }
        m_reloadTimer.start();
    });
    m_watcher.addPath(m_path);

    m_restore = Settings::instance().documentState(m_path);
    openDocument();
}

DocumentTab::~DocumentTab() = default;

QString DocumentTab::fileName() const
{
    return QFileInfo(m_path).fileName();
}

QString DocumentTab::documentTitle() const
{
    return m_title.isEmpty() ? fileName() : m_title;
}

void DocumentTab::setSidebarVisible(bool visible)
{
    m_sidebar->setVisible(visible);
}

bool DocumentTab::isSidebarVisible() const
{
    return !m_sidebar->isHidden();
}

void DocumentTab::openDocument()
{
    m_loaded = false;
    m_urgent.clear();
    m_thumbQueue.clear();
    m_search.reset();
    m_render = std::make_unique<RenderService>(m_path, m_password);
    // Les signaux arrivent en différé depuis le thread de rendu : on ignore
    // ceux d'un service remplacé entre-temps (rechargement, mot de passe).
    const int generation = ++m_generation;
    RenderService *service = m_render.get();
    connect(service, &RenderService::opened, this, [this, generation](int count, const QString &title) {
        if (generation == m_generation)
            onOpened(count, title);
    });
    connect(service, &RenderService::openFailed, this, [this, generation](int status, const QString &message) {
        if (generation == m_generation)
            onOpenFailed(status, message);
    });
    connect(service, &RenderService::pageSizesReady, this, [this, generation](int first, const QVector<QSizeF> &sizes) {
        if (generation == m_generation)
            onPageSizes(first, sizes);
    });
    connect(service, &RenderService::rendered, this, [this, generation](const RenderRequest &request, const QImage &image) {
        if (generation == m_generation)
            onRendered(request, image);
    });
    connect(service, &RenderService::outlineReady, this, [this, generation](const OutlineList &outline) {
        if (generation == m_generation)
            onOutline(outline);
    });
    connect(service, &RenderService::editableChanged, this, [this, generation](bool editable) {
        if (generation != m_generation)
            return;
        m_editable = editable;
        emit editStateChanged();
    });
    connect(service, &RenderService::annotationsChanged, this, [this, generation](int page, const QVector<AnnotationInfo> &list) {
        if (generation == m_generation)
            onAnnotations(page, list);
    });
    connect(service, &RenderService::pagesChanged, this, [this, generation](const QVector<int> &pages) {
        if (generation != m_generation)
            return;
        m_view->invalidatePages(pages);
        for (int page : pages)
            m_thumbModel->invalidate(page);
    });
    connect(service, &RenderService::editStateChanged, this, [this, generation](bool canUndo, bool canRedo, bool modified) {
        if (generation != m_generation)
            return;
        m_canUndo = canUndo;
        m_canRedo = canRedo;
        m_modified = modified;
        emit editStateChanged();
    });
    connect(service, &RenderService::saveFinished, this, [this, generation](bool ok, const QString &path, const QString &error) {
        if (generation != m_generation)
            return;
        if (!m_textDocument) {
            m_textDocument = std::make_unique<PdfDocument>();
            if (m_textDocument->open(m_path, m_password) == PdfDocument::OpenStatus::Ok)
                m_view->setTextDocument(m_textDocument.get());
            else
                m_textDocument.reset();
        }
        if (ok) {
            m_modified = false;
            emit editStateChanged();
            emit saved(path);
        } else {
            emit saveFailed(error);
        }
    });
    service->setAuthor(author());
    m_annotations.clear();
    m_editable = false;
    m_modified = false;
    m_canUndo = m_canRedo = false;
}

void DocumentTab::onOpened(int pageCount, const QString &title)
{
    m_pageCount = pageCount;
    m_title = title;
    m_view->reset(pageCount);
    m_thumbModel->reset(pageCount);
    emit titleChanged();
}

void DocumentTab::onOpenFailed(int status, const QString &message)
{
    const auto openStatus = static_cast<PdfDocument::OpenStatus>(status);
    if (openStatus == PdfDocument::OpenStatus::NeedsPassword || openStatus == PdfDocument::OpenStatus::WrongPassword) {
        const QString prompt = openStatus == PdfDocument::OpenStatus::WrongPassword
                                   ? tr("Mot de passe incorrect. Réessayez :")
                                   : tr("Ce document est protégé. Saisissez son mot de passe :");
        bool ok = false;
        const QString password = QInputDialog::getText(this, fileName(), prompt, QLineEdit::Password, {}, &ok);
        if (ok && !password.isEmpty()) {
            m_password = password.toUtf8();
            // L'ancien service a terminé son thread ; on le remplace hors de son signal.
            QTimer::singleShot(0, this, &DocumentTab::openDocument);
            return;
        }
        emit loadFailed(tr("Ouverture annulée : mot de passe requis."));
        return;
    }
    emit loadFailed(message.isEmpty() ? tr("Le fichier est illisible ou n'est pas un PDF valide.") : message);
}

void DocumentTab::onPageSizes(int first, const QVector<QSizeF> &sizes)
{
    m_view->setPageSizes(first, sizes);
    if (first == 0 && !m_loaded) {
        m_loaded = true;
        // Instance réservée au texte (sélection, liens), sur ce thread.
        m_view->setTextDocument(nullptr);
        m_textDocument = std::make_unique<PdfDocument>();
        if (m_textDocument->open(m_path, m_password) == PdfDocument::OpenStatus::Ok)
            m_view->setTextDocument(m_textDocument.get());
        else
            m_textDocument.reset();
        if (m_restore.valid) {
            const auto mode = static_cast<DocumentView::ZoomMode>(m_restore.zoomMode);
            if (mode == DocumentView::ZoomMode::Custom)
                m_view->setZoom(m_restore.zoom);
            else
                m_view->setZoomMode(mode);
            m_view->goToPage(std::clamp(m_restore.page, 0, m_pageCount - 1));
        }
        emit pageChanged(m_view->currentPage(), m_pageCount);
        emit zoomChanged(m_view->zoom(), m_view->zoomMode());
        emit loaded();
    }
}

void DocumentTab::onRendered(const RenderRequest &request, const QImage &image)
{
    if (request.thumbnail) {
        m_thumbQueue.removeAll(request.page);
        QImage thumb = image;
        thumb.setDevicePixelRatio(devicePixelRatioF()); // net sur écran haute densité (Surface)
        m_thumbModel->setThumbnail(request.page, thumb);
    } else {
        m_view->addImage(request, image);
    }
}

void DocumentTab::onOutline(const OutlineList &outline)
{
    m_outlineData = outline;
    m_outline->clear();
    addOutlineItems(nullptr, outline);
    const bool hasOutline = m_outline->topLevelItemCount() > 0;
    m_sidebar->setTabVisible(1, hasOutline);
    if (hasOutline && m_outline->topLevelItemCount() > 0)
        m_sidebar->setCurrentIndex(1);
}

void DocumentTab::addOutlineItems(QTreeWidgetItem *parent, const OutlineList &items)
{
    for (const OutlineItem &entry : items) {
        auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_outline);
        item->setText(0, entry.title);
        item->setToolTip(0, entry.page >= 0 ? tr("%1 (page %2)").arg(entry.title).arg(entry.page + 1) : entry.title);
        item->setData(0, kPageRole, entry.page);
        addOutlineItems(item, entry.children);
        item->setExpanded(entry.open);
    }
}

void DocumentTab::pushRequests()
{
    if (!m_render)
        return;
    const int thumbWidth = int(std::lround(m_thumbModel->thumbnailSize().width() * devicePixelRatioF()));
    QVector<RenderRequest> background;
    background.reserve(m_thumbQueue.size());
    for (int page : std::as_const(m_thumbQueue))
        background.append(RenderRequest{page, thumbWidth, true, {}});
    m_render->setRequests(m_urgent, background);
}

void DocumentTab::showSearch()
{
    m_searchBar->activate();
}

void DocumentTab::search(const QString &text)
{
    m_searchBar->activate();
    m_searchBar->setText(text);
    startSearch(text);
}

void DocumentTab::startSearch(const QString &text)
{
    m_hits.clear();
    m_currentHit = {};
    m_view->clearSearch();
    m_searchText = text;
    if (text.trimmed().isEmpty() || !m_loaded) {
        if (m_search)
            m_search->cancel();
        m_searchBar->setStatus({});
        return;
    }
    if (!m_search) {
        m_search = std::make_unique<SearchService>(m_path, m_password);
        connect(m_search.get(), &SearchService::pageResults, this, [this](int id, int page, const QVector<QRectF> &hits) {
            if (id != m_searchId)
                return;
            m_hits.insert(page, hits);
            m_view->setSearchHits(page, hits);
            if (m_currentHit.page < 0)
                selectHit(Hit{page, 0});
            updateSearchStatus();
        });
        connect(m_search.get(), &SearchService::progress, this, [this](int id, int done, int count) {
            if (id == m_searchId && m_hits.isEmpty())
                m_searchBar->setStatus(tr("Recherche… %1 %").arg(done * 100 / std::max(1, count)));
        });
        connect(m_search.get(), &SearchService::finished, this, [this](int id) {
            if (id != m_searchId)
                return;
            m_searchFinished = true;
            updateSearchStatus();
        });
    }
    m_searchFinished = false;
    m_searchId = m_search->search(text, m_view->currentPage());
    m_searchBar->setStatus(tr("Recherche…"));
}

int DocumentTab::totalHits() const
{
    int total = 0;
    for (const auto &hits : m_hits)
        total += hits.size();
    return total;
}

void DocumentTab::updateSearchStatus()
{
    const int total = totalHits();
    if (total == 0) {
        m_searchBar->setStatus(m_searchFinished ? tr("Aucun résultat") : tr("Recherche…"));
        return;
    }
    int position = 0;
    for (auto it = m_hits.cbegin(); it != m_hits.cend(); ++it) {
        if (it.key() < m_currentHit.page)
            position += it->size();
    }
    position += m_currentHit.index + 1;
    const QString suffix = m_searchFinished ? QString() : tr(" (recherche en cours)");
    m_searchBar->setStatus(tr("%1 sur %2%3").arg(position).arg(total).arg(suffix));
}

void DocumentTab::selectHit(const Hit &hit)
{
    const auto it = m_hits.constFind(hit.page);
    if (it == m_hits.cend() || hit.index < 0 || hit.index >= it->size())
        return;
    m_currentHit = hit;
    m_view->setCurrentHit(hit.page, it->at(hit.index));
    updateSearchStatus();
}

void DocumentTab::findNext()
{
    if (m_hits.isEmpty()) {
        if (!m_searchBar->isVisible() || m_searchBar->text() != m_searchText)
            showSearch();
        return;
    }
    auto it = m_hits.constFind(m_currentHit.page);
    if (it != m_hits.cend() && m_currentHit.index + 1 < it->size()) {
        selectHit(Hit{m_currentHit.page, m_currentHit.index + 1});
        return;
    }
    it = std::as_const(m_hits).upperBound(m_currentHit.page);
    if (it == m_hits.cend())
        it = m_hits.cbegin();
    selectHit(Hit{it.key(), 0});
}

void DocumentTab::findPrevious()
{
    if (m_hits.isEmpty()) {
        showSearch();
        return;
    }
    if (m_currentHit.index > 0) {
        selectHit(Hit{m_currentHit.page, m_currentHit.index - 1});
        return;
    }
    auto it = std::as_const(m_hits).lowerBound(m_currentHit.page);
    if (it == m_hits.cbegin())
        it = m_hits.cend();
    --it;
    selectHit(Hit{it.key(), int(it->size()) - 1});
}

void DocumentTab::saveState() const
{
    if (!m_loaded)
        return;
    DocumentState state;
    state.page = m_view->currentPage();
    state.zoom = m_view->zoom();
    state.zoomMode = static_cast<int>(m_view->zoomMode());
    state.valid = true;
    Settings::instance().setDocumentState(m_path, state);
}

void DocumentTab::releaseFile()
{
    ++m_generation; // les signaux tardifs des services fermés sont ignorés
    m_render.reset();
    m_search.reset();
    m_view->setTextDocument(nullptr);
    m_textDocument.reset();
}

void DocumentTab::reload()
{
    if (!QFileInfo::exists(m_path))
        return; // Fichier en cours de remplacement : le prochain signal relancera.
    if (!m_watcher.files().contains(m_path))
        m_watcher.addPath(m_path);
    if (m_loaded) {
        m_restore.page = m_view->currentPage();
        m_restore.zoom = m_view->zoom();
        m_restore.zoomMode = static_cast<int>(m_view->zoomMode());
        m_restore.valid = true;
    }
    m_hits.clear();
    m_currentHit = {};
    m_view->setTextDocument(nullptr);
    m_textDocument.reset();
    openDocument();
}

// --- Annotations -------------------------------------------------------------

void DocumentTab::onAnnotations(int page, const QVector<AnnotationInfo> &annotations)
{
    if (annotations.isEmpty())
        m_annotations.remove(page);
    else
        m_annotations.insert(page, annotations);
    m_view->setPageAnnotations(page, annotations);
    refreshCommentList();
}

namespace {
QString kindLabel(AnnotationKind kind)
{
    switch (kind) {
    case AnnotationKind::Highlight:
        return DocumentTab::tr("Surlignage");
    case AnnotationKind::Underline:
        return DocumentTab::tr("Soulignement");
    case AnnotationKind::StrikeOut:
        return DocumentTab::tr("Texte barré");
    case AnnotationKind::Note:
        return DocumentTab::tr("Note");
    case AnnotationKind::FreeText:
        return DocumentTab::tr("Texte");
    case AnnotationKind::Ink:
        return DocumentTab::tr("Dessin");
    case AnnotationKind::Line:
    case AnnotationKind::Arrow:
        return DocumentTab::tr("Ligne");
    case AnnotationKind::Square:
        return DocumentTab::tr("Rectangle");
    case AnnotationKind::Circle:
        return DocumentTab::tr("Ellipse");
    case AnnotationKind::Stamp:
        return DocumentTab::tr("Tampon");
    default:
        return DocumentTab::tr("Annotation");
    }
}
} // namespace

void DocumentTab::refreshCommentList()
{
    m_comments->clear();
    for (auto it = m_annotations.cbegin(); it != m_annotations.cend(); ++it) {
        for (const AnnotationInfo &info : it.value()) {
            auto *item = new QTreeWidgetItem(m_comments);
            item->setText(0, QString::number(info.page + 1));
            item->setText(1, info.author);
            const QString text = info.contents.simplified();
            item->setText(2, text.isEmpty() ? kindLabel(info.kind) : text);
            item->setToolTip(2, kindLabel(info.kind) + (info.modified.isValid() ? QStringLiteral(", ") + info.modified.toString(QStringLiteral("dd/MM/yyyy HH:mm")) : QString())
                                    + (info.contents.isEmpty() ? QString() : QStringLiteral("\n") + info.contents));
            item->setData(0, kPageRole, info.page);
            item->setData(0, kPageRole + 1, info.id);
        }
    }
    m_sidebar->setTabVisible(2, m_comments->topLevelItemCount() > 0);
}

void DocumentTab::undo()
{
    if (!m_render || !m_canUndo)
        return;
    m_render->edit([](PdfDocument &doc) {
        const int page = doc.undo();
        return page >= 0 ? QVector<int>{page} : QVector<int>{};
    });
}

void DocumentTab::redo()
{
    if (!m_render || !m_canRedo)
        return;
    m_render->edit([](PdfDocument &doc) {
        const int page = doc.redo();
        return page >= 0 ? QVector<int>{page} : QVector<int>{};
    });
}

void DocumentTab::save()
{
    if (!m_render || !m_editable)
        return;
    const QString backup = Settings::instance().backupFor(m_path);
    if (backup.isEmpty() || !QFile::copy(m_path, backup)) {
        emit saveFailed(tr("Impossible de sauvegarder l'original avant de l'enregistrer."));
        return;
    }
    m_ignoreChangesUntil = QDateTime::currentMSecsSinceEpoch() + 5000;
    // Les autres instances (recherche, texte) libèrent le fichier : nécessaire
    // sous Windows si l'enregistrement remplace le fichier au lieu de le compléter.
    m_search.reset();
    m_view->setTextDocument(nullptr);
    m_textDocument.reset();
    m_render->save(m_path);
}

void DocumentTab::saveAs(const QString &path)
{
    if (!m_render || !m_editable)
        return;
    m_render->save(QFileInfo(path).absoluteFilePath());
}

QString DocumentTab::author() const
{
    QString name = Settings::instance().raw().value(QStringLiteral("annotations/author")).toString();
    if (name.isEmpty()) {
        name = qEnvironmentVariable("USERNAME");
        if (name.isEmpty())
            name = qEnvironmentVariable("USER");
    }
    return name;
}

void DocumentTab::setAuthor(const QString &author)
{
    Settings::instance().raw().setValue(QStringLiteral("annotations/author"), author);
    if (m_render)
        m_render->setAuthor(author);
}

void DocumentTab::addNote(int page, QPointF point, const QString &text)
{
    if (!m_render)
        return;
    const QColor color = m_view->annotationColor();
    m_render->edit([page, point, text, color](PdfDocument &doc) {
        return doc.addNote(page, point, text, color) > 0 ? QVector<int>{page} : QVector<int>{};
    });
}

void DocumentTab::addFreeText(int page, const QRectF &rect, const QString &text, double fontSize)
{
    if (!m_render)
        return;
    const QColor color = m_view->annotationColor();
    m_render->edit([page, rect, text, fontSize, color](PdfDocument &doc) {
        return doc.addFreeText(page, rect, text, fontSize, color) > 0 ? QVector<int>{page} : QVector<int>{};
    });
}

void DocumentTab::addStamp(int page, const QRectF &rect, const QImage &image, const QString &label)
{
    if (!m_render)
        return;
    m_render->edit([page, rect, image, label](PdfDocument &doc) {
        return doc.addStampImage(page, rect, image, label) > 0 ? QVector<int>{page} : QVector<int>{};
    });
}

void DocumentTab::editAnnotation(int page, int id, const QString &text)
{
    if (!m_render)
        return;
    QString newText = text;
    if (newText.isNull()) {
        QString current;
        for (const AnnotationInfo &info : m_annotations.value(page)) {
            if (info.id == id)
                current = info.contents;
        }
        bool ok = false;
        newText = QInputDialog::getMultiLineText(this, tr("Commentaire"), tr("Texte du commentaire :"), current, &ok);
        if (!ok)
            return;
    }
    m_render->edit([page, id, newText](PdfDocument &doc) {
        return doc.setAnnotationContents(page, id, newText) ? QVector<int>{page} : QVector<int>{};
    });
}

void DocumentTab::deleteAnnotation(int page, int id)
{
    if (!m_render)
        return;
    m_render->edit([page, id](PdfDocument &doc) {
        return doc.deleteAnnotation(page, id) ? QVector<int>{page} : QVector<int>{};
    });
}
