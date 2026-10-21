// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PdfDocument.h"
#include "TempPath.h"

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

#include <QBuffer>
#include <QFile>
#include <QFileInfo>

#include <cstring>
#include <vector>

// Règle MuPDF : fz_try utilise setjmp/longjmp. Aucun objet C++ doté d'un
// destructeur n'est créé dans un bloc fz_try, et on ne fait jamais `return`
// depuis fz_try. Les variables modifiées dans fz_try et lues ensuite sont
// déclarées avec fz_var.

// Depuis MuPDF 1.24, chaque erreur interceptée doit être déclarée traitée,
// sinon MuPDF signale une « UNHANDLED EXCEPTION ».
#if FZ_VERSION_MAJOR > 1 || FZ_VERSION_MINOR >= 24
#define CARMOPDF_ERROR_HANDLED(ctx) fz_ignore_error(ctx)
#else
#define CARMOPDF_ERROR_HANDLED(ctx) ((void)0)
#endif

namespace {
// Mémoire maximale du cache interne de MuPDF (polices, images décodées).
constexpr size_t kStoreSize = 256u << 20;

int pageNumber(fz_context *ctx, fz_document *doc, fz_location location)
{
    int page = -1;
    fz_var(page);
    fz_try(ctx) { page = fz_page_number_from_location(ctx, doc, location); }
    fz_catch(ctx)
    {
        CARMOPDF_ERROR_HANDLED(ctx);
        page = -1;
    }
    return page;
}

// Page visée par un lien interne, ou -1. Isolé dans sa propre fonction pour
// que fz_try ne partage aucune variable avec la boucle appelante.
int resolveInternalLink(fz_context *ctx, fz_document *doc, const char *uri)
{
    fz_location location = {-1, -1};
    fz_var(location);
    fz_try(ctx) { location = fz_resolve_link(ctx, doc, uri, nullptr, nullptr); }
    fz_catch(ctx)
    {
        CARMOPDF_ERROR_HANDLED(ctx);
        return -1;
    }
    return location.page >= 0 ? pageNumber(ctx, doc, location) : -1;
}

void convertOutline(fz_context *ctx, fz_document *doc, fz_outline *node, OutlineList &out)
{
    for (; node; node = node->next) {
        OutlineItem item;
        item.title = QString::fromUtf8(node->title ? node->title : "").simplified();
        item.open = node->is_open;
        item.page = pageNumber(ctx, doc, node->page);
        if (node->down)
            convertOutline(ctx, doc, node->down, item.children);
        out.push_back(std::move(item));
    }
}
} // namespace

PdfDocument::PdfDocument()
{
    m_ctx = fz_new_context(nullptr, nullptr, kStoreSize);
    if (m_ctx) {
        fz_try(m_ctx) { fz_register_document_handlers(m_ctx); }
        fz_catch(m_ctx)
        {
            fz_drop_context(m_ctx);
            m_ctx = nullptr;
        }
    }
    if (!m_ctx)
        m_error = QStringLiteral("Impossible d'initialiser le moteur PDF.");
}

PdfDocument::~PdfDocument()
{
    if (m_ctx) {
        dropTextPages();
        fz_drop_document(m_ctx, m_doc);
        fz_drop_context(m_ctx);
    }
}

PdfDocument::OpenStatus PdfDocument::open(const QString &path, const QByteArray &password)
{
    if (!m_ctx)
        return OpenStatus::Failed;
    dropTextPages();
    fz_drop_document(m_ctx, m_doc);
    m_doc = nullptr;
    m_pdf = nullptr;
    m_pageCount = 0;
    m_path = path;
    m_lastPassword = password;
    m_dirty = false;
    m_undoPages.clear();
    m_redoPages.clear();

    const QByteArray utf8Path = path.toUtf8();
    fz_document *doc = nullptr;
    fz_var(doc);
    fz_try(m_ctx) { doc = fz_open_document(m_ctx, utf8Path.constData()); }
    fz_catch(m_ctx)
    {
        m_error = QString::fromUtf8(fz_caught_message(m_ctx));
        CARMOPDF_ERROR_HANDLED(m_ctx);
        return OpenStatus::Failed;
    }

    int needsPassword = 0;
    fz_var(needsPassword);
    fz_try(m_ctx) { needsPassword = fz_needs_password(m_ctx, doc); }
    fz_catch(m_ctx)
    {
        m_error = QString::fromUtf8(fz_caught_message(m_ctx));
        CARMOPDF_ERROR_HANDLED(m_ctx);
        fz_drop_document(m_ctx, doc);
        return OpenStatus::Failed;
    }
    if (needsPassword) {
        if (password.isEmpty()) {
            fz_drop_document(m_ctx, doc);
            return OpenStatus::NeedsPassword;
        }
        if (!fz_authenticate_password(m_ctx, doc, password.constData())) {
            fz_drop_document(m_ctx, doc);
            return OpenStatus::WrongPassword;
        }
    }

    int count = 0;
    fz_var(count);
    fz_try(m_ctx) { count = fz_count_pages(m_ctx, doc); }
    fz_catch(m_ctx)
    {
        m_error = QString::fromUtf8(fz_caught_message(m_ctx));
        CARMOPDF_ERROR_HANDLED(m_ctx);
        fz_drop_document(m_ctx, doc);
        return OpenStatus::Failed;
    }
    if (count <= 0) {
        m_error = QStringLiteral("Le document ne contient aucune page.");
        fz_drop_document(m_ctx, doc);
        return OpenStatus::Failed;
    }

    m_doc = doc;
    m_pageCount = count;
    return OpenStatus::Ok;
}

