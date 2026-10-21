// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PageOrganizer.h"

#include "Icons.h"
#include "app/Settings.h"
#include "core/PdfDocument.h"

#include <QAction>
#include <QCloseEvent>
#include <QDir>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QStandardPaths>
#include <QToolBar>
#include <QTransform>
#include <QVBoxLayout>

#include <algorithm>
#include <map>
#include <memory>

namespace {
constexpr int kThumbWidth = 150;
constexpr int kIdRole = Qt::UserRole + 1;
}

// --- Miniatures ---------------------------------------------------------------

OrganizerThumbnails::OrganizerThumbnails(int width, QObject *parent)
    : QObject(parent)
    , m_width(width)
{
    m_thread = std::thread([this] { run(); });
}

OrganizerThumbnails::~OrganizerThumbnails()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}

QString OrganizerThumbnails::keyOf(const PageRef &ref)
{
    return ref.isBlank() ? QStringLiteral("blanche:%1x%2").arg(ref.blankWidth).arg(ref.blankHeight)
                         : ref.sourcePath + QLatin1Char('#') + QString::number(ref.page);
}

void OrganizerThumbnails::request(const PageRef &ref)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.append(ref);
    }
    m_wake.notify_all();
}

void OrganizerThumbnails::run()
{
    std::map<QString, std::unique_ptr<PdfDocument>> documents;
    for (;;) {
        PageRef ref;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [&] { return m_stop || !m_queue.isEmpty(); });
            if (m_stop)
                return;
            ref = m_queue.takeLast(); // les plus récentes d'abord
        }
        QImage image;
        if (ref.isBlank()) {
            image = QImage(m_width, int(m_width * ref.blankHeight / ref.blankWidth), QImage::Format_RGB888);
            image.fill(Qt::white);
        } else {
            auto &doc = documents[ref.sourcePath];
            if (!doc) {
                doc = std::make_unique<PdfDocument>();
                if (doc->open(ref.sourcePath, ref.password) != PdfDocument::OpenStatus::Ok)
                    doc.reset();
            }
            if (doc) {
                const QSizeF size = doc->pageSize(ref.page);
                image = doc->render(ref.page, m_width / size.width());
            }
        }
        if (!image.isNull())
            emit ready(keyOf(ref), image);
    }
}

// --- Liste -------------------------------------------------------------------

PageList::PageList(QWidget *parent)
    : QListWidget(parent)
{
    setViewMode(QListWidget::IconMode);
    setFlow(QListWidget::LeftToRight);
    setWrapping(true);
    setResizeMode(QListWidget::Adjust);
    setMovement(QListWidget::Snap);
    setDragDropMode(QAbstractItemView::InternalMove);
    setDefaultDropAction(Qt::MoveAction);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setIconSize(QSize(kThumbWidth + 2, kThumbWidth * 3 / 2 + 2));
    setGridSize(QSize(kThumbWidth + 40, kThumbWidth * 3 / 2 + 44));
    setSpacing(4);
    setUniformItemSizes(true);
    setAccessibleName(tr("Pages du document"));
}

void PageList::dropEvent(QDropEvent *event)
{
    QListWidget::dropEvent(event);
    emit reordered();
}

// --- Organiseur --------------------------------------------------------------

PageOrganizer::PageOrganizer(const QString &path, const QByteArray &password, QWidget *parent)
    : QDialog(parent)
    , m_path(path)
    , m_password(password)
    , m_renderer(new OrganizerThumbnails(int(kThumbWidth * devicePixelRatioF()), this))
{
    setWindowTitle(path.isEmpty() ? tr("Fusionner des documents") : tr("Organiser les pages : %1").arg(QFileInfo(path).fileName()));
    setWindowFlags(windowFlags() | Qt::WindowMaximizeButtonHint);
    resize(1000, 720);
    buildUi();
    connect(m_renderer, &OrganizerThumbnails::ready, this, [this](const QString &key, const QImage &image) {
        m_thumbnails.insert(key, image);
        for (int i = 0; i < m_list->count(); ++i) {
            QListWidgetItem *item = m_list->item(i);
            if (OrganizerThumbnails::keyOf(m_refById.value(item->data(kIdRole).toInt())) == key)
                applyThumbnail(item);
        }
    });
    if (!path.isEmpty())
        loadDocument(path, password);
    m_history.clear();
    m_history.append(m_pages);
    m_historyIndex = 0;
    updateActions();
}

PageOrganizer::~PageOrganizer() = default;

