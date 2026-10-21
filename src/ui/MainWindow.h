// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "DocumentView.h"

#include <QMainWindow>

class DocumentTab;
class QAction;
class QComboBox;
class QLabel;
class QLineEdit;
class QMenu;
class QStackedWidget;
class QTabWidget;
class QToolBar;
class QToolButton;
class QActionGroup;
class WelcomeWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    void openFile(const QString &path);
    void startSearch(const QString &text);

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void createActions();
    void createMenus();
    void createToolBar();
    DocumentTab *currentTab() const;
    void showOpenDialog();
    void closeTab(int index);
    void onCurrentTabChanged();
    void updatePageControls(int page, int pageCount);
    void updateZoomControls(double zoom, DocumentView::ZoomMode mode);
    void applyZoomText(const QString &text);
    void goToPageDialog();
    void toggleSidebar();
    void toggleFullScreen();
    void updateRecentMenu();
    void updateActions();
    void showAbout();
    void createAnnotationBar();
    void updateEditActions();
    void chooseColor();
    void placeStamp(const QString &text);
    void placeSignature();
    void saveCurrent();
    void saveCurrentAs();
    bool confirmDiscard(DocumentTab *tab);
    void organizePages();
    void mergeDocuments();
    void splitDocument();

    QStackedWidget *m_stack;
    WelcomeWidget *m_welcome;
    QTabWidget *m_tabs;
    QToolBar *m_toolBar;
    QToolBar *m_annotBar;
    QToolButton *m_colorButton;
    QToolButton *m_stampButton;
    QActionGroup *m_toolGroup;
    QLineEdit *m_pageEdit;
    QLabel *m_pageCountLabel;
    QComboBox *m_zoomCombo;
    QMenu *m_recentMenu = nullptr;

    QAction *m_openAct;
    QAction *m_printAct;
    QAction *m_closeTabAct;
    QAction *m_reloadAct;
    QAction *m_quitAct;
    QAction *m_copyAct;
    QAction *m_selectPageAct;
    QAction *m_invertAct;
    QAction *m_findAct;
    QAction *m_findNextAct;
    QAction *m_findPrevAct;
    QAction *m_firstAct;
    QAction *m_prevAct;
    QAction *m_nextAct;
    QAction *m_lastAct;
    QAction *m_goToAct;
    QAction *m_zoomInAct;
    QAction *m_zoomOutAct;
    QAction *m_fitWidthAct;
    QAction *m_fitPageAct;
    QAction *m_actualSizeAct;
    QAction *m_sidebarAct;
    QAction *m_fullScreenAct;
    QAction *m_nextTabAct;
    QAction *m_prevTabAct;
    QAction *m_saveAct;
    QAction *m_saveAsAct;
    QAction *m_undoAct;
    QAction *m_redoAct;
    QAction *m_authorAct;
    QAction *m_annotateAct;
    QAction *m_signatureAct;
    QAction *m_organizeAct;
    QAction *m_mergeAct;
    QAction *m_splitAct;
    QAction *m_aboutAct;

    bool m_wasMaximized = false;
};