QSizeF PdfDocument::pageSize(int index)
{
    if (!m_doc || index < 0 || index >= m_pageCount)
        return {};
    fz_page *page = nullptr;
    fz_rect bounds = fz_empty_rect;
    fz_var(page);
    fz_try(m_ctx)
    {
        page = fz_load_page(m_ctx, m_doc, index);
        bounds = fz_bound_page(m_ctx, page);
    }
    fz_always(m_ctx) { fz_drop_page(m_ctx, page); }
    fz_catch(m_ctx)
    {
        // Page illisible : format Lettre par défaut.
        CARMOPDF_ERROR_HANDLED(m_ctx);
        return QSizeF(612, 792);
    }
    const QSizeF size(bounds.x1 - bounds.x0, bounds.y1 - bounds.y0);
    return size.isEmpty() ? QSizeF(612, 792) : size;
}

QImage PdfDocument::render(int index, double scale, const QRect &clip)
{
    if (!m_doc || index < 0 || index >= m_pageCount || scale <= 0)
        return {};

    fz_page *page = nullptr;
    fz_pixmap *pix = nullptr;
    fz_device *dev = nullptr;
    fz_var(page);
    fz_var(pix);
    fz_var(dev);
    fz_try(m_ctx)
    {
        page = fz_load_page(m_ctx, m_doc, index);
        if (m_pdf) {
            // Apparence des annotations ajoutées ou modifiées depuis le dernier rendu.
            pdf_page *pdfPage = pdf_page_from_fz_page(m_ctx, page);
            if (pdfPage)
                pdf_update_page(m_ctx, pdfPage);
        }
        const fz_rect bounds = fz_bound_page(m_ctx, page);
        // L'origine de la page est ramenée en (0,0) pour que `clip`
        // s'exprime en pixels depuis le coin supérieur gauche.
        const fz_matrix ctm = fz_concat(fz_translate(-bounds.x0, -bounds.y0), fz_scale(scale, scale));
        fz_irect area = fz_round_rect(fz_transform_rect(bounds, ctm));
        if (!clip.isEmpty()) {
            fz_irect wanted = fz_make_irect(clip.left(), clip.top(), clip.left() + clip.width(),
                                            clip.top() + clip.height());
            area = fz_intersect_irect(area, wanted);
        }
        if (fz_is_empty_irect(area))
            fz_throw(m_ctx, FZ_ERROR_GENERIC, "zone de rendu vide");
        pix = fz_new_pixmap_with_bbox(m_ctx, fz_device_rgb(m_ctx), area, nullptr, 0);
        fz_clear_pixmap_with_value(m_ctx, pix, 0xff);
        dev = fz_new_draw_device(m_ctx, fz_identity, pix);
        fz_run_page(m_ctx, page, dev, ctm, nullptr);
        fz_close_device(m_ctx, dev);
    }
    fz_always(m_ctx)
    {
        fz_drop_device(m_ctx, dev);
        fz_drop_page(m_ctx, page);
    }
    fz_catch(m_ctx)
    {
        fz_drop_pixmap(m_ctx, pix);
        m_error = QString::fromUtf8(fz_caught_message(m_ctx));
        CARMOPDF_ERROR_HANDLED(m_ctx);
        return {};
    }

    const int w = fz_pixmap_width(m_ctx, pix);
    const int h = fz_pixmap_height(m_ctx, pix);
    const int stride = static_cast<int>(fz_pixmap_stride(m_ctx, pix));
    const unsigned char *samples = fz_pixmap_samples(m_ctx, pix);
    QImage image(w, h, QImage::Format_RGB888);
    if (!image.isNull()) {
        for (int y = 0; y < h; ++y)
            std::memcpy(image.scanLine(y), samples + static_cast<size_t>(y) * stride, static_cast<size_t>(w) * 3);
    }
    fz_drop_pixmap(m_ctx, pix);
    return image;
}

OutlineList PdfDocument::outline()
{
    OutlineList result;
    if (!m_doc)
        return result;
    fz_outline *root = nullptr;
    fz_var(root);
    fz_try(m_ctx) { root = fz_load_outline(m_ctx, m_doc); }
    fz_catch(m_ctx)
    {
        CARMOPDF_ERROR_HANDLED(m_ctx);
        return result;
    }
    convertOutline(m_ctx, m_doc, root, result);
    fz_drop_outline(m_ctx, root);
    return result;
}

QVector<QRectF> PdfDocument::search(int index, const QString &needle, int maxHits)
{
    QVector<QRectF> rects;
    if (!m_doc || needle.isEmpty() || index < 0 || index >= m_pageCount)
        return rects;

    const QByteArray utf8 = needle.toUtf8();
    std::vector<fz_quad> quads(static_cast<size_t>(maxHits));
    fz_page *page = nullptr;
    fz_rect bounds = fz_empty_rect;
    int count = 0;
    fz_var(page);
    fz_var(count);
    fz_try(m_ctx)
    {
        page = fz_load_page(m_ctx, m_doc, index);
        bounds = fz_bound_page(m_ctx, page);
        count = fz_search_page(m_ctx, page, utf8.constData(), nullptr, quads.data(), maxHits);
    }
    fz_always(m_ctx) { fz_drop_page(m_ctx, page); }
    fz_catch(m_ctx)
    {
        CARMOPDF_ERROR_HANDLED(m_ctx);
        return rects;
    }

    rects.reserve(count);
    for (int i = 0; i < count; ++i) {
        const fz_rect r = fz_rect_from_quad(quads[static_cast<size_t>(i)]);
        rects.append(QRectF(r.x0 - bounds.x0, r.y0 - bounds.y0, r.x1 - r.x0, r.y1 - r.y0));
    }
    return rects;
}

