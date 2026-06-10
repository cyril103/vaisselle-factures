#include "app.h"
#include <cairo/cairo-pdf.h>
#include <glib/gstdio.h>
#include <pango/pangocairo.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAGE_W 595.28
#define PAGE_H 841.89
#define MARGIN 40.0
#define CONTENT_W (PAGE_W - 2 * MARGIN)
#define COMPANY_BOX_W 250.0
#define HEADER_GAP 20.0

#define C_BRAND_R 0.22
#define C_BRAND_G 0.37
#define C_BRAND_B 0.27
#define C_TEXT_R 0.12
#define C_TEXT_G 0.12
#define C_TEXT_B 0.12
#define C_MUTED_R 0.40
#define C_MUTED_G 0.40
#define C_MUTED_B 0.40
#define C_PANEL_BG_R 0.94
#define C_PANEL_BG_G 0.97
#define C_PANEL_BG_B 0.95
#define C_LINE_R 0.82
#define C_LINE_G 0.82
#define C_LINE_B 0.82

static char export_error[512];

const char *invoice_export_error_message(void) {
    return export_error[0] ? export_error : "Export impossible";
}

static void set_export_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(export_error, sizeof(export_error), fmt, args);
    va_end(args);
}

static const char *safe(const unsigned char *text) {
    return text ? (const char *)text : "";
}

static char *fmt_amount(double amount, gboolean with_euro) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", amount);
    for (char *p = buf; *p; p++) {
        if (*p == '.') {
            *p = ',';
            break;
        }
    }
    return with_euro ? g_strdup_printf("%s €", buf) : g_strdup(buf);
}

typedef struct {
    cairo_t *cr;
    double y;
} PdfCtx;

static PangoLayout *make_layout(cairo_t *cr, gboolean bold, double size) {
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *font = pango_font_description_from_string("Sans");
    pango_font_description_set_weight(font, bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
    pango_font_description_set_size(font, (gint)(size * PANGO_SCALE));
    pango_layout_set_font_description(layout, font);
    pango_font_description_free(font);
    return layout;
}

static char *dup_text(const char *text) {
    return g_utf8_make_valid(text ? text : "", -1);
}

static char *dup_column(sqlite3_stmt *st, int col) {
    return dup_text(safe(sqlite3_column_text(st, col)));
}

static double text_height(cairo_t *cr, const char *text, gboolean bold, double size, double width) {
    PangoLayout *layout = make_layout(cr, bold, size);
    if (width > 0) {
        pango_layout_set_width(layout, (int)(width * PANGO_SCALE));
    }
    pango_layout_set_text(layout, text, -1);
    int h = 0;
    pango_layout_get_pixel_size(layout, NULL, &h);
    g_object_unref(layout);
    return h;
}

static double draw_text(PdfCtx *ctx, double x, const char *text, gboolean bold, double size) {
    PangoLayout *layout = make_layout(ctx->cr, bold, size);
    pango_layout_set_text(layout, text, -1);
    cairo_move_to(ctx->cr, x, ctx->y);
    pango_cairo_show_layout(ctx->cr, layout);
    int h = 0;
    pango_layout_get_pixel_size(layout, NULL, &h);
    g_object_unref(layout);
    return h;
}

static double draw_text_width(PdfCtx *ctx, double x, double width, const char *text, gboolean bold, double size) {
    PangoLayout *layout = make_layout(ctx->cr, bold, size);
    pango_layout_set_width(layout, (int)(width * PANGO_SCALE));
    pango_layout_set_text(layout, text, -1);
    cairo_move_to(ctx->cr, x, ctx->y);
    pango_cairo_show_layout(ctx->cr, layout);
    int h = 0;
    pango_layout_get_pixel_size(layout, NULL, &h);
    g_object_unref(layout);
    return h;
}

static double draw_text_right(PdfCtx *ctx, double right_x, const char *text, gboolean bold, double size) {
    PangoLayout *layout = make_layout(ctx->cr, bold, size);
    pango_layout_set_text(layout, text, -1);
    int w = 0, h = 0;
    pango_layout_get_pixel_size(layout, &w, &h);
    cairo_move_to(ctx->cr, right_x - w, ctx->y);
    pango_cairo_show_layout(ctx->cr, layout);
    g_object_unref(layout);
    return h;
}

static void set_rgb(cairo_t *cr, double r, double g, double b) {
    cairo_set_source_rgb(cr, r, g, b);
}

static void fill_rect(cairo_t *cr, double x, double y, double w, double h, double r, double g, double b) {
    set_rgb(cr, r, g, b);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
}

static void stroke_rect(cairo_t *cr, double x, double y, double w, double h, double lw, double r, double g, double b) {
    set_rgb(cr, r, g, b);
    cairo_set_line_width(cr, lw);
    cairo_rectangle(cr, x, y, w, h);
    cairo_stroke(cr);
}

static void draw_hline(cairo_t *cr, double x1, double x2, double y, double r, double g, double b) {
    set_rgb(cr, r, g, b);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, x1, y);
    cairo_line_to(cr, x2, y);
    cairo_stroke(cr);
}