void PageOrganizer::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    m_toolBar = new QToolBar(this);
    m_toolBar->setIconSize(QSize(20, 20));
    m_toolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

    const auto make = [this](const QString &text, const QKeySequence &key, auto slot) {
        auto *action = new QAction(text, this);
        if (!key.isEmpty())
            action->setShortcut(key);
        connect(action, &QAction::triggered, this, slot);
        addAction(action);
        m_toolBar->addAction(action);
        return action;
    };
    m_undoAct = make(tr("Annuler"), QKeySequence::Undo, [this] { undo(); });
    m_redoAct = make(tr("Rétablir"), QKeySequence::Redo, [this] { redo(); });
    m_toolBar->addSeparator();
    m_rotateLeftAct = make(tr("Tourner à gauche"), QKeySequence(Qt::CTRL | Qt::Key_L), [this] { rotateSelection(-90); });
    m_rotateRightAct = make(tr("Tourner à droite"), QKeySequence(Qt::CTRL | Qt::Key_R), [this] { rotateSelection(90); });
    m_moveUpAct = make(tr("Avancer"), QKeySequence(Qt::CTRL | Qt::Key_Left), [this] { moveSelection(-1); });
    m_moveDownAct = make(tr("Reculer"), QKeySequence(Qt::CTRL | Qt::Key_Right), [this] { moveSelection(1); });
    m_deleteAct = make(tr("Supprimer"), QKeySequence::Delete, [this] { deleteSelection(); });
    m_toolBar->addSeparator();
    m_insertFileAct = make(tr("Insérer un PDF…"), QKeySequence(Qt::CTRL | Qt::Key_I), [this] { insertFileDialog(); });
    m_insertBlankAct = make(tr("Page blanche"), QKeySequence(Qt::CTRL | Qt::Key_B), [this] { insertBlankPage(); });
    m_extractAct = make(tr("Extraire…"), QKeySequence(Qt::CTRL | Qt::Key_E), [this] { extractSelectionDialog(); });
    m_toolBar->addSeparator();
    QAction *selectAll = make(tr("Tout sélectionner"), QKeySequence::SelectAll, [this] { m_list->selectAll(); });
    Q_UNUSED(selectAll);

    m_list = new PageList(this);
    connect(m_list, &PageList::reordered, this, [this] {
        QVector<PageRef> pages;
        for (int i = 0; i < m_list->count(); ++i)
            pages.append(m_refById.value(m_list->item(i)->data(kIdRole).toInt()));
        m_pages = pages;
        m_history.resize(m_historyIndex + 1);
        m_history.append(m_pages);
        m_historyIndex = int(m_history.size()) - 1;
        renumber();
        updateActions();
    });
    connect(m_list, &QListWidget::itemSelectionChanged, this, &PageOrganizer::updateActions);
    connect(m_list->verticalScrollBar(), &QScrollBar::valueChanged, this, &PageOrganizer::requestVisibleThumbnails);

    m_status = new QLabel(this);
    auto *hint = new QLabel(tr("Glissez les pages pour les réordonner. Maintenez Ctrl ou Maj pour en sélectionner plusieurs."), this);
    hint->setWordWrap(true);

    auto *buttons = new QHBoxLayout;
    m_saveButton = new QPushButton(tr("Enregistrer"), this);
    m_saveButton->setToolTip(tr("Remplace le document ; une copie de l'original est conservée."));
    m_saveAsButton = new QPushButton(tr("Enregistrer sous…"), this);
    auto *cancel = new QPushButton(tr("Fermer"), this);
    buttons->addWidget(m_status, 1);
    buttons->addWidget(m_saveButton);
    buttons->addWidget(m_saveAsButton);
    buttons->addWidget(cancel);
    connect(m_saveButton, &QPushButton::clicked, this, &PageOrganizer::save);
    connect(m_saveAsButton, &QPushButton::clicked, this, &PageOrganizer::saveAs);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);

    layout->addWidget(m_toolBar);
    layout->addWidget(m_list, 1);
    layout->addWidget(hint);
    layout->addLayout(buttons);
}

bool PageOrganizer::loadDocument(const QString &path, const QByteArray &password, int insertAt)
{
    PdfDocument probe;
    const auto status = probe.open(path, password);
    if (status != PdfDocument::OpenStatus::Ok) {
        QMessageBox::warning(this, tr("Organiser les pages"),
                             status == PdfDocument::OpenStatus::NeedsPassword
                                 ? tr("« %1 » est protégé par un mot de passe : ouvrez-le d'abord dans CARMO PDF.").arg(QFileInfo(path).fileName())
                                 : tr("Impossible de lire « %1 ».").arg(QFileInfo(path).fileName()));
        return false;
    }
    QVector<PageRef> pages = m_pages;
    const int at = insertAt < 0 ? int(pages.size()) : std::clamp(insertAt, 0, int(pages.size()));
    for (int i = 0; i < probe.pageCount(); ++i) {
        PageRef ref;
        ref.sourcePath = QFileInfo(path).absoluteFilePath();
        ref.password = password;
        ref.page = i;
        pages.insert(at + i, ref);
    }
    setPages(pages);
    return true;
}

