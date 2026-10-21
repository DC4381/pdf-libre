// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "DocumentView.h"
#include "app/Settings.h"
#include "core/Types.h"

#include <QFileSystemWatcher>
#include <QMap>
#include <QPointer>
#include <QTimer>
#include <QVector>
#include <QWidget>

#include <memory>

class QListView;
class QSplitter;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;
class PdfDocument;
class RenderService;
class SearchBar;
class SearchService;
class ThumbnailModel;

// Un document ouvert dans un onglet : barre de recherche, panneau latéral
// (miniatures, signets) et vue des pages.
class DocumentTab : public QWidget
{
    Q_OBJECT
public:
    explicit DocumentTab(const QString &path, QWidget *parent = nullptr);
    ~DocumentTab() override;

    QString path() const { return m_path; }
    QString fileName() const;
    QString documentTitle() const;
    QByteArray password() const { return m_password; }
    bool isLoaded() const { return m_loaded; }
    int pageCount() const { return m_pageCount; }
    DocumentView *view() const { return m_view; }

    void setSidebarVisible(bool visible);
    bool isSidebarVisible() const;

    void showSearch();
    void search(const QString &text);
    void findNext();
    void findPrevious();
    void saveState() const;
    void reload();

signals:
    void loaded();
    void loadFailed(const QString &message);
    void titleChanged();
    void pageChanged(int page, int pageCount);
    void zoomChanged(double zoom, DocumentView::ZoomMode mode);

private:
    struct Hit
    {
        int page = -1;
        int index = -1;
    };

    void openDocument();
    void onOpened(int pageCount, const QString &title);
    void onOpenFailed(int status, const QString &message);
    void onPageSizes(int first, const QVector<QSizeF> &sizes);
    void onRendered(const RenderRequest &request, const QImage &image);
    void onOutline(const OutlineList &outline);
    void addOutlineItems(QTreeWidgetItem *parent, const OutlineList &items);
    void pushRequests();
    void startSearch(const QString &text);
    void selectHit(const Hit &hit);
    void updateSearchStatus();
    int totalHits() const;

    QString m_path;
    QByteArray m_password;
    QString m_title;
    bool m_loaded = false;
    int m_pageCount = 0;
    int m_generation = 0;
    DocumentState m_restore;

    std::unique_ptr<RenderService> m_render;
    std::unique_ptr<PdfDocument> m_textDocument;
    std::unique_ptr<SearchService> m_search;

    SearchBar *m_searchBar;
    QSplitter *m_splitter;
    QTabWidget *m_sidebar;
    QListView *m_thumbnails;
    QTreeWidget *m_outline;
    DocumentView *m_view;
    ThumbnailModel *m_thumbModel;

    QVector<RenderRequest> m_urgent;
    QVector<int> m_thumbQueue;
    QTimer m_pushTimer;

    int m_searchId = 0;
    bool m_searchFinished = true;
    QString m_searchText;
    QMap<int, QVector<QRectF>> m_hits;
    Hit m_currentHit;

    QFileSystemWatcher m_watcher;
    QTimer m_reloadTimer;
};