static gboolean ensure_space(PdfCtx *ctx, double needed) {
    if (ctx->y + needed <= PAGE_H - MARGIN) {
        return TRUE;
    }
    cairo_show_page(ctx->cr);
    ctx->y = MARGIN;
    return TRUE;
}

static double measure_panel_text(cairo_t *cr, const char *text, gboolean bold, double size, double width) {
    if (!text || !*text) {
        return 0;
    }
    return text_height(cr, text, bold, size, width);
}

static double draw_company_box(PdfCtx *ctx, const char *name, const char *siret, const char *address,
                               const char *email, const char *phone) {
    const double box_x = PAGE_W - MARGIN - COMPANY_BOX_W;
    const double box_y = ctx->y;
    const double box_w = COMPANY_BOX_W;
    const double inner_x = box_x + 14;
    const double inner_w = box_w - 28;
    char siret_line[128];
    char phone_line[128];

    snprintf(siret_line, sizeof(siret_line), "SIRET : %s", siret && *siret ? siret : "—");
    snprintf(phone_line, sizeof(phone_line), "Tél : %s", phone && *phone ? phone : "—");

    gboolean has_email = email && *email;
    double h_label = measure_panel_text(ctx->cr, "Émetteur", TRUE, 8, inner_w) + 10;
    double h_name = measure_panel_text(ctx->cr, name, TRUE, 12, inner_w) + 6;
    double h_siret = measure_panel_text(ctx->cr, siret_line, FALSE, 9, inner_w) + 4;
    double h_addr = measure_panel_text(ctx->cr, address, FALSE, 9, inner_w) + 4;
    double h_email = has_email ? measure_panel_text(ctx->cr, email, FALSE, 9, inner_w) + 4 : 0;
    double h_phone = measure_panel_text(ctx->cr, phone_line, FALSE, 9, inner_w);
    double box_h = 14 + h_label + h_name + h_siret + h_addr + h_email + h_phone + 14;

    fill_rect(ctx->cr, box_x, box_y, box_w, box_h, C_PANEL_BG_R, C_PANEL_BG_G, C_PANEL_BG_B);
    stroke_rect(ctx->cr, box_x, box_y, box_w, box_h, 1.4, C_BRAND_R, C_BRAND_G, C_BRAND_B);

    double text_y = box_y + 14;
    ctx->y = text_y;
    set_rgb(ctx->cr, C_BRAND_R, C_BRAND_G, C_BRAND_B);
    draw_text_width(ctx, inner_x, inner_w, "ÉMETTEUR", TRUE, 8);
    ctx->y += h_label;

    set_rgb(ctx->cr, C_TEXT_R, C_TEXT_G, C_TEXT_B);
    draw_text_width(ctx, inner_x, inner_w, name && *name ? name : "Entreprise", TRUE, 12);
    ctx->y += h_name;

    set_rgb(ctx->cr, C_TEXT_R, C_TEXT_G, C_TEXT_B);
    draw_text_width(ctx, inner_x, inner_w, siret_line, FALSE, 9);
    ctx->y += h_siret;
    draw_text_width(ctx, inner_x, inner_w, address && *address ? address : "", FALSE, 9);
    ctx->y += h_addr;
    if (has_email) {
        draw_text_width(ctx, inner_x, inner_w, email, FALSE, 9);
        ctx->y += h_email;
    }
    draw_text_width(ctx, inner_x, inner_w, phone_line, FALSE, 9);

    return box_y + box_h;
}