void PageOrganizer::insertBlankPage(int at)
{
    QVector<PageRef> pages = m_pages;
    PageRef blank;
    const QVector<int> selected = selectedPositions();
    if (at < 0)
        at = selected.isEmpty() ? int(pages.size()) : selected.last() + 1;
    if (!pages.isEmpty()) {
        // Même format que la page voisine.
        const PageRef &neighbour = pages[std::clamp(at - 1, 0, int(pages.size()) - 1)];
        if (!neighbour.isBlank()) {
            PdfDocument doc;
            if (doc.open(neighbour.sourcePath, neighbour.password) == PdfDocument::OpenStatus::Ok) {
                const QSizeF size = doc.pageSize(neighbour.page);
                blank.blankWidth = size.width();
                blank.blankHeight = size.height();
            }
        } else {
            blank.blankWidth = neighbour.blankWidth;
            blank.blankHeight = neighbour.blankHeight;
        }
    }
    pages.insert(std::clamp(at, 0, int(pages.size())), blank);
    setPages(pages);
    selectPages({std::clamp(at, 0, int(pages.size()) - 1)});
}

void PageOrganizer::rotateSelection(int degrees)
{
    const QVector<int> selected = selectedPositions();
    if (selected.isEmpty())
        return;
    QVector<PageRef> pages = m_pages;
    for (int position : selected)
        pages[position].rotation = ((pages[position].rotation + degrees) % 360 + 360) % 360;
    setPages(pages);
    selectPages(selected);
}

void PageOrganizer::deleteSelection()
{
    const QVector<int> selected = selectedPositions();
    if (selected.isEmpty() || selected.size() >= m_pages.size()) {
        if (!selected.isEmpty())
            QMessageBox::information(this, tr("Supprimer"), tr("Un document doit garder au moins une page."));
        return;
    }
    QVector<PageRef> pages;
    for (int i = 0; i < m_pages.size(); ++i) {
        if (!selected.contains(i))
            pages.append(m_pages[i]);
    }
    setPages(pages);
    selectPages({std::min(selected.first(), int(pages.size()) - 1)});
}

void PageOrganizer::moveSelection(int delta)
{
    QVector<int> selected = selectedPositions();
    if (selected.isEmpty() || delta == 0)
        return;
    std::sort(selected.begin(), selected.end());
    if ((delta < 0 && selected.first() == 0) || (delta > 0 && selected.last() == m_pages.size() - 1))
        return;
    QVector<PageRef> pages = m_pages;
    QVector<int> moved;
    if (delta < 0) {
        for (int position : selected) {
            std::swap(pages[position], pages[position - 1]);
            moved.append(position - 1);
        }
    } else {
        for (auto it = selected.rbegin(); it != selected.rend(); ++it) {
            std::swap(pages[*it], pages[*it + 1]);
            moved.append(*it + 1);
        }
    }
    setPages(pages);
    selectPages(moved);
}

void PageOrganizer::selectPages(const QVector<int> &positions)
{
    m_list->clearSelection();
    for (int position : positions) {
        if (position >= 0 && position < m_list->count())
            m_list->item(position)->setSelected(true);
    }
    if (!positions.isEmpty() && positions.first() < m_list->count()) {
        m_list->setCurrentItem(m_list->item(positions.first()), QItemSelectionModel::NoUpdate);
        m_list->scrollToItem(m_list->item(positions.first()));
    }
}

void PageOrganizer::undo()
{
    if (m_historyIndex <= 0)
        return;
    --m_historyIndex;
    setPages(m_history[m_historyIndex], false);
}

void PageOrganizer::redo()
{
    if (m_historyIndex + 1 >= m_history.size())
        return;
    ++m_historyIndex;
    setPages(m_history[m_historyIndex], false);
}

bool PageOrganizer::isModified() const
{
    return m_history.isEmpty() || m_pages != m_history.first();
}

void PageOrganizer::setPages(const QVector<PageRef> &pages, bool record)
{
    m_pages = pages;
    if (record) {
        m_history.resize(m_historyIndex + 1);
        m_history.append(m_pages);
        m_historyIndex = int(m_history.size()) - 1;
    }
    rebuildList();
    updateActions();
}

