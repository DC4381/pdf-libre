// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QString>

class QWidget;

// Ouvre la boîte d'impression Windows, puis imprime les pages choisies.
// Les pages paysage sont tournées automatiquement pour remplir la feuille.
void printDocument(QWidget *parent, const QString &path, const QByteArray &password, int pageCount, int currentPage);
