// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "MainWindow.h"

#include "DocumentTab.h"
#include "Icons.h"
#include "PageOrganizer.h"
#include "SignatureDialog.h"
#include "SplitDialog.h"
#include "Printing.h"
#include "WelcomeWidget.h"
#include "app/Settings.h"

#include <QAction>
#include <QActionGroup>
#include <QColorDialog>
#include <QDate>
#include <QToolButton>
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
#include <QStatusBar>
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
    , m_annotBar(nullptr)
{
    statusBar();
    setWindowTitle(QStringLiteral("CARMO PDF"));
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
    createAnnotationBar();

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
    m_saveAct = make(tr("&Enregistrer"), QKeySequence::Save, [this] { saveCurrent(); });
    m_saveAsAct = make(tr("Enregistrer &sous…"), QKeySequence::SaveAs, [this] { saveCurrentAs(); });
    m_undoAct = make(tr("&Annuler"), QKeySequence::Undo, onTab([](DocumentTab *tab) { tab->undo(); }));
    m_redoAct = make(tr("&Rétablir"), QKeySequence::Redo, onTab([](DocumentTab *tab) { tab->redo(); }));
    m_authorAct = make(tr("&Nom de l'auteur des commentaires…"), QKeySequence(), [this] {
        DocumentTab *tab = currentTab();
        const QString current = tab ? tab->author() : Settings::instance().raw().value(QStringLiteral("annotations/author")).toString();
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Auteur des commentaires"), tr("Nom affiché sur vos annotations :"),
                                                   QLineEdit::Normal, current, &ok);
        if (!ok)
            return;
        Settings::instance().raw().setValue(QStringLiteral("annotations/author"), name);
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (auto *t = qobject_cast<DocumentTab *>(m_tabs->widget(i)))
                t->setAuthor(name);
        }
    });
    m_annotateAct = make(tr("Barre d'&annotation"), QKeySequence(Qt::Key_F9), [this] { m_annotBar->setVisible(m_annotateAct->isChecked()); });
    m_annotateAct->setCheckable(true);
    m_annotateAct->setChecked(Settings::instance().raw().value(QStringLiteral("ui/annotBar"), true).toBool());
    m_signatureAct = make(tr("Si&gnature visuelle…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G), [this] { placeSignature(); });
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
    m_organizeAct = make(tr("&Organiser les pages…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O), [this] { organizePages(); });
    m_organizeAct->setIcon(Icons::make(Icons::Kind::Pages));
    m_mergeAct = make(tr("&Fusionner des documents…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M), [this] { mergeDocuments(); });
    m_splitAct = make(tr("&Séparer le document…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P), [this] { splitDocument(); });
    m_aboutAct = make(tr("À &propos de CARMO PDF"), QKeySequence(), [this] { showAbout(); });
}

void MainWindow::createMenus()
{
    QMenu *file = menuBar()->addMenu(tr("&Fichier"));
    file->addAction(m_openAct);
    m_recentMenu = file->addMenu(tr("Documents &récents"));
    connect(m_recentMenu, &QMenu::aboutToShow, this, &MainWindow::updateRecentMenu);
    file->addAction(m_reloadAct);
    file->addSeparator();
    file->addAction(m_saveAct);
    file->addAction(m_saveAsAct);
    file->addSeparator();
    file->addAction(m_printAct);
    file->addSeparator();
    file->addAction(m_closeTabAct);
    file->addAction(m_quitAct);

    QMenu *edit = menuBar()->addMenu(tr("&Édition"));
    edit->addAction(m_undoAct);
    edit->addAction(m_redoAct);
    edit->addSeparator();
    edit->addAction(m_copyAct);
    edit->addAction(m_selectPageAct);
    edit->addSeparator();
    edit->addAction(m_findAct);
    edit->addAction(m_findNextAct);
    edit->addAction(m_findPrevAct);
    edit->addSeparator();
    edit->addAction(m_signatureAct);
    edit->addAction(m_authorAct);
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
    view->addAction(m_annotateAct);
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

    QMenu *pages = menuBar()->addMenu(tr("&Pages"));
    pages->addAction(m_organizeAct);
    pages->addAction(m_splitAct);
    pages->addSeparator();
    pages->addAction(m_mergeAct);

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
    m_toolBar->addAction(m_organizeAct);
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
    tab->view()->setAnnotationColor(QColor(Settings::instance().raw().value(QStringLiteral("annotations/color"), QStringLiteral("#ffdc00")).toString()));
    tab->view()->setPenWidth(Settings::instance().raw().value(QStringLiteral("annotations/width"), 2.0).toDouble());
    connect(tab, &DocumentTab::editStateChanged, this, [this, tab] {
        if (tab == currentTab())
            updateEditActions();
        const int i = m_tabs->indexOf(tab);
        if (i >= 0)
            m_tabs->setTabText(i, (tab->isModified() ? QStringLiteral("● ") : QString()) + tab->fileName());
    });
    connect(tab->view(), &DocumentView::toolChanged, this, [this, tab] {
        if (tab == currentTab())
            updateEditActions();
    });
    connect(tab, &DocumentTab::saved, this, [this, tab](const QString &path) {
        statusBar()->showMessage(tr("Enregistré : %1").arg(QDir::toNativeSeparators(path)), 4000);
        if (QFileInfo(path).absoluteFilePath() != tab->path())
            openFile(path);
    });
    connect(tab, &DocumentTab::saveFailed, this, [this](const QString &message) {
        QMessageBox::warning(this, tr("Enregistrer"), message);
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
            setWindowTitle(tab->documentTitle() + QStringLiteral(" - CARMO PDF"));
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
    if (!confirmDiscard(tab))
        return;
    tab->saveState();
    m_tabs->removeTab(index);
    tab->deleteLater();
    if (m_tabs->count() == 0) {
        m_welcome->setRecentFiles(Settings::instance().recentFiles());
        m_stack->setCurrentWidget(m_welcome);
        setWindowTitle(QStringLiteral("CARMO PDF"));
    }
    updateActions();
}

void MainWindow::onCurrentTabChanged()
{
    DocumentTab *tab = currentTab();
    if (tab) {
        setWindowTitle(tab->documentTitle() + QStringLiteral(" - CARMO PDF"));
        updatePageControls(tab->view()->currentPage(), tab->pageCount());
        updateZoomControls(tab->view()->zoom(), tab->view()->zoomMode());
        tab->setSidebarVisible(m_sidebarAct->isChecked());
    }
    updateActions();
    m_copyAct->setEnabled(tab && tab->view()->hasSelection());
    updateEditActions();
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
                            m_fitPageAct, m_actualSizeAct, m_selectPageAct, m_organizeAct, m_splitAct})
        action->setEnabled(hasDoc);
    m_copyAct->setEnabled(hasDoc && currentTab()->view()->hasSelection());
    m_pageEdit->setEnabled(hasDoc);
    m_zoomCombo->setEnabled(hasDoc);
    if (!hasDoc) {
        m_pageEdit->clear();
        m_pageCountLabel->clear();
        m_zoomCombo->setEditText(QString());
    }
    if (m_annotBar)
        updateEditActions();
}

void MainWindow::createAnnotationBar()
{
    m_annotBar = new QToolBar(tr("Annoter"), this);
    m_annotBar->setObjectName(QStringLiteral("annotBar"));
    m_annotBar->setMovable(false);
    m_annotBar->setIconSize(QSize(20, 20));
    addToolBarBreak();
    addToolBar(m_annotBar);
    m_toolGroup = new QActionGroup(this);
    m_toolGroup->setExclusive(true);

    const auto tool = [this](const QString &text, DocumentView::Tool kind, Icons::Kind icon, const QKeySequence &key = {}) {
        auto *action = new QAction(Icons::make(icon), text, this);
        action->setCheckable(true);
        action->setData(int(kind));
        if (!key.isEmpty()) {
            action->setShortcut(key);
            action->setToolTip(QStringLiteral("%1 (%2)").arg(text, key.toString(QKeySequence::NativeText)));
        }
        connect(action, &QAction::triggered, this, [this, kind] {
            if (DocumentTab *tab = currentTab())
                tab->view()->setTool(kind);
        });
        addAction(action);
        m_toolGroup->addAction(action);
        m_annotBar->addAction(action);
        return action;
    };
    QAction *select = tool(tr("Sélection"), DocumentView::Tool::Select, Icons::Kind::Cursor, QKeySequence(Qt::Key_V));
    select->setChecked(true);
    m_annotBar->addSeparator();
    tool(tr("Surligner"), DocumentView::Tool::Highlight, Icons::Kind::Highlight, QKeySequence(Qt::Key_H));
    tool(tr("Souligner"), DocumentView::Tool::Underline, Icons::Kind::Underline, QKeySequence(Qt::Key_U));
    tool(tr("Barrer"), DocumentView::Tool::StrikeOut, Icons::Kind::StrikeOut);
    m_annotBar->addSeparator();
    tool(tr("Note"), DocumentView::Tool::Note, Icons::Kind::Note, QKeySequence(Qt::Key_C));
    tool(tr("Texte"), DocumentView::Tool::FreeText, Icons::Kind::Text, QKeySequence(Qt::Key_T));
    tool(tr("Dessin"), DocumentView::Tool::Ink, Icons::Kind::Pen, QKeySequence(Qt::Key_D));
    tool(tr("Ligne"), DocumentView::Tool::Line, Icons::Kind::Line);
    tool(tr("Flèche"), DocumentView::Tool::Arrow, Icons::Kind::Arrow);
    tool(tr("Rectangle"), DocumentView::Tool::Square, Icons::Kind::Rectangle, QKeySequence(Qt::Key_R));
    tool(tr("Ellipse"), DocumentView::Tool::Circle, Icons::Kind::Ellipse);
    m_annotBar->addSeparator();

    // Tampons : menu de textes prêts à l'emploi, et signature.
    m_stampButton = new QToolButton(m_annotBar);
    m_stampButton->setText(tr("Tampon"));
    m_stampButton->setIcon(Icons::make(Icons::Kind::Stamp));
    m_stampButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_stampButton->setPopupMode(QToolButton::InstantPopup);
    auto *stampMenu = new QMenu(m_stampButton);
    for (const QString &text : {tr("APPROUVÉ"), tr("REFUSÉ"), tr("CONFIDENTIEL"), tr("COPIE"), tr("BROUILLON"), tr("ORIGINAL"),
                                tr("SIGNÉ"), tr("POUR ACCORD"), tr("VU")})
        stampMenu->addAction(text, this, [this, text] { placeStamp(text); });
    stampMenu->addAction(tr("REÇU LE %1").arg(QDate::currentDate().toString(QStringLiteral("dd/MM/yyyy"))), this,
                         [this] { placeStamp(tr("REÇU LE %1").arg(QDate::currentDate().toString(QStringLiteral("dd/MM/yyyy")))); });
    stampMenu->addSeparator();
    stampMenu->addAction(tr("Texte personnalisé…"), this, [this] {
        bool ok = false;
        const QString text = QInputDialog::getText(this, tr("Tampon"), tr("Texte du tampon :"), QLineEdit::Normal, {}, &ok);
        if (ok && !text.trimmed().isEmpty())
            placeStamp(text.trimmed());
    });
    stampMenu->addAction(tr("Image (logo, signature scannée)…"), this, [this] {
        const QString file = QFileDialog::getOpenFileName(this, tr("Image du tampon"), QString(), tr("Images (*.png *.jpg *.jpeg *.bmp)"));
        if (file.isEmpty())
            return;
        const QImage image(file);
        if (image.isNull() || !currentTab())
            return;
        currentTab()->view()->setStampImage(image, QFileInfo(file).fileName());
        currentTab()->view()->setTool(DocumentView::Tool::Stamp);
    });
    m_stampButton->setMenu(stampMenu);
    m_annotBar->addWidget(m_stampButton);
    m_annotBar->addAction(m_signatureAct);
    m_annotBar->addSeparator();

    m_colorButton = new QToolButton(m_annotBar);
    m_colorButton->setToolTip(tr("Couleur des annotations"));
    m_colorButton->setAccessibleName(tr("Couleur des annotations"));
    m_colorButton->setPopupMode(QToolButton::InstantPopup);
    auto *colorMenu = new QMenu(m_colorButton);
    struct Preset
    {
        const char *name;
        QColor color;
    };
    const Preset presets[] = {{"Jaune", QColor(255, 220, 0)}, {"Vert", QColor(80, 200, 80)}, {"Bleu", QColor(60, 130, 255)},
                              {"Rose", QColor(255, 120, 200)}, {"Orange", QColor(255, 140, 0)}, {"Rouge", QColor(220, 30, 30)},
                              {"Noir", QColor(0, 0, 0)}};
    for (const Preset &preset : presets) {
        QPixmap swatch(16, 16);
        swatch.fill(preset.color);
        colorMenu->addAction(QIcon(swatch), tr(preset.name), this, [this, preset] {
            Settings::instance().raw().setValue(QStringLiteral("annotations/color"), preset.color.name());
            for (int i = 0; i < m_tabs->count(); ++i)
                if (auto *t = qobject_cast<DocumentTab *>(m_tabs->widget(i)))
                    t->view()->setAnnotationColor(preset.color);
            updateEditActions();
        });
    }
    colorMenu->addSeparator();
    colorMenu->addAction(tr("Autre couleur…"), this, [this] { chooseColor(); });
    m_colorButton->setMenu(colorMenu);
    m_annotBar->addWidget(m_colorButton);

    auto *widthButton = new QToolButton(m_annotBar);
    widthButton->setText(tr("Épaisseur"));
    widthButton->setPopupMode(QToolButton::InstantPopup);
    auto *widthMenu = new QMenu(widthButton);
    for (double width : {1.0, 2.0, 4.0, 8.0}) {
        widthMenu->addAction(tr("%1 pt").arg(width), this, [this, width] {
            Settings::instance().raw().setValue(QStringLiteral("annotations/width"), width);
            for (int i = 0; i < m_tabs->count(); ++i)
                if (auto *t = qobject_cast<DocumentTab *>(m_tabs->widget(i)))
                    t->view()->setPenWidth(width);
        });
    }
    widthButton->setMenu(widthMenu);
    m_annotBar->addWidget(widthButton);
    m_annotBar->addSeparator();
    m_annotBar->addAction(m_undoAct);
    m_annotBar->addAction(m_redoAct);
    m_annotBar->addAction(m_saveAct);
    m_undoAct->setIcon(Icons::make(Icons::Kind::Undo));
    m_redoAct->setIcon(Icons::make(Icons::Kind::Redo));
    m_saveAct->setIcon(Icons::make(Icons::Kind::Save));
    m_signatureAct->setIcon(Icons::make(Icons::Kind::Signature));
    m_annotBar->setVisible(m_annotateAct->isChecked());
    updateEditActions();
}

void MainWindow::updateEditActions()
{
    DocumentTab *tab = currentTab();
    const bool editable = tab && tab->isEditable();
    m_saveAct->setEnabled(editable && tab->isModified());
    m_saveAsAct->setEnabled(editable);
    m_undoAct->setEnabled(editable && tab->canUndo());
    m_redoAct->setEnabled(editable && tab->canRedo());
    m_signatureAct->setEnabled(editable);
    m_stampButton->setEnabled(editable);
    for (QAction *action : m_toolGroup->actions()) {
        action->setEnabled(editable);
        if (tab && action->data().toInt() == int(tab->view()->tool()))
            action->setChecked(true);
    }
    const QColor color = tab ? tab->view()->annotationColor()
                             : QColor(Settings::instance().raw().value(QStringLiteral("annotations/color"), QStringLiteral("#ffdc00")).toString());
    QPixmap swatch(18, 18);
    swatch.fill(color);
    m_colorButton->setIcon(QIcon(swatch));
    Settings::instance().raw().setValue(QStringLiteral("ui/annotBar"), m_annotateAct->isChecked());
}

void MainWindow::chooseColor()
{
    DocumentTab *tab = currentTab();
    const QColor current = tab ? tab->view()->annotationColor() : QColor(255, 220, 0);
    const QColor color = QColorDialog::getColor(current, this, tr("Couleur des annotations"), QColorDialog::ShowAlphaChannel);
    if (!color.isValid())
        return;
    Settings::instance().raw().setValue(QStringLiteral("annotations/color"), color.name(QColor::HexArgb));
    for (int i = 0; i < m_tabs->count(); ++i)
        if (auto *t = qobject_cast<DocumentTab *>(m_tabs->widget(i)))
            t->view()->setAnnotationColor(color);
    updateEditActions();
}

void MainWindow::placeStamp(const QString &text)
{
    DocumentTab *tab = currentTab();
    if (!tab)
        return;
    // Image du tampon dessinée ici : identique dans tous les lecteurs.
    const QColor color(196, 43, 28);
    const double scale = 4.0;
    QFont font(QStringLiteral("Arial"));
    font.setBold(true);
    font.setPixelSize(int(26 * scale));
    const QFontMetrics metrics(font);
    const int width = metrics.horizontalAdvance(text) + int(40 * scale);
    const int height = int(60 * scale);
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(color, 3 * scale));
        painter.drawRoundedRect(QRectF(3 * scale, 3 * scale, width - 6 * scale, height - 6 * scale), 6 * scale, 6 * scale);
        painter.setFont(font);
        painter.drawText(image.rect(), Qt::AlignCenter, text);
    }
    tab->view()->setStampImage(image, text);
    tab->view()->setTool(DocumentView::Tool::Stamp);
    statusBar()->showMessage(tr("Cliquez sur la page pour poser le tampon « %1 » (glissez pour choisir sa taille).").arg(text), 6000);
}

void MainWindow::placeSignature()
{
    DocumentTab *tab = currentTab();
    if (!tab)
        return;
    SignatureDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted || dialog.signature().isNull())
        return;
    tab->view()->setStampImage(dialog.signature(), tr("Signature"));
    tab->view()->setTool(DocumentView::Tool::Stamp);
    statusBar()->showMessage(tr("Cliquez sur la page à l'endroit de la signature (glissez pour choisir sa taille)."), 6000);
}

void MainWindow::saveCurrent()
{
    DocumentTab *tab = currentTab();
    if (!tab || !tab->isEditable())
        return;
    tab->save();
}

void MainWindow::saveCurrentAs()
{
    DocumentTab *tab = currentTab();
    if (!tab || !tab->isEditable())
        return;
    const QString suggested = QFileInfo(tab->path()).absolutePath() + QLatin1Char('/') + QFileInfo(tab->path()).completeBaseName()
                              + tr(" - annoté.pdf");
    const QString target = QFileDialog::getSaveFileName(this, tr("Enregistrer sous"), suggested, tr("Documents PDF (*.pdf)"));
    if (!target.isEmpty())
        tab->saveAs(target);
}

bool MainWindow::confirmDiscard(DocumentTab *tab)
{
    if (!tab || !tab->isModified())
        return true;
    m_tabs->setCurrentWidget(tab);
    const auto answer = QMessageBox::question(this, tr("Modifications non enregistrées"),
                                              tr("« %1 » contient des annotations non enregistrées. Les enregistrer ?").arg(tab->fileName()),
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save) {
        tab->save();
        return !tab->isModified() || true; // l'enregistrement est asynchrone : la fermeture suit
    }
    return true;
}

void MainWindow::organizePages()
{
    DocumentTab *tab = currentTab();
    if (!tab || !tab->isLoaded())
        return;
    if (!confirmDiscard(tab))
        return;
    PageOrganizer organizer(tab->path(), tab->password(), this);
    bool released = false;
    connect(&organizer, &PageOrganizer::overwriteRequested, this, [tab, &released] {
        tab->releaseFile();
        released = true;
    });
    connect(&organizer, &PageOrganizer::documentSaved, this, [this, tab, &released](const QString &path) {
        if (QFileInfo(path).absoluteFilePath() == tab->path()) {
            tab->reload();
            released = false;
        } else {
            openFile(path);
        }
    });
    organizer.exec();
    if (released)
        tab->reload(); // l'enregistrement a échoué : on rouvre tel quel
}

void MainWindow::mergeDocuments()
{
    PageOrganizer organizer(QString(), {}, this);
    DocumentTab *tab = currentTab();
    if (tab && tab->isLoaded())
        organizer.loadDocument(tab->path(), tab->password());
    connect(&organizer, &PageOrganizer::documentSaved, this, [this](const QString &path) { openFile(path); });
    organizer.exec();
}

void MainWindow::splitDocument()
{
    DocumentTab *tab = currentTab();
    if (!tab || !tab->isLoaded())
        return;
    SplitDialog dialog(tab->path(), tab->password(), tab->pageCount(), tab->outline(), this);
    dialog.exec();
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this, tr("À propos de CARMO PDF"),
        tr("<h3>CARMO PDF %1</h3>"
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
        auto *tab = qobject_cast<DocumentTab *>(m_tabs->widget(i));
        if (tab && !confirmDiscard(tab)) {
            event->ignore();
            return;
        }
        if (tab)
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
