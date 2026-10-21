// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QWidget>

class QLabel;
class QListWidget;
class QPushButton;

// Écran affiché quand aucun document n'est ouvert : bouton d'ouverture et
// documents récents. Aucune promotion, aucune actualité.
class WelcomeWidget : public QWidget
{
    Q_OBJECT
public:
    explicit WelcomeWidget(QWidget *parent = nullptr);
    void setRecentFiles(const QStringList &files);

signals:
    void openRequested();
    void recentActivated(const QString &path);
    void clearRecentRequested();

private:
    QPushButton *m_open;
    QLabel *m_recentLabel;
    QListWidget *m_recent;
    QPushButton *m_clear;
};
