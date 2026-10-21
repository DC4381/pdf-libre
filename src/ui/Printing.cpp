// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Printing.h"

#include "core/PdfDocument.h"

#include <QApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QProgressDialog>
#include <QTransform>

#include <algorithm>

void printDocument(QWidget *parent, const QString &path, const QByteArray &password, int pageCount, int currentPage)
{
    if (pageCount <= 0)
        return;

    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(QFileInfo(path).fileName());
    printer.setFromTo(1, pageCount);

    QPrintDialog dialog(&printer, parent);
    dialog.setWindowTitle(QObject::tr("Imprimer"));
    dialog.setOptions(QAbstractPrintDialog::PrintPageRange | QAbstractPrintDialog::PrintCurrentPage
                      | QAbstractPrintDialog::PrintToFile | QAbstractPrintDialog::PrintShowPageSize);
    dialog.setMinMax(1, pageCount);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QVector<int> pages;
    switch (printer.printRange()) {
    case QPrinter::PageRange:
        for (int p = printer.fromPage(); p <= printer.toPage(); ++p)
            pages.append(p - 1);
        break;
    case QPrinter::CurrentPage:
        pages.append(currentPage);
        break;
    default:
        for (int p = 0; p < pageCount; ++p)
            pages.append(p);
        break;
    }

    PdfDocument doc;
    if (doc.open(path, password) != PdfDocument::OpenStatus::Ok) {
        QMessageBox::warning(parent, QObject::tr("Imprimer"), QObject::tr("Impossible de relire le document pour l'impression."));
        return;
    }

    QPainter painter;
    if (!painter.begin(&printer)) {
        QMessageBox::warning(parent, QObject::tr("Imprimer"), QObject::tr("L'imprimante n'a pas pu être initialisée."));
        return;
    }
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    QProgressDialog progress(QObject::tr("Impression en cours…"), QObject::tr("Annuler"), 0, int(pages.size()), parent);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);

    // 300 dpi suffisent pour le texte et limitent la mémoire envoyée au pilote.
    const double maxScale = std::min(printer.resolution(), 300) / 72.0;
    for (int i = 0; i < pages.size(); ++i) {
        if (progress.wasCanceled()) {
            printer.abort();
            break;
        }
        if (i > 0)
            printer.newPage();
        const int page = pages.at(i);
        const QSizeF size = doc.pageSize(page);
        const QRect area = painter.viewport();
        const bool rotate = (size.width() > size.height()) != (area.width() > area.height());
        const QSizeF oriented = rotate ? size.transposed() : size;
        const double fit = std::min(area.width() / oriented.width(), area.height() / oriented.height());

        QImage image = doc.render(page, std::min(fit, maxScale));
        if (image.isNull())
            continue;
        if (rotate)
            image = image.transformed(QTransform().rotate(90));
        const QSizeF drawn = oriented * fit;
        const QRectF target((area.width() - drawn.width()) / 2, (area.height() - drawn.height()) / 2, drawn.width(),
                            drawn.height());
        painter.drawImage(target, image);
        progress.setValue(i + 1);
        QApplication::processEvents();
    }
    painter.end();
}