QString PdfDocument::metadata(const char *key)
{
    if (!m_doc)
        return {};
    char buffer[512] = {0};
    int length = -1;
    fz_var(length);
    fz_try(m_ctx) { length = fz_lookup_metadata(m_ctx, m_doc, key, buffer, sizeof buffer); }
    fz_catch(m_ctx)
    {
        CARMOPDF_ERROR_HANDLED(m_ctx);
        length = -1;
    }
    return length > 0 ? QString::fromUtf8(buffer).trimmed() : QString();
}

void PdfDocument::dropTextPages()
{
    for (TextCacheEntry &entry : m_textCache) {
        fz_drop_stext_page(m_ctx, static_cast<fz_stext_page *>(entry.text));
        entry = TextCacheEntry{};
    }
}

// Texte structuré d'une page, conservé pour les 4 dernières pages consultées :
// l'extraction coûte quelques millisecondes et sert à chaque mouvement de souris.
const PdfDocument::TextCacheEntry *PdfDocument::textPage(int index)
{
    if (!m_doc || index < 0 || index >= m_pageCount)
        return nullptr;
    for (const TextCacheEntry &entry : m_textCache) {
        if (entry.page == index)
            return &entry;
    }
    fz_page *page = nullptr;
    fz_stext_page *text = nullptr;
    fz_rect bounds = fz_empty_rect;
    fz_var(page);
    fz_var(text);
    fz_try(m_ctx)
    {
        page = fz_load_page(m_ctx, m_doc, index);
        bounds = fz_bound_page(m_ctx, page);
        text = fz_new_stext_page_from_page(m_ctx, page, nullptr);
    }
    fz_always(m_ctx) { fz_drop_page(m_ctx, page); }
    fz_catch(m_ctx)
    {
        CARMOPDF_ERROR_HANDLED(m_ctx);
        return nullptr;
    }
    TextCacheEntry &slot = m_textCache[m_textCacheNext];
    m_textCacheNext = (m_textCacheNext + 1) % 4;
    fz_drop_stext_page(m_ctx, static_cast<fz_stext_page *>(slot.text));
    slot = TextCacheEntry{index, text, bounds.x0, bounds.y0};
    return &slot;
}

namespace {
int snapOf(PdfDocument::SnapMode mode)
{
    switch (mode) {
    case PdfDocument::SnapMode::Words:
        return FZ_SELECT_WORDS;
    case PdfDocument::SnapMode::Lines:
        return FZ_SELECT_LINES;
    default:
        return FZ_SELECT_CHARS;
    }
}
} // namespace

QVector<QRectF> PdfDocument::selectionRects(int index, QPointF a, QPointF b, SnapMode snap)
{
    QVector<QRectF> rects;
    const TextCacheEntry *entry = textPage(index);
    if (!entry)
        return rects;
    auto *text = static_cast<fz_stext_page *>(entry->text);
    fz_point pa = fz_make_point(float(a.x()) + entry->originX, float(a.y()) + entry->originY);
    fz_point pb = fz_make_point(float(b.x()) + entry->originX, float(b.y()) + entry->originY);
    if (snap != SnapMode::Chars)
        fz_snap_selection(m_ctx, text, &pa, &pb, snapOf(snap));
    std::vector<fz_quad> quads(2048);
    const int count = fz_highlight_selection(m_ctx, text, pa, pb, quads.data(), int(quads.size()));
    rects.reserve(count);
    for (int i = 0; i < count; ++i) {
        const fz_rect r = fz_rect_from_quad(quads[size_t(i)]);
        rects.append(QRectF(r.x0 - entry->originX, r.y0 - entry->originY, r.x1 - r.x0, r.y1 - r.y0));
    }
    return rects;
}

QString PdfDocument::selectionText(int index, QPointF a, QPointF b, SnapMode snap)
{
    const TextCacheEntry *entry = textPage(index);
    if (!entry)
        return {};
    auto *text = static_cast<fz_stext_page *>(entry->text);
    fz_point pa = fz_make_point(float(a.x()) + entry->originX, float(a.y()) + entry->originY);
    fz_point pb = fz_make_point(float(b.x()) + entry->originX, float(b.y()) + entry->originY);
    if (snap != SnapMode::Chars)
        fz_snap_selection(m_ctx, text, &pa, &pb, snapOf(snap));
    char *copy = nullptr;
    fz_var(copy);
    fz_try(m_ctx) { copy = fz_copy_selection(m_ctx, text, pa, pb, 1); }
    fz_catch(m_ctx)
    {
        CARMOPDF_ERROR_HANDLED(m_ctx);
        return {};
    }
    QString result = QString::fromUtf8(copy ? copy : "");
    fz_free(m_ctx, copy);
    return result;
}

bool PdfDocument::hasTextAt(int index, QPointF point)
{
    const TextCacheEntry *entry = textPage(index);
    if (!entry)
        return false;
    auto *text = static_cast<fz_stext_page *>(entry->text);
    const fz_point p = fz_make_point(float(point.x()) + entry->originX, float(point.y()) + entry->originY);
    for (fz_stext_block *block = text->first_block; block; block = block->next) {
        if (block->type != FZ_STEXT_BLOCK_TEXT || !fz_is_point_inside_rect(p, block->bbox))
            continue;
        for (fz_stext_line *line = block->u.t.first_line; line; line = line->next) {
            if (fz_is_point_inside_rect(p, line->bbox))
                return true;
        }
    }
    return false;
}

