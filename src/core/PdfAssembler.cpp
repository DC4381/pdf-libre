// Copyright (C) 2026 David Carmo
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PdfAssembler.h"

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSaveFile>
#include <QTemporaryFile>

#include <functional>
#include <map>

#if FZ_VERSION_MAJOR > 1 || FZ_VERSION_MINOR >= 24
#define CARMOPDF_ERROR_HANDLED(ctx) fz_ignore_error(ctx)
#else
#define CARMOPDF_ERROR_HANDLED(ctx) ((void)0)
#endif

namespace {

struct Context
{
    fz_context *ctx = nullptr;
    Context()
    {
        ctx = fz_new_context(nullptr, nullptr, 128u << 20);
        if (ctx) {
            fz_try(ctx) { fz_register_document_handlers(ctx); }
            fz_catch(ctx)
            {
                fz_drop_context(ctx);
                ctx = nullptr;
            }
        }
    }
    ~Context() { fz_drop_context(ctx); }
};

// Document source ouvert, avec sa table de greffe vers la destination.
struct Source
{
    pdf_document *doc = nullptr;
    pdf_graft_map *map = nullptr;
};

QString caught(fz_context *ctx)
{
    QString message = QString::fromUtf8(fz_caught_message(ctx));
    CARMOPDF_ERROR_HANDLED(ctx);
    return message;
}

// Ouvre une source PDF (avec mot de passe éventuel). Lève une erreur MuPDF
// si le fichier n'est pas un PDF lisible.
pdf_document *openSource(fz_context *ctx, const PageRef &ref)
{
    const QByteArray path = ref.sourcePath.toUtf8();
    pdf_document *doc = pdf_open_document(ctx, path.constData());
    if (pdf_needs_password(ctx, doc)) {
        if (ref.password.isEmpty() || !pdf_authenticate_password(ctx, doc, ref.password.constData())) {
            pdf_drop_document(ctx, doc);
            fz_throw(ctx, FZ_ERROR_GENERIC, "mot de passe requis pour %s", path.constData());
        }
    }
    return doc;
}

// Rotation effective (0..270) après ajout de `delta` à la rotation existante.
void rotatePage(fz_context *ctx, pdf_document *doc, int index, int delta)
{
    if (delta % 360 == 0)
        return;
    pdf_obj *page = pdf_lookup_page_obj(ctx, doc, index);
    const int current = pdf_dict_get_int(ctx, page, PDF_NAME(Rotate));
    int rotation = ((current + delta) % 360 + 360) % 360;
    pdf_dict_put_int(ctx, page, PDF_NAME(Rotate), rotation);
}

int pageNumberOf(fz_context *ctx, fz_document *doc, fz_location location)
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

// Signets : on ne les conserve que si toutes les pages ont une même source.
// Chaque signet est redirigé vers la nouvelle position de sa page.
void copyOutline(fz_context *ctx, pdf_document *dst, fz_document *srcFz, const QHash<int, int> &newIndexOf)
{
    fz_outline *root = fz_load_outline(ctx, srcFz);
    if (!root)
        return;
    pdf_obj *outlines = nullptr;
    // Construction récursive : on ajoute les entrées dont la page existe encore.
    std::function<pdf_obj *(fz_outline *, pdf_obj *, int &)> build;
    build = [&](fz_outline *node, pdf_obj *parent, int &count) -> pdf_obj * {
        pdf_obj *first = nullptr;
        pdf_obj *previous = nullptr;
        for (; node; node = node->next) {
            const int newPage = newIndexOf.value(pageNumberOf(ctx, srcFz, node->page), -1);
            if (newPage < 0)
                continue;
            pdf_obj *item = pdf_add_new_dict(ctx, dst, 6);
            pdf_dict_put_text_string(ctx, item, PDF_NAME(Title), node->title ? node->title : "");
            pdf_obj *dest = pdf_new_array(ctx, dst, 3);
            pdf_array_push(ctx, dest, pdf_lookup_page_obj(ctx, dst, newPage));
            pdf_array_push(ctx, dest, PDF_NAME(XYZ));
            pdf_array_push(ctx, dest, PDF_NULL);
            pdf_array_push(ctx, dest, PDF_NULL);
            pdf_array_push(ctx, dest, PDF_NULL);
            pdf_dict_put_drop(ctx, item, PDF_NAME(Dest), dest);
            pdf_dict_put(ctx, item, PDF_NAME(Parent), parent);
            if (previous) {
                pdf_dict_put(ctx, item, PDF_NAME(Prev), previous);
                pdf_dict_put(ctx, previous, PDF_NAME(Next), item);
            } else {
                first = item;
            }
            int childCount = 0;
            if (node->down) {
                pdf_obj *firstChild = build(node->down, item, childCount);
                if (firstChild) {
                    pdf_dict_put(ctx, item, PDF_NAME(First), firstChild);
                    pdf_dict_put_int(ctx, item, PDF_NAME(Count), node->is_open ? childCount : -childCount);
                }
            }
            previous = item;
            ++count;
        }
        if (previous && parent)
            pdf_dict_put(ctx, parent, PDF_NAME(Last), previous);
        return first;
    };

    fz_try(ctx)
    {
        outlines = pdf_add_new_dict(ctx, dst, 4);
        pdf_dict_put(ctx, outlines, PDF_NAME(Type), PDF_NAME(Outlines));
        int count = 0;
        pdf_obj *first = build(root, outlines, count);
        if (first) {
            pdf_dict_put(ctx, outlines, PDF_NAME(First), first);
            pdf_dict_put_int(ctx, outlines, PDF_NAME(Count), count);
            pdf_dict_put(ctx, pdf_dict_get(ctx, pdf_trailer(ctx, dst), PDF_NAME(Root)), PDF_NAME(Outlines), outlines);
        }
    }
    fz_always(ctx) { fz_drop_outline(ctx, root); }
    fz_catch(ctx) { CARMOPDF_ERROR_HANDLED(ctx); } // Sans signets plutôt que sans document.
}

} // namespace

