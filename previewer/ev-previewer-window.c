/* ev-previewer-window.c:
 *  this file is part of xreader, a generic document viewer
 *
 * Copyright (C) 2009 Carlos Garcia Campos <carlosgc@gnome.org>
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

#include <config.h>

#include <glib/gi18n.h>
#include <xreader-view.h>
#include "ev-document-model.h"

#include "ev-previewer-window.h"

struct _EvPreviewerWindow {
	GtkWindow         base_instance;

	EvDocumentModel  *model;
	EvDocument       *document;

	GSimpleActionGroup *action_group;

	GtkWidget        *swindow;
	EvView           *view;
	gdouble           dpi;

	/* Printing */
	GtkPrintSettings *print_settings;
	GtkPageSetup     *print_page_setup;
	gchar            *print_job_title;
	gchar            *source_file;
};

struct _EvPreviewerWindowClass {
	GtkWindowClass base_class;
};

enum {
	PROP_0,
	PROP_MODEL
};

#define MIN_SCALE 0.05409
#define MAX_SCALE 4.0

G_DEFINE_TYPE (EvPreviewerWindow, ev_previewer_window, GTK_TYPE_WINDOW)

static gdouble
get_screen_dpi (EvPreviewerWindow *window)
{
	return ev_document_misc_get_screen_dpi_at_window (GTK_WINDOW(window));
}

static void
ev_previewer_window_close (GSimpleAction     *action,
			   GVariant          *parameter,
			   gpointer          user_data)
{
	EvPreviewerWindow *window = EV_PREVIEWER_WINDOW (user_data);
	gtk_window_destroy (GTK_WINDOW (window));
}

static void
ev_previewer_window_previous_page (GSimpleAction     *action,
				   GVariant          *parameter,
				   gpointer          user_data)
{
	EvPreviewerWindow *window = EV_PREVIEWER_WINDOW (user_data);
	ev_view_previous_page (window->view);
}

static void
ev_previewer_window_next_page (GSimpleAction     *action,
			       GVariant          *parameter,
			       gpointer          user_data)
{
	EvPreviewerWindow *window = EV_PREVIEWER_WINDOW (user_data);
	ev_view_next_page (window->view);
}

static void
ev_previewer_window_zoom_in (GSimpleAction     *action,
			     GVariant          *parameter,
			     gpointer          user_data)
{
	EvPreviewerWindow *window = EV_PREVIEWER_WINDOW (user_data);
	ev_document_model_set_sizing_mode (window->model, EV_SIZING_FREE);
	ev_view_zoom_in (window->view);
}

static void
ev_previewer_window_zoom_out (GSimpleAction     *action,
			      GVariant          *parameter,
			      gpointer          user_data)
{
	EvPreviewerWindow *window = EV_PREVIEWER_WINDOW (user_data);
	ev_document_model_set_sizing_mode (window->model, EV_SIZING_FREE);
	ev_view_zoom_out (window->view);
}

static void
ev_previewer_window_zoom_reset (GSimpleAction     *action,
			      GVariant          *parameter,
			      gpointer          user_data)
{
	EvPreviewerWindow *window = EV_PREVIEWER_WINDOW (user_data);
	ev_document_model_set_sizing_mode (window->model, EV_SIZING_FREE);
  ev_document_model_set_scale (window->model, get_screen_dpi (window) / 72.0);
}

static void
ev_previewer_window_zoom_best_fit (GSimpleAction     *action,
				   GVariant          *state,
				   gpointer          user_data)
{
	EvPreviewerWindow *window = EV_PREVIEWER_WINDOW (user_data);
	gboolean active = g_variant_get_boolean (state);
	ev_document_model_set_sizing_mode (window->model,
					   active ? EV_SIZING_BEST_FIT : EV_SIZING_FREE);
	g_simple_action_set_state (action, state);
}

