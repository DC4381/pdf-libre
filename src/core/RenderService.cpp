// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "RenderService.h"

#include "PdfDocument.h"

#include <algorithm>

namespace {
// Nombre de tailles de pages calculées entre deux rendus urgents.
constexpr int kSizeChunk = 64;
}

RenderService::RenderService(const QString &path, const QByteArray &password, QObject *parent)
    : QObject(parent)
    , m_path(path)
    , m_password(password)
{
    m_thread = std::thread([this] { run(); });
}

RenderService::~RenderService()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}

void RenderService::setRequests(const QVector<RenderRequest> &urgent, const QVector<RenderRequest> &background)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_urgent = urgent;
        m_background = background;
    }
    m_wake.notify_all();
}

void RenderService::run()
{
    PdfDocument doc;
    const auto status = doc.open(m_path, m_password);
    if (status != PdfDocument::OpenStatus::Ok) {
        emit openFailed(static_cast<int>(status), doc.lastError());
        return;
    }

    const int count = doc.pageCount();
    QString title = doc.metadata("info:Title");
    emit opened(count, title);

    // Tailles déjà connues, pour ne pas recharger une page avant son rendu.
    QVector<QSizeF> known(count);
    const auto sizeOf = [&](int page) {
        if (!known[page].isValid())
            known[page] = doc.pageSize(page);
        return known[page];
    };
    // Première salve de tailles immédiatement, pour une mise en page
    // correcte des premières pages avant même le premier rendu.
    int sizesDone = 0;
    {
        QVector<QSizeF> sizes;
        const int end = std::min(count, 16);
        for (int i = 0; i < end; ++i)
            sizes.append(sizeOf(i));
        sizesDone = end;
        emit pageSizesReady(0, sizes);
    }
    bool outlineDone = false;

    for (;;) {
        RenderRequest request;
        bool computeSizes = false;
        bool loadOutline = false;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [&] {
                return m_stop || !m_urgent.isEmpty() || sizesDone < count || !outlineDone || !m_background.isEmpty();
            });
            if (m_stop)
                return;
            if (!m_urgent.isEmpty()) {
                request = m_urgent.takeFirst();
            } else if (!outlineDone) {
                loadOutline = true;
            } else if (sizesDone < count) {
                computeSizes = true;
            } else {
                request = m_background.takeFirst();
            }
        }

        if (loadOutline) {
            emit outlineReady(doc.outline());
            outlineDone = true;
        } else if (computeSizes) {
            QVector<QSizeF> sizes;
            const int end = std::min(count, sizesDone + kSizeChunk);
            for (int i = sizesDone; i < end; ++i)
                sizes.append(sizeOf(i));
            emit pageSizesReady(sizesDone, sizes);
            sizesDone = end;
        } else if (request.page >= 0 && request.page < count && request.width > 0) {
            const QSizeF size = sizeOf(request.page);
            const double scale = request.width / size.width();
            QImage image = doc.render(request.page, scale, request.clip);
            if (!image.isNull())
                emit rendered(request, image);
            // Une liste envoyée pendant ce rendu le redemande forcément :
            // on retire le doublon.
            std::lock_guard<std::mutex> lock(m_mutex);
            m_urgent.removeAll(request);
            m_background.removeAll(request);
        }
    }
}
