/*
 *  Copyright (C) 2009 Juanjo Marín <juanj.marin@juntadeandalucia.es>
 *  Copyright (c) 2007 Carlos Garcia Campos <carlosgc@gnome.org>
 *  Copyright (C) 2000-2003 Marco Pesenti Gritti
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <config.h>

#include <string.h>
#include <math.h>

#include <gtk/gtk.h>

#include "ev-document-misc.h"

/**
 * Returns a new GdkPixbuf that is suitable for placing in the thumbnail view.
 * If source_pixbuf is not NULL, then it will fill the return pixbuf with the
 * contents of source_pixbuf.
 */
static GdkPixbuf *
create_thumbnail_frame (int        width,
			int        height,
			GdkPixbuf *source_pixbuf,
			gboolean   fill_bg)
{
	GdkPixbuf *retval;
	int width_r, height_r;

	if (source_pixbuf)
		g_return_val_if_fail (GDK_IS_PIXBUF (source_pixbuf), NULL);

	width_r = gdk_pixbuf_get_width (source_pixbuf);
	height_r = gdk_pixbuf_get_height (source_pixbuf);

	/* make sure no one is passing us garbage */
	g_return_val_if_fail (width_r >= 0 && height_r >= 0, NULL);

	retval = gdk_pixbuf_new (GDK_COLORSPACE_RGB,
				 TRUE, 8,
				 width_r,
				 height_r);

	gdk_pixbuf_fill (retval, 0x000000ff);

	/* copy the source pixbuf */
	gdk_pixbuf_copy_area (source_pixbuf, 0, 0,
				  width_r,
				  height_r,
				  retval,
				  0, 0);

	return retval;
}

GdkPixbuf *
ev_document_misc_get_thumbnail_frame (int        width,
				      int        height,
				      GdkPixbuf *source_pixbuf)
{
	return create_thumbnail_frame (width, height, source_pixbuf, TRUE);
}

GdkPixbuf *
ev_document_misc_get_loading_thumbnail (int      width,
					int      height,
					gboolean inverted_colors)
{
	return create_thumbnail_frame (width, height, NULL, !inverted_colors);
}

/* GTK4: Thumbnail frame rendering using GtkSnapshot-compatible approach.
 * We use cairo to render the frame since we need a GdkPixbuf result,
 * but we no longer use GtkStyleContext render functions (deprecated path).
 * Instead we use a simple border rendering approach.
 */
static GdkPixbuf *
ev_document_misc_render_thumbnail_frame (GtkWidget *widget,
                                         int        width,
                                         int        height,
                                         gboolean   inverted_colors,
                                         GdkPixbuf *source_pixbuf)
{
        int              width_r, height_r;
        int              width_f, height_f;
        cairo_surface_t *surface;
        cairo_t         *cr;
        GtkBorder        border = {1, 1, 1, 1};
        GdkPixbuf       *retval;

        if (source_pixbuf) {
                g_return_val_if_fail (GDK_IS_PIXBUF (source_pixbuf), NULL);

                width_r = gdk_pixbuf_get_width (source_pixbuf);
                height_r = gdk_pixbuf_get_height (source_pixbuf);
        } else {
                width_r = width;
                height_r = height;
        }

        width_f = width_r + border.left + border.right;
        height_f = height_r + border.top + border.bottom;

        surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                              width_f, height_f);
        cr = cairo_create (surface);

        /* Draw background */
        if (inverted_colors)
                cairo_set_source_rgb (cr, 0.0, 0.0, 0.0);
        else
                cairo_set_source_rgb (cr, 1.0, 1.0, 1.0);
        cairo_rectangle (cr, 0, 0, width_f, height_f);
        cairo_fill (cr);

        /* Draw source pixbuf if provided */
        if (source_pixbuf) {
                gdk_cairo_set_source_pixbuf (cr, source_pixbuf, border.left, border.top);
                cairo_paint (cr);
        }

        /* Draw border frame */
        cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.5);
        cairo_set_line_width (cr, 1.0);
        cairo_rectangle (cr, 0.5, 0.5, width_f - 1.0, height_f - 1.0);
        cairo_stroke (cr);

        cairo_destroy (cr);

        retval = gdk_pixbuf_get_from_surface (surface, 0, 0, width_f, height_f);
        cairo_surface_destroy (surface);

        return retval;
}

