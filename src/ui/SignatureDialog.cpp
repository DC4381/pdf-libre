// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SignatureDialog.h"

#include "app/Settings.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTabletEvent>
#include <QVBoxLayout>

#include <algorithm>

// --- Tracé -------------------------------------------------------------------

SignaturePad::SignaturePad(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(520, 200);
    setCursor(Qt::CrossCursor);
    setAccessibleName(tr("Zone de signature"));
    setAttribute(Qt::WA_AcceptTouchEvents);
}

void SignaturePad::clear()
{
    m_strokes.clear();
    update();
}

void SignaturePad::paintStrokes(QPainter &painter, double scale, QPointF offset) const
{
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(Qt::NoBrush);
    for (const auto &stroke : m_strokes) {
        for (int i = 1; i < stroke.size(); ++i) {
            // Épaisseur liée à la pression du stylet : 1,5 à 4 pixels.
            const double width = (1.5 + 2.5 * stroke[i].pressure) * scale;
            painter.setPen(QPen(QColor(20, 30, 90), width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawLine((stroke[i - 1].pos - offset) * scale, (stroke[i].pos - offset) * scale);
        }
        if (stroke.size() == 1) {
            painter.setBrush(QColor(20, 30, 90));
            painter.setPen(Qt::NoPen);
            painter.drawEllipse((stroke.first().pos - offset) * scale, 1.5 * scale, 1.5 * scale);
            painter.setBrush(Qt::NoBrush);
        }
    }
}

void SignaturePad::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::white);
    painter.setPen(QPen(QColor(180, 180, 180), 1, Qt::DashLine));
    painter.drawLine(20, height() - 40, width() - 20, height() - 40);
    if (m_strokes.isEmpty()) {
        painter.setPen(QColor(150, 150, 150));
        painter.drawText(rect(), Qt::AlignCenter, tr("Signez ici, au stylet, au doigt ou à la souris"));
    }
    paintStrokes(painter, 1.0, QPointF(0, 0));
}

void SignaturePad::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || m_tabletActive)
        return;
    m_strokes.append({Point{event->position(), 1.0}});
    m_drawing = true;
    update();
}

void SignaturePad::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_drawing || m_tabletActive)
        return;
    m_strokes.last().append(Point{event->position(), 1.0});
    update();
}

void SignaturePad::mouseReleaseEvent(QMouseEvent *)
{
    m_drawing = false;
}

void SignaturePad::tabletEvent(QTabletEvent *event)
{
    // Stylet (Surface Pen) : pression prise en compte ; les événements souris
    // synthétisés sont ignorés pendant ce temps.
    switch (event->type()) {
    case QEvent::TabletPress:
        m_tabletActive = true;
        m_strokes.append({Point{event->position(), std::clamp(event->pressure(), 0.1, 1.0)}});
        break;
    case QEvent::TabletMove:
        if (m_tabletActive && !m_strokes.isEmpty())
            m_strokes.last().append(Point{event->position(), std::clamp(event->pressure(), 0.1, 1.0)});
        break;
    case QEvent::TabletRelease:
        m_tabletActive = false;
        break;
    default:
        break;
    }
    event->accept();
    update();
}

QImage SignaturePad::image() const
{
    if (m_strokes.isEmpty())
        return {};
    QRectF bounds;
    for (const auto &stroke : m_strokes)
        for (const Point &p : stroke)
            bounds = bounds.isNull() ? QRectF(p.pos, QSizeF(0.1, 0.1)) : bounds.united(QRectF(p.pos, QSizeF(0.1, 0.1)));
    bounds = bounds.adjusted(-8, -8, 8, 8);
    const double scale = 3.0; // image nette même agrandie
    QImage image(int(bounds.width() * scale), int(bounds.height() * scale), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    paintStrokes(painter, scale, bounds.topLeft());
    return image;
}

// --- Fenêtre -----------------------------------------------------------------

SignatureDialog::SignatureDialog(QWidget *parent)
    : QDialog(parent)
    , m_pad(new SignaturePad(this))
    , m_saved(new QListWidget(this))
{
    setWindowTitle(tr("Signature visuelle"));
    auto *layout = new QVBoxLayout(this);
    auto *warning = new QLabel(tr("Cette signature est une image posée sur la page. Elle n'a pas de valeur "
                                  "cryptographique : pour une signature électronique vérifiable, utilisez un certificat."), this);
    warning->setWordWrap(true);
    layout->addWidget(warning);

    auto *tabs = new QTabWidget(this);
    auto *drawPage = new QWidget(tabs);
    auto *drawLayout = new QVBoxLayout(drawPage);
    drawLayout->addWidget(m_pad, 1);
    auto *clear = new QPushButton(tr("Effacer"), drawPage);
    connect(clear, &QPushButton::clicked, m_pad, &SignaturePad::clear);
    drawLayout->addWidget(clear, 0, Qt::AlignRight);
    tabs->addTab(drawPage, tr("Dessiner"));

    auto *savedPage = new QWidget(tabs);
    auto *savedLayout = new QVBoxLayout(savedPage);
    m_saved->setViewMode(QListWidget::IconMode);
    m_saved->setIconSize(QSize(220, 90));
    m_saved->setResizeMode(QListWidget::Adjust);
    m_saved->setAccessibleName(tr("Signatures enregistrées"));
    savedLayout->addWidget(m_saved, 1);
    auto *savedButtons = new QHBoxLayout;
    auto *import = new QPushButton(tr("Importer une image…"), savedPage);
    auto *remove = new QPushButton(tr("Supprimer"), savedPage);
    connect(import, &QPushButton::clicked, this, &SignatureDialog::importImage);
    connect(remove, &QPushButton::clicked, this, &SignatureDialog::deleteSaved);
    savedButtons->addWidget(import);
    savedButtons->addWidget(remove);
    savedButtons->addStretch(1);
    savedLayout->addLayout(savedButtons);
    tabs->addTab(savedPage, tr("Enregistrées et images"));
    layout->addWidget(tabs, 1);

    auto *keep = new QCheckBox(tr("Enregistrer cette signature pour la réutiliser"), this);
    keep->setObjectName(QStringLiteral("keep"));
    keep->setChecked(true);
    layout->addWidget(keep);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Poser sur la page"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_saved, &QListWidget::itemDoubleClicked, this, [this] { accept(); });
    connect(tabs, &QTabWidget::currentChanged, this, [this, tabs, keep](int index) {
        keep->setEnabled(index == 0);
        Q_UNUSED(tabs);
    });
    reloadSaved();
    resize(640, 460);
}

QString SignatureDialog::storageDirectory()
{
    const QString base = Settings::instance().isPortable()
                             ? QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("signatures"))
                             : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/signatures");
    QDir().mkpath(base);
    return base;
}

