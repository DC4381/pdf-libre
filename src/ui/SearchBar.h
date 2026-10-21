// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QTimer>
#include <QWidget>

class QLabel;
class QLineEdit;
class QToolButton;

// Barre de recherche d'un onglet : recherche incrémentale, Entrée pour
// l'occurrence suivante, Maj+Entrée pour la précédente, Échap pour fermer.
class SearchBar : public QWidget
{
    Q_OBJECT
public:
    explicit SearchBar(QWidget *parent = nullptr);

    QString text() const;
    void setText(const QString &text);
    void activate();
    void setStatus(const QString &status);

signals:
    void searchRequested(const QString &text);
    void nextRequested();
    void previousRequested();
    void closed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QLineEdit *m_edit;
    QLabel *m_status;
    QToolButton *m_previous;
    QToolButton *m_next;
    QToolButton *m_close;
    QTimer m_debounce;
};
