// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Types.h"

#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QSizeF>
#include <QString>
#include <QVector>

#include <condition_variable>
#include <mutex>
#include <thread>

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

signals:
    void opened(int pageCount, const QString &title);
    void openFailed(int status, const QString &message);
    void pageSizesReady(int first, const QVector<QSizeF> &sizes);
    void outlineReady(const OutlineList &outline);
    void rendered(const RenderRequest &request, const QImage &image);

private:
    void run();

    const QString m_path;
    const QByteArray m_password;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_stop = false;
    QVector<RenderRequest> m_urgent;
    QVector<RenderRequest> m_background;
    std::thread m_thread;
};
