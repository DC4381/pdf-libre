// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ThumbnailModel.h"

#include <QPainter>
#include <QPixmap>

ThumbnailModel::ThumbnailModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void ThumbnailModel::reset(int pageCount)
{
    beginResetModel();
    m_pageCount = pageCount;
    m_images.clear();
    m_requested.clear();
    endResetModel();
}

void ThumbnailModel::setThumbnail(int page, const QImage &image)
{
    if (page < 0 || page >= m_pageCount)
        return;
    // Cadre gris pour distinguer les pages blanches du fond du panneau.
    QImage framed(image.size() + QSize(2, 2), QImage::Format_RGB32);
    framed.setDevicePixelRatio(image.devicePixelRatio());
    framed.fill(QColor(160, 160, 160));
    QPainter painter(&framed);
    painter.drawImage(QPoint(1, 1), image);
    painter.end();
    m_images.insert(page, framed);
    const QModelIndex idx = index(page);
    emit dataChanged(idx, idx, {Qt::DecorationRole});
}

int ThumbnailModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_pageCount;
}

QVariant ThumbnailModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_pageCount)
        return {};
    const int page = index.row();
    switch (role) {
    case Qt::DisplayRole:
        return QString::number(page + 1);
    case Qt::AccessibleTextRole:
    case Qt::ToolTipRole:
        return tr("Page %1").arg(page + 1);
    case Qt::DecorationRole: {
        const auto it = m_images.constFind(page);
        if (it != m_images.cend())
            return QPixmap::fromImage(*it);
        if (!m_requested.contains(page)) {
            m_requested.insert(page);
            emit thumbnailNeeded(page);
        }
        if (m_placeholder.isNull()) {
            m_placeholder = QPixmap(m_size.width() + 2, int(m_size.width() * 1.414) + 2);
            m_placeholder.fill(QColor(160, 160, 160));
            QPainter painter(&m_placeholder);
            painter.fillRect(m_placeholder.rect().adjusted(1, 1, -1, -1), Qt::white);
        }
        return m_placeholder;
    }
    case Qt::TextAlignmentRole:
        return int(Qt::AlignHCenter | Qt::AlignTop);
    default:
        return {};
    }
}
