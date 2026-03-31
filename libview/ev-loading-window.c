/* ev-loading-window.c
 *  this file is part of xreader, a mate document viewer
 *
 * Copyright (C) 2010 Carlos Garcia Campos <carlosgc@gnome.org>
 *
 * Xreader is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xreader is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "config.h"

#include <string.h>
#include <glib/gi18n.h>
#include "ev-loading-window.h"

enum {
	PROP_0,
	PROP_PARENT
};

struct _EvLoadingWindow {
	GtkWindow  base_instance;

	GtkWindow *parent;
	GtkWidget *spinner;

	gint       x;
	gint       y;
	gint       width;
	gint       height;
};

struct _EvLoadingWindowClass {
	GtkWindowClass base_class;
};

G_DEFINE_TYPE (EvLoadingWindow, ev_loading_window, GTK_TYPE_WINDOW)

static void
ev_loading_window_set_property (GObject      *object,
				guint         prop_id,
				const GValue *value,
				GParamSpec   *pspec)
{
	EvLoadingWindow *window = EV_LOADING_WINDOW (object);

	switch (prop_id) {
	case PROP_PARENT:
		window->parent = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
ev_loading_window_init (EvLoadingWindow *window)
{
	GtkWindow   *gtk_window = GTK_WINDOW (window);
	GtkWidget   *widget = GTK_WIDGET (window);
	GtkWidget   *hbox;
	GtkWidget   *label;
	GtkStyleContext *context;
	GdkRGBA    fg, bg;
	const gchar *loading_text = _("Loading…");
	const gchar *fg_color_name = "info_fg_color";
	const gchar *bg_color_name = "info_bg_color";

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);

	window->spinner = gtk_spinner_new ();
	gtk_box_append (GTK_BOX (hbox), window->spinner);
	gtk_widget_show (window->spinner);

	label = gtk_label_new (loading_text);
	gtk_box_append (GTK_BOX (hbox), label);
	gtk_widget_show (label);

	gtk_window_set_child (GTK_WINDOW (window), hbox);
	gtk_widget_show (hbox);

	

	

	
	
	gtk_window_set_decorated (gtk_window, FALSE);
	gtk_window_set_resizable (gtk_window, FALSE);

	context = gtk_widget_get_style_context (widget);
	if (!gtk_style_context_lookup_color (context, fg_color_name, &fg) ||
	    !gtk_style_context_lookup_color (context, bg_color_name, &bg)) {
		fg.red = 0.7;
		fg.green = 0.67;
		fg.blue = 0.63;
		fg.alpha = 1.0;

		bg.red = 0.99;
		bg.green = 0.99;
		bg.blue = 0.71;
		bg.alpha = 1.0;
	}

        gchar *css = g_strdup_printf ("* { background-color: rgba(%d,%d,%d,%f); color: rgba(%d,%d,%d,%f); }",
                                      (int)(bg.red * 255), (int)(bg.green * 255), (int)(bg.blue * 255), bg.alpha,
                                      (int)(fg.red * 255), (int)(fg.green * 255), (int)(fg.blue * 255), fg.alpha);
        GtkCssProvider *provider = gtk_css_provider_new ();
        gtk_css_provider_load_from_string (provider, css);
        gtk_style_context_add_provider (gtk_widget_get_style_context (widget), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_free (css);
        g_object_unref (provider);
}

static GObject *
ev_loading_window_constructor (GType                  type,
			       guint                  n_construct_properties,
			       GObjectConstructParam *construct_params)
{
	GObject         *object;
	EvLoadingWindow *window;
	GtkWindow       *gtk_window;

	object = G_OBJECT_CLASS (ev_loading_window_parent_class)->constructor (type,
									       n_construct_properties,
									       construct_params);
	window = EV_LOADING_WINDOW (object);
	gtk_window = GTK_WINDOW (window);

	gtk_window_set_transient_for (gtk_window, window->parent);
	gtk_window_set_destroy_with_parent (gtk_window, TRUE);

	return object;
}



static void
ev_loading_window_hide (GtkWidget *widget)
{
	EvLoadingWindow *window = EV_LOADING_WINDOW (widget);

	window->x = window->y = 0;

	gtk_spinner_stop (GTK_SPINNER (window->spinner));

	GTK_WIDGET_CLASS (ev_loading_window_parent_class)->hide (widget);
}

static void
ev_loading_window_show (GtkWidget *widget)
{
	EvLoadingWindow *window = EV_LOADING_WINDOW (widget);

	gtk_spinner_start (GTK_SPINNER (window->spinner));

	GTK_WIDGET_CLASS (ev_loading_window_parent_class)->show (widget);
}

static void
ev_loading_window_class_init (EvLoadingWindowClass *klass)
{
	GObjectClass   *g_object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *gtk_widget_class = GTK_WIDGET_CLASS (klass);

	g_object_class->constructor = ev_loading_window_constructor;
	g_object_class->set_property = ev_loading_window_set_property;

	
	gtk_widget_class->show = ev_loading_window_show;
	gtk_widget_class->hide = ev_loading_window_hide;

	g_object_class_install_property (g_object_class,
					 PROP_PARENT,
					 g_param_spec_object ("parent",
							      "Parent",
							      "The parent window",
							      GTK_TYPE_WINDOW,
							      G_PARAM_WRITABLE |
							      G_PARAM_CONSTRUCT_ONLY));
}

/* Public methods */
GtkWidget *
ev_loading_window_new (GtkWindow *parent)
{
	GtkWidget *window;

	g_return_val_if_fail (GTK_IS_WINDOW (parent), NULL);

	window = g_object_new (EV_TYPE_LOADING_WINDOW,
                               
			       "parent", parent,
			       NULL);
	return window;
}

void
ev_loading_window_get_size (EvLoadingWindow *window,
			    gint            *width,
			    gint            *height)
{
	if (width) *width = window->width;
	if (height) *height = window->height;
}

void
ev_loading_window_move (EvLoadingWindow *window,
			gint             x,
			gint             y)
{
	if (x == window->x && y == window->y)
		return;

	window->x = x;
	window->y = y;
	/* gtk_window_move is removed in GTK4. Positioning handled by WM or overlay */
}
