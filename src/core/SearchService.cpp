// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SearchService.h"

#include "PdfDocument.h"

namespace {
// Identifiants uniques pour toute l'application : un résultat tardif d'un
// service détruit ne peut pas être confondu avec celui d'un nouveau service.
std::atomic<int> s_nextId{0};
}

SearchService::SearchService(const QString &path, const QByteArray &password, QObject *parent)
    : QObject(parent)
    , m_path(path)
    , m_password(password)
{
    m_thread = std::thread([this] { run(); });
}

SearchService::~SearchService()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_currentId = ++s_nextId;
    m_wake.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}

int SearchService::search(const QString &needle, int startPage)
{
    int id;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        id = ++s_nextId;
        m_currentId = id;
        m_needle = needle;
        m_startPage = startPage;
        m_pending = true;
    }
    m_wake.notify_all();
    return id;
}

void SearchService::cancel()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_currentId = ++s_nextId;
    m_pending = false;
}

void SearchService::run()
{
    // Le document n'est ouvert qu'à la première recherche : aucun coût
    // tant que l'utilisateur ne cherche rien.
    PdfDocument doc;
    bool opened = false;

    for (;;) {
        QString needle;
        int start = 0;
        int id = 0;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [&] { return m_stop || m_pending; });
            if (m_stop)
                return;
            needle = m_needle;
            start = m_startPage;
            id = m_currentId;
            m_pending = false;
        }

        if (!opened) {
            opened = doc.open(m_path, m_password) == PdfDocument::OpenStatus::Ok;
            if (!opened) {
                emit finished(id);
                continue;
            }
        }

        const int count = doc.pageCount();
        start = qBound(0, start, count - 1);
        for (int i = 0; i < count; ++i) {
            if (m_currentId != id)
                break;
            const int page = (start + i) % count;
            const QVector<QRectF> hits = doc.search(page, needle);
            if (!hits.isEmpty())
                emit pageResults(id, page, hits);
            if ((i + 1) % 25 == 0 || i + 1 == count)
                emit progress(id, i + 1, count);
        }
        if (m_currentId == id)
            emit finished(id);
    }
}