GdkPixbuf *
ev_document_misc_render_loading_thumbnail (GtkWidget *widget,
                                           int        width,
                                           int        height,
                                           gboolean   inverted_colors)
{
        return ev_document_misc_render_thumbnail_frame (widget, width, height, inverted_colors, NULL);
}

GdkPixbuf *
ev_document_misc_render_thumbnail_with_frame (GtkWidget *widget,
                                              GdkPixbuf *source_pixbuf)
{
        return ev_document_misc_render_thumbnail_frame (widget, -1, -1, FALSE, source_pixbuf);
}

void
ev_document_misc_get_page_border_size (gint       page_width,
				       gint       page_height,
				       GtkBorder *border)
{
	g_assert (border);

	border->left = 1;
	border->top = 1;
	if (page_width < 100) {
		border->right = 2;
		border->bottom = 2;
	} else if (page_width < 500) {
		border->right = 3;
		border->bottom = 3;
	} else {
		border->right = 4;
		border->bottom = 4;
	}
}


void
ev_document_misc_paint_one_page (cairo_t      *cr,
				 GtkWidget    *widget,
				 GdkRectangle *area,
				 GtkBorder    *border,
				 gboolean      highlight,
				 gboolean      inverted_colors)
{
	GdkRGBA fg, shade_bg;

	/* GTK4: Use widget color from CSS. Default to reasonable colors. */
	GtkStyleContext *context = gtk_widget_get_style_context (widget);

	/* Get colors from style context - GTK4 compatible API */
	gtk_style_context_get_color (context, &fg);
	shade_bg = fg;
	shade_bg.alpha *= 0.5;

	if (highlight) {
		cairo_set_source_rgba (cr, fg.red, fg.green, fg.blue, fg.alpha);
	} else {
		cairo_set_source_rgba (cr, shade_bg.red, shade_bg.green, shade_bg.blue, shade_bg.alpha);
	}

	cairo_rectangle (cr,
			 area->x,
			 area->y,
			 area->width - border->right + border->left,
			 area->height - border->bottom + border->top);
	cairo_rectangle (cr,
			 area->x + area->width - border->right,
			 area->y + border->right - border->left,
			 border->right,
			 area->height - border->right + border->left);
	cairo_rectangle (cr,
			 area->x + border->bottom - border->top,
			 area->y + area->height - border->bottom,
			 area->width - border->bottom + border->top,
			 border->bottom);
	cairo_fill (cr);

	if (inverted_colors)
		cairo_set_source_rgb (cr, 0, 0, 0);
	else
		cairo_set_source_rgb (cr, 1, 1, 1);
	cairo_rectangle (cr,
			 area->x + border->left,
			 area->y + border->top,
			 area->width - (border->left + border->right),
			 area->height - (border->top + border->bottom));
	cairo_fill (cr);
}

cairo_surface_t *
ev_document_misc_surface_from_pixbuf (GdkPixbuf *pixbuf)
{
	cairo_surface_t *surface;
	cairo_t         *cr;

	g_return_val_if_fail (GDK_IS_PIXBUF (pixbuf), NULL);

	surface = cairo_image_surface_create (gdk_pixbuf_get_has_alpha (pixbuf) ?
					      CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_RGB24,
					      gdk_pixbuf_get_width (pixbuf),
					      gdk_pixbuf_get_height (pixbuf));
	cr = cairo_create (surface);
	gdk_cairo_set_source_pixbuf (cr, pixbuf, 0, 0);
	cairo_paint (cr);
	cairo_destroy (cr);

	return surface;
}

