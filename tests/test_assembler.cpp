// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
// Tests de l'assembleur : fusion, extraction, réordonnancement, rotation,
// suppression, pages blanches, conservation des signets, fichier de sortie valide.
#include "core/PdfAssembler.h"
#include "core/PdfDocument.h"

#include <QFile>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QTemporaryDir>
#include <QtTest>

namespace {
// PDF minimal écrit à la main, avec 4 pages et des signets vers les pages 1, 2 et 4.
// Sans table xref : MuPDF la reconstruit, ce qui suffit pour les tests.
QByteArray outlinedPdf()
{
    QByteArray pdf = "%PDF-1.4\n";
    pdf += "1 0 obj << /Type /Catalog /Pages 2 0 R /Outlines 20 0 R >> endobj\n";
    pdf += "2 0 obj << /Type /Pages /Kids [3 0 R 4 0 R 5 0 R 6 0 R] /Count 4 >> endobj\n";
    for (int i = 0; i < 4; ++i) {
        const QByteArray content = "BT /F1 24 Tf 72 700 Td (Page " + QByteArray::number(i + 1) + ") Tj ET";
        pdf += QByteArray::number(3 + i) + " 0 obj << /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] "
               "/Resources << /Font << /F1 10 0 R >> >> /Contents " + QByteArray::number(11 + i) + " 0 R >> endobj\n";
        pdf += QByteArray::number(11 + i) + " 0 obj << /Length " + QByteArray::number(content.size()) + " >> stream\n"
               + content + "\nendstream endobj\n";
    }
    pdf += "10 0 obj << /Type /Font /Subtype /Type1 /BaseFont /Helvetica >> endobj\n";
    pdf += "20 0 obj << /Type /Outlines /First 21 0 R /Last 23 0 R /Count 3 >> endobj\n";
    pdf += "21 0 obj << /Title (Un) /Parent 20 0 R /Next 22 0 R /Dest [3 0 R /XYZ null null null] >> endobj\n";
    pdf += "22 0 obj << /Title (Deux) /Parent 20 0 R /Prev 21 0 R /Next 23 0 R /Dest [4 0 R /XYZ null null null] >> endobj\n";
    pdf += "23 0 obj << /Title (Quatre) /Parent 20 0 R /Prev 22 0 R /Dest [6 0 R /XYZ null null null] >> endobj\n";
    pdf += "trailer << /Root 1 0 R >>\n%%EOF\n";
    return pdf;
}
} // namespace

