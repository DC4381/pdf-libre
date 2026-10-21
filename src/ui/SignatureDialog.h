// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <QDialog>
#include <QImage>
#include <QVector>
#include <QWidget>

class QListWidget;

// Zone de tracé de la signature, à la souris, au doigt ou au stylet.
class SignaturePad : public QWidget
{
    Q_OBJECT
public:
    explicit SignaturePad(QWidget *parent = nullptr);
    bool isEmpty() const { return m_strokes.isEmpty(); }
    void clear();
    // Image transparente, recadrée sur le tracé, avec une marge.
    QImage image() const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void tabletEvent(QTabletEvent *event) override;

private:
    struct Point
    {
        QPointF pos;
        double pressure = 1.0;
    };
    void paintStrokes(QPainter &painter, double scale, QPointF offset) const;
    QVector<QVector<Point>> m_strokes;
    bool m_drawing = false;
    bool m_tabletActive = false;
};

// Fenêtre « Signature visuelle » : dessiner, importer une image (fond blanc
// rendu transparent), réutiliser une signature enregistrée. Le résultat est
// une image à poser sur la page comme un tampon ; elle n'a aucune valeur
// cryptographique, et la fenêtre le dit.
class SignatureDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SignatureDialog(QWidget *parent = nullptr);
    QImage signature() const { return m_result; }

    // Rend transparent le fond blanc ou clair d'une image importée.
    static QImage makeTransparent(const QImage &source);
    static QString storageDirectory();

private:
    void accept() override;
    void importImage();
    void reloadSaved();
    void deleteSaved();

    SignaturePad *m_pad;
    QListWidget *m_saved;
    QImage m_imported;
    QImage m_result;
};