QVector<PdfLink> PdfDocument::links(int index)
{
    QVector<PdfLink> result;
    if (!m_doc || index < 0 || index >= m_pageCount)
        return result;
    fz_page *page = nullptr;
    fz_link *links = nullptr;
    fz_rect bounds = fz_empty_rect;
    fz_var(page);
    fz_var(links);
    fz_try(m_ctx)
    {
        page = fz_load_page(m_ctx, m_doc, index);
        bounds = fz_bound_page(m_ctx, page);
        links = fz_load_links(m_ctx, page);
    }
    fz_always(m_ctx) { fz_drop_page(m_ctx, page); }
    fz_catch(m_ctx)
    {
        CARMOPDF_ERROR_HANDLED(m_ctx);
        return result;
    }
    for (fz_link *link = links; link; link = link->next) {
        if (!link->uri)
            continue;
        PdfLink item;
        item.rect = QRectF(link->rect.x0 - bounds.x0, link->rect.y0 - bounds.y0, link->rect.x1 - link->rect.x0,
                           link->rect.y1 - link->rect.y0);
        if (fz_is_external_link(m_ctx, link->uri)) {
            item.uri = QString::fromUtf8(link->uri);
        } else {
            item.page = resolveInternalLink(m_ctx, m_doc, link->uri);
            if (item.page < 0)
                continue;
        }
        result.append(item);
    }
    fz_drop_link(m_ctx, links);
    return result;
}

// --- Annotations -------------------------------------------------------------
//
// Les fonctions MuPDF pdf_set_annot_* et pdf_annot_rect travaillent déjà dans
// l'espace de la page affichée (rotation comprise) ; il ne reste qu'à tenir
// compte de l'origine de la page, en général (0, 0).

namespace {

AnnotationKind kindOf(fz_context *ctx, pdf_annot *annot)
{
    switch (pdf_annot_type(ctx, annot)) {
    case PDF_ANNOT_HIGHLIGHT:
        return AnnotationKind::Highlight;
    case PDF_ANNOT_UNDERLINE:
        return AnnotationKind::Underline;
    case PDF_ANNOT_STRIKE_OUT:
        return AnnotationKind::StrikeOut;
    case PDF_ANNOT_TEXT:
        return AnnotationKind::Note;
    case PDF_ANNOT_FREE_TEXT:
        return AnnotationKind::FreeText;
    case PDF_ANNOT_INK:
        return AnnotationKind::Ink;
    case PDF_ANNOT_LINE:
        return AnnotationKind::Line;
    case PDF_ANNOT_SQUARE:
        return AnnotationKind::Square;
    case PDF_ANNOT_CIRCLE:
        return AnnotationKind::Circle;
    case PDF_ANNOT_STAMP:
        return AnnotationKind::Stamp;
    default:
        return AnnotationKind::Other;
    }
}

void setColor(fz_context *ctx, pdf_annot *annot, const QColor &color, bool interior)
{
    if (!color.isValid())
        return;
    const float rgb[3] = {float(color.redF()), float(color.greenF()), float(color.blueF())};
    if (interior)
        pdf_set_annot_interior_color(ctx, annot, 3, rgb);
    else
        pdf_set_annot_color(ctx, annot, 3, rgb);
    if (color.alphaF() < 0.999)
        pdf_set_annot_opacity(ctx, annot, float(color.alphaF()));
}

QColor colorOf(fz_context *ctx, pdf_annot *annot)
{
    int n = 0;
    float c[4] = {0, 0, 0, 0};
    pdf_annot_color(ctx, annot, &n, c);
    if (n == 1)
        return QColor::fromRgbF(c[0], c[0], c[0]);
    if (n == 3)
        return QColor::fromRgbF(c[0], c[1], c[2]);
    if (n == 4)
        return QColor::fromCmykF(c[0], c[1], c[2], c[3]);
    return {};
}

fz_point origin(fz_context *ctx, pdf_page *page)
{
    const fz_rect bounds = fz_bound_page(ctx, &page->super);
    return fz_make_point(bounds.x0, bounds.y0);
}

} // namespace

bool PdfDocument::enableEditing()
{
    if (!m_doc)
        return false;
    if (m_pdf)
        return true;
    pdf_document *pdf = nullptr;
    fz_var(pdf);
    fz_try(m_ctx)
    {
        pdf = pdf_specifics(m_ctx, m_doc);
        if (pdf)
            pdf_enable_journal(m_ctx, pdf);
    }
    fz_catch(m_ctx)
    {
        CARMOPDF_ERROR_HANDLED(m_ctx);
        return false;
    }
    m_pdf = pdf;
    return m_pdf != nullptr;
}

pdf_annot *PdfDocument::findAnnot(pdf_page *page, int id)
{
    for (pdf_annot *annot = pdf_first_annot(m_ctx, page); annot; annot = pdf_next_annot(m_ctx, annot)) {
        if (pdf_to_num(m_ctx, pdf_annot_obj(m_ctx, annot)) == id)
            return annot;
    }
    return nullptr;
}

