// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Settings.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <algorithm>

namespace {
constexpr int kMaxRecent = 15;
constexpr int kMaxRemembered = 300;
constexpr int kBackupDays = 30;
constexpr qint64 kBackupBudget = 2ll << 30;
}

Settings &Settings::instance()
{
    static Settings settings;
    return settings;
}

Settings::Settings()
{
    const QString portableFile = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("portable.ini"));
    if (QFileInfo::exists(portableFile)) {
        m_portable = true;
        m_settings = std::make_unique<QSettings>(portableFile, QSettings::IniFormat);
    } else {
        m_settings = std::make_unique<QSettings>();
    }
}

QString Settings::keyFor(const QString &path)
{
    const QString canonical = QFileInfo(path).absoluteFilePath();
    const QByteArray normalized = canonical.toCaseFolded().toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(normalized, QCryptographicHash::Sha1).toHex());
}

QStringList Settings::recentFiles() const
{
    return m_settings->value(QStringLiteral("recentFiles")).toStringList();
}

void Settings::addRecentFile(const QString &path)
{
    QStringList files = recentFiles();
    const QString absolute = QFileInfo(path).absoluteFilePath();
    files.removeAll(absolute);
    files.prepend(absolute);
    while (files.size() > kMaxRecent)
        files.removeLast();
    m_settings->setValue(QStringLiteral("recentFiles"), files);
}

void Settings::removeRecentFile(const QString &path)
{
    QStringList files = recentFiles();
    files.removeAll(QFileInfo(path).absoluteFilePath());
    m_settings->setValue(QStringLiteral("recentFiles"), files);
}

void Settings::clearRecentFiles()
{
    m_settings->remove(QStringLiteral("recentFiles"));
    m_settings->remove(QStringLiteral("documents"));
}

DocumentState Settings::documentState(const QString &path) const
{
    DocumentState state;
    m_settings->beginGroup(QStringLiteral("documents/") + keyFor(path));
    if (m_settings->contains(QStringLiteral("page"))) {
        state.page = m_settings->value(QStringLiteral("page")).toInt();
        state.zoom = m_settings->value(QStringLiteral("zoom"), 1.0).toDouble();
        state.zoomMode = m_settings->value(QStringLiteral("zoomMode"), 0).toInt();
        state.valid = true;
    }
    m_settings->endGroup();
    return state;
}

void Settings::setDocumentState(const QString &path, const DocumentState &state)
{
    m_settings->beginGroup(QStringLiteral("documents"));
    const QString key = keyFor(path);
    m_settings->setValue(key + QStringLiteral("/page"), state.page);
    m_settings->setValue(key + QStringLiteral("/zoom"), state.zoom);
    m_settings->setValue(key + QStringLiteral("/zoomMode"), state.zoomMode);
    m_settings->setValue(key + QStringLiteral("/used"), QDateTime::currentSecsSinceEpoch());

    // On ne garde que les documents les plus récemment utilisés.
    const QStringList keys = m_settings->childGroups();
    if (keys.size() > kMaxRemembered) {
        QList<QPair<qint64, QString>> byDate;
        for (const QString &k : keys)
            byDate.append({m_settings->value(k + QStringLiteral("/used")).toLongLong(), k});
        std::sort(byDate.begin(), byDate.end());
        for (int i = 0; i < byDate.size() - kMaxRemembered; ++i)
            m_settings->remove(byDate.at(i).second);
    }
    m_settings->endGroup();
}

bool Settings::sidebarVisible() const
{
    return m_settings->value(QStringLiteral("ui/sidebar"), true).toBool();
}

void Settings::setSidebarVisible(bool visible)
{
    m_settings->setValue(QStringLiteral("ui/sidebar"), visible);
}

QString Settings::backupDirectory() const
{
    const QString custom = m_settings->value(QStringLiteral("backups/directory")).toString();
    if (!custom.isEmpty())
        return custom;
    if (m_portable)
        return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("sauvegardes"));
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/sauvegardes");
}

QString Settings::backupFor(const QString &original)
{
    QDir dir(backupDirectory());
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        return {};

    // Nettoyage : sauvegardes de plus de 30 jours, puis au-delà de 2 Go.
    const QDateTime limit = QDateTime::currentDateTime().addDays(-kBackupDays);
    QFileInfoList files = dir.entryInfoList({QStringLiteral("*.pdf")}, QDir::Files, QDir::Time | QDir::Reversed);
    qint64 total = 0;
    for (const QFileInfo &info : std::as_const(files))
        total += info.size();
    for (const QFileInfo &info : std::as_const(files)) {
        if (info.lastModified() < limit || total > kBackupBudget) {
            total -= info.size();
            QFile::remove(info.absoluteFilePath());
        }
    }

    const QFileInfo source(original);
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH.mm.ss"));
    return dir.filePath(source.completeBaseName() + QStringLiteral(" (") + stamp + QStringLiteral(").pdf"));
}