static double draw_client_box(PdfCtx *ctx, const char *name, const char *address,
                              const char *email, const char *phone) {
    const double box_x = MARGIN;
    const double box_y = ctx->y;
    const double box_w = 280;
    const double inner_x = box_x + 14;
    const double inner_w = box_w - 28;
    char phone_line[128];

    snprintf(phone_line, sizeof(phone_line), "Tél : %s", phone && *phone ? phone : "—");

    gboolean has_email = email && *email;
    double h_label = measure_panel_text(ctx->cr, "Client", TRUE, 8, inner_w) + 10;
    double h_name = measure_panel_text(ctx->cr, name, TRUE, 11, inner_w) + 6;
    double h_addr = measure_panel_text(ctx->cr, address, FALSE, 9, inner_w) + 4;
    double h_email = has_email ? measure_panel_text(ctx->cr, email, FALSE, 9, inner_w) + 4 : 0;
    double h_phone = measure_panel_text(ctx->cr, phone_line, FALSE, 9, inner_w);
    double box_h = 14 + h_label + h_name + h_addr + h_email + h_phone + 14;

    fill_rect(ctx->cr, box_x, box_y, box_w, box_h, 1.0, 1.0, 1.0);
    stroke_rect(ctx->cr, box_x, box_y, box_w, box_h, 1.0, C_LINE_R, C_LINE_G, C_LINE_B);

    ctx->y = box_y + 14;
    set_rgb(ctx->cr, C_BRAND_R, C_BRAND_G, C_BRAND_B);
    draw_text_width(ctx, inner_x, inner_w, "CLIENT", TRUE, 8);
    ctx->y += h_label;

    set_rgb(ctx->cr, C_TEXT_R, C_TEXT_G, C_TEXT_B);
    draw_text_width(ctx, inner_x, inner_w, name && *name ? name : "—", TRUE, 11);
    ctx->y += h_name;
    draw_text_width(ctx, inner_x, inner_w, address && *address ? address : "", FALSE, 9);
    ctx->y += h_addr;
    if (has_email) {
        draw_text_width(ctx, inner_x, inner_w, email, FALSE, 9);
        ctx->y += h_email;
    }
    draw_text_width(ctx, inner_x, inner_w, phone_line, FALSE, 9);

    return box_y + box_h;
}

static double draw_invoice_header(PdfCtx *ctx, const char *num, const char *date,
                                  const char *oname, const char *osiret, const char *oaddr,
                                  const char *oemail, const char *ophone) {
    const double company_box_x = PAGE_W - MARGIN - COMPANY_BOX_W;
    const double title_w = company_box_x - MARGIN - HEADER_GAP;
    const double header_y = ctx->y;
    char date_line[128];

    snprintf(date_line, sizeof(date_line), "Date : %s", date);

    set_rgb(ctx->cr, C_MUTED_R, C_MUTED_G, C_MUTED_B);
    draw_text_width(ctx, MARGIN, title_w, "FACTURE", TRUE, 9);
    ctx->y += 14;

    set_rgb(ctx->cr, C_BRAND_R, C_BRAND_G, C_BRAND_B);
    double num_h = draw_text_width(ctx, MARGIN, title_w, num, TRUE, 15);
    ctx->y += num_h + 8;

    set_rgb(ctx->cr, C_MUTED_R, C_MUTED_G, C_MUTED_B);
    double date_h = draw_text_width(ctx, MARGIN, title_w, date_line, FALSE, 10);
    double left_bottom = ctx->y + date_h;

    ctx->y = header_y;
    double company_bottom = draw_company_box(ctx, oname, osiret, oaddr, oemail, ophone);

    return left_bottom > company_bottom ? left_bottom : company_bottom;
}