QVector<AnnotationInfo> PdfDocument::annotations(int index)
{
    QVector<AnnotationInfo> list;
    if (!m_pdf || index < 0 || index >= m_pageCount)
        return list;
    pdf_page *page = nullptr;
    fz_var(page);
    fz_try(m_ctx)
    {
        page = pdf_load_page(m_ctx, m_pdf, index);
        const fz_point o = origin(m_ctx, page);
        for (pdf_annot *annot = pdf_first_annot(m_ctx, page); annot; annot = pdf_next_annot(m_ctx, annot)) {
            AnnotationInfo info;
            info.id = pdf_to_num(m_ctx, pdf_annot_obj(m_ctx, annot));
            info.page = index;
            info.kind = kindOf(m_ctx, annot);
            const fz_rect r = pdf_bound_annot(m_ctx, annot);
            info.rect = QRectF(r.x0 - o.x, r.y0 - o.y, r.x1 - r.x0, r.y1 - r.y0);
            info.contents = QString::fromUtf8(pdf_annot_contents(m_ctx, annot));
            info.author = QString::fromUtf8(pdf_annot_author(m_ctx, annot));
            const int64_t date = pdf_annot_modification_date(m_ctx, annot);
            if (date > 0)
                info.modified = QDateTime::fromSecsSinceEpoch(date);
            info.color = colorOf(m_ctx, annot);
            list.append(info);
        }
    }
    fz_always(m_ctx) { fz_drop_page(m_ctx, page ? &page->super : nullptr); }
    fz_catch(m_ctx) { CARMOPDF_ERROR_HANDLED(m_ctx); }
    return list;
}

// Charge la page et ouvre une opération du journal. Renvoie 0 ou -1.
int PdfDocument::beginAnnot(int index, const char *name, pdf_page **pageOut)
{
    *pageOut = nullptr;
    if (!m_pdf || index < 0 || index >= m_pageCount)
        return -1;
    pdf_page *page = nullptr;
    fz_var(page);
    fz_try(m_ctx)
    {
        page = pdf_load_page(m_ctx, m_pdf, index);
        pdf_begin_operation(m_ctx, m_pdf, name);
    }
    fz_catch(m_ctx)
    {
        CARMOPDF_ERROR_HANDLED(m_ctx);
        fz_drop_page(m_ctx, page ? &page->super : nullptr);
        return -1;
    }
    *pageOut = page;
    return 0;
}

// Clôt l'opération (ou l'abandonne), met à jour l'apparence, libère la page.
// Renvoie l'identifiant de l'annotation, ou -1.
void PdfDocument::stampMeta(pdf_annot *annot)
{
    const QByteArray author = m_author.toUtf8();
    if (!author.isEmpty())
        pdf_set_annot_author(m_ctx, annot, author.constData());
    pdf_set_annot_modification_date(m_ctx, annot, QDateTime::currentSecsSinceEpoch());
}

int PdfDocument::finishAnnot(int index, pdf_page *page, pdf_annot *annot, bool ok, bool setMeta)
{
    int id = -1;
    fz_var(id);
    fz_try(m_ctx)
    {
        if (ok) {
            if (annot) {
                if (setMeta)
                    stampMeta(annot);
                id = pdf_to_num(m_ctx, pdf_annot_obj(m_ctx, annot));
            } else {
                id = 0;
            }
            pdf_update_page(m_ctx, page);
            pdf_end_operation(m_ctx, m_pdf);
            m_undoPages.append(index);
            m_redoPages.clear();
            m_dirty = true;
        } else {
            pdf_abandon_operation(m_ctx, m_pdf);
        }
    }
    fz_catch(m_ctx)
    {
        CARMOPDF_ERROR_HANDLED(m_ctx);
        id = -1;
    }
    pdf_drop_annot(m_ctx, annot);
    fz_drop_page(m_ctx, &page->super);
    return id;
}

int PdfDocument::addTextMarkup(int index, AnnotationKind kind, const QVector<QRectF> &rects, const QColor &color)
{
    if (rects.isEmpty())
        return -1;
    pdf_page *page = nullptr;
    if (beginAnnot(index, "surlignage", &page) < 0)
        return -1;
    std::vector<fz_quad> quads;
    pdf_annot *annot = nullptr;
    bool ok = false;
    fz_var(annot);
    fz_var(ok);
    fz_try(m_ctx)
    {
        const fz_point o = origin(m_ctx, page);
        for (const QRectF &r : rects) {
            quads.push_back(fz_make_quad(float(r.left() + o.x), float(r.top() + o.y), float(r.right() + o.x), float(r.top() + o.y),
                                         float(r.left() + o.x), float(r.bottom() + o.y), float(r.right() + o.x), float(r.bottom() + o.y)));
        }
        const auto type = kind == AnnotationKind::Underline ? PDF_ANNOT_UNDERLINE
                          : kind == AnnotationKind::StrikeOut ? PDF_ANNOT_STRIKE_OUT
                                                              : PDF_ANNOT_HIGHLIGHT;
        annot = pdf_create_annot(m_ctx, page, type);
        pdf_set_annot_quad_points(m_ctx, annot, int(quads.size()), quads.data());
        setColor(m_ctx, annot, color, false);
        ok = true;
    }
    fz_catch(m_ctx) { CARMOPDF_ERROR_HANDLED(m_ctx); }
    return finishAnnot(index, page, annot, ok);
}

