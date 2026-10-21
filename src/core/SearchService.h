// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QObject>
#include <QRectF>
#include <QString>
#include <QVector>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

// Recherche plein texte dans un thread séparé, avec sa propre instance du
// document, pour ne jamais bloquer l'affichage. Les résultats arrivent page
// par page ; une nouvelle recherche annule la précédente.
class SearchService : public QObject
{
    Q_OBJECT
public:
    SearchService(const QString &path, const QByteArray &password, QObject *parent = nullptr);
    ~SearchService() override;

    // Lance une recherche à partir de `startPage` (parcours circulaire).
    // Renvoie l'identifiant de la recherche, repris dans les signaux.
    int search(const QString &needle, int startPage);
    void cancel();

signals:
    void pageResults(int searchId, int page, const QVector<QRectF> &hits);
    void progress(int searchId, int pagesDone, int pageCount);
    void finished(int searchId);

private:
    void run();

    const QString m_path;
    const QByteArray m_password;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_stop = false;
    bool m_pending = false;
    QString m_needle;
    int m_startPage = 0;
    std::atomic<int> m_currentId{0};
    std::thread m_thread;
};