static void
ev_previewer_window_zoom_page_width (GSimpleAction     *action,
				     GVariant          *state,
				     gpointer          user_data)
{
	EvPreviewerWindow *window = EV_PREVIEWER_WINDOW (user_data);
	gboolean active = g_variant_get_boolean (state);
	ev_document_model_set_sizing_mode (window->model,
					   active ? EV_SIZING_FIT_WIDTH : EV_SIZING_FREE);
	g_simple_action_set_state (action, state);
}

static void
ev_previewer_window_print (GSimpleAction     *action,
			   GVariant          *parameter,
			   gpointer          user_data)
{
	/* EvPreviewerWindow *window = EV_PREVIEWER_WINDOW (user_data); */
	/* FIXME: Implement printing using GtkPrintOperation for GTK4.
	 * GtkPrintJob and GtkPrinter enumeration are deprecated/removed.
	 */
}

static const GActionEntry action_entries[] = {
	{ "FileCloseWindow", ev_previewer_window_close },
	{ "GoPreviousPage", ev_previewer_window_previous_page },
	{ "GoNextPage", ev_previewer_window_next_page },
	{ "ViewZoomIn", ev_previewer_window_zoom_in },
	{ "ViewZoomOut", ev_previewer_window_zoom_out },
	{ "ViewZoomReset", ev_previewer_window_zoom_reset },
	{ "PreviewPrint", ev_previewer_window_print },
	{ "ViewBest_fit", NULL, NULL, "false", ev_previewer_window_zoom_best_fit },
	{ "ViewPageWidth", NULL, NULL, "false", ev_previewer_window_zoom_page_width }
};

#if 0
// These will be handled by GtkShortcut or manual key press
#endif

// view_focus_changed removed


static void
view_sizing_mode_changed (EvDocumentModel   *model,
			  GParamSpec        *pspec,
			  EvPreviewerWindow *window)
{
	EvSizingMode sizing_mode = ev_document_model_get_sizing_mode (model);
	GAction     *action;

	action = g_action_map_lookup_action (G_ACTION_MAP (window), "ViewBestFit");
	g_simple_action_set_state (G_SIMPLE_ACTION (action),
				   g_variant_new_boolean (sizing_mode == EV_SIZING_BEST_FIT));

	action = g_action_map_lookup_action (G_ACTION_MAP (window), "ViewPageWidth");
	g_simple_action_set_state (G_SIMPLE_ACTION (action),
				   g_variant_new_boolean (sizing_mode == EV_SIZING_FIT_WIDTH));
}

static void
ev_previewer_window_set_document (EvPreviewerWindow *window,
				  GParamSpec        *pspec,
				  EvDocumentModel   *model)
{
	EvDocument *document = ev_document_model_get_document (model);

	window->document = g_object_ref (document);

	g_signal_connect (model, "notify::sizing-mode",
			  G_CALLBACK (view_sizing_mode_changed),
			  window);
	/* g_simple_action_group_set_enabled removed in GTK4 */
}

static void
ev_previewer_window_connect_action_accelerators (EvPreviewerWindow *window)
{
    // TODO: Implement GTK4 shortcuts
}

static void
ev_previewer_window_dispose (GObject *object)
{
	EvPreviewerWindow *window = EV_PREVIEWER_WINDOW (object);

	if (window->model) {
		g_object_unref (window->model);
		window->model = NULL;
	}

	if (window->document) {
		g_object_unref (window->document);
		window->document = NULL;
	}

	if (window->action_group) {
		g_object_unref (window->action_group);
		window->action_group = NULL;
	}

	if (window->print_settings) {
		g_object_unref (window->print_settings);
		window->print_settings = NULL;
	}

	if (window->print_page_setup) {
		g_object_unref (window->print_page_setup);
		window->print_page_setup = NULL;
	}

	if (window->print_job_title) {
		g_free (window->print_job_title);
		window->print_job_title = NULL;
	}

	if (window->source_file) {
		g_free (window->source_file);
		window->source_file = NULL;
	}

	G_OBJECT_CLASS (ev_previewer_window_parent_class)->dispose (object);
}

