// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "core/PdfAssembler.h"
#include "core/Types.h"

#include <QDialog>
#include <QVector>

class QComboBox;
class QLineEdit;
class QRadioButton;
class QSpinBox;

// Fenêtre « Séparer le document » : toutes les n pages, plages de pages,
// ou un fichier par signet de premier niveau.
class SplitDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode { EveryN, Ranges, Bookmarks };

    SplitDialog(const QString &path, const QByteArray &password, int pageCount, const OutlineList &outline,
                QWidget *parent = nullptr);

    // Découpage calculé sans interface, aussi utilisé par les tests.
    // Chaque élément est la liste des pages (0..n-1) d'un fichier produit.
    static QVector<QVector<int>> plan(Mode mode, int pageCount, int everyN, const QString &ranges,
                                      const OutlineList &outline, QString *error = nullptr);
    // Analyse « 1-3, 5, 8-10 » en positions de pages (0..n-1), sans doublons.
    static QVector<int> parseRanges(const QString &text, int pageCount, QString *error = nullptr);

    // Écrit les fichiers `<dossier>/<base> - partie 01.pdf`… Renvoie les chemins
    // créés, ou une erreur.
    static QStringList split(const QString &path, const QByteArray &password, const QVector<QVector<int>> &parts,
                             const QString &directory, const QString &baseName, QString *error);

private:
    void accept() override;
    void updateState();

    QString m_path;
    QByteArray m_password;
    int m_pageCount;
    OutlineList m_outline;
    QRadioButton *m_everyN;
    QRadioButton *m_ranges;
    QRadioButton *m_bookmarks;
    QSpinBox *m_everyNValue;
    QLineEdit *m_rangesValue;
    QLineEdit *m_directory;
    QLineEdit *m_baseName;
};
