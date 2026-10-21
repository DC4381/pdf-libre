// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QDir>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QString>

// Nom de fichier temporaire unique dans `directory`, sans créer ni ouvrir le
// fichier : sous Windows, un fichier ouvert par nous ne peut être ni réécrit
// ni supprimé par MuPDF. Vide si le dossier n'est pas accessible en écriture.
inline QString temporaryPathIn(const QString &directory)
{
    const QFileInfo info(directory);
    if (!info.isDir() || !info.isWritable())
        return {};
    for (int attempt = 0; attempt < 100; ++attempt) {
        const QString name = QStringLiteral(".carmopdf-%1.tmp").arg(QRandomGenerator::global()->generate64(), 16, 16, QLatin1Char('0'));
        const QString path = QDir(directory).filePath(name);
        if (!QFileInfo::exists(path))
            return path;
    }
    return {};
}
