// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QImage>
#include <QPixmap>
#include <QSet>
#include <QSize>

// Modèle des miniatures. Une miniature n'est demandée que lorsque la vue la
// dessine : un document de 2 000 pages ne coûte rien tant qu'on ne fait pas
// défiler le panneau.
class ThumbnailModel : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit ThumbnailModel(QObject *parent = nullptr);

    void reset(int pageCount);
    void setThumbnail(int page, const QImage &image);
    QSize thumbnailSize() const { return m_size; }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;

signals:
    void thumbnailNeeded(int page) const;

private:
    int m_pageCount = 0;
    QSize m_size{110, 156};
    QHash<int, QImage> m_images;
    mutable QSet<int> m_requested;
    mutable QPixmap m_placeholder;
};
