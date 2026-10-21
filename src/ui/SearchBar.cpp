// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SearchBar.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QStyle>
#include <QToolButton>

SearchBar::SearchBar(QWidget *parent)
    : QWidget(parent)
    , m_edit(new QLineEdit(this))
    , m_status(new QLabel(this))
    , m_previous(new QToolButton(this))
    , m_next(new QToolButton(this))
    , m_close(new QToolButton(this))
{
    setAutoFillBackground(true);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);

    m_edit->setPlaceholderText(tr("Rechercher dans le document"));
    m_edit->setAccessibleName(tr("Texte à rechercher"));
    m_edit->setClearButtonEnabled(true);
    m_edit->setMinimumWidth(260);
    m_edit->installEventFilter(this);

    m_previous->setText(tr("Précédent"));
    m_previous->setToolTip(tr("Occurrence précédente (Maj+F3)"));
    m_previous->setIcon(style()->standardIcon(QStyle::SP_ArrowUp));
    m_previous->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_next->setText(tr("Suivant"));
    m_next->setToolTip(tr("Occurrence suivante (F3)"));
    m_next->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
    m_next->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_close->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    m_close->setToolTip(tr("Fermer la recherche (Échap)"));
    m_close->setAccessibleName(tr("Fermer la recherche"));
    m_close->setAutoRaise(true);
    m_status->setAccessibleName(tr("Résultats de la recherche"));

    layout->addWidget(m_edit);
    layout->addWidget(m_previous);
    layout->addWidget(m_next);
    layout->addWidget(m_status, 1);
    layout->addWidget(m_close);

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(250);
    connect(&m_debounce, &QTimer::timeout, this, [this] { emit searchRequested(m_edit->text()); });
    connect(m_edit, &QLineEdit::textChanged, this, [this] { m_debounce.start(); });
    connect(m_next, &QToolButton::clicked, this, &SearchBar::nextRequested);
    connect(m_previous, &QToolButton::clicked, this, &SearchBar::previousRequested);
    connect(m_close, &QToolButton::clicked, this, &SearchBar::closed);
}

QString SearchBar::text() const
{
    return m_edit->text();
}

void SearchBar::setText(const QString &text)
{
    const QSignalBlocker blocker(m_edit);
    m_edit->setText(text);
}

void SearchBar::activate()
{
    show();
    m_edit->setFocus(Qt::ShortcutFocusReason);
    m_edit->selectAll();
}

void SearchBar::setStatus(const QString &status)
{
    m_status->setText(status);
}

bool SearchBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_edit && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            if (m_debounce.isActive()) {
                m_debounce.stop();
                emit searchRequested(m_edit->text());
            } else if (key->modifiers() & Qt::ShiftModifier) {
                emit previousRequested();
            } else {
                emit nextRequested();
            }
            return true;
        }
        if (key->key() == Qt::Key_Escape) {
            emit closed();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
