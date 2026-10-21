// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "WelcomeWidget.h"

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

WelcomeWidget::WelcomeWidget(QWidget *parent)
    : QWidget(parent)
    , m_open(new QPushButton(tr("Ouvrir un document…"), this))
    , m_recentLabel(new QLabel(tr("Documents récents"), this))
    , m_recent(new QListWidget(this))
    , m_clear(new QPushButton(tr("Effacer l'historique"), this))
{
    auto *outer = new QHBoxLayout(this);
    auto *column = new QVBoxLayout;
    outer->addStretch(1);
    outer->addLayout(column, 3);
    outer->addStretch(1);

    auto *title = new QLabel(tr("CARMO PDF"), this);
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 2.0);
    titleFont.setBold(true);
    title->setFont(titleFont);
    auto *hint = new QLabel(tr("Glissez un fichier PDF dans cette fenêtre, ou :"), this);

    m_open->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    m_open->setDefault(true);
    m_open->setMinimumHeight(44); // cible tactile confortable
    QFont recentFont = m_recentLabel->font();
    recentFont.setBold(true);
    m_recentLabel->setFont(recentFont);
    m_recent->setAccessibleName(tr("Documents récents"));
    m_recent->setMinimumHeight(220);
    m_clear->setFlat(true);

    column->addStretch(1);
    column->addWidget(title);
    column->addWidget(hint);
    column->addWidget(m_open, 0, Qt::AlignLeft);
    column->addSpacing(24);
    column->addWidget(m_recentLabel);
    column->addWidget(m_recent, 2);
    column->addWidget(m_clear, 0, Qt::AlignRight);
    column->addStretch(1);

    connect(m_open, &QPushButton::clicked, this, &WelcomeWidget::openRequested);
    connect(m_clear, &QPushButton::clicked, this, &WelcomeWidget::clearRecentRequested);
    connect(m_recent, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        emit recentActivated(item->data(Qt::UserRole).toString());
    });
    connect(m_recent, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        emit recentActivated(item->data(Qt::UserRole).toString());
    });
}

void WelcomeWidget::setRecentFiles(const QStringList &files)
{
    m_recent->clear();
    for (const QString &path : files) {
        const QFileInfo info(path);
        auto *item = new QListWidgetItem(style()->standardIcon(QStyle::SP_FileIcon),
                                         info.fileName() + QStringLiteral("  ·  ") + QDir::toNativeSeparators(info.absolutePath()), m_recent);
        item->setData(Qt::UserRole, path);
        item->setToolTip(QDir::toNativeSeparators(path));
        if (!info.exists())
            item->setForeground(palette().color(QPalette::Disabled, QPalette::Text));
    }
    const bool any = !files.isEmpty();
    m_recentLabel->setVisible(any);
    m_recent->setVisible(any);
    m_clear->setVisible(any);
}
