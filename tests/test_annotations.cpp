// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
// Tests des annotations : création de chaque type, rendu visible, liste,
// modification, suppression, annuler/rétablir, enregistrement incrémental et
// relecture par une instance indépendante.
#include "core/PdfDocument.h"

#include <QFile>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QTemporaryDir>
#include <QtTest>

namespace {
double darkRatio(const QImage &image, const QRect &area)
{
    qint64 dark = 0, total = 0;
    for (int y = area.top(); y <= area.bottom(); ++y) {
        for (int x = area.left(); x <= area.right(); ++x) {
            ++total;
            dark += qGray(image.pixel(x, y)) < 160;
        }
    }
    return total ? double(dark) / total : 0;
}

bool hasColor(const QImage &image, const QRect &area, const QColor &wanted, int tolerance = 60)
{
    for (int y = area.top(); y <= area.bottom(); ++y) {
        for (int x = area.left(); x <= area.right(); ++x) {
            const QColor c = image.pixelColor(x, y);
            if (std::abs(c.red() - wanted.red()) < tolerance && std::abs(c.green() - wanted.green()) < tolerance
                && std::abs(c.blue() - wanted.blue()) < tolerance)
                return true;
        }
    }
    return false;
}
} // namespace

class TestAnnotations : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_path;

    QString writeSample(const QString &name)
    {
        const QString path = m_dir.filePath(name);
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setResolution(72);
        QPainter painter(&writer);
        QFont font = painter.font();
        font.setPointSize(18);
        painter.setFont(font);
        for (int i = 0; i < 3; ++i) {
            if (i > 0)
                writer.newPage();
            painter.drawText(72, 100, QStringLiteral("Page %1 : compromis de vente").arg(i + 1));
        }
        return path;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_path = writeSample(QStringLiteral("annoter.pdf"));
    }

    void createEveryKind()
    {
        PdfDocument doc;
        QCOMPARE(doc.open(m_path), PdfDocument::OpenStatus::Ok);
        QVERIFY(doc.enableEditing());
        doc.setAuthor(QStringLiteral("David Carmo"));

        const QRectF word = doc.search(0, QStringLiteral("compromis")).first();
        const int highlight = doc.addTextMarkup(0, AnnotationKind::Highlight, {word}, QColor(255, 220, 0));
        QVERIFY(highlight > 0);
        QVERIFY(doc.addTextMarkup(0, AnnotationKind::Underline, {word}, Qt::blue) > 0);
        QVERIFY(doc.addTextMarkup(0, AnnotationKind::StrikeOut, {word}, Qt::red) > 0);
        const int note = doc.addNote(0, QPointF(400, 90), QStringLiteral("À vérifier avec le notaire"), QColor(255, 200, 0));
        QVERIFY(note > 0);
        QVERIFY(doc.addFreeText(0, QRectF(72, 200, 300, 40), QStringLiteral("Texte ajouté : clé n° 3, étage 2"), 14, Qt::darkBlue) > 0);
        InkStroke stroke;
        for (int i = 0; i <= 20; ++i)
            stroke.append(QPointF(100 + i * 10, 400 + (i % 2) * 15));
        QVERIFY(doc.addInk(0, {stroke}, Qt::red, 3) > 0);
        QVERIFY(doc.addShape(0, AnnotationKind::Square, QRectF(100, 450, 200, 80), Qt::red, QColor(255, 0, 0, 60), 2) > 0);
        QVERIFY(doc.addShape(0, AnnotationKind::Circle, QRectF(350, 450, 120, 80), Qt::darkGreen, QColor(), 2) > 0);
        QVERIFY(doc.addShape(0, AnnotationKind::Arrow, QRectF(100, 600, 300, 650), Qt::black, QColor(), 2) > 0);
        QVERIFY(doc.addStampText(0, QRectF(300, 700, 220, 60), QStringLiteral("APPROUVÉ"), QColor(196, 43, 28)) > 0);
        QImage logo(60, 30, QImage::Format_ARGB32);
        logo.fill(QColor(0, 120, 255));
        QVERIFY(doc.addStampImage(1, QRectF(72, 72, 120, 60), logo) > 0);

        const auto list = doc.annotations(0);
        QCOMPARE(list.size(), 10);
        QCOMPARE(list.first().kind, AnnotationKind::Highlight);
        QCOMPARE(list.first().author, QStringLiteral("David Carmo"));
        QVERIFY(list.first().modified.isValid());
        QVERIFY(list.first().rect.intersects(word));
        bool noteFound = false;
        for (const AnnotationInfo &info : list) {
            if (info.id == note) {
                noteFound = true;
                QCOMPARE(info.kind, AnnotationKind::Note);
                QCOMPARE(info.contents, QStringLiteral("À vérifier avec le notaire"));
            }
        }
        QVERIFY(noteFound);
        QCOMPARE(doc.annotations(1).size(), 1);
        QCOMPARE(doc.annotations(1).first().kind, AnnotationKind::Stamp);
        QCOMPARE(doc.annotations(2).size(), 0);

        // Rendu : le surlignage jaune, le dessin rouge et le tampon sont visibles.
        const QImage image = doc.render(0, 1.0);
        QVERIFY(hasColor(image, word.toAlignedRect(), QColor(255, 220, 0)));
        QVERIFY(hasColor(image, QRect(100, 395, 200, 25), Qt::red));
        QVERIFY(darkRatio(image, QRect(300, 700, 220, 60)) > 0.01);
        QVERIFY(darkRatio(image, QRect(72, 200, 300, 40)) > 0.005); // texte libre
        const QImage page2 = doc.render(1, 1.0);
        QVERIFY(hasColor(page2, QRect(72, 72, 120, 60), QColor(0, 120, 255)));

        // Enregistrement incrémental, puis relecture par une autre instance.
        QVERIFY(doc.hasUnsavedChanges());
        QVERIFY2(doc.save(m_path), qPrintable(doc.lastError()));
        QVERIFY(!doc.hasUnsavedChanges());
        PdfDocument reader;
        QCOMPARE(reader.open(m_path), PdfDocument::OpenStatus::Ok);
        QVERIFY(reader.enableEditing());
        QCOMPARE(reader.annotations(0).size(), 10);
        QCOMPARE(reader.annotations(1).size(), 1);
        QVERIFY(hasColor(reader.render(0, 1.0), word.toAlignedRect(), QColor(255, 220, 0)));
    }

    void editDeleteUndoRedo()
    {
        const QString path = writeSample(QStringLiteral("annuler.pdf"));
        PdfDocument doc;
        QCOMPARE(doc.open(path), PdfDocument::OpenStatus::Ok);
        QVERIFY(doc.enableEditing());
        QVERIFY(!doc.canUndo());

        const int note = doc.addNote(1, QPointF(100, 100), QStringLiteral("Première"), Qt::yellow);
        QVERIFY(note > 0);
        QVERIFY(doc.canUndo());
        QVERIFY(doc.setAnnotationContents(1, note, QStringLiteral("Seconde")));
        QCOMPARE(doc.annotations(1).first().contents, QStringLiteral("Seconde"));
        QVERIFY(doc.setAnnotationRect(1, note, QRectF(200, 200, 20, 20)));
        QVERIFY(doc.annotations(1).first().rect.intersects(QRectF(195, 195, 30, 30)));

        QCOMPARE(doc.undo(), 1); // retour à la position initiale
        QVERIFY(doc.annotations(1).first().rect.intersects(QRectF(95, 95, 30, 30)));
        QCOMPARE(doc.undo(), 1); // retour au texte « Première »
        QCOMPARE(doc.annotations(1).first().contents, QStringLiteral("Première"));
        QCOMPARE(doc.redo(), 1);
        QCOMPARE(doc.annotations(1).first().contents, QStringLiteral("Seconde"));
        QVERIFY(doc.canRedo());

        QVERIFY(doc.deleteAnnotation(1, note));
        QVERIFY(doc.annotations(1).isEmpty());
        QVERIFY(!doc.canRedo());
        QCOMPARE(doc.undo(), 1);
        QCOMPARE(doc.annotations(1).size(), 1);
        QVERIFY(!doc.deleteAnnotation(1, 999999)); // identifiant inconnu
        QVERIFY(!doc.setAnnotationContents(0, note, QStringLiteral("x"))); // mauvaise page

        // Copie complète vers un autre fichier.
        const QString copy = m_dir.filePath(QStringLiteral("copie.pdf"));
        QVERIFY2(doc.save(copy), qPrintable(doc.lastError()));
        PdfDocument reader;
        QCOMPARE(reader.open(copy), PdfDocument::OpenStatus::Ok);
        QVERIFY(reader.enableEditing());
        QCOMPARE(reader.annotations(1).size(), 1);
        QCOMPARE(reader.annotations(1).first().contents, QStringLiteral("Seconde"));
    }

    void notEditable()
    {
        PdfDocument doc;
        QVERIFY(!doc.enableEditing());
        QVERIFY(doc.annotations(0).isEmpty());
        QCOMPARE(doc.addNote(0, QPointF(), QStringLiteral("x"), Qt::yellow), -1);
        QVERIFY(!doc.save(m_dir.filePath(QStringLiteral("rien.pdf"))));
    }
};

QTEST_MAIN(TestAnnotations)
#include "test_annotations.moc"
