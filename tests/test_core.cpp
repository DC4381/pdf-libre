// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
// Tests du noyau : ouverture, rendu, recherche, mots de passe, robustesse.
// Les PDF de test sont générés à la volée : aucun fichier binaire dans le dépôt.
#include "core/PdfDocument.h"
#include "core/RenderService.h"
#include "core/SearchService.h"

#include <QFile>
#include <QGuiApplication>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class TestCore : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_sample;

    QString writeSample(int pages)
    {
        const QString path = m_dir.filePath(QStringLiteral("exemple-%1.pdf").arg(pages));
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setResolution(72);
        QPainter painter(&writer);
        QFont font = painter.font();
        font.setPointSize(18);
        painter.setFont(font);
        for (int i = 0; i < pages; ++i) {
            if (i > 0)
                writer.newPage();
            painter.drawText(72, 100, QStringLiteral("Page %1 : compromis de vente").arg(i + 1));
            if (i % 2 == 1)
                painter.drawText(72, 200, QStringLiteral("Acquéreur Dupont"));
            painter.fillRect(QRect(72, 300, 200, 100), Qt::black);
        }
        painter.end();
        return path;
    }

private slots:
    void initTestCase()
    {
        qRegisterMetaType<OutlineList>();
        qRegisterMetaType<RenderRequest>();
        QVERIFY(m_dir.isValid());
        m_sample = writeSample(6);
    }

    void openAndCount()
    {
        PdfDocument doc;
        QCOMPARE(doc.open(m_sample), PdfDocument::OpenStatus::Ok);
        QCOMPARE(doc.pageCount(), 6);
        const QSizeF size = doc.pageSize(0);
        QVERIFY(qAbs(size.width() - 595) < 2);
        QVERIFY(qAbs(size.height() - 842) < 2);
    }

    void renderPage()
    {
        PdfDocument doc;
        QCOMPARE(doc.open(m_sample), PdfDocument::OpenStatus::Ok);
        const QImage image = doc.render(0, 1.0);
        QVERIFY(!image.isNull());
        QVERIFY(qAbs(image.width() - 595) <= 1);
        // Le rectangle noir dessiné en (72,300) doit apparaître.
        QCOMPARE(QColor(image.pixel(150, 350)), QColor(Qt::black));
        QCOMPARE(QColor(image.pixel(5, 5)), QColor(Qt::white));
    }

    void renderClip()
    {
        PdfDocument doc;
        QCOMPARE(doc.open(m_sample), PdfDocument::OpenStatus::Ok);
        const QImage tile = doc.render(0, 2.0, QRect(144, 600, 100, 80));
        QCOMPARE(tile.size(), QSize(100, 80));
        QCOMPARE(QColor(tile.pixel(50, 40)), QColor(Qt::black));
    }

    void search()
    {
        PdfDocument doc;
        QCOMPARE(doc.open(m_sample), PdfDocument::OpenStatus::Ok);
        QCOMPARE(doc.search(0, QStringLiteral("compromis")).size(), 1);
        QCOMPARE(doc.search(0, QStringLiteral("COMPROMIS")).size(), 1); // insensible à la casse
        QCOMPARE(doc.search(0, QStringLiteral("Dupont")).size(), 0);
        QCOMPARE(doc.search(1, QStringLiteral("Acquéreur")).size(), 1); // accents
        const QRectF hit = doc.search(0, QStringLiteral("compromis")).first();
        // Ligne écrite vers y = 100 pt (plus la marge de QPdfWriter).
        QVERIFY(hit.top() > 70 && hit.bottom() < 130 && hit.width() > 50);
    }

    void selectionAndCopy()
    {
        PdfDocument doc;
        QCOMPARE(doc.open(m_sample), PdfDocument::OpenStatus::Ok);
        const QRectF word = doc.search(0, QStringLiteral("compromis")).first();
        QVERIFY(doc.hasTextAt(0, word.center()));
        QVERIFY(!doc.hasTextAt(0, QPointF(5, 5)));
        // Sélection du mot par double-clic (extension au mot).
        const QString text = doc.selectionText(0, word.center(), word.center(), PdfDocument::SnapMode::Words);
        QCOMPARE(text.trimmed(), QStringLiteral("compromis"));
        // Sélection de toute la ligne : les accents doivent être conservés.
        const QString line = doc.selectionText(1, QPointF(0, 150), QPointF(600, 250), PdfDocument::SnapMode::Lines);
        QVERIFY2(line.contains(QStringLiteral("Acquéreur Dupont")), qPrintable(line));
        QVERIFY(!doc.selectionRects(0, word.topLeft(), word.bottomRight()).isEmpty());
        QVERIFY(doc.selectionRects(0, QPointF(1, 1), QPointF(2, 2)).isEmpty());
    }

    void links()
    {
        PdfDocument doc;
        QCOMPARE(doc.open(m_sample), PdfDocument::OpenStatus::Ok);
        QVERIFY(doc.links(0).isEmpty()); // le document de test n'a pas de lien
    }

    void searchService()
    {
        SearchService service(m_sample, {});
        QSignalSpy results(&service, &SearchService::pageResults);
        QSignalSpy done(&service, &SearchService::finished);
        service.search(QStringLiteral("Dupont"), 0);
        QVERIFY(done.wait(5000));
        QCOMPARE(results.count(), 3); // pages 2, 4 et 6
    }

    void renderService()
    {
        RenderService service(m_sample, {});
        QSignalSpy opened(&service, &RenderService::opened);
        QSignalSpy rendered(&service, &RenderService::rendered);
        QVERIFY(opened.wait(5000));
        QCOMPARE(opened.first().at(0).toInt(), 6);
        service.setRequests({RenderRequest{2, 300, false, {}}}, {RenderRequest{0, 80, true, {}}});
        QTRY_COMPARE_WITH_TIMEOUT(rendered.count(), 2, 5000);
        const auto first = rendered.at(0).at(0).value<RenderRequest>();
        QCOMPARE(first.page, 2); // l'urgent passe avant la miniature
        QCOMPARE(rendered.at(0).at(1).value<QImage>().width(), 300);
    }

    void garbageFile()
    {
        const QString path = m_dir.filePath(QStringLiteral("abime.pdf"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("%PDF-1.7\nceci n'est pas un PDF valide\n%%EOF");
        file.close();
        PdfDocument doc;
        QVERIFY(doc.open(path) != PdfDocument::OpenStatus::Ok || doc.pageCount() >= 0);
        QCOMPARE(PdfDocument().open(m_dir.filePath(QStringLiteral("absent.pdf"))), PdfDocument::OpenStatus::Failed);
    }

    void largeDocument()
    {
        const QString path = writeSample(400);
        QElapsedTimer timer;
        timer.start();
        PdfDocument doc;
        QCOMPARE(doc.open(path), PdfDocument::OpenStatus::Ok);
        QVERIFY(!doc.render(0, 1.5).isNull());
        qInfo("Ouverture + rendu de la 1re page (400 pages) : %lld ms", timer.elapsed());
        QVERIFY(timer.elapsed() < 2000);
    }
};

QTEST_MAIN(TestCore)
#include "test_core.moc"
