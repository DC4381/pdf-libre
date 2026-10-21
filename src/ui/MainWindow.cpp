// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "MainWindow.h"

#include "DocumentTab.h"
#include "Icons.h"
#include "Printing.h"
#include "WelcomeWidget.h"
#include "app/Settings.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>

#include <cmath>

namespace {
constexpr int kFitWidth = -1;
constexpr int kFitPage = -2;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_stack(new QStackedWidget(this))
    , m_welcome(new WelcomeWidget(m_stack))
    , m_tabs(new QTabWidget(m_stack))
{
    setWindowTitle(QStringLiteral("PDF Libre"));
    setAcceptDrops(true);

    m_tabs->setDocumentMode(true);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setElideMode(Qt::ElideMiddle);
    m_tabs->tabBar()->setExpanding(false);
    m_tabs->setAccessibleName(tr("Documents ouverts"));

    m_stack->addWidget(m_welcome);
    m_stack->addWidget(m_tabs);
    setCentralWidget(m_stack);

    createActions();
    createMenus();
    createToolBar();

    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    connect(m_tabs, &QTabWidget::currentChanged, this, &MainWindow::onCurrentTabChanged);
    connect(m_welcome, &WelcomeWidget::openRequested, this, &MainWindow::showOpenDialog);
    connect(m_welcome, &WelcomeWidget::recentActivated, this, &MainWindow::openFile);
    connect(m_welcome, &WelcomeWidget::clearRecentRequested, this, [this] {
        Settings::instance().clearRecentFiles();
        m_welcome->setRecentFiles({});
    });

    m_welcome->setRecentFiles(Settings::instance().recentFiles());
    if (!restoreGeometry(Settings::instance().raw().value(QStringLiteral("ui/geometry")).toByteArray()))
        resize(1100, 800);
    updateActions();
}

void MainWindow::createActions()
{
    const auto make = [this](const QString &text, const QKeySequence &key, auto slot) {
        auto *action = new QAction(text, this);
        if (!key.isEmpty())
            action->setShortcut(key);
        connect(action, &QAction::triggered, this, slot);
        addAction(action); // raccourcis actifs même en plein écran
        return action;
    };
    const auto onTab = [this](auto fn) {
        return [this, fn] {
            if (DocumentTab *tab = currentTab())
                fn(tab);
        };
    };

    m_openAct = make(tr("&Ouvrir…"), QKeySequence::Open, [this] { showOpenDialog(); });
    m_openAct->setIcon(Icons::make(Icons::Kind::Open));
    m_printAct = make(tr("&Imprimer…"), QKeySequence::Print, onTab([this](DocumentTab *tab) {
        printDocument(this, tab->path(), tab->password(), tab->pageCount(), tab->view()->currentPage());
    }));
    m_printAct->setIcon(Icons::make(Icons::Kind::Print));
    m_closeTabAct = make(tr("&Fermer l'onglet"), QKeySequence(Qt::CTRL | Qt::Key_W), [this] { closeTab(m_tabs->currentIndex()); });
    m_reloadAct = make(tr("&Recharger"), QKeySequence(Qt::CTRL | Qt::Key_R), onTab([](DocumentTab *tab) { tab->reload(); }));
    m_quitAct = make(tr("&Quitter"), QKeySequence(Qt::CTRL | Qt::Key_Q), [this] { close(); });

    m_copyAct = make(tr("&Copier"), QKeySequence::Copy, onTab([](DocumentTab *tab) { tab->view()->copySelection(); }));
    m_selectPageAct = make(tr("Sélectionner toute la &page"), QKeySequence::SelectAll,
                           onTab([](DocumentTab *tab) { tab->view()->selectAllOnPage(tab->view()->currentPage()); }));
    m_invertAct = make(tr("&Inverser les couleurs (mode nuit)"), QKeySequence(Qt::CTRL | Qt::Key_I), [this] {
        Settings::instance().raw().setValue(QStringLiteral("ui/invert"), m_invertAct->isChecked());
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (auto *tab = qobject_cast<DocumentTab *>(m_tabs->widget(i)))
                tab->view()->setInvertColors(m_invertAct->isChecked());
        }
    });
    m_invertAct->setCheckable(true);
    m_invertAct->setChecked(Settings::instance().raw().value(QStringLiteral("ui/invert"), false).toBool());
    m_findAct = make(tr("&Rechercher…"), QKeySequence::Find, onTab([](DocumentTab *tab) { tab->showSearch(); }));
    m_findAct->setIcon(Icons::make(Icons::Kind::Find));
    m_findNextAct = make(tr("Occurrence &suivante"), QKeySequence(Qt::Key_F3), onTab([](DocumentTab *tab) { tab->findNext(); }));
    m_findPrevAct = make(tr("Occurrence &précédente"), QKeySequence(Qt::SHIFT | Qt::Key_F3),
                         onTab([](DocumentTab *tab) { tab->findPrevious(); }));

