// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PdfDocument.h"

extern "C" {
#include <mupdf/fitz.h>
}

#include <cstring>
#include <vector>

// Règle MuPDF : fz_try utilise setjmp/longjmp. Aucun objet C++ doté d'un
// destructeur n'est créé dans un bloc fz_try, et on ne fait jamais `return`
// depuis fz_try. Les variables modifiées dans fz_try et lues ensuite sont
// déclarées avec fz_var.

// Depuis MuPDF 1.24, chaque erreur interceptée doit être déclarée traitée,
// sinon MuPDF signale une « UNHANDLED EXCEPTION ».
#if FZ_VERSION_MAJOR > 1 || FZ_VERSION_MINOR >= 24
#define PDFLIBRE_ERROR_HANDLED(ctx) fz_ignore_error(ctx)
#else
#define PDFLIBRE_ERROR_HANDLED(ctx) ((void)0)
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
        PDFLIBRE_ERROR_HANDLED(ctx);
        page = -1;
    }
    return page;
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
        fz_drop_document(m_ctx, m_doc);
        fz_drop_context(m_ctx);
    }
}

PdfDocument::OpenStatus PdfDocument::open(const QString &path, const QByteArray &password)
{
    if (!m_ctx)
        return OpenStatus::Failed;
    fz_drop_document(m_ctx, m_doc);
    m_doc = nullptr;
    m_pageCount = 0;

    const QByteArray utf8Path = path.toUtf8();
    fz_document *doc = nullptr;
    fz_var(doc);
    fz_try(m_ctx) { doc = fz_open_document(m_ctx, utf8Path.constData()); }
    fz_catch(m_ctx)
    {
        m_error = QString::fromUtf8(fz_caught_message(m_ctx));
        PDFLIBRE_ERROR_HANDLED(m_ctx);
        return OpenStatus::Failed;
    }

    int needsPassword = 0;
    fz_var(needsPassword);
    fz_try(m_ctx) { needsPassword = fz_needs_password(m_ctx, doc); }
    fz_catch(m_ctx)
    {
        m_error = QString::fromUtf8(fz_caught_message(m_ctx));
        PDFLIBRE_ERROR_HANDLED(m_ctx);
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
        PDFLIBRE_ERROR_HANDLED(m_ctx);
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
        PDFLIBRE_ERROR_HANDLED(m_ctx);
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
        PDFLIBRE_ERROR_HANDLED(m_ctx);
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
        PDFLIBRE_ERROR_HANDLED(m_ctx);
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
        PDFLIBRE_ERROR_HANDLED(m_ctx);
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
        PDFLIBRE_ERROR_HANDLED(m_ctx);
        length = -1;
    }
    return length > 0 ? QString::fromUtf8(buffer).trimmed() : QString();
}