int PdfDocument::addNote(int index, QPointF point, const QString &text, const QColor &color)
{
    pdf_page *page = nullptr;
    if (beginAnnot(index, "note", &page) < 0)
        return -1;
    const QByteArray utf8 = text.toUtf8();
    pdf_annot *annot = nullptr;
    bool ok = false;
    fz_var(annot);
    fz_var(ok);
    fz_try(m_ctx)
    {
        const fz_point o = origin(m_ctx, page);
        annot = pdf_create_annot(m_ctx, page, PDF_ANNOT_TEXT);
        pdf_set_annot_rect(m_ctx, annot, fz_make_rect(float(point.x() + o.x), float(point.y() + o.y), float(point.x() + o.x + 20),
                                                       float(point.y() + o.y + 20)));
        pdf_set_annot_icon_name(m_ctx, annot, "Comment");
        pdf_set_annot_contents(m_ctx, annot, utf8.constData());
        setColor(m_ctx, annot, color, false);
        ok = true;
    }
    fz_catch(m_ctx) { CARMOPDF_ERROR_HANDLED(m_ctx); }
    return finishAnnot(index, page, annot, ok);
}

int PdfDocument::addFreeText(int index, const QRectF &rect, const QString &text, double fontSize, const QColor &color)
{
    pdf_page *page = nullptr;
    if (beginAnnot(index, "texte", &page) < 0)
        return -1;
    const QByteArray utf8 = text.toUtf8();
    pdf_annot *annot = nullptr;
    bool ok = false;
    fz_var(annot);
    fz_var(ok);
    fz_try(m_ctx)
    {
        const fz_point o = origin(m_ctx, page);
        annot = pdf_create_annot(m_ctx, page, PDF_ANNOT_FREE_TEXT);
        pdf_set_annot_rect(m_ctx, annot, fz_make_rect(float(rect.left() + o.x), float(rect.top() + o.y), float(rect.right() + o.x),
                                                       float(rect.bottom() + o.y)));
        pdf_set_annot_contents(m_ctx, annot, utf8.constData());
        const QColor c = color.isValid() ? color : QColor(Qt::black);
        const float rgb[3] = {float(c.redF()), float(c.greenF()), float(c.blueF())};
        pdf_set_annot_default_appearance(m_ctx, annot, "Helv", float(fontSize), 3, rgb);
        pdf_set_annot_border_width(m_ctx, annot, 0);
        ok = true;
    }
    fz_catch(m_ctx) { CARMOPDF_ERROR_HANDLED(m_ctx); }
    return finishAnnot(index, page, annot, ok);
}

int PdfDocument::addInk(int index, const QVector<InkStroke> &strokes, const QColor &color, double width)
{
    if (strokes.isEmpty())
        return -1;
    pdf_page *page = nullptr;
    if (beginAnnot(index, "dessin", &page) < 0)
        return -1;
    std::vector<int> counts;
    std::vector<fz_point> points;
    pdf_annot *annot = nullptr;
    bool ok = false;
    fz_var(annot);
    fz_var(ok);
    fz_try(m_ctx)
    {
        const fz_point o = origin(m_ctx, page);
        for (const InkStroke &stroke : strokes) {
            if (stroke.isEmpty())
                continue;
            counts.push_back(int(stroke.size()));
            for (const QPointF &p : stroke)
                points.push_back(fz_make_point(float(p.x() + o.x), float(p.y() + o.y)));
        }
        annot = pdf_create_annot(m_ctx, page, PDF_ANNOT_INK);
        pdf_set_annot_ink_list(m_ctx, annot, int(counts.size()), counts.data(), points.data());
        pdf_set_annot_border_width(m_ctx, annot, float(width));
        setColor(m_ctx, annot, color, false);
        ok = true;
    }
    fz_catch(m_ctx) { CARMOPDF_ERROR_HANDLED(m_ctx); }
    return finishAnnot(index, page, annot, ok);
}

int PdfDocument::addShape(int index, AnnotationKind kind, const QRectF &rect, const QColor &stroke, const QColor &fill, double width)
{
    pdf_page *page = nullptr;
    if (beginAnnot(index, "forme", &page) < 0)
        return -1;
    pdf_annot *annot = nullptr;
    bool ok = false;
    fz_var(annot);
    fz_var(ok);
    fz_try(m_ctx)
    {
        const fz_point o = origin(m_ctx, page);
        if (kind == AnnotationKind::Line || kind == AnnotationKind::Arrow) {
            annot = pdf_create_annot(m_ctx, page, PDF_ANNOT_LINE);
            pdf_set_annot_line(m_ctx, annot, fz_make_point(float(rect.left() + o.x), float(rect.top() + o.y)),
                               fz_make_point(float(rect.right() + o.x), float(rect.bottom() + o.y)));
            if (kind == AnnotationKind::Arrow) {
                pdf_set_annot_line_ending_styles(m_ctx, annot, PDF_ANNOT_LE_NONE, PDF_ANNOT_LE_CLOSED_ARROW);
                setColor(m_ctx, annot, stroke, true);
            }
        } else {
            annot = pdf_create_annot(m_ctx, page, kind == AnnotationKind::Circle ? PDF_ANNOT_CIRCLE : PDF_ANNOT_SQUARE);
            const QRectF n = rect.normalized();
            pdf_set_annot_rect(m_ctx, annot, fz_make_rect(float(n.left() + o.x), float(n.top() + o.y), float(n.right() + o.x),
                                                           float(n.bottom() + o.y)));
            if (fill.isValid())
                setColor(m_ctx, annot, fill, true);
        }
        pdf_set_annot_border_width(m_ctx, annot, float(width));
        setColor(m_ctx, annot, stroke, false);
        ok = true;
    }
    fz_catch(m_ctx) { CARMOPDF_ERROR_HANDLED(m_ctx); }
    return finishAnnot(index, page, annot, ok);
}