    m_firstAct = make(tr("Première page"), QKeySequence(Qt::CTRL | Qt::Key_Home), onTab([](DocumentTab *tab) { tab->view()->goToPage(0); }));
    m_prevAct = make(tr("Page précédente"), QKeySequence(), onTab([](DocumentTab *tab) { tab->view()->previousPage(); }));
    m_prevAct->setIcon(Icons::make(Icons::Kind::PreviousPage));
    m_nextAct = make(tr("Page suivante"), QKeySequence(), onTab([](DocumentTab *tab) { tab->view()->nextPage(); }));
    m_nextAct->setIcon(Icons::make(Icons::Kind::NextPage));
    m_lastAct = make(tr("Dernière page"), QKeySequence(Qt::CTRL | Qt::Key_End),
                     onTab([](DocumentTab *tab) { tab->view()->goToPage(tab->pageCount() - 1); }));
    m_goToAct = make(tr("&Aller à la page…"), QKeySequence(Qt::CTRL | Qt::Key_G), [this] { goToPageDialog(); });

    m_zoomInAct = make(tr("Zoom a&vant"), QKeySequence::ZoomIn, onTab([](DocumentTab *tab) { tab->view()->zoomIn(); }));
    m_zoomOutAct = make(tr("Zoom a&rrière"), QKeySequence::ZoomOut, onTab([](DocumentTab *tab) { tab->view()->zoomOut(); }));
    m_zoomInAct->setIcon(Icons::make(Icons::Kind::ZoomIn));
    m_zoomOutAct->setIcon(Icons::make(Icons::Kind::ZoomOut));
    // Raccourcis identiques à SumatraPDF : Ctrl+0 page entière, Ctrl+1 taille réelle, Ctrl+2 largeur.
    m_fitPageAct = make(tr("Page &entière"), QKeySequence(Qt::CTRL | Qt::Key_0),
                        onTab([](DocumentTab *tab) { tab->view()->setZoomMode(DocumentView::ZoomMode::FitPage); }));
    m_actualSizeAct = make(tr("&Taille réelle"), QKeySequence(Qt::CTRL | Qt::Key_1), onTab([](DocumentTab *tab) { tab->view()->setZoom(1.0); }));
    m_fitWidthAct = make(tr("Ajuster à la &largeur"), QKeySequence(Qt::CTRL | Qt::Key_2),
                         onTab([](DocumentTab *tab) { tab->view()->setZoomMode(DocumentView::ZoomMode::FitWidth); }));
    m_sidebarAct = make(tr("&Panneau latéral"), QKeySequence(Qt::Key_F12), [this] { toggleSidebar(); });
    m_sidebarAct->setCheckable(true);
    m_sidebarAct->setIcon(Icons::make(Icons::Kind::Sidebar));
    m_sidebarAct->setChecked(Settings::instance().sidebarVisible());
    m_fullScreenAct = make(tr("Plein é&cran"), QKeySequence(Qt::Key_F11), [this] { toggleFullScreen(); });
    m_fullScreenAct->setCheckable(true);

