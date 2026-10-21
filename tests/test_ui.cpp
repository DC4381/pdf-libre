// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
// Tests de l'interface : chargement, navigation, zoom fort (rendu par zones),
// miniatures. Exécutés sans écran (QT_QPA_PLATFORM=offscreen).
#include "ui/DocumentTab.h"
#include "ui/DocumentView.h"

#include <QApplication>
#include <QClipboard>
#include <QListView>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {
// Proportion de pixels sombres (texte) dans une image.
double darkRatio(const QImage &image)
{
    const QImage img = image.convertToFormat(QImage::Format_RGB32);
    qint64 dark = 0;
    for (int y = 0; y < img.height(); y += 2) {
        const auto *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); x += 2)
            dark += qGray(line[x]) < 100;
    }
    return double(dark) / (double(img.width() / 2) * (img.height() / 2));
}

// Si PDFLIBRE_TEST_CAPTURES est défini, les captures y sont enregistrées
// pour un contrôle visuel.
void saveCapture(QWidget *widget, const QString &name)
{
    const QString dir = qEnvironmentVariable("PDFLIBRE_TEST_CAPTURES");
    if (!dir.isEmpty())
        widget->grab().save(dir + QLatin1Char('/') + name + QStringLiteral(".png"));
}
} // namespace

class TestUi : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_path;

