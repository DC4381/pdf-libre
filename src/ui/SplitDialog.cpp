// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SplitDialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

SplitDialog::SplitDialog(const QString &path, const QByteArray &password, int pageCount, const OutlineList &outline,
                         QWidget *parent)
    : QDialog(parent)
    , m_path(path)
    , m_password(password)
    , m_pageCount(pageCount)
    , m_outline(outline)
{
    setWindowTitle(tr("Séparer : %1").arg(QFileInfo(path).fileName()));
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Document de %n page(s). Comment le séparer ?", nullptr, pageCount), this));

    m_everyN = new QRadioButton(tr("Toutes les"), this);
    m_everyNValue = new QSpinBox(this);
    m_everyNValue->setRange(1, std::max(1, pageCount));
    m_everyNValue->setSuffix(tr(" pages"));
    auto *everyRow = new QHBoxLayout;
    everyRow->addWidget(m_everyN);
    everyRow->addWidget(m_everyNValue);
    everyRow->addStretch(1);
    layout->addLayout(everyRow);

    m_ranges = new QRadioButton(tr("Par plages de pages"), this);
    m_rangesValue = new QLineEdit(this);
    m_rangesValue->setPlaceholderText(tr("Exemple : 1-3, 4-10, 11"));
    m_rangesValue->setAccessibleName(tr("Plages de pages"));
    auto *rangesRow = new QHBoxLayout;
    rangesRow->addWidget(m_ranges);
    rangesRow->addWidget(m_rangesValue, 1);
    layout->addLayout(rangesRow);

    m_bookmarks = new QRadioButton(tr("Un fichier par signet de premier niveau (%n signet(s))", nullptr, int(outline.size())), this);
    m_bookmarks->setEnabled(!outline.empty());
    layout->addWidget(m_bookmarks);

    auto *form = new QFormLayout;
    m_directory = new QLineEdit(QFileInfo(path).absolutePath(), this);
    auto *browse = new QToolButton(this);
    browse->setText(QStringLiteral("…"));
    browse->setAccessibleName(tr("Choisir le dossier"));
    connect(browse, &QToolButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Dossier de destination"), m_directory->text());
        if (!dir.isEmpty())
            m_directory->setText(dir);
    });
    auto *dirRow = new QHBoxLayout;
    dirRow->addWidget(m_directory, 1);
    dirRow->addWidget(browse);
    form->addRow(tr("Dossier :"), dirRow);
    m_baseName = new QLineEdit(QFileInfo(path).completeBaseName(), this);
    form->addRow(tr("Nom des fichiers :"), m_baseName);
    layout->addLayout(form);
    layout->addWidget(new QLabel(tr("Les fichiers seront nommés « nom - partie 01.pdf », « nom - partie 02.pdf »…"), this));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Séparer"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    m_everyN->setChecked(true);
    for (QRadioButton *button : {m_everyN, m_ranges, m_bookmarks})
        connect(button, &QRadioButton::toggled, this, &SplitDialog::updateState);
    connect(m_rangesValue, &QLineEdit::textEdited, this, [this] { m_ranges->setChecked(true); });
    connect(m_everyNValue, &QSpinBox::valueChanged, this, [this] { m_everyN->setChecked(true); });
    updateState();
}

void SplitDialog::updateState()
{
    m_everyNValue->setEnabled(m_everyN->isChecked());
    m_rangesValue->setEnabled(m_ranges->isChecked());
}

QVector<int> SplitDialog::parseRanges(const QString &text, int pageCount, QString *error)
{
    QVector<int> pages;
    static const QRegularExpression item(QStringLiteral("^\\s*(\\d+)\\s*(?:-\\s*(\\d+)\\s*)?$"));
    const QStringList parts = text.split(QRegularExpression(QStringLiteral("[,;]")), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const auto match = item.match(part);
        if (!match.hasMatch()) {
            if (error)
                *error = tr("« %1 » n'est pas une plage valide. Exemple : 1-3, 5, 8-10").arg(part.trimmed());
            return {};
        }
        const int from = match.captured(1).toInt();
        const int to = match.captured(2).isEmpty() ? from : match.captured(2).toInt();
        if (from < 1 || to < from || to > pageCount) {
            if (error)
                *error = tr("La plage « %1 » sort du document (1 à %2).").arg(part.trimmed()).arg(pageCount);
            return {};
        }
        for (int p = from; p <= to; ++p) {
            if (!pages.contains(p - 1))
                pages.append(p - 1);
        }
    }
    if (pages.isEmpty() && error)
        *error = tr("Indiquez au moins une plage de pages.");
    return pages;
}