GdkPixbuf *
ev_document_misc_pixbuf_from_surface (cairo_surface_t *surface)
{
	g_return_val_if_fail (surface, NULL);

	GdkPixbuf       *pixbuf;
	cairo_surface_t *image;
	cairo_t         *cr;
	gboolean         has_alpha;
	gint             width, height;
	cairo_format_t   surface_format;
	gint             pixbuf_n_channels;
	gint             pixbuf_rowstride;
	guchar          *pixbuf_pixels;
	gint             x, y;

	width = cairo_image_surface_get_width (surface);
	height = cairo_image_surface_get_height (surface);

	surface_format = cairo_image_surface_get_format (surface);
	has_alpha = (surface_format == CAIRO_FORMAT_ARGB32);

	pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB,
				 TRUE, 8,
				 width, height);
	pixbuf_n_channels = gdk_pixbuf_get_n_channels (pixbuf);
	pixbuf_rowstride = gdk_pixbuf_get_rowstride (pixbuf);
	pixbuf_pixels = gdk_pixbuf_get_pixels (pixbuf);

	image = cairo_image_surface_create_for_data (pixbuf_pixels,
						     surface_format,
						     width, height,
						     pixbuf_rowstride);
	cr = cairo_create (image);
	cairo_set_source_surface (cr, surface, 0, 0);

	if (has_alpha)
		cairo_mask_surface (cr, surface, 0, 0);
	else
		cairo_paint (cr);

	cairo_destroy (cr);
	cairo_surface_destroy (image);

	for (y = 0; y < height; y++) {
		guchar *p = pixbuf_pixels + y * pixbuf_rowstride;

		for (x = 0; x < width; x++) {
			guchar tmp;

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
			tmp = p[0];
			p[0] = p[2];
			p[2] = tmp;
			p[3] = (has_alpha) ? p[3] : 0xff;
#else
			tmp = p[0];
			p[0] = p[1];
			p[1] = p[2];
			p[2] = p[3];
			p[3] = (has_alpha) ? tmp : 0xff;
#endif
			p += pixbuf_n_channels;
		}
	}

	return pixbuf;
}

cairo_surface_t *
ev_document_misc_surface_rotate_and_scale (cairo_surface_t *surface,
					   gint             dest_width,
					   gint             dest_height,
					   gint             dest_rotation)
{
	cairo_surface_t *new_surface;
	cairo_t         *cr;
	gint             width, height;
	gint             new_width = dest_width;
	gint             new_height = dest_height;

	width = cairo_image_surface_get_width (surface);
	height = cairo_image_surface_get_height (surface);

	if (dest_width == width &&
	    dest_height == height &&
	    dest_rotation == 0) {
		return cairo_surface_reference (surface);
	}

	if (dest_rotation == 90 || dest_rotation == 270) {
		new_width = dest_height;
		new_height = dest_width;
	}

	new_surface = cairo_surface_create_similar (surface,
						    cairo_surface_get_content (surface),
						    new_width, new_height);

	cr = cairo_create (new_surface);
	switch (dest_rotation) {
	        case 90:
			cairo_translate (cr, new_width, 0);
			break;
	        case 180:
			cairo_translate (cr, new_width, new_height);
			break;
	        case 270:
			cairo_translate (cr, 0, new_height);
			break;
	        default:
			cairo_translate (cr, 0, 0);
	}
	cairo_rotate (cr, dest_rotation * G_PI / 180.0);

	if (dest_width != width || dest_height != height) {
		cairo_pattern_set_filter (cairo_get_source (cr), CAIRO_FILTER_BILINEAR);
		cairo_scale (cr,
			     (gdouble)dest_width / width,
			     (gdouble)dest_height / height);
	}

	cairo_set_source_surface (cr, surface, 0, 0);
	cairo_paint (cr);
	cairo_destroy (cr);

	return new_surface;
}

void
ev_document_misc_invert_surface (cairo_surface_t *surface) {
	cairo_t *cr;

	cr = cairo_create (surface);

	/* white + DIFFERENCE -> invert */
	cairo_set_operator (cr, CAIRO_OPERATOR_DIFFERENCE);
	cairo_set_source_rgb (cr, 1., 1., 1.);
	cairo_paint(cr);
	cairo_destroy (cr);
}

