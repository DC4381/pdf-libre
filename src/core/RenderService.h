// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Annotations.h"
#include "Types.h"

#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QSizeF>
#include <QString>
#include <QVector>

#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

class PdfDocument;

// Service de rendu : un thread dédié, propriétaire de sa propre instance du
// document. Les demandes urgentes (pages visibles) passent avant le calcul
// des tailles de pages, lui-même prioritaire sur les miniatures.
//
// Chaque appel à setRequests() remplace les demandes en attente : quand
// l'utilisateur fait défiler, les rendus devenus inutiles sont abandonnés.
class RenderService : public QObject
{
    Q_OBJECT
public:
    RenderService(const QString &path, const QByteArray &password, QObject *parent = nullptr);
    ~RenderService() override;

    void setRequests(const QVector<RenderRequest> &urgent, const QVector<RenderRequest> &background);

    // Modification du document, exécutée sur le thread du service avant tout
    // rendu. La fonction renvoie les pages dont l'affichage doit être refait.
    using EditFn = std::function<QVector<int>(PdfDocument &)>;
    void edit(const EditFn &fn);
    void setAuthor(const QString &author);
    void save(const QString &path);

signals:
    void opened(int pageCount, const QString &title);
    void openFailed(int status, const QString &message);
    void pageSizesReady(int first, const QVector<QSizeF> &sizes);
    void outlineReady(const OutlineList &outline);
    void rendered(const RenderRequest &request, const QImage &image);
    void editableChanged(bool editable);
    void pagesChanged(const QVector<int> &pages);
    void annotationsChanged(int page, const QVector<AnnotationInfo> &annotations);
    void editStateChanged(bool canUndo, bool canRedo, bool modified);
    void saveFinished(bool ok, const QString &path, const QString &error);

private:
    void run();

    const QString m_path;
    const QByteArray m_password;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_stop = false;
    QVector<RenderRequest> m_urgent;
    QVector<RenderRequest> m_background;
    QVector<EditFn> m_edits;
    QString m_author;
    bool m_authorChanged = false;
    std::thread m_thread;
};