static void draw_lines_table(PdfCtx *ctx, App *app, int invoice_id) {
    const double col_desc = MARGIN + 8;
    const double col_qty = 300;
    const double col_price = 360;
    const double col_vat = 430;
    const double col_ttc = PAGE_W - MARGIN - 8;
    const double row_h = 24;
    int row_idx = 0;

    ensure_space(ctx, 70);
    set_rgb(ctx->cr, C_BRAND_R, C_BRAND_G, C_BRAND_B);
    draw_text(ctx, MARGIN, "Détail de la prestation", TRUE, 11);
    ctx->y += 20;

    const double table_top = ctx->y;
    fill_rect(ctx->cr, MARGIN, table_top, CONTENT_W, row_h, C_PANEL_BG_R, C_PANEL_BG_G, C_PANEL_BG_B);
    stroke_rect(ctx->cr, MARGIN, table_top, CONTENT_W, row_h, 1.0, C_BRAND_R, C_BRAND_G, C_BRAND_B);
    ctx->y = table_top + 7;
    set_rgb(ctx->cr, C_TEXT_R, C_TEXT_G, C_TEXT_B);
    draw_text(ctx, col_desc, "Désignation", TRUE, 9);
    draw_text_right(ctx, col_qty + 30, "Qté", TRUE, 9);
    draw_text_right(ctx, col_price + 40, "PU HT", TRUE, 9);
    draw_text_right(ctx, col_vat + 20, "TVA", TRUE, 9);
    draw_text_right(ctx, col_ttc, "Total TTC", TRUE, 9);
    ctx->y = table_top + row_h + 4;

    sqlite3_stmt *ls;
    if (sqlite3_prepare_v2(app->db, "SELECT description,quantity,unit_price,vat_rate,line_ttc FROM invoice_lines WHERE invoice_id=? ORDER BY id", -1, &ls, NULL) != SQLITE_OK) {
        return;
    }
    sqlite3_bind_int(ls, 1, invoice_id);
    while (sqlite3_step(ls) == SQLITE_ROW) {
        char *qty_s = fmt_amount(sqlite3_column_double(ls, 1), FALSE);
        char *price_s = fmt_amount(sqlite3_column_double(ls, 2), TRUE);
        char *vat_fmt = fmt_amount(sqlite3_column_double(ls, 3), FALSE);
        char *vat_s = g_strdup_printf("%s %%", vat_fmt);
        g_free(vat_fmt);
        char *ttc_s = fmt_amount(sqlite3_column_double(ls, 4), TRUE);
        char *desc = dup_column(ls, 0);
        double desc_h = text_height(ctx->cr, desc, FALSE, 9, 250);
        double line_h = desc_h + 12;

        ensure_space(ctx, line_h + 4);
        if (row_idx % 2 == 0) {
            fill_rect(ctx->cr, MARGIN, ctx->y - 2, CONTENT_W, line_h, 0.99, 0.99, 0.99);
        }
        set_rgb(ctx->cr, C_TEXT_R, C_TEXT_G, C_TEXT_B);
        draw_text_width(ctx, col_desc, 250, desc, FALSE, 9);
        draw_text_right(ctx, col_qty + 30, qty_s, FALSE, 9);
        draw_text_right(ctx, col_price + 40, price_s, FALSE, 9);
        draw_text_right(ctx, col_vat + 20, vat_s, FALSE, 9);
        draw_text_right(ctx, col_ttc, ttc_s, FALSE, 9);
        ctx->y += desc_h + 6;
        draw_hline(ctx->cr, MARGIN, PAGE_W - MARGIN, ctx->y, C_LINE_R, C_LINE_G, C_LINE_B);
        ctx->y += 8;
        row_idx++;
        g_free(desc);
        g_free(qty_s);
        g_free(price_s);
        g_free(vat_s);
        g_free(ttc_s);
    }
    sqlite3_finalize(ls);
    stroke_rect(ctx->cr, MARGIN, table_top, CONTENT_W, ctx->y - table_top - 4, 1.0, C_LINE_R, C_LINE_G, C_LINE_B);
}