    m_nextTabAct = make(tr("Onglet suivant"), QKeySequence(Qt::CTRL | Qt::Key_Tab), [this] {
        if (m_tabs->count() > 1)
            m_tabs->setCurrentIndex((m_tabs->currentIndex() + 1) % m_tabs->count());
    });
    m_prevTabAct = make(tr("Onglet précédent"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), [this] {
        if (m_tabs->count() > 1)
            m_tabs->setCurrentIndex((m_tabs->currentIndex() + m_tabs->count() - 1) % m_tabs->count());
    });
    m_aboutAct = make(tr("À &propos de PDF Libre"), QKeySequence(), [this] { showAbout(); });
}

void MainWindow::createMenus()
{
    QMenu *file = menuBar()->addMenu(tr("&Fichier"));
    file->addAction(m_openAct);
    m_recentMenu = file->addMenu(tr("Documents &récents"));
    connect(m_recentMenu, &QMenu::aboutToShow, this, &MainWindow::updateRecentMenu);
    file->addAction(m_reloadAct);
    file->addSeparator();
    file->addAction(m_printAct);
    file->addSeparator();
    file->addAction(m_closeTabAct);
    file->addAction(m_quitAct);

    QMenu *edit = menuBar()->addMenu(tr("&Édition"));
    edit->addAction(m_copyAct);
    edit->addAction(m_selectPageAct);
    edit->addSeparator();
    edit->addAction(m_findAct);
    edit->addAction(m_findNextAct);
    edit->addAction(m_findPrevAct);

    QMenu *view = menuBar()->addMenu(tr("&Affichage"));
    view->addAction(m_fitWidthAct);
    view->addAction(m_fitPageAct);
    view->addAction(m_actualSizeAct);
    view->addSeparator();
    view->addAction(m_zoomInAct);
    view->addAction(m_zoomOutAct);
    view->addSeparator();
    view->addAction(m_invertAct);
    view->addSeparator();
    view->addAction(m_sidebarAct);
    view->addAction(m_fullScreenAct);

    QMenu *go = menuBar()->addMenu(tr("A&ller"));
    go->addAction(m_firstAct);
    go->addAction(m_prevAct);
    go->addAction(m_nextAct);
    go->addAction(m_lastAct);
    go->addSeparator();
    go->addAction(m_goToAct);
    go->addSeparator();
    go->addAction(m_nextTabAct);
    go->addAction(m_prevTabAct);

    QMenu *help = menuBar()->addMenu(tr("&Aide"));
    help->addAction(m_aboutAct);
}