class TestAssembler : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_a; // 3 pages A4 portrait "A1".."A3"
    QString m_b; // 2 pages paysage "B1", "B2"
    QString m_outlined;

    QString writeSample(const QString &name, const QString &prefix, int pages, bool landscape)
    {
        const QString path = m_dir.filePath(name);
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setPageOrientation(landscape ? QPageLayout::Landscape : QPageLayout::Portrait);
        writer.setResolution(72);
        QPainter painter(&writer);
        QFont font = painter.font();
        font.setPointSize(24);
        painter.setFont(font);
        for (int i = 0; i < pages; ++i) {
            if (i > 0)
                writer.newPage();
            painter.drawText(72, 100, prefix + QString::number(i + 1));
        }
        return path;
    }

    static QStringList pageTexts(const QString &path)
    {
        PdfDocument doc;
        if (doc.open(path) != PdfDocument::OpenStatus::Ok)
            return {QStringLiteral("<illisible>")};
        QStringList texts;
        for (int i = 0; i < doc.pageCount(); ++i)
            texts.append(doc.selectionText(i, QPointF(0, 0), QPointF(2000, 2000)).trimmed());
        return texts;
    }

    static PageRef ref(const QString &path, int page, int rotation = 0)
    {
        PageRef r;
        r.sourcePath = path;
        r.page = page;
        r.rotation = rotation;
        return r;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_a = writeSample(QStringLiteral("a.pdf"), QStringLiteral("A"), 3, false);
        m_b = writeSample(QStringLiteral("b.pdf"), QStringLiteral("B"), 2, true);
        m_outlined = m_dir.filePath(QStringLiteral("signets.pdf"));
        QFile file(m_outlined);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(outlinedPdf());
        file.close();
    }

    void mergeAndReorder()
    {
        const QString out = m_dir.filePath(QStringLiteral("fusion.pdf"));
        const auto result = PdfAssembler::assemble({ref(m_b, 1), ref(m_a, 2), ref(m_a, 0), ref(m_b, 0)}, out);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.pageCount, 4);
        QCOMPARE(pageTexts(out), QStringList({"B2", "A3", "A1", "B1"}));
        PdfDocument doc;
        QCOMPARE(doc.open(out), PdfDocument::OpenStatus::Ok);
        QVERIFY(doc.pageSize(0).width() > doc.pageSize(0).height()); // paysage conservé
        QVERIFY(doc.pageSize(1).width() < doc.pageSize(1).height());
    }

    void deleteExtractAndRotate()
    {
        const QString out = m_dir.filePath(QStringLiteral("extrait.pdf"));
        // Suppression de A2, rotation de A3 à 90°.
        const auto result = PdfAssembler::assemble({ref(m_a, 0), ref(m_a, 2, 90)}, out);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(pageTexts(out), QStringList({"A1", "A3"}));
        PdfDocument doc;
        QCOMPARE(doc.open(out), PdfDocument::OpenStatus::Ok);
        QVERIFY(doc.pageSize(0).width() < doc.pageSize(0).height());
        QVERIFY(doc.pageSize(1).width() > doc.pageSize(1).height()); // tournée
        // Double rotation : 90 puis 270 = 0.
        const QString out2 = m_dir.filePath(QStringLiteral("rotation.pdf"));
        QVERIFY(PdfAssembler::assemble({ref(out, 1, 270)}, out2).ok);
        PdfDocument doc2;
        QCOMPARE(doc2.open(out2), PdfDocument::OpenStatus::Ok);
        QVERIFY(doc2.pageSize(0).width() < doc2.pageSize(0).height());
    }

    void blankPageAndDuplicate()
    {
        const QString out = m_dir.filePath(QStringLiteral("blanche.pdf"));
        PageRef blank;
        blank.blankWidth = 400;
        blank.blankHeight = 300;
        const auto result = PdfAssembler::assemble({ref(m_a, 0), blank, ref(m_a, 0)}, out);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(pageTexts(out), QStringList({"A1", "", "A1"}));
        PdfDocument doc;
        QCOMPARE(doc.open(out), PdfDocument::OpenStatus::Ok);
        QCOMPARE(doc.pageSize(1), QSizeF(400, 300));
    }

    void outlineKeptAndRemapped()
    {
        // Suppression de la page 2 et inversion des pages 1 et 4.
        const QString out = m_dir.filePath(QStringLiteral("signets-out.pdf"));
        const auto result = PdfAssembler::assemble({ref(m_outlined, 3), ref(m_outlined, 2), ref(m_outlined, 0)}, out);
        QVERIFY2(result.ok, qPrintable(result.error));
        PdfDocument doc;
        QCOMPARE(doc.open(out), PdfDocument::OpenStatus::Ok);
        const OutlineList outline = doc.outline();
        QCOMPARE(int(outline.size()), 2); // « Deux » pointait vers une page supprimée
        QCOMPARE(outline[0].title, QStringLiteral("Un"));
        QCOMPARE(outline[0].page, 2);
        QCOMPARE(outline[1].title, QStringLiteral("Quatre"));
        QCOMPARE(outline[1].page, 0);
    }

    void outlineDroppedWhenMerging()
    {
        const QString out = m_dir.filePath(QStringLiteral("fusion-signets.pdf"));
        QVERIFY(PdfAssembler::assemble({ref(m_outlined, 0), ref(m_a, 0)}, out).ok);
        PdfDocument doc;
        QCOMPARE(doc.open(out), PdfDocument::OpenStatus::Ok);
        QVERIFY(doc.outline().empty());
    }

    void overwriteIsAtomic()
    {
        const QString out = m_dir.filePath(QStringLiteral("ecrase.pdf"));
        QVERIFY(PdfAssembler::assemble({ref(m_a, 0)}, out).ok);
        // Une erreur (page inexistante) ne doit pas toucher le fichier existant.
        const auto bad = PdfAssembler::assemble({ref(m_a, 99)}, out);
        QVERIFY(!bad.ok);
        QVERIFY(!bad.error.isEmpty());
        QCOMPARE(pageTexts(out), QStringList({"A1"}));
        QVERIFY(QDir(m_dir.path()).entryList({QStringLiteral(".carmopdf-*")}, QDir::Files | QDir::Hidden).isEmpty());
    }

    void errors()
    {
        const QString out = m_dir.filePath(QStringLiteral("erreur.pdf"));
        QVERIFY(!PdfAssembler::assemble({}, out).ok);
        QVERIFY(!PdfAssembler::assemble({ref(m_dir.filePath(QStringLiteral("absent.pdf")), 0)}, out).ok);
        QVERIFY(!QFile::exists(out));
        QCOMPARE(PdfAssembler::signatureCount(m_a), 0);
    }
};

QTEST_MAIN(TestAssembler)
#include "test_assembler.moc"