void PageOrganizer::rebuildList()
{
    m_list->clear();
    m_refById.clear();
    for (const PageRef &ref : std::as_const(m_pages)) {
        const int id = m_nextId++;
        m_refById.insert(id, ref);
        auto *item = new QListWidgetItem(m_list);
        item->setData(kIdRole, id);
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsDragEnabled);
        applyThumbnail(item);
    }
    renumber();
    requestVisibleThumbnails();
}

void PageOrganizer::renumber()
{
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem *item = m_list->item(i);
        const PageRef ref = m_refById.value(item->data(kIdRole).toInt());
        QString label = QString::number(i + 1);
        if (ref.isBlank())
            label += tr(" (blanche)");
        else if (!m_path.isEmpty() && ref.sourcePath != QFileInfo(m_path).absoluteFilePath())
            label += QStringLiteral("\n") + QFileInfo(ref.sourcePath).fileName();
        if (ref.rotation)
            label += tr("\n%1°").arg(ref.rotation);
        item->setText(label);
        item->setToolTip(ref.isBlank() ? tr("Page blanche") : tr("%1, page %2").arg(QFileInfo(ref.sourcePath).fileName()).arg(ref.page + 1));
    }
    m_status->setText(tr("%n page(s)", nullptr, int(m_pages.size())));
}

void PageOrganizer::requestVisibleThumbnails()
{
    const QRect visible = m_list->viewport()->rect().adjusted(0, -400, 0, 400);
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem *item = m_list->item(i);
        if (!m_list->visualItemRect(item).intersects(visible))
            continue;
        const PageRef ref = m_refById.value(item->data(kIdRole).toInt());
        if (!m_thumbnails.contains(OrganizerThumbnails::keyOf(ref)))
            m_renderer->request(ref);
    }
}

void PageOrganizer::applyThumbnail(QListWidgetItem *item)
{
    const PageRef ref = m_refById.value(item->data(kIdRole).toInt());
    QImage image = m_thumbnails.value(OrganizerThumbnails::keyOf(ref));
    if (image.isNull()) {
        image = QImage(kThumbWidth, kThumbWidth * 3 / 2, QImage::Format_RGB888);
        image.fill(QColor(235, 235, 235));
    } else if (ref.rotation) {
        image = image.transformed(QTransform().rotate(ref.rotation));
    }
    QImage framed(image.size() + QSize(2, 2), QImage::Format_RGB32);
    framed.fill(QColor(150, 150, 150));
    QPainter painter(&framed);
    painter.drawImage(QPoint(1, 1), image);
    painter.end();
    framed.setDevicePixelRatio(devicePixelRatioF());
    item->setIcon(QPixmap::fromImage(framed));
}

QVector<int> PageOrganizer::selectedPositions() const
{
    QVector<int> positions;
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->isSelected())
            positions.append(i);
    }
    return positions;
}

void PageOrganizer::updateActions()
{
    const bool any = !selectedPositions().isEmpty();
    for (QAction *action : {m_rotateLeftAct, m_rotateRightAct, m_deleteAct, m_moveUpAct, m_moveDownAct, m_extractAct})
        action->setEnabled(any);
    m_undoAct->setEnabled(m_historyIndex > 0);
    m_redoAct->setEnabled(m_historyIndex + 1 < m_history.size());
    m_saveButton->setEnabled(!m_path.isEmpty() && isModified() && !m_pages.isEmpty());
    m_saveAsButton->setEnabled(!m_pages.isEmpty());
}

void PageOrganizer::insertFileDialog()
{
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Insérer un PDF"), QFileInfo(m_path).absolutePath(),
                                                            tr("Documents PDF (*.pdf)"));
    if (files.isEmpty())
        return;
    const QVector<int> selected = selectedPositions();
    int at = selected.isEmpty() ? int(m_pages.size()) : selected.last() + 1;
    for (const QString &file : files) {
        const int before = int(m_pages.size());
        if (loadDocument(file, {}, at))
            at += int(m_pages.size()) - before;
    }
}

void PageOrganizer::extractSelectionDialog()
{
    const QVector<int> selected = selectedPositions();
    if (selected.isEmpty())
        return;
    const QString suggested = QFileInfo(m_path).absolutePath() + QLatin1Char('/') + QFileInfo(m_path).completeBaseName()
                              + tr(" - extrait.pdf");
    const QString target = QFileDialog::getSaveFileName(this, tr("Extraire les pages sélectionnées"), suggested, tr("Documents PDF (*.pdf)"));
    if (target.isEmpty())
        return;
    QVector<PageRef> pages;
    for (int position : selected)
        pages.append(m_pages[position]);
    const auto result = PdfAssembler::assemble(pages, target);
    if (!result.ok)
        QMessageBox::warning(this, tr("Extraire"), result.error);
    else
        m_status->setText(tr("%n page(s) extraite(s) vers %1", nullptr, int(pages.size())).arg(QFileInfo(target).fileName()));
}