QVector<QVector<int>> SplitDialog::plan(Mode mode, int pageCount, int everyN, const QString &ranges,
                                        const OutlineList &outline, QString *error)
{
    QVector<QVector<int>> parts;
    switch (mode) {
    case Mode::EveryN:
        if (everyN < 1)
            everyN = 1;
        for (int start = 0; start < pageCount; start += everyN) {
            QVector<int> part;
            for (int p = start; p < std::min(pageCount, start + everyN); ++p)
                part.append(p);
            parts.append(part);
        }
        break;
    case Mode::Ranges: {
        const QStringList pieces = ranges.split(QRegularExpression(QStringLiteral("[,;]")), Qt::SkipEmptyParts);
        for (const QString &piece : pieces) {
            const QVector<int> part = parseRanges(piece, pageCount, error);
            if (part.isEmpty())
                return {};
            parts.append(part);
        }
        if (parts.isEmpty() && error)
            *error = tr("Indiquez au moins une plage de pages.");
        break;
    }
    case Mode::Bookmarks: {
        QVector<int> starts;
        for (const OutlineItem &item : outline) {
            if (item.page >= 0 && item.page < pageCount && !starts.contains(item.page))
                starts.append(item.page);
        }
        std::sort(starts.begin(), starts.end());
        if (starts.isEmpty()) {
            if (error)
                *error = tr("Aucun signet ne pointe vers une page du document.");
            return {};
        }
        if (starts.first() != 0)
            starts.prepend(0); // pages avant le premier signet
        for (int i = 0; i < starts.size(); ++i) {
            const int end = i + 1 < starts.size() ? starts[i + 1] : pageCount;
            QVector<int> part;
            for (int p = starts[i]; p < end; ++p)
                part.append(p);
            parts.append(part);
        }
        break;
    }
    }
    return parts;
}

QStringList SplitDialog::split(const QString &path, const QByteArray &password, const QVector<QVector<int>> &parts,
                               const QString &directory, const QString &baseName, QString *error)
{
    QStringList created;
    const int digits = parts.size() >= 100 ? 3 : 2;
    for (int i = 0; i < parts.size(); ++i) {
        QVector<PageRef> pages;
        for (int p : parts[i]) {
            PageRef ref;
            ref.sourcePath = path;
            ref.password = password;
            ref.page = p;
            pages.append(ref);
        }
        const QString target = QDir(directory).filePath(
            QStringLiteral("%1 - partie %2.pdf").arg(baseName, QString::number(i + 1).rightJustified(digits, QLatin1Char('0'))));
        const auto result = PdfAssembler::assemble(pages, target);
        if (!result.ok) {
            if (error)
                *error = result.error;
            return created;
        }
        created.append(target);
    }
    return created;
}

void SplitDialog::accept()
{
    const Mode mode = m_ranges->isChecked() ? Mode::Ranges : m_bookmarks->isChecked() ? Mode::Bookmarks : Mode::EveryN;
    QString error;
    const auto parts = plan(mode, m_pageCount, m_everyNValue->value(), m_rangesValue->text(), m_outline, &error);
    if (parts.isEmpty()) {
        QMessageBox::warning(this, tr("Séparer"), error);
        return;
    }
    if (!QDir(m_directory->text()).exists()) {
        QMessageBox::warning(this, tr("Séparer"), tr("Le dossier de destination n'existe pas."));
        return;
    }
    const QString base = m_baseName->text().trimmed();
    if (base.isEmpty()) {
        QMessageBox::warning(this, tr("Séparer"), tr("Indiquez un nom de fichier."));
        return;
    }
    const QStringList created = split(m_path, m_password, parts, m_directory->text(), base, &error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Séparer"), error);
        return;
    }
    QMessageBox::information(this, tr("Séparer"), tr("%n fichier(s) créé(s) dans %1.", nullptr, int(created.size()))
                                                       .arg(QDir::toNativeSeparators(m_directory->text())));
    QDialog::accept();
}