static void
ev_previewer_window_init (EvPreviewerWindow *window)
{
	gtk_window_set_default_size (GTK_WINDOW (window), 600, 600);
}

static void
ev_previewer_window_set_property (GObject      *object,
				  guint         prop_id,
				  const GValue *value,
				  GParamSpec   *pspec)
{
	EvPreviewerWindow *window = EV_PREVIEWER_WINDOW (object);

	switch (prop_id) {
	case PROP_MODEL:
		window->model = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

#if 0
static void
_gtk_css_provider_load_from_resource (GtkCssProvider *provider,
                                      const char     *resource_path)
{
	GBytes *data;

	data = g_resources_lookup_data (resource_path, 0, NULL);
	if (!data)
			return;

	gtk_css_provider_load_from_data (provider,
									  g_bytes_get_data (data, NULL),
									  g_bytes_get_size (data));
	g_bytes_unref (data);
}
#endif

static GObject *
ev_previewer_window_constructor (GType                  type,
				 guint                  n_construct_properties,
				 GObjectConstructParam *construct_params)
{
	GObject           *object;
	EvPreviewerWindow *window;
	GtkWidget         *vbox;
	GtkWidget         *toolbar;
	GtkWidget         *button;
	gdouble            dpi;

	object = G_OBJECT_CLASS (ev_previewer_window_parent_class)->constructor (type,
										 n_construct_properties,
										 construct_params);
	window = EV_PREVIEWER_WINDOW (object);
	gtk_widget_set_visible (GTK_WIDGET (window), FALSE);

	dpi = get_screen_dpi (window);
	ev_document_model_set_min_scale (window->model, MIN_SCALE * dpi / 72.0);
	ev_document_model_set_max_scale (window->model, MAX_SCALE * dpi / 72.0);
	ev_document_model_set_sizing_mode (window->model, EV_SIZING_FIT_WIDTH);
	g_signal_connect_swapped (window->model, "notify::document",
				  G_CALLBACK (ev_previewer_window_set_document),
				  window);

	window->action_group = g_simple_action_group_new ();
	g_action_map_add_action_entries (G_ACTION_MAP (window->action_group),
				    action_entries,
				    G_N_ELEMENTS (action_entries),
				    window);
	gtk_widget_insert_action_group (GTK_WIDGET (window), "prev", G_ACTION_GROUP (window->action_group));
	/* g_simple_action_group_set_enabled removed in GTK4 */

	ev_previewer_window_connect_action_accelerators (window);

	view_sizing_mode_changed (window->model, NULL, window);

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_window_set_child (GTK_WINDOW (window), vbox);

	toolbar = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_add_css_class (toolbar, "toolbar");
	gtk_box_append (GTK_BOX (vbox), toolbar);

	/* Previous Page */
	button = gtk_button_new_from_icon_name ("go-up-symbolic");
	gtk_actionable_set_action_name (GTK_ACTIONABLE (button), "prev.GoPreviousPage");
	gtk_box_append (GTK_BOX (toolbar), button);

	/* Next Page */
	button = gtk_button_new_from_icon_name ("go-down-symbolic");
	gtk_actionable_set_action_name (GTK_ACTIONABLE (button), "prev.GoNextPage");
	gtk_box_append (GTK_BOX (toolbar), button);

	gtk_box_append (GTK_BOX (toolbar), gtk_separator_new (GTK_ORIENTATION_VERTICAL));

	/* Zoom Out */
	button = gtk_button_new_from_icon_name ("zoom-out-symbolic");
	gtk_actionable_set_action_name (GTK_ACTIONABLE (button), "prev.ViewZoomOut");
	gtk_box_append (GTK_BOX (toolbar), button);

	/* Zoom In */
	button = gtk_button_new_from_icon_name ("zoom-in-symbolic");
	gtk_actionable_set_action_name (GTK_ACTIONABLE (button), "prev.ViewZoomIn");
	gtk_box_append (GTK_BOX (toolbar), button);

	gtk_box_append (GTK_BOX (toolbar), gtk_separator_new (GTK_ORIENTATION_VERTICAL));

	/* Print */
	button = gtk_button_new_from_icon_name ("printer-symbolic");
	gtk_actionable_set_action_name (GTK_ACTIONABLE (button), "prev.PreviewPrint");
	gtk_box_append (GTK_BOX (toolbar), button);

	window->swindow = gtk_scrolled_window_new ();

	window->view = EV_VIEW (ev_view_new ());
	ev_view_set_model (window->view, window->model);
	ev_document_model_set_continuous (window->model, FALSE);
	ev_view_set_loading (window->view, TRUE);

	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (window->swindow), GTK_WIDGET (window->view));
	gtk_box_append (GTK_BOX (vbox), window->swindow);
	gtk_widget_set_vexpand (window->swindow, TRUE);

	return object;
}


static void
ev_previewer_window_class_init (EvPreviewerWindowClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

	gobject_class->constructor = ev_previewer_window_constructor;
	gobject_class->set_property = ev_previewer_window_set_property;
	gobject_class->dispose = ev_previewer_window_dispose;

	g_object_class_install_property (gobject_class,
					 PROP_MODEL,
					 g_param_spec_object ("model",
							      "Model",
							      "The document model",
							      EV_TYPE_DOCUMENT_MODEL,
							      G_PARAM_WRITABLE |
							      G_PARAM_CONSTRUCT_ONLY));
}

/* Public methods */
GtkWidget *
ev_previewer_window_new (EvDocumentModel *model)
{
	return GTK_WIDGET (g_object_new (EV_TYPE_PREVIEWER_WINDOW, "model", model, NULL));
}

void
ev_previewer_window_set_print_settings (EvPreviewerWindow *window,
					const gchar       *print_settings)
{
	if (window->print_settings)
		g_object_unref (window->print_settings);
	if (window->print_page_setup)
		g_object_unref (window->print_page_setup);
	if (window->print_job_title)
		g_free (window->print_job_title);

	if (print_settings && g_file_test (print_settings, G_FILE_TEST_IS_REGULAR)) {
		GKeyFile *key_file;
		GError   *error = NULL;

		key_file = g_key_file_new ();
		g_key_file_load_from_file (key_file,
					   print_settings,
					   G_KEY_FILE_KEEP_COMMENTS |
					   G_KEY_FILE_KEEP_TRANSLATIONS,
					   &error);
		if (!error) {
			GtkPrintSettings *psettings;
			GtkPageSetup     *psetup;
			gchar            *job_name;

			psettings = gtk_print_settings_new_from_key_file (key_file,
									  "Print Settings",
									  NULL);
			window->print_settings = psettings ? psettings : gtk_print_settings_new ();

			psetup = gtk_page_setup_new_from_key_file (key_file,
								   "Page Setup",
								   NULL);
			window->print_page_setup = psetup ? psetup : gtk_page_setup_new ();

			job_name = g_key_file_get_string (key_file,
							  "Print Job", "title",
							  NULL);
			if (job_name) {
				window->print_job_title = job_name;
				gtk_window_set_title (GTK_WINDOW (window), job_name);
			}
		} else {
			window->print_settings = gtk_print_settings_new ();
			window->print_page_setup = gtk_page_setup_new ();
			g_error_free (error);
		}

		g_key_file_free (key_file);
	} else {
		window->print_settings = gtk_print_settings_new ();
		window->print_page_setup = gtk_page_setup_new ();
	}
}

void
ev_previewer_window_set_source_file (EvPreviewerWindow *window,
				     const gchar       *source_file)
{
	if (window->source_file)
		g_free (window->source_file);
	window->source_file = g_strdup (source_file);
}
