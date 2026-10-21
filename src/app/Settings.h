// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QSettings>
#include <QString>
#include <QStringList>

#include <memory>

// État mémorisé pour un document : reprise à la dernière page et au dernier zoom.
struct DocumentState
{
    int page = 0;
    double zoom = 1.0;
    int zoomMode = 0; // DocumentView::ZoomMode
    bool valid = false;
};

// Accès centralisé aux réglages.
//
// Mode portable : si un fichier `portable.ini` se trouve à côté de
// l'exécutable, tous les réglages y sont écrits et rien n'est écrit dans le
// registre ni dans le profil utilisateur.
class Settings
{
public:
    static Settings &instance();

    bool isPortable() const { return m_portable; }
    QSettings &raw() { return *m_settings; }

    QStringList recentFiles() const;
    void addRecentFile(const QString &path);
    void removeRecentFile(const QString &path);
    void clearRecentFiles();

    DocumentState documentState(const QString &path) const;
    void setDocumentState(const QString &path, const DocumentState &state);

    // Dossier des copies de sauvegarde des originaux, et chemin d'une
    // nouvelle sauvegarde pour `original` (vide si le dossier est inaccessible).
    QString backupDirectory() const;
    QString backupFor(const QString &original);

    bool sidebarVisible() const;
    void setSidebarVisible(bool visible);

private:
    Settings();
    static QString keyFor(const QString &path);

    bool m_portable = false;
    std::unique_ptr<QSettings> m_settings;
};