void MainWindow::createToolBar()
{
    m_toolBar = addToolBar(tr("Barre d'outils"));
    m_toolBar->setObjectName(QStringLiteral("mainToolBar"));
    m_toolBar->setMovable(false);
    m_toolBar->setIconSize(QSize(20, 20));
    m_toolBar->addAction(m_sidebarAct);
    m_toolBar->addSeparator();
    m_toolBar->addAction(m_openAct);
    m_toolBar->addAction(m_printAct);
    m_toolBar->addSeparator();
    m_toolBar->addAction(m_prevAct);
    m_toolBar->addAction(m_nextAct);

    m_pageEdit = new QLineEdit(m_toolBar);
    m_pageEdit->setFixedWidth(56);
    m_pageEdit->setAlignment(Qt::AlignCenter);
    m_pageEdit->setAccessibleName(tr("Numéro de page"));
    m_pageEdit->setToolTip(tr("Page actuelle (Ctrl+G)"));
    m_pageCountLabel = new QLabel(m_toolBar);
    m_pageCountLabel->setMinimumWidth(48);
    m_toolBar->addWidget(m_pageEdit);
    m_toolBar->addWidget(m_pageCountLabel);
    connect(m_pageEdit, &QLineEdit::returnPressed, this, [this] {
        if (DocumentTab *tab = currentTab()) {
            bool ok = false;
            const int page = m_pageEdit->text().toInt(&ok);
            if (ok)
                tab->view()->goToPage(page - 1);
            tab->view()->setFocus();
        }
    });

    m_toolBar->addSeparator();
    m_toolBar->addAction(m_zoomOutAct);
    m_zoomCombo = new QComboBox(m_toolBar);
    m_zoomCombo->setEditable(true);
    m_zoomCombo->setInsertPolicy(QComboBox::NoInsert);
    m_zoomCombo->setMinimumContentsLength(16);
    m_zoomCombo->setAccessibleName(tr("Zoom"));
    m_zoomCombo->addItem(tr("Ajuster à la largeur"), kFitWidth);
    m_zoomCombo->addItem(tr("Page entière"), kFitPage);
    for (int percent : {25, 50, 75, 100, 125, 150, 200, 300, 400, 800, 1600, 3200, 6400})
        m_zoomCombo->addItem(QStringLiteral("%1 %").arg(percent), percent);
    m_toolBar->addWidget(m_zoomCombo);
    m_toolBar->addAction(m_zoomInAct);
    connect(m_zoomCombo, &QComboBox::activated, this, [this](int index) {
        DocumentTab *tab = currentTab();
        if (!tab)
            return;
        const int value = m_zoomCombo->itemData(index).toInt();
        if (value == kFitWidth)
            tab->view()->setZoomMode(DocumentView::ZoomMode::FitWidth);
        else if (value == kFitPage)
            tab->view()->setZoomMode(DocumentView::ZoomMode::FitPage);
        else
            tab->view()->setZoom(value / 100.0);
        tab->view()->setFocus();
    });
    connect(m_zoomCombo->lineEdit(), &QLineEdit::returnPressed, this, [this] { applyZoomText(m_zoomCombo->currentText()); });

    m_toolBar->addSeparator();
    m_toolBar->addAction(m_findAct);
}

DocumentTab *MainWindow::currentTab() const
{
    return qobject_cast<DocumentTab *>(m_tabs->currentWidget());
}

void MainWindow::showOpenDialog()
{
    QString dir;
    const QStringList recent = Settings::instance().recentFiles();
    if (!recent.isEmpty())
        dir = QFileInfo(recent.first()).absolutePath();
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Ouvrir un document"), dir,
                                                            tr("Documents PDF (*.pdf);;Tous les fichiers (*)"));
    for (const QString &file : files)
        openFile(file);
}

void MainWindow::openFile(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        QMessageBox::warning(this, tr("Ouvrir"), tr("Le fichier « %1 » est introuvable.").arg(QDir::toNativeSeparators(path)));
        Settings::instance().removeRecentFile(path);
        m_welcome->setRecentFiles(Settings::instance().recentFiles());
        return;
    }
    const QString absolute = info.absoluteFilePath();
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto *tab = qobject_cast<DocumentTab *>(m_tabs->widget(i));
        if (tab && tab->path().compare(absolute, Qt::CaseInsensitive) == 0) {
            m_tabs->setCurrentIndex(i);
            return;
        }
    }

    auto *tab = new DocumentTab(absolute, m_tabs);
    tab->setSidebarVisible(m_sidebarAct->isChecked());
    tab->view()->setInvertColors(m_invertAct->isChecked());
    connect(tab->view(), &DocumentView::selectionChanged, this, [this, tab](bool has) {
        if (tab == currentTab())
            m_copyAct->setEnabled(has);
    });
    const int index = m_tabs->addTab(tab, info.fileName());
    m_tabs->setTabToolTip(index, QDir::toNativeSeparators(absolute));
    m_tabs->setCurrentIndex(index);
    m_stack->setCurrentWidget(m_tabs);
    tab->setFocus();

    connect(tab, &DocumentTab::loaded, this, [this, tab] {
        Settings::instance().addRecentFile(tab->path());
        if (tab == currentTab())
            onCurrentTabChanged();
    });
    connect(tab, &DocumentTab::loadFailed, this, [this, tab](const QString &message) {
        QMessageBox::warning(this, tr("Ouvrir"), tr("Impossible d'ouvrir « %1 ».\n\n%2").arg(tab->fileName(), message));
        const int i = m_tabs->indexOf(tab);
        if (i >= 0)
            closeTab(i);
    });
    connect(tab, &DocumentTab::titleChanged, this, [this, tab] {
        if (tab == currentTab())
            setWindowTitle(tab->documentTitle() + QStringLiteral(" - PDF Libre"));
    });
    connect(tab, &DocumentTab::pageChanged, this, [this, tab](int page, int count) {
        if (tab == currentTab())
            updatePageControls(page, count);
    });
    connect(tab, &DocumentTab::zoomChanged, this, [this, tab](double zoom, DocumentView::ZoomMode mode) {
        if (tab == currentTab())
            updateZoomControls(zoom, mode);
    });
    updateActions();
}