int PdfDocument::addStampImage(int index, const QRectF &rect, const QImage &image, const QString &contents)
{
    if (image.isNull() || rect.isEmpty())
        return -1;
    QByteArray png;
    {
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
    }
    pdf_page *page = nullptr;
    if (beginAnnot(index, "tampon", &page) < 0)
        return -1;
    const QByteArray utf8 = contents.toUtf8();
    pdf_annot *annot = nullptr;
    fz_buffer *data = nullptr;
    fz_image *fzImage = nullptr;
    bool ok = false;
    fz_var(annot);
    fz_var(data);
    fz_var(fzImage);
    fz_var(ok);
    fz_try(m_ctx)
    {
        const fz_point o = origin(m_ctx, page);
        data = fz_new_buffer_from_copied_data(m_ctx, reinterpret_cast<const unsigned char *>(png.constData()), size_t(png.size()));
        fzImage = fz_new_image_from_buffer(m_ctx, data);
        annot = pdf_create_annot(m_ctx, page, PDF_ANNOT_STAMP);
        pdf_set_annot_rect(m_ctx, annot, fz_make_rect(float(rect.left() + o.x), float(rect.top() + o.y), float(rect.right() + o.x),
                                                       float(rect.bottom() + o.y)));
        if (!utf8.isEmpty())
            pdf_set_annot_contents(m_ctx, annot, utf8.constData());
        // Auteur et date avant l'image : toute modification ultérieure
        // demanderait à MuPDF de régénérer l'apparence, et effacerait l'image.
        stampMeta(annot);
        pdf_set_annot_stamp_image(m_ctx, annot, fzImage);
        // L'image est l'apparence définitive : rien à régénérer (MuPDF 1.23).
        pdf_set_annot_resynthesised(m_ctx, annot);
        ok = true;
    }
    fz_always(m_ctx)
    {
        fz_drop_image(m_ctx, fzImage);
        fz_drop_buffer(m_ctx, data);
    }
    fz_catch(m_ctx) { CARMOPDF_ERROR_HANDLED(m_ctx); }
    return finishAnnot(index, page, annot, ok, false);
}

int PdfDocument::addStampText(int index, const QRectF &rect, const QString &text, const QColor &color)
{
    // Le tampon texte est dessiné en image : rendu identique dans tous les
    // lecteurs, accents garantis.
    const double scale = 4.0;
    QImage image(int(rect.width() * scale), int(rect.height() * scale), QImage::Format_ARGB32_Premultiplied);
    if (image.isNull())
        return -1;
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        const QColor c = color.isValid() ? color : QColor(196, 43, 28);
        const double border = 3 * scale;
        painter.setPen(QPen(c, border));
        painter.drawRoundedRect(QRectF(border, border, image.width() - 2 * border, image.height() - 2 * border), 6 * scale, 6 * scale);
        QFont font(QStringLiteral("Arial"));
        font.setBold(true);
        font.setPixelSize(int(image.height() * 0.45));
        painter.setFont(font);
        QRectF textRect = image.rect().adjusted(border * 2, border, -border * 2, -border);
        // Réduit la police jusqu'à ce que le texte tienne.
        while (font.pixelSize() > 8 && painter.fontMetrics().horizontalAdvance(text) > textRect.width()) {
            font.setPixelSize(font.pixelSize() - 2);
            painter.setFont(font);
        }
        painter.drawText(textRect, Qt::AlignCenter, text);
    }
    return addStampImage(index, rect, image, text);
}

bool PdfDocument::setAnnotationContents(int index, int id, const QString &text)
{
    pdf_page *page = nullptr;
    if (beginAnnot(index, "modification", &page) < 0)
        return false;
    const QByteArray utf8 = text.toUtf8();
    pdf_annot *annot = nullptr;
    bool ok = false;
    fz_var(annot);
    fz_var(ok);
    fz_try(m_ctx)
    {
        annot = findAnnot(page, id);
        if (annot) {
            pdf_keep_annot(m_ctx, annot);
            pdf_set_annot_contents(m_ctx, annot, utf8.constData());
            ok = true;
        }
    }
    fz_catch(m_ctx) { CARMOPDF_ERROR_HANDLED(m_ctx); }
    return finishAnnot(index, page, annot, ok) >= 0;
}

bool PdfDocument::setAnnotationRect(int index, int id, const QRectF &rect)
{
    pdf_page *page = nullptr;
    if (beginAnnot(index, "déplacement", &page) < 0)
        return false;
    pdf_annot *annot = nullptr;
    bool ok = false;
    fz_var(annot);
    fz_var(ok);
    fz_try(m_ctx)
    {
        annot = findAnnot(page, id);
        if (annot) {
            pdf_keep_annot(m_ctx, annot);
            const fz_point o = origin(m_ctx, page);
            const QRectF n = rect.normalized();
            const fz_rect r = fz_make_rect(float(n.left() + o.x), float(n.top() + o.y), float(n.right() + o.x), float(n.bottom() + o.y));
            if (pdf_annot_type(m_ctx, annot) == PDF_ANNOT_LINE)
                pdf_set_annot_line(m_ctx, annot, fz_make_point(r.x0, r.y0), fz_make_point(r.x1, r.y1));
            else if (pdf_annot_has_rect(m_ctx, annot))
                pdf_set_annot_rect(m_ctx, annot, r);
            else
                fz_throw(m_ctx, FZ_ERROR_GENERIC, "annotation non déplaçable");
            ok = true;
        }
    }
    fz_catch(m_ctx) { CARMOPDF_ERROR_HANDLED(m_ctx); }
    return finishAnnot(index, page, annot, ok) >= 0;
}