void
ev_document_misc_invert_pixbuf (GdkPixbuf *pixbuf)
{
	guchar *data, *p;
	guint   width, height, x, y, rowstride, n_channels;

	n_channels = gdk_pixbuf_get_n_channels (pixbuf);
	g_assert (gdk_pixbuf_get_colorspace (pixbuf) == GDK_COLORSPACE_RGB);
	g_assert (gdk_pixbuf_get_bits_per_sample (pixbuf) == 8);

	/* First grab a pointer to the raw pixel data. */
	data = gdk_pixbuf_get_pixels (pixbuf);

	/* Find the number of bytes per row (could be padded). */
	rowstride = gdk_pixbuf_get_rowstride (pixbuf);

	width = gdk_pixbuf_get_width (pixbuf);
	height = gdk_pixbuf_get_height (pixbuf);
	for (x = 0; x < width; x++) {
		for (y = 0; y < height; y++) {
			/* Calculate pixel's offset into the data array. */
			p = data + x * n_channels + y * rowstride;
			/* Change the RGB values*/
			p[0] = 255 - p[0];
			p[1] = 255 - p[1];
			p[2] = 255 - p[2];
		}
	}
}

/* GTK4: GdkScreen removed. Use GdkDisplay + GdkMonitor directly. */
gdouble
ev_document_misc_get_screen_dpi (GdkDisplay *display, GdkMonitor *monitor)
{
    GdkRectangle monitorRect;
    int mmX, mmY;
    gdouble dp, di;

    /*diagonal in pixels*/
    gdk_monitor_get_geometry(monitor, &monitorRect);

    /*diagonal in inches*/
    mmX = gdk_monitor_get_width_mm(monitor);
    mmY = gdk_monitor_get_height_mm(monitor);

    /* Fallback in cases where devices report their aspect ratio */
    if ((mmX == 160 && (mmY == 90 || mmY == 100)) ||
        (mmX == 16  && (mmY == 9  || mmY == 10))  ||
        mmX == 0 || mmY == 0 ||
        monitorRect.width == 0 || monitorRect.height == 0) {
        return DEFAULT_DPI;
    }

    dp = hypot (monitorRect.width, monitorRect.height);

    di = hypot (mmX, mmY) / 25.4;
    di /= gdk_monitor_get_scale_factor(monitor);

    return (dp / di);
}


gdouble
ev_document_misc_get_screen_dpi_at_window(GtkWindow *window)
{
	GdkDisplay *display;
	GdkMonitor *monitor;
	GdkSurface *surface;

	if (!window)
		return DEFAULT_DPI;

	surface = gtk_native_get_surface (GTK_NATIVE (window));
	if (!surface)
		return DEFAULT_DPI;

	display = gtk_widget_get_display (GTK_WIDGET (window));
	monitor = gdk_display_get_monitor_at_surface (display, surface);
	if (!monitor)
		return DEFAULT_DPI;

	return ev_document_misc_get_screen_dpi (display, monitor) / gdk_monitor_get_scale_factor (monitor);
}

void
ev_document_misc_get_pointer_position (GtkWidget *widget,
                                       gint      *x,
                                       gint      *y)
{
        /* GTK4: Direct pointer position querying is not recommended.
         * Event controllers should be used instead. This function
         * provides a compatibility fallback using GdkSurface.
         * For accurate tracking, callers should migrate to
         * GtkEventControllerMotion.
         */
        if (x)
                *x = -1;
        if (y)
                *y = -1;

        if (!gtk_widget_get_realized (widget))
                return;

        GdkSurface *surface = gtk_native_get_surface (gtk_widget_get_native (widget));
        if (!surface)
                return;

        GdkDisplay *display = gtk_widget_get_display (widget);
        GdkSeat *seat = gdk_display_get_default_seat (display);
        GdkDevice *pointer = gdk_seat_get_pointer (seat);

        double dx, dy;
        gdk_surface_get_device_position (surface, pointer, &dx, &dy, NULL);

        /* GTK4: widgets always have their own coordinate space,
         * no need to adjust for allocation like in GTK3.
         */
        graphene_point_t widget_point;
        if (gtk_widget_compute_point (GTK_WIDGET (gtk_widget_get_native (widget)),
                                      widget, &GRAPHENE_POINT_INIT (dx, dy),
                                      &widget_point)) {
                if (x)
                        *x = (gint) widget_point.x;
                if (y)
                        *y = (gint) widget_point.y;
        }
}
