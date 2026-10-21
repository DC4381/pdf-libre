// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "core/PdfAssembler.h"

#include <QDialog>
#include <QHash>
#include <QImage>
#include <QListWidget>
#include <QVector>

#include <condition_variable>
#include <mutex>
#include <thread>

class QLabel;
class QPushButton;
class QToolBar;

// Rendu des miniatures de l'organiseur, dans un thread dédié qui garde une
// instance ouverte par document source. Les demandes les plus récentes
// (pages visibles) sont servies en premier.
class OrganizerThumbnails : public QObject
{
    Q_OBJECT
public:
    explicit OrganizerThumbnails(int width, QObject *parent = nullptr);
    ~OrganizerThumbnails() override;
    void request(const PageRef &ref);
    static QString keyOf(const PageRef &ref);

signals:
    void ready(const QString &key, const QImage &image);

private:
    void run();
    const int m_width;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_stop = false;
    QVector<PageRef> m_queue;
    std::thread m_thread;
};

// Liste de miniatures réordonnable par glisser-déposer.
class PageList : public QListWidget
{
    Q_OBJECT
public:
    explicit PageList(QWidget *parent = nullptr);
signals:
    void reordered();
protected:
    void dropEvent(QDropEvent *event) override;
};

// Fenêtre « Organiser les pages » : fusionner, séparer, extraire, réordonner,
// tourner, insérer et supprimer, avec annuler/rétablir. Le résultat est
// reconstruit par PdfAssembler ; l'original est sauvegardé avant écrasement.
class PageOrganizer : public QDialog
{
    Q_OBJECT
public:
    // `path` vide : assemblage libre (fusion), enregistrement « sous » obligatoire.
    PageOrganizer(const QString &path, const QByteArray &password, QWidget *parent = nullptr);
    ~PageOrganizer() override;

    // Opérations, aussi utilisées par les tests.
    bool loadDocument(const QString &path, const QByteArray &password, int insertAt = -1);
    void insertBlankPage(int at = -1);
    void rotateSelection(int degrees);
    void deleteSelection();
    void moveSelection(int delta);
    void selectPages(const QVector<int> &positions);
    QVector<int> selectedPositions() const;
    void undo();
    void redo();
    QVector<PageRef> pages() const { return m_pages; }
    bool isModified() const;
    PdfAssembler::Result saveTo(const QString &path);
    QString savedPath() const { return m_savedPath; }

signals:
    // Émis juste avant de remplacer le fichier d'origine : l'appelant doit
    // fermer ses propres instances du document (verrou Windows).
    void overwriteRequested(const QString &path);
    void documentSaved(const QString &path);

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void buildUi();
    void resetRenderer();
    void setPages(const QVector<PageRef> &pages, bool record = true);
    void rebuildList();
    void renumber();
    void requestVisibleThumbnails();
    void applyThumbnail(QListWidgetItem *item);
    void updateActions();
    void insertFileDialog();
    void extractSelectionDialog();
    void save();
    void saveAs();
    bool confirmSignatures(const QString &target);

    QString m_path;
    QByteArray m_password;
    QString m_savedPath;
    QVector<PageRef> m_pages;
    QVector<QVector<PageRef>> m_history;
    int m_historyIndex = -1;
    int m_nextId = 0;
    QHash<int, PageRef> m_refById;
    QHash<QString, QImage> m_thumbnails;
    OrganizerThumbnails *m_renderer;

    PageList *m_list;
    QToolBar *m_toolBar;
    QLabel *m_status;
    QPushButton *m_saveButton;
    QPushButton *m_saveAsButton;
    QAction *m_undoAct;
    QAction *m_redoAct;
    QAction *m_rotateLeftAct;
    QAction *m_rotateRightAct;
    QAction *m_deleteAct;
    QAction *m_moveUpAct;
    QAction *m_moveDownAct;
    QAction *m_insertFileAct;
    QAction *m_insertBlankAct;
    QAction *m_extractAct;
};