bool PdfDocument::deleteAnnotation(int index, int id)
{
    // pdf_delete_annot ouvre sa propre opération de journal ; l'imbriquer
    // dans une opération englobante fausse l'annulation (une seule des deux
    // entrées du tableau /Annots est restaurée). On la laisse donc seule.
    if (!m_pdf || index < 0 || index >= m_pageCount)
        return false;
    pdf_page *page = nullptr;
    bool ok = false;
    fz_var(page);
    fz_var(ok);
    fz_try(m_ctx)
    {
        page = pdf_load_page(m_ctx, m_pdf, index);
        pdf_annot *annot = findAnnot(page, id);
        if (annot) {
            pdf_delete_annot(m_ctx, page, annot);
            ok = true;
        }
    }
    fz_always(m_ctx) { fz_drop_page(m_ctx, page ? &page->super : nullptr); }
    fz_catch(m_ctx)
    {
        CARMOPDF_ERROR_HANDLED(m_ctx);
        return false;
    }
    if (ok) {
        m_undoPages.append(index);
        m_redoPages.clear();
        m_dirty = true;
    }
    return ok;
}

bool PdfDocument::canUndo() const
{
    return m_pdf && !m_undoPages.isEmpty() && pdf_can_undo(m_ctx, m_pdf);
}

bool PdfDocument::canRedo() const
{
    return m_pdf && !m_redoPages.isEmpty() && pdf_can_redo(m_ctx, m_pdf);
}

int PdfDocument::undo()
{
    if (!canUndo())
        return -1;
    fz_try(m_ctx) { pdf_undo(m_ctx, m_pdf); }
    fz_catch(m_ctx)
    {
        CARMOPDF_ERROR_HANDLED(m_ctx);
        return -1;
    }
    const int page = m_undoPages.takeLast();
    m_redoPages.append(page);
    m_dirty = true;
    return page;
}

int PdfDocument::redo()
{
    if (!canRedo())
        return -1;
    fz_try(m_ctx) { pdf_redo(m_ctx, m_pdf); }
    fz_catch(m_ctx)
    {
        CARMOPDF_ERROR_HANDLED(m_ctx);
        return -1;
    }
    const int page = m_redoPages.takeLast();
    m_undoPages.append(page);
    m_dirty = true;
    return page;
}

bool PdfDocument::hasUnsavedChanges() const
{
    // Indicateur maison : MuPDF 1.23 ne remet pas le sien à zéro après un
    // enregistrement incrémental.
    return m_pdf && m_dirty;
}

bool PdfDocument::save(const QString &path)
{
    if (!m_pdf)
        return false;
    const bool samePath = QFileInfo(path).absoluteFilePath() == QFileInfo(m_path).absoluteFilePath();
    bool incremental = false;
    fz_var(incremental);
    fz_try(m_ctx) { incremental = samePath && pdf_can_be_saved_incrementally(m_ctx, m_pdf); }
    fz_catch(m_ctx)
    {
        CARMOPDF_ERROR_HANDLED(m_ctx);
        incremental = false;
    }

    if (incremental) {
        // Ajout en fin de fichier : rapide, et les signatures existantes restent valides.
        pdf_write_options options = pdf_default_write_options;
        options.do_incremental = 1;
        const QByteArray utf8 = path.toUtf8();
        fz_try(m_ctx) { pdf_save_document(m_ctx, m_pdf, utf8.constData(), &options); }
        fz_catch(m_ctx)
        {
            m_error = QString::fromUtf8(fz_caught_message(m_ctx));
            CARMOPDF_ERROR_HANDLED(m_ctx);
            return false;
        }
        m_dirty = false;
        return true;
    }

    // Copie complète dans un fichier temporaire, puis remplacement.
    const QFileInfo target(path);
    const QString tempPath = temporaryPathIn(target.absolutePath());
    if (tempPath.isEmpty()) {
        m_error = QStringLiteral("Impossible d'écrire dans le dossier de destination.");
        return false;
    }
    const QByteArray tempUtf8 = tempPath.toUtf8();
    pdf_write_options options = pdf_default_write_options;
    options.do_garbage = 1;
    options.do_compress = 1;
    fz_try(m_ctx) { pdf_save_document(m_ctx, m_pdf, tempUtf8.constData(), &options); }
    fz_catch(m_ctx)
    {
        m_error = QString::fromUtf8(fz_caught_message(m_ctx));
        CARMOPDF_ERROR_HANDLED(m_ctx);
        QFile::remove(tempPath);
        return false;
    }
    if (samePath) {
        // Le document ouvert verrouille le fichier sous Windows : on le ferme
        // avant de le remplacer, puis on le rouvre. L'historique repart de zéro.
        dropTextPages();
        fz_drop_document(m_ctx, m_doc);
        m_doc = nullptr;
        m_pdf = nullptr;
        m_undoPages.clear();
        m_redoPages.clear();
        if ((QFile::exists(path) && !QFile::remove(path)) || !QFile::rename(tempPath, path)) {
            m_error = QStringLiteral("Impossible de remplacer le fichier : il est utilisé par un autre programme.");
            QFile::remove(tempPath);
            open(path, m_lastPassword);
            enableEditing();
            return false;
        }
        const auto status = open(path, m_lastPassword);
        enableEditing();
        return status == OpenStatus::Ok;
    }
    if ((QFile::exists(path) && !QFile::remove(path)) || !QFile::rename(tempPath, path)) {
        m_error = QStringLiteral("Impossible d'écrire le fichier de destination.");
        QFile::remove(tempPath);
        return false;
    }
    m_dirty = false;
    return true;
}