static void draw_totals_box(PdfCtx *ctx, const char *total_ht_s, const char *total_tva_s, const char *total_ttc_s) {
    const double box_w = 250;
    const double box_x = PAGE_W - MARGIN - box_w;
    const double box_y = ctx->y;
    const double label_x = box_x + 14;
    const double value_x = PAGE_W - MARGIN - 14;
    const double row_h = 22;
    const double box_h = 14 + row_h * 2 + 30 + 14;

    fill_rect(ctx->cr, box_x, box_y, box_w, box_h, 1.0, 1.0, 1.0);
    stroke_rect(ctx->cr, box_x, box_y, box_w, box_h, 1.0, C_LINE_R, C_LINE_G, C_LINE_B);

    ctx->y = box_y + 14;
    set_rgb(ctx->cr, C_TEXT_R, C_TEXT_G, C_TEXT_B);
    draw_text(ctx, label_x, "Total HT", FALSE, 10);
    draw_text_right(ctx, value_x, total_ht_s, FALSE, 10);
    ctx->y += row_h;
    draw_text(ctx, label_x, "TVA", FALSE, 10);
    draw_text_right(ctx, value_x, total_tva_s, FALSE, 10);
    ctx->y += row_h;

    fill_rect(ctx->cr, box_x + 1, ctx->y, box_w - 2, 30, C_PANEL_BG_R, C_PANEL_BG_G, C_PANEL_BG_B);
    ctx->y += 8;
    set_rgb(ctx->cr, C_BRAND_R, C_BRAND_G, C_BRAND_B);
    draw_text(ctx, label_x, "Total TTC", TRUE, 11);
    draw_text_right(ctx, value_x, total_ttc_s, TRUE, 11);
    ctx->y = box_y + box_h + 10;
}

static gboolean write_invoice_pdf(const char *path, App *app, int invoice_id,
                                  const char *num, const char *date,
                                  double total_ht, double total_tva, double total_ttc,
                                  const char *cname, const char *caddr, const char *cemail, const char *cphone,
                                  const char *oname, const char *osiret, const char *oaddr,
                                  const char *oemail, const char *ophone, const char *olegal) {
    cairo_surface_t *surface = cairo_pdf_surface_create(path, PAGE_W, PAGE_H);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        set_export_error("Impossible de créer le fichier PDF %s", path);
        cairo_surface_destroy(surface);
        return FALSE;
    }

    cairo_t *cr = cairo_create(surface);
    PdfCtx ctx = { .cr = cr, .y = MARGIN };

    fill_rect(cr, 0, 0, PAGE_W, 8, C_BRAND_R, C_BRAND_G, C_BRAND_B);
    ctx.y = MARGIN;

    double header_bottom = draw_invoice_header(&ctx, num, date, oname, osiret, oaddr, oemail, ophone);
    ctx.y = header_bottom + 24;

    double client_bottom = draw_client_box(&ctx, cname, caddr, cemail, cphone);
    ctx.y = client_bottom + 28;

    draw_lines_table(&ctx, app, invoice_id);

    char *total_ht_s = fmt_amount(total_ht, TRUE);
    char *total_tva_s = fmt_amount(total_tva, TRUE);
    char *total_ttc_s = fmt_amount(total_ttc, TRUE);

    ensure_space(&ctx, 110);
    ctx.y += 12;
    draw_totals_box(&ctx, total_ht_s, total_tva_s, total_ttc_s);

    if (olegal && *olegal) {
        ensure_space(&ctx, 40);
        draw_hline(cr, MARGIN, PAGE_W - MARGIN, ctx.y, C_LINE_R, C_LINE_G, C_LINE_B);
        ctx.y += 14;
        set_rgb(cr, C_MUTED_R, C_MUTED_G, C_MUTED_B);
        double legal_h = draw_text_width(&ctx, MARGIN, CONTENT_W, olegal, FALSE, 8);
        ctx.y += legal_h;
    }

    cairo_show_page(cr);
    cairo_destroy(cr);
    cairo_status_t status = cairo_surface_status(surface);
    cairo_surface_destroy(surface);
    g_free(total_ht_s);
    g_free(total_tva_s);
    g_free(total_ttc_s);

    if (status != CAIRO_STATUS_SUCCESS) {
        set_export_error("Erreur lors de l'écriture du PDF %s", path);
        return FALSE;
    }
    return TRUE;
}