QImage SignatureDialog::makeTransparent(const QImage &source)
{
    QImage image = source.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const int gray = qGray(line[x]);
            // Blanc et gris clair deviennent transparents, avec dégradé pour l'antialiasing.
            if (gray > 235)
                line[x] = qRgba(0, 0, 0, 0);
            else if (gray > 170)
                line[x] = qRgba(qRed(line[x]), qGreen(line[x]), qBlue(line[x]), int((235 - gray) * 255 / 65));
        }
    }
    return image;
}

void SignatureDialog::importImage()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Importer une signature ou un logo"), QString(),
                                                      tr("Images (*.png *.jpg *.jpeg *.bmp *.gif)"));
    if (file.isEmpty())
        return;
    QImage image(file);
    if (image.isNull()) {
        QMessageBox::warning(this, tr("Importer"), tr("Impossible de lire cette image."));
        return;
    }
    if (image.width() > 1600)
        image = image.scaledToWidth(1600, Qt::SmoothTransformation);
    const auto answer = QMessageBox::question(this, tr("Importer"), tr("Rendre le fond blanc transparent ?"),
                                              QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer == QMessageBox::Yes)
        image = makeTransparent(image);
    const QString target = QDir(storageDirectory()).filePath(QStringLiteral("signature-%1.png").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
    image.save(target, "PNG");
    reloadSaved();
    m_saved->setCurrentRow(m_saved->count() - 1);
}

void SignatureDialog::reloadSaved()
{
    m_saved->clear();
    const QDir dir(storageDirectory());
    for (const QFileInfo &info : dir.entryInfoList({QStringLiteral("*.png")}, QDir::Files, QDir::Name)) {
        QImage image(info.absoluteFilePath());
        if (image.isNull())
            continue;
        QImage preview(220, 90, QImage::Format_ARGB32);
        preview.fill(Qt::white);
        QPainter painter(&preview);
        const QImage scaled = image.scaled(210, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        painter.drawImage((220 - scaled.width()) / 2, (90 - scaled.height()) / 2, scaled);
        painter.end();
        auto *item = new QListWidgetItem(QIcon(QPixmap::fromImage(preview)), info.completeBaseName(), m_saved);
        item->setData(Qt::UserRole, info.absoluteFilePath());
    }
}

void SignatureDialog::deleteSaved()
{
    QListWidgetItem *item = m_saved->currentItem();
    if (!item)
        return;
    if (QMessageBox::question(this, tr("Supprimer"), tr("Supprimer cette signature enregistrée ?")) == QMessageBox::Yes) {
        QFile::remove(item->data(Qt::UserRole).toString());
        reloadSaved();
    }
}

void SignatureDialog::accept()
{
    if (auto *tabs = findChild<QTabWidget *>(); tabs && tabs->currentIndex() == 1) {
        QListWidgetItem *item = m_saved->currentItem();
        if (!item) {
            QMessageBox::information(this, windowTitle(), tr("Choisissez une signature enregistrée, ou dessinez-en une."));
            return;
        }
        m_result = QImage(item->data(Qt::UserRole).toString());
    } else {
        if (m_pad->isEmpty()) {
            QMessageBox::information(this, windowTitle(), tr("Signez d'abord dans la zone prévue."));
            return;
        }
        m_result = m_pad->image();
        if (auto *keep = findChild<QCheckBox *>(QStringLiteral("keep")); keep && keep->isChecked()) {
            const QString target = QDir(storageDirectory()).filePath(QStringLiteral("signature-%1.png").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
            m_result.save(target, "PNG");
        }
    }
    QDialog::accept();
}
