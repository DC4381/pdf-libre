// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
// Tests de l'interface : chargement, navigation, zoom fort (rendu par zones),
// miniatures. Exécutés sans écran (QT_QPA_PLATFORM=offscreen).
#include "core/PdfDocument.h"
#include "ui/DocumentTab.h"
#include "ui/DocumentView.h"
#include "ui/PageOrganizer.h"
#include "ui/SplitDialog.h"

#include "app/Settings.h"

#include <QApplication>
#include <QDir>
#include <QFile>
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

bool hasColorIn(const QImage &image, const QRect &area, const QColor &wanted)
{
    for (int y = std::max(0, area.top()); y < std::min(image.height(), area.bottom()); y += 2) {
        for (int x = std::max(0, area.left()); x < std::min(image.width(), area.right()); x += 2) {
            const QColor c = image.pixelColor(x, y);
            if (std::abs(c.red() - wanted.red()) < 70 && std::abs(c.green() - wanted.green()) < 70 && std::abs(c.blue() - wanted.blue()) < 70)
                return true;
        }
    }
    return false;
}

// Si CARMOPDF_TEST_CAPTURES est défini, les captures y sont enregistrées
// pour un contrôle visuel.
void saveCapture(QWidget *widget, const QString &name)
{
    const QString dir = qEnvironmentVariable("CARMOPDF_TEST_CAPTURES");
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

    void organizer()
    {
        // Copie de travail : l'organiseur remplace le fichier.
        const QString work = m_dir.filePath(QStringLiteral("organiser.pdf"));
        QVERIFY(QFile::copy(m_path, work));
        PageOrganizer organizer(work, {});
        organizer.show();
        QCOMPARE(organizer.pages().size(), 60);
        QVERIFY(!organizer.isModified());

        organizer.selectPages({0});
        organizer.rotateSelection(90);
        QCOMPARE(organizer.pages()[0].rotation, 90);
        organizer.selectPages({1, 2});
        QCOMPARE(organizer.selectedPositions(), QVector<int>({1, 2}));
        organizer.deleteSelection();
        QCOMPARE(organizer.pages().size(), 58);
        QCOMPARE(organizer.pages()[1].page, 3); // l'ancienne page 4 suit la page 1
        organizer.selectPages({1});
        organizer.moveSelection(-1);
        QCOMPARE(organizer.pages()[0].page, 3);
        QCOMPARE(organizer.pages()[1].page, 0);
        organizer.insertBlankPage(1);
        QVERIFY(organizer.pages()[1].isBlank());
        QCOMPARE(organizer.pages().size(), 59);

        // Annuler deux fois (page blanche, déplacement), rétablir une fois.
        organizer.undo();
        organizer.undo();
        QCOMPARE(organizer.pages().size(), 58);
        QCOMPARE(organizer.pages()[0].page, 0);
        organizer.redo();
        QCOMPARE(organizer.pages()[0].page, 3);
        QVERIFY(organizer.isModified());

        // Miniatures : au moins une arrive en arrière-plan.
        QTRY_VERIFY_WITH_TIMEOUT(darkRatio(organizer.findChild<PageList *>()->viewport()->grab().toImage()) > 0.001, 5000);
        QTest::qWait(300);
        saveCapture(&organizer, QStringLiteral("organiseur"));

        // Enregistrement par-dessus l'original, avec sauvegarde.
        const auto result = organizer.saveTo(work);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.pageCount, 58);
        PdfDocument doc;
        QCOMPARE(doc.open(work), PdfDocument::OpenStatus::Ok);
        QCOMPARE(doc.pageCount(), 58);
        QVERIFY(doc.selectionText(0, QPointF(0, 0), QPointF(2000, 2000)).contains(QStringLiteral("Page 4 ligne 0")));
        QVERIFY(doc.pageSize(1).width() > doc.pageSize(1).height()); // page 1 tournée, devenue 2e
        QVERIFY(!organizer.isModified()); // repart du fichier enregistré
        const QDir backups(Settings::instance().backupDirectory());
        QVERIFY(!backups.entryList({QStringLiteral("organiser (*.pdf")}, QDir::Files).isEmpty());
    }

    void splitPlans()
    {
        QString error;
        auto parts = SplitDialog::plan(SplitDialog::Mode::EveryN, 7, 3, {}, {}, &error);
        QCOMPARE(parts.size(), 3);
        QCOMPARE(parts[2], QVector<int>({6}));

        parts = SplitDialog::plan(SplitDialog::Mode::Ranges, 10, 1, QStringLiteral("1-3, 5, 8-10"), {}, &error);
        QCOMPARE(parts.size(), 3);
        QCOMPARE(parts[0], QVector<int>({0, 1, 2}));
        QCOMPARE(parts[1], QVector<int>({4}));
        QVERIFY(SplitDialog::plan(SplitDialog::Mode::Ranges, 10, 1, QStringLiteral("1-12"), {}, &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(SplitDialog::plan(SplitDialog::Mode::Ranges, 10, 1, QStringLiteral("abc"), {}, &error).isEmpty());

        OutlineList outline;
        OutlineItem a;
        a.title = QStringLiteral("A");
        a.page = 2;
        OutlineItem b;
        b.title = QStringLiteral("B");
        b.page = 5;
        outline.push_back(a);
        outline.push_back(b);
        parts = SplitDialog::plan(SplitDialog::Mode::Bookmarks, 8, 1, {}, outline, &error);
        QCOMPARE(parts.size(), 3); // pages avant A, A, B
        QCOMPARE(parts[0], QVector<int>({0, 1}));
        QCOMPARE(parts[1], QVector<int>({2, 3, 4}));
        QCOMPARE(parts[2], QVector<int>({5, 6, 7}));

        // Écriture réelle.
        error.clear();
        const QString dir = m_dir.filePath(QStringLiteral("parties"));
        QVERIFY(QDir().mkpath(dir));
        const QStringList files = SplitDialog::split(m_path, {}, SplitDialog::plan(SplitDialog::Mode::EveryN, 60, 25, {}, {}),
                                                     dir, QStringLiteral("ui"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(files.size(), 3);
        QVERIFY(files[0].endsWith(QStringLiteral("ui - partie 01.pdf")));
        PdfDocument last;
        QCOMPARE(last.open(files[2]), PdfDocument::OpenStatus::Ok);
        QCOMPARE(last.pageCount(), 10);
    }

    void annotations()
    {
        const QString work = m_dir.filePath(QStringLiteral("annoter.pdf"));
        QVERIFY(QFile::copy(m_path, work));
        DocumentTab tab(work, {});
        tab.setSidebarVisible(false);
        tab.resize(1000, 800);
        tab.show();
        QSignalSpy loaded(&tab, &DocumentTab::loaded);
        QVERIFY(loaded.wait(5000));
        QTRY_VERIFY_WITH_TIMEOUT(tab.isEditable(), 5000);
        DocumentView *view = tab.view();
        view->setZoomMode(DocumentView::ZoomMode::FitWidth);
        QTRY_VERIFY_WITH_TIMEOUT(darkRatio(view->viewport()->grab().toImage()) > 0.01, 5000);
        QSignalSpy state(&tab, &DocumentTab::editStateChanged);

        // Surlignage à la souris sur la 1re ligne.
        const QRectF page = view->pageViewportRect(0);
        const double scale = page.width() / 595.0;
        view->setAnnotationColor(QColor(255, 220, 0));
        view->setTool(DocumentView::Tool::Highlight);
        const QPoint from = (page.topLeft() + QPointF(60 * scale, 50 * scale)).toPoint();
        const QPoint to = (page.topLeft() + QPointF(400 * scale, 55 * scale)).toPoint();
        QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(view->viewport(), from + QPoint(20, 2));
        QTest::mouseMove(view->viewport(), to);
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, to);
        QTRY_COMPARE_WITH_TIMEOUT(view->pageAnnotations(0).size(), 1, 5000);
        QCOMPARE(view->pageAnnotations(0).first().kind, AnnotationKind::Highlight);
        QVERIFY(tab.isModified());
        QVERIFY(tab.canUndo());

        // Dessin à main levée : deux traits rapprochés = une annotation.
        view->setTool(DocumentView::Tool::Ink);
        view->setAnnotationColor(Qt::red);
        const QPoint inkStart = (page.topLeft() + QPointF(100 * scale, 300 * scale)).toPoint();
        QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, inkStart);
        QTest::mouseMove(view->viewport(), inkStart + QPoint(40, 10));
        QTest::mouseMove(view->viewport(), inkStart + QPoint(80, -10));
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, inkStart + QPoint(80, -10));
        QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, inkStart + QPoint(0, 30));
        QTest::mouseMove(view->viewport(), inkStart + QPoint(80, 30));
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, inkStart + QPoint(80, 30));
        view->commitInk();
        QTRY_COMPARE_WITH_TIMEOUT(view->pageAnnotations(0).size(), 2, 5000);
        QCOMPARE(view->pageAnnotations(0).last().kind, AnnotationKind::Ink);

        // Rectangle, note, texte, tampon.
        view->setTool(DocumentView::Tool::Square);
        const QPoint r1 = (page.topLeft() + QPointF(300 * scale, 300 * scale)).toPoint();
        QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, r1);
        QTest::mouseMove(view->viewport(), r1 + QPoint(60, 40));
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, r1 + QPoint(60, 40));
        QTRY_COMPARE_WITH_TIMEOUT(view->pageAnnotations(0).size(), 3, 5000);
        tab.addNote(0, QPointF(450, 60), QStringLiteral("Vérifier la surface"));
        tab.addFreeText(0, QRectF(60, 500, 300, 30), QStringLiteral("Clé n° 3"), 14);
        QImage logo(80, 40, QImage::Format_ARGB32);
        logo.fill(QColor(0, 120, 255));
        tab.addStamp(0, QRectF(300, 200, 160, 80), logo, QStringLiteral("Logo"));
        QTRY_COMPARE_WITH_TIMEOUT(view->pageAnnotations(0).size(), 6, 5000);

        // Le rendu reflète les annotations : jaune sur la ligne, bleu du tampon.
        const QRect lineArea(from.x(), from.y() - 10, to.x() - from.x(), 20);
        const QRect stampArea(int(page.left() + 300 * scale), int(page.top() + 200 * scale), int(160 * scale), int(80 * scale));
        bool yellow = false;
        bool blue = false;
        for (int attempt = 0; attempt < 50 && !(yellow && blue); ++attempt) {
            QTest::qWait(100);
            const QImage shot = view->viewport()->grab().toImage();
            yellow = hasColorIn(shot, lineArea, QColor(255, 220, 0));
            blue = hasColorIn(shot, stampArea, QColor(0, 120, 255));
        }
        QVERIFY(yellow);
        QVERIFY(blue);
        view->setTool(DocumentView::Tool::Select);
        QTest::qWait(200);
        saveCapture(&tab, QStringLiteral("annotations"));

        // Sélection et suppression au clavier, puis annulation.
        view->selectAnnotation(0, view->pageAnnotations(0)[2].id);
        QTest::keyClick(view, Qt::Key_Delete);
        QTRY_COMPARE_WITH_TIMEOUT(view->pageAnnotations(0).size(), 5, 5000);
        tab.undo();
        QTRY_COMPARE_WITH_TIMEOUT(view->pageAnnotations(0).size(), 6, 5000);
        tab.editAnnotation(0, view->pageAnnotations(0)[3].id, QStringLiteral("Vérifier la surface et le cadastre"));
        QTRY_COMPARE_WITH_TIMEOUT(view->pageAnnotations(0)[3].contents, QStringLiteral("Vérifier la surface et le cadastre"), 5000);

        // Enregistrement puis relecture indépendante.
        QSignalSpy savedSpy(&tab, &DocumentTab::saved);
        tab.save();
        QVERIFY(savedSpy.wait(5000));
        QVERIFY(!tab.isModified());
        PdfDocument reader;
        QCOMPARE(reader.open(work), PdfDocument::OpenStatus::Ok);
        QVERIFY(reader.enableEditing());
        QCOMPARE(reader.annotations(0).size(), 6);
        const QDir backups(Settings::instance().backupDirectory());
        QVERIFY(!backups.entryList({QStringLiteral("annoter (*.pdf")}, QDir::Files).isEmpty());
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