void MainWindow::startSearch(const QString &text)
{
    if (DocumentTab *tab = currentTab())
        tab->search(text);
}

void MainWindow::closeTab(int index)
{
    auto *tab = qobject_cast<DocumentTab *>(m_tabs->widget(index));
    if (!tab)
        return;
    tab->saveState();
    m_tabs->removeTab(index);
    tab->deleteLater();
    if (m_tabs->count() == 0) {
        m_welcome->setRecentFiles(Settings::instance().recentFiles());
        m_stack->setCurrentWidget(m_welcome);
        setWindowTitle(QStringLiteral("PDF Libre"));
    }
    updateActions();
}

void MainWindow::onCurrentTabChanged()
{
    DocumentTab *tab = currentTab();
    if (tab) {
        setWindowTitle(tab->documentTitle() + QStringLiteral(" - PDF Libre"));
        updatePageControls(tab->view()->currentPage(), tab->pageCount());
        updateZoomControls(tab->view()->zoom(), tab->view()->zoomMode());
        tab->setSidebarVisible(m_sidebarAct->isChecked());
    }
    updateActions();
    m_copyAct->setEnabled(tab && tab->view()->hasSelection());
}

void MainWindow::updatePageControls(int page, int pageCount)
{
    if (pageCount <= 0) {
        m_pageEdit->clear();
        m_pageCountLabel->clear();
        return;
    }
    if (!m_pageEdit->hasFocus())
        m_pageEdit->setText(QString::number(page + 1));
    m_pageCountLabel->setText(tr(" / %1").arg(pageCount));
    m_prevAct->setEnabled(page > 0);
    m_nextAct->setEnabled(page + 1 < pageCount);
}

void MainWindow::updateZoomControls(double zoom, DocumentView::ZoomMode mode)
{
    QString text;
    if (mode == DocumentView::ZoomMode::FitWidth)
        text = tr("Ajuster à la largeur");
    else if (mode == DocumentView::ZoomMode::FitPage)
        text = tr("Page entière");
    else
        text = QStringLiteral("%1 %").arg(std::lround(zoom * 100));
    if (!m_zoomCombo->lineEdit()->hasFocus())
        m_zoomCombo->setEditText(text);
}

void MainWindow::applyZoomText(const QString &text)
{
    DocumentTab *tab = currentTab();
    if (!tab)
        return;
    QString digits = text;
    digits.remove(QLatin1Char('%')).replace(QLatin1Char(','), QLatin1Char('.'));
    bool ok = false;
    const double percent = digits.trimmed().toDouble(&ok);
    if (ok && percent > 0)
        tab->view()->setZoom(percent / 100.0);
    else
        updateZoomControls(tab->view()->zoom(), tab->view()->zoomMode());
    tab->view()->setFocus();
}

void MainWindow::goToPageDialog()
{
    DocumentTab *tab = currentTab();
    if (!tab || tab->pageCount() <= 0)
        return;
    bool ok = false;
    const int page = QInputDialog::getInt(this, tr("Aller à la page"), tr("Numéro de page (1 à %1) :").arg(tab->pageCount()),
                                          tab->view()->currentPage() + 1, 1, tab->pageCount(), 1, &ok);
    if (ok)
        tab->view()->goToPage(page - 1);
}