PdfAssembler::Result PdfAssembler::assemble(const QVector<PageRef> &pages, const QString &outputPath)
{
    Result result;
    if (pages.isEmpty()) {
        result.error = QStringLiteral("Aucune page à enregistrer.");
        return result;
    }
    Context context;
    fz_context *ctx = context.ctx;
    if (!ctx) {
        result.error = QStringLiteral("Impossible d'initialiser le moteur PDF.");
        return result;
    }

    // Fichier temporaire dans le même dossier : le remplacement final est atomique.
    const QFileInfo outInfo(outputPath);
    QTemporaryFile temp(outInfo.absolutePath() + QStringLiteral("/.carmopdf-XXXXXX.tmp"));
    if (!temp.open()) {
        result.error = QStringLiteral("Impossible d'écrire dans le dossier %1.").arg(QDir::toNativeSeparators(outInfo.absolutePath()));
        return result;
    }
    const QString tempPath = temp.fileName();
    temp.close();

    // Signets et métadonnées conservés si une seule source et aucune page blanche.
    QHash<int, int> newIndexOf;
    bool singleSource = true;
    fz_var(singleSource);
    for (int i = 0; i < pages.size(); ++i) {
        if (pages[i].isBlank() || pages[i].sourcePath != pages.first().sourcePath) {
            singleSource = false;
            break;
        }
        if (!newIndexOf.contains(pages[i].page))
            newIndexOf.insert(pages[i].page, i);
    }
    const QByteArray tempUtf8 = tempPath.toUtf8();

    pdf_document *dst = nullptr;
    std::map<QString, Source> sources;
    fz_var(dst);
    fz_try(ctx)
    {
        dst = pdf_create_document(ctx);
        int index = 0;
        for (const PageRef &ref : pages) {
            if (ref.isBlank()) {
                const fz_rect box = fz_make_rect(0, 0, float(ref.blankWidth), float(ref.blankHeight));
                pdf_obj *page = pdf_add_page(ctx, dst, box, ref.rotation, nullptr, nullptr);
                pdf_insert_page(ctx, dst, index, page);
                pdf_drop_obj(ctx, page);
            } else {
                Source &source = sources[ref.sourcePath];
                if (!source.doc) {
                    source.doc = openSource(ctx, ref);
                    source.map = pdf_new_graft_map(ctx, dst);
                }
                const int count = pdf_count_pages(ctx, source.doc);
                if (ref.page < 0 || ref.page >= count)
                    fz_throw(ctx, FZ_ERROR_GENERIC, "page %d inexistante", ref.page + 1);
                pdf_graft_mapped_page(ctx, source.map, index, source.doc, ref.page);
                rotatePage(ctx, dst, index, ref.rotation);
            }
            ++index;
        }

        if (singleSource) {
            Source &source = sources.begin()->second;
            copyOutline(ctx, dst, &source.doc->super, newIndexOf);
            pdf_obj *info = pdf_dict_get(ctx, pdf_trailer(ctx, source.doc), PDF_NAME(Info));
            if (info)
                pdf_dict_put_drop(ctx, pdf_trailer(ctx, dst), PDF_NAME(Info), pdf_graft_object(ctx, dst, info));
        }

        pdf_write_options options = pdf_default_write_options;
        options.do_garbage = 3; // nettoie et dédoublonne les objets greffés
        options.do_compress = 1;
        pdf_save_document(ctx, dst, tempUtf8.constData(), &options);
        result.pageCount = pdf_count_pages(ctx, dst);
    }
    fz_always(ctx)
    {
        for (auto &entry : sources) {
            pdf_drop_graft_map(ctx, entry.second.map);
            pdf_drop_document(ctx, entry.second.doc);
        }
        pdf_drop_document(ctx, dst);
    }
    fz_catch(ctx)
    {
        result.error = caught(ctx);
        QFile::remove(tempPath);
        return result;
    }

    // Remplacement atomique du fichier de destination.
    if (QFile::exists(outputPath) && !QFile::remove(outputPath)) {
        result.error = QStringLiteral("Le fichier de destination est verrouillé par un autre programme.");
        QFile::remove(tempPath);
        return result;
    }
    if (!QFile::rename(tempPath, outputPath)) {
        result.error = QStringLiteral("Impossible d'écrire %1.").arg(QDir::toNativeSeparators(outputPath));
        QFile::remove(tempPath);
        return result;
    }
    result.ok = true;
    return result;
}

int PdfAssembler::signatureCount(const QString &path, const QByteArray &password)
{
    Context context;
    fz_context *ctx = context.ctx;
    if (!ctx)
        return 0;
    PageRef ref;
    ref.sourcePath = path;
    ref.password = password;
    pdf_document *doc = nullptr;
    int count = 0;
    fz_var(doc);
    fz_var(count);
    fz_try(ctx)
    {
        doc = openSource(ctx, ref);
        count = pdf_count_signatures(ctx, doc);
    }
    fz_always(ctx) { pdf_drop_document(ctx, doc); }
    fz_catch(ctx)
    {
        CARMOPDF_ERROR_HANDLED(ctx);
        return 0;
    }
    return count;
}