bool PageOrganizer::confirmSignatures(const QString &target)
{
    // SIG-11 : toute reconstruction invalide les signatures du document.
    QStringList signedFiles;
    QSet<QString> seen;
    for (const PageRef &ref : std::as_const(m_pages)) {
        if (ref.isBlank() || seen.contains(ref.sourcePath))
            continue;
        seen.insert(ref.sourcePath);
        if (PdfAssembler::signatureCount(ref.sourcePath, ref.password) > 0)
            signedFiles.append(QFileInfo(ref.sourcePath).fileName());
    }
    if (signedFiles.isEmpty())
        return true;
    const bool overwrite = QFileInfo(target).absoluteFilePath() == QFileInfo(m_path).absoluteFilePath();
    QMessageBox box(QMessageBox::Warning, tr("Document signé"),
                    tr("%1 contient une ou plusieurs signatures électroniques. Réorganiser les pages les rendra invalides : "
                       "le fichier produit ne sera plus signé.")
                        .arg(signedFiles.join(QStringLiteral(", "))),
                    QMessageBox::NoButton, this);
    QPushButton *copy = box.addButton(tr("Enregistrer une copie…"), QMessageBox::AcceptRole);
    QPushButton *proceed = box.addButton(overwrite ? tr("Remplacer quand même") : tr("Continuer"), QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(copy);
    box.exec();
    if (box.clickedButton() == proceed)
        return true;
    if (box.clickedButton() == copy) {
        if (overwrite)
            saveAs();
        return false;
    }
    return false;
}

PdfAssembler::Result PageOrganizer::saveTo(const QString &path)
{
    const QString target = QFileInfo(path).absoluteFilePath();
    if (target == QFileInfo(m_path).absoluteFilePath()) {
        // Copie de l'original avant remplacement (REL-03).
        const QString backup = Settings::instance().backupFor(m_path);
        if (backup.isEmpty() || !QFile::copy(m_path, backup)) {
            PdfAssembler::Result result;
            result.error = tr("Impossible de sauvegarder l'original avant de le remplacer.");
            return result;
        }
    }
    PdfAssembler::Result result = PdfAssembler::assemble(m_pages, target);
    if (result.ok) {
        m_savedPath = target;
        if (target == QFileInfo(m_path).absoluteFilePath()) {
            // Les pages viennent désormais du fichier enregistré : repartir de lui.
            m_pages.clear();
            m_history.clear();
            m_historyIndex = -1;
            m_thumbnails.clear();
            loadDocument(m_path, m_password);
            m_history = {m_pages};
            m_historyIndex = 0;
            updateActions();
        }
        emit documentSaved(target);
    }
    return result;
}

void PageOrganizer::save()
{
    if (m_path.isEmpty()) {
        saveAs();
        return;
    }
    if (!confirmSignatures(m_path))
        return;
    const auto result = saveTo(m_path);
    if (!result.ok) {
        QMessageBox::warning(this, tr("Enregistrer"), result.error);
        return;
    }
    accept();
}

void PageOrganizer::saveAs()
{
    const QString dir = m_path.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                                         : QFileInfo(m_path).absolutePath();
    const QString suggested = m_path.isEmpty() ? dir + tr("/document fusionné.pdf")
                                               : dir + QLatin1Char('/') + QFileInfo(m_path).completeBaseName() + tr(" - modifié.pdf");
    const QString target = QFileDialog::getSaveFileName(this, tr("Enregistrer sous"), suggested, tr("Documents PDF (*.pdf)"));
    if (target.isEmpty())
        return;
    if (!confirmSignatures(target))
        return;
    const auto result = saveTo(target);
    if (!result.ok) {
        QMessageBox::warning(this, tr("Enregistrer sous"), result.error);
        return;
    }
    accept();
}

void PageOrganizer::closeEvent(QCloseEvent *event)
{
    if (isModified() && m_savedPath.isEmpty()) {
        const auto answer = QMessageBox::question(this, windowTitle(), tr("Abandonner les modifications non enregistrées ?"),
                                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
    }
    event->accept();
}

void PageOrganizer::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QDialog::keyPressEvent(event);
}