private slots:
    void initTestCase()
    {
        qRegisterMetaType<OutlineList>();
        qRegisterMetaType<RenderRequest>();
        m_path = m_dir.filePath(QStringLiteral("ui.pdf"));
        QPdfWriter writer(m_path);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setResolution(72);
        QPainter painter(&writer);
        QFont font = painter.font();
        font.setPointSize(30);
        painter.setFont(font);
        for (int i = 0; i < 60; ++i) {
            if (i > 0)
                writer.newPage();
            for (int line = 0; line < 14; ++line)
                painter.drawText(40, 60 + line * 50, QStringLiteral("Page %1 ligne %2 MMMMMMMM").arg(i + 1).arg(line));
        }
    }

    void loadNavigateZoom()
    {
        DocumentTab tab(m_path);
        tab.resize(1000, 800);
        tab.show();
        QSignalSpy loaded(&tab, &DocumentTab::loaded);
        QVERIFY(loaded.wait(5000));
        QCOMPARE(tab.pageCount(), 60);

        DocumentView *view = tab.view();
        view->goToPage(41);
        QCOMPARE(view->currentPage(), 41);

        // Rendu net de la page visible.
        QTRY_VERIFY_WITH_TIMEOUT(darkRatio(view->viewport()->grab().toImage()) > 0.01, 5000);

        // Zoom 1600 % : la page dépasse la limite du rendu complet ; la zone
        // visible doit tout de même être rendue (texte présent).
        view->setZoom(16.0);
        QCOMPARE(view->zoomMode(), DocumentView::ZoomMode::Custom);
        QCOMPARE(view->currentPage(), 41);
        QTRY_VERIFY_WITH_TIMEOUT(darkRatio(view->viewport()->grab().toImage()) > 0.02, 8000);
        QTest::qWait(300);
        saveCapture(&tab, QStringLiteral("zoom-1600"));

        view->setZoomMode(DocumentView::ZoomMode::FitPage);
        QCOMPARE(view->currentPage(), 41);
        view->nextPage();
        QCOMPARE(view->currentPage(), 42);
    }

    void thumbnails()
    {
        DocumentTab tab(m_path);
        tab.setSidebarVisible(true);
        tab.resize(1000, 800);
        tab.show();
        QSignalSpy loaded(&tab, &DocumentTab::loaded);
        QVERIFY(loaded.wait(5000));
        auto *list = tab.findChild<QListView *>();
        QVERIFY(list);
        // Les miniatures visibles finissent par contenir du texte.
        QTRY_VERIFY_WITH_TIMEOUT(darkRatio(list->viewport()->grab().toImage()) > 0.002, 5000);
        QTest::qWait(300);
        saveCapture(&tab, QStringLiteral("miniatures"));
    }

    void selectionWithMouse()
    {
        DocumentTab tab(m_path);
        tab.setSidebarVisible(false);
        tab.resize(1000, 800);
        tab.show();
        QSignalSpy loaded(&tab, &DocumentTab::loaded);
        QVERIFY(loaded.wait(5000));
        DocumentView *view = tab.view();
        view->setZoomMode(DocumentView::ZoomMode::FitWidth);
        QTRY_VERIFY_WITH_TIMEOUT(darkRatio(view->viewport()->grab().toImage()) > 0.01, 5000);

        // Glisser de la 1re à la 3e ligne de la page 1.
        const QRectF page = view->pageViewportRect(0);
        const double scale = page.width() / 595.0;
        const QPoint from = (page.topLeft() + QPointF(80 * scale, 50 * scale)).toPoint();
        const QPoint to = (page.topLeft() + QPointF(400 * scale, 150 * scale)).toPoint();
        QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(view->viewport(), from + QPoint(30, 10));
        QTest::mouseMove(view->viewport(), to);
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, to);
        QVERIFY(view->hasSelection());
        const QString text = view->selectedText();
        QVERIFY2(text.contains(QStringLiteral("ligne 0 MMMMMMMM")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("ligne 2")), qPrintable(text));
        saveCapture(&tab, QStringLiteral("selection"));

        view->copySelection();
        QCOMPARE(QApplication::clipboard()->text(), text);

        // Double-clic : un mot.
        const QPoint word = (page.topLeft() + QPointF(60 * scale, 60 * scale)).toPoint();
        QTest::mouseDClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, word);
        QCOMPARE(view->selectedText(), QStringLiteral("Page"));

        // Sélection de toute la page, puis Échap.
        view->selectAllOnPage(5);
        QVERIFY(view->selectedText().contains(QStringLiteral("Page 6 ligne 13")));
        QTest::keyClick(view, Qt::Key_Escape);
        QVERIFY(!view->hasSelection());

        // Hors du texte : clic puis glisser = déplacement du document.
        QTest::qWait(QApplication::doubleClickInterval() + 50); // sinon ce clic compte comme un triple-clic
        const int before = view->verticalScrollBar()->value();
        const QPoint blank = (page.topLeft() + QPointF(300 * scale, 12 * scale)).toPoint(); // marge haute, sans texte
        QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, blank);
        QTest::mouseMove(view->viewport(), blank - QPoint(0, 120));
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, blank - QPoint(0, 120));
        QCOMPARE(view->verticalScrollBar()->value(), before + 120);
        QVERIFY(!view->hasSelection());
    }

    void invertColors()
    {
        DocumentTab tab(m_path);
        tab.setSidebarVisible(false);
        tab.resize(600, 500);
        tab.show();
        QSignalSpy loaded(&tab, &DocumentTab::loaded);
        QVERIFY(loaded.wait(5000));
        DocumentView *view = tab.view();
        QTRY_VERIFY_WITH_TIMEOUT(darkRatio(view->viewport()->grab().toImage()) > 0.01, 5000);
        view->setInvertColors(true);
        // Fond de page noir : la proportion de pixels sombres devient majoritaire.
        QVERIFY(darkRatio(view->viewport()->grab().toImage()) > 0.5);
        saveCapture(&tab, QStringLiteral("mode-nuit"));
        view->setInvertColors(false);
        QVERIFY(darkRatio(view->viewport()->grab().toImage()) < 0.5);
    }

    void search()
    {
        DocumentTab tab(m_path);
        tab.resize(1000, 800);
        tab.show();
        QSignalSpy loaded(&tab, &DocumentTab::loaded);
        QVERIFY(loaded.wait(5000));
        tab.search(QStringLiteral("Page 30 ligne"));
        QTRY_COMPARE_WITH_TIMEOUT(tab.view()->currentPage(), 29, 5000);
        tab.findNext();
        QCOMPARE(tab.view()->currentPage(), 29);
    }
};

QTEST_MAIN(TestUi)
#include "test_ui.moc"
