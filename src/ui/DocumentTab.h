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
    OutlineList outline() const { return m_outlineData; }
    DocumentView *view() const { return m_view; }

    void setSidebarVisible(bool visible);
    bool isSidebarVisible() const;

    void showSearch();
    void search(const QString &text);

    // Annotations.
    bool isEditable() const { return m_editable; }
    bool isModified() const { return m_modified; }
    bool canUndo() const { return m_canUndo; }
    bool canRedo() const { return m_canRedo; }
    void undo();
    void redo();
    // Enregistre (incrémental si possible) après copie de sauvegarde de l'original.
    void save();
    void saveAs(const QString &path);
    QString author() const;
    void setAuthor(const QString &author);
    // Ajouts sans boîte de dialogue, pour l'interface et les tests.
    void addNote(int page, QPointF point, const QString &text);
    void addFreeText(int page, const QRectF &rect, const QString &text, double fontSize);
    void addStamp(int page, const QRectF &rect, const QImage &image, const QString &label);
    void editAnnotation(int page, int id, const QString &text);
    void deleteAnnotation(int page, int id);
    void findNext();
    void findPrevious();
    void saveState() const;
    void reload();
    // Ferme toutes les instances du document (le fichier peut alors être
    // remplacé par un autre programme) ; reload() rouvre.
    void releaseFile();

signals:
    void loaded();
    void loadFailed(const QString &message);
    void titleChanged();
    void pageChanged(int page, int pageCount);
    void zoomChanged(double zoom, DocumentView::ZoomMode mode);
    void editStateChanged();
    void saved(const QString &path);
    void saveFailed(const QString &message);

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
    void onAnnotations(int page, const QVector<AnnotationInfo> &annotations);
    void refreshCommentList();
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
    OutlineList m_outlineData;

    std::unique_ptr<RenderService> m_render;
    std::unique_ptr<PdfDocument> m_textDocument;
    std::unique_ptr<SearchService> m_search;

    SearchBar *m_searchBar;
    QSplitter *m_splitter;
    QTabWidget *m_sidebar;
    QListView *m_thumbnails;
    QTreeWidget *m_outline;
    QTreeWidget *m_comments;
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
    qint64 m_ignoreChangesUntil = 0;

    bool m_editable = false;
    bool m_modified = false;
    bool m_canUndo = false;
    bool m_canRedo = false;
    QMap<int, QVector<AnnotationInfo>> m_annotations;
};