void MainWindow::toggleSidebar()
{
    const bool visible = m_sidebarAct->isChecked();
    Settings::instance().setSidebarVisible(visible);
    if (DocumentTab *tab = currentTab())
        tab->setSidebarVisible(visible);
}

void MainWindow::toggleFullScreen()
{
    if (isFullScreen()) {
        menuBar()->show();
        m_toolBar->show();
        m_tabs->tabBar()->show();
        if (m_wasMaximized)
            showMaximized();
        else
            showNormal();
        m_fullScreenAct->setChecked(false);
    } else {
        m_wasMaximized = isMaximized();
        menuBar()->hide();
        m_toolBar->hide();
        m_tabs->tabBar()->hide();
        showFullScreen();
        m_fullScreenAct->setChecked(true);
    }
}

void MainWindow::updateRecentMenu()
{
    m_recentMenu->clear();
    const QStringList files = Settings::instance().recentFiles();
    for (const QString &path : files) {
        QAction *action = m_recentMenu->addAction(QFileInfo(path).fileName(), this, [this, path] { openFile(path); });
        action->setToolTip(QDir::toNativeSeparators(path));
    }
    if (files.isEmpty()) {
        m_recentMenu->addAction(tr("(aucun)"))->setEnabled(false);
        return;
    }
    m_recentMenu->addSeparator();
    m_recentMenu->addAction(tr("Effacer l'historique"), this, [this] {
        Settings::instance().clearRecentFiles();
        m_welcome->setRecentFiles({});
    });
}

void MainWindow::updateActions()
{
    const bool hasDoc = currentTab() != nullptr;
    for (QAction *action : {m_printAct, m_closeTabAct, m_reloadAct, m_findAct, m_findNextAct, m_findPrevAct, m_firstAct,
                            m_prevAct, m_nextAct, m_lastAct, m_goToAct, m_zoomInAct, m_zoomOutAct, m_fitWidthAct,
                            m_fitPageAct, m_actualSizeAct, m_selectPageAct})
        action->setEnabled(hasDoc);
    m_copyAct->setEnabled(hasDoc && currentTab()->view()->hasSelection());
    m_pageEdit->setEnabled(hasDoc);
    m_zoomCombo->setEnabled(hasDoc);
    if (!hasDoc) {
        m_pageEdit->clear();
        m_pageCountLabel->clear();
        m_zoomCombo->setEditText(QString());
    }
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this, tr("À propos de PDF Libre"),
        tr("<h3>PDF Libre %1</h3>"
           "<p>Lecteur PDF gratuit, rapide et sans publicité. Vos documents restent sur votre ordinateur : "
           "aucune donnée n'est transmise.</p>"
           "<p>Créé par David Carmo. Copyright © 2026 David Carmo.<br>"
           "Logiciel libre sous licence GNU AGPL version 3.<br>Code source : <a href=\"%2\">%2</a></p>"
           "<p>Composants : MuPDF © Artifex Software (AGPL-3.0), Qt © The Qt Company (LGPL-3.0).</p>"
           "<p>Mode portable : %3</p>")
            .arg(QApplication::applicationVersion(), QStringLiteral(APP_HOMEPAGE),
                 Settings::instance().isPortable() ? tr("oui") : tr("non")));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto *tab = qobject_cast<DocumentTab *>(m_tabs->widget(i)))
            tab->saveState();
    }
    if (isFullScreen())
        toggleFullScreen();
    Settings::instance().raw().setValue(QStringLiteral("ui/geometry"), saveGeometry());
    event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile())
            openFile(url.toLocalFile());
    }
    event->acceptProposedAction();
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && isFullScreen()) {
        toggleFullScreen();
        return;
    }
    QMainWindow::keyPressEvent(event);
}