char *invoice_export_pdf(App *app, int invoice_id) {
    export_error[0] = '\0';
    sqlite3_stmt *st;
    const char *sql = "SELECT i.number,i.date,i.total_ht,i.total_tva,i.total_ttc,c.name,c.address,c.email,c.phone,co.name,co.siret,co.address,co.email,co.phone,co.legal FROM invoices i JOIN clients c ON c.id=i.client_id JOIN company co ON co.id=1 WHERE i.id=?";
    if (sqlite3_prepare_v2(app->db, sql, -1, &st, NULL) != SQLITE_OK) {
        set_export_error("Impossible de préparer la requête facture");
        return NULL;
    }
    sqlite3_bind_int(st, 1, invoice_id);
    if (sqlite3_step(st) != SQLITE_ROW) {
        sqlite3_finalize(st);
        set_export_error("Facture introuvable");
        return NULL;
    }

    char *num = dup_column(st, 0);
    char *date_raw = dup_column(st, 1);
    char *date = format_date_fr(date_raw);
    g_free(date_raw);
    double total_ht = sqlite3_column_double(st, 2);
    double total_tva = sqlite3_column_double(st, 3);
    double total_ttc = sqlite3_column_double(st, 4);
    char *cname = dup_column(st, 5);
    char *caddr = dup_column(st, 6);
    char *cemail = dup_column(st, 7);
    char *cphone = dup_column(st, 8);
    char *oname = dup_column(st, 9);
    char *osiret = dup_column(st, 10);
    char *oaddr = dup_column(st, 11);
    char *oemail = dup_column(st, 12);
    char *ophone = dup_column(st, 13);
    char *olegal = dup_column(st, 14);
    sqlite3_finalize(st);

    char *dir = g_build_filename(g_get_home_dir(), "FacturesVaisselle", NULL);
    if (g_mkdir_with_parents(dir, 0700) != 0) {
        set_export_error("Impossible de créer le dossier %s", dir);
        g_free(num); g_free(date);
        g_free(cname); g_free(caddr); g_free(cemail); g_free(cphone);
        g_free(oname); g_free(osiret); g_free(oaddr); g_free(oemail); g_free(ophone); g_free(olegal);
        g_free(dir);
        return NULL;
    }

    char *safe_name = g_strdup(num);
    for (char *p = safe_name; *p; p++) {
        if (*p == '/' || *p == ' ') {
            *p = '-';
        }
    }
    char *path = g_strdup_printf("%s/%s.pdf", dir, safe_name);
    gboolean ok = write_invoice_pdf(path, app, invoice_id, num, date, total_ht, total_tva, total_ttc,
                                    cname, caddr, cemail, cphone, oname, osiret, oaddr, oemail, ophone, olegal);
    g_free(dir);
    g_free(safe_name);
    g_free(num); g_free(date);
    g_free(cname); g_free(caddr); g_free(cemail); g_free(cphone);
    g_free(oname); g_free(osiret); g_free(oaddr); g_free(oemail); g_free(ophone); g_free(olegal);
    if (!ok) {
        g_free(path);
        return NULL;
    }
    return path;
}