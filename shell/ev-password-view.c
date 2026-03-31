/* this file is part of xreader, a mate document viewer
 *
 *  Copyright (C) 2008 Carlos Garcia Campos <carlosgc@gnome.org>
 *  Copyright (C) 2005 Red Hat, Inc
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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gio/gio.h>

#include "ev-keyring.h"
#include "ev-password-view.h"

enum {
	UNLOCK,
	LAST_SIGNAL
};
struct _EvPasswordViewPrivate {
	GtkWindow    *parent_window;
	GtkWidget    *label;
	GtkWidget    *password_entry;

	gchar        *password;
	GPasswordSave password_save;

	GFile        *uri_file;
};

static guint password_view_signals [LAST_SIGNAL] = { 0 };


G_DEFINE_TYPE_WITH_PRIVATE (EvPasswordView, ev_password_view, GTK_TYPE_VIEWPORT)

static void
ev_password_view_finalize (GObject *object)
{
	EvPasswordView *password_view = EV_PASSWORD_VIEW (object);

	if (password_view->priv->password) {
		g_free (password_view->priv->password);
		password_view->priv->password = NULL;
	}

	password_view->priv->parent_window = NULL;

	if (password_view->priv->uri_file) {
		g_object_unref (password_view->priv->uri_file);
		password_view->priv->uri_file = NULL;
	}

	G_OBJECT_CLASS (ev_password_view_parent_class)->finalize (object);
}

static void
ev_password_view_class_init (EvPasswordViewClass *class)
{
	GObjectClass *g_object_class;

	g_object_class = G_OBJECT_CLASS (class);

	password_view_signals[UNLOCK] =
		g_signal_new ("unlock",
			      G_TYPE_FROM_CLASS (g_object_class),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
			      G_STRUCT_OFFSET (EvPasswordViewClass, unlock),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	g_object_class->finalize = ev_password_view_finalize;
}

static void
ev_password_view_clicked_cb (GtkWidget      *button,
			     EvPasswordView *password_view)
{
	ev_password_view_ask_password (password_view);
}

static void
ev_password_view_init (EvPasswordView *password_view)
{
	GtkWidget *vbox;
	GtkWidget *hbox;
	GtkWidget *image;
	GtkWidget *button;
	GtkWidget *label;
	gchar     *markup;

	password_view->priv = ev_password_view_get_instance_private (password_view);

	password_view->priv->password_save = G_PASSWORD_SAVE_NEVER;

	/* set ourselves up */
	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 24);
	gtk_widget_set_valign (vbox, GTK_ALIGN_CENTER);
	gtk_widget_set_hexpand (vbox, FALSE);
	gtk_widget_set_vexpand (vbox, FALSE);
	gtk_box_append (GTK_BOX (password_view), vbox);

	password_view->priv->label =
		(GtkWidget *) g_object_new (GTK_TYPE_LABEL,
					    "wrap", TRUE,
					    "selectable", TRUE,
					    NULL);
	gtk_box_append (GTK_BOX (vbox), password_view->priv->label);

	image = gtk_image_new_from_icon_name ("dialog-password");
	gtk_box_append (GTK_BOX (vbox), image);

	label = gtk_label_new (NULL);
	gtk_label_set_wrap (GTK_LABEL (label), TRUE);
	markup = g_strdup_printf ("<span size=\"x-large\">%s</span>",
				  _("This document is locked and can only be read by entering the correct password."));
	gtk_label_set_markup (GTK_LABEL (label), markup);
	g_free (markup);

	gtk_box_append (GTK_BOX (vbox), label);

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_append (GTK_BOX (vbox), hbox);

	button = gtk_button_new_with_mnemonic (_("_Unlock Document"));
	gtk_style_context_add_class (gtk_widget_get_style_context (button), "suggested-action");
	g_signal_connect (button, "clicked", G_CALLBACK (ev_password_view_clicked_cb), password_view);
	gtk_box_append(GTK_BOX (hbox), button);

	gtk_widget_show (vbox);
}

/* Public functions */
void
ev_password_view_set_uri (EvPasswordView *password_view,
			  const char     *uri)
{
	gchar *markup, *file_name;
	GFile *file;

	g_return_if_fail (EV_IS_PASSWORD_VIEW (password_view));
	g_return_if_fail (uri != NULL);

	file = g_file_new_for_uri (uri);
	if (password_view->priv->uri_file &&
	    g_file_equal (file, password_view->priv->uri_file)) {
		g_object_unref (file);
		return;
	}
	if (password_view->priv->uri_file)
		g_object_unref (password_view->priv->uri_file);
	password_view->priv->uri_file = file;

	file_name = g_file_get_basename (password_view->priv->uri_file);
	markup = g_markup_printf_escaped ("<span size=\"x-large\" weight=\"bold\">%s</span>",
					  file_name);
	g_free (file_name);

	gtk_label_set_markup (GTK_LABEL (password_view->priv->label), markup);
	g_free (markup);
}

static void
ev_password_dialog_got_response (GtkDialog      *dialog,
				 gint            response_id,
				 EvPasswordView *password_view)
{
	gtk_widget_set_sensitive (GTK_WIDGET (password_view), TRUE);

	if (response_id == GTK_RESPONSE_OK) {
		g_free (password_view->priv->password);
		password_view->priv->password =
			g_strdup (gtk_editable_get_text (GTK_EDITABLE (password_view->priv->password_entry)));

		g_signal_emit (password_view, password_view_signals[UNLOCK], 0);
	}

	gtk_window_destroy (GTK_WINDOW (dialog));
}

static void
ev_password_dialog_remember_button_toggled (GtkToggleButton *button,
					    EvPasswordView  *password_view)
{
	if (gtk_toggle_button_get_active (button)) {
		gpointer data;

		data = g_object_get_data (G_OBJECT (button), "password-save");
		password_view->priv->password_save = GPOINTER_TO_INT (data);
	}
}

static void
ev_password_dialog_entry_changed_cb (GtkEditable *editable,
				     GtkDialog   *dialog)
{
	const char *text;

	text = gtk_editable_get_text (GTK_EDITABLE (editable));

	gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog), GTK_RESPONSE_OK,
					   (text != NULL && *text != '\0'));
}

static void
ev_password_dialog_entry_activated_cb (GtkEntry  *entry,
				       GtkDialog *dialog)
{
	gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
}

void
ev_password_view_ask_password (EvPasswordView *password_view)
{
	GtkDialog *dialog;
	GtkWidget *content_area;
	GtkWidget *hbox, *main_vbox, *vbox, *icon;
	GtkWidget *grid;
	GtkWidget *label;
	GtkWidget *button;
	gchar     *text, *markup, *file_name;

	gtk_widget_set_sensitive (GTK_WIDGET (password_view), FALSE);

	dialog = GTK_DIALOG (gtk_dialog_new ());
	content_area = gtk_dialog_get_content_area (dialog);
	
	/* Set the dialog up with HIG properties */
	gtk_box_set_spacing (GTK_BOX (content_area), 2); /* 2 * 5 + 2 = 12 */
	
	gtk_window_set_title (GTK_WINDOW (dialog), _("Enter password"));
	gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
	gtk_window_set_icon_name (GTK_WINDOW (dialog), "dialog-password");
	gtk_window_set_transient_for (GTK_WINDOW (dialog), password_view->priv->parent_window);
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);

	gtk_dialog_add_button (dialog, Q_("_Cancel"), GTK_RESPONSE_CANCEL);
	button = gtk_dialog_add_button (dialog, _("_Unlock Document"), GTK_RESPONSE_OK);

	gtk_style_context_add_class (gtk_widget_get_style_context (button), "suggested-action");
	gtk_dialog_set_default_response (dialog, GTK_RESPONSE_OK);
	gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog),
					   GTK_RESPONSE_OK, FALSE);

	/* Build contents */
	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_box_append (GTK_BOX (content_area), hbox);
	gtk_widget_show (hbox);

	icon = gtk_image_new_from_icon_name ("dialog-password");

	gtk_widget_set_halign (icon, GTK_ALIGN_CENTER);
	gtk_widget_set_valign (icon, GTK_ALIGN_START);
	gtk_box_append (GTK_BOX (hbox), icon);
	gtk_widget_show (icon);

	main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
	gtk_box_append (GTK_BOX (hbox), main_vbox);
	gtk_widget_show (main_vbox);

	label = gtk_label_new (NULL);
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_widget_set_valign (label, GTK_ALIGN_CENTER);
	gtk_label_set_wrap (GTK_LABEL (label), TRUE);
	file_name = g_file_get_basename (password_view->priv->uri_file);
	text = g_markup_printf_escaped (_("The document “%s” is locked and requires a password before it can be opened."),
                                        file_name);
	markup = g_strdup_printf ("<span size=\"larger\" weight=\"bold\">%s</span>\n\n%s",
				  _("Password required"),
                                  text);
	gtk_label_set_markup (GTK_LABEL (label), markup);
	g_free (text);
	g_free (markup);
	g_free (file_name);
	gtk_box_append (GTK_BOX (main_vbox), label);
	gtk_widget_show (label);

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	gtk_box_append (GTK_BOX (main_vbox), vbox);
	gtk_widget_show (vbox);

	/* The grid that holds the entries */
	grid = gtk_grid_new ();
	gtk_grid_set_column_spacing (GTK_GRID (grid), 12);
	gtk_grid_set_row_spacing (GTK_GRID (grid), 6);
	gtk_widget_set_valign (grid, GTK_ALIGN_START);
	gtk_widget_set_hexpand (grid, TRUE);
	gtk_widget_set_vexpand (grid, TRUE);
	gtk_widget_set_margin_top (grid, 0);
	gtk_widget_set_margin_bottom (grid, 0);
	gtk_widget_set_margin_start (grid, 0);
	gtk_widget_set_margin_end (grid, 0);
	gtk_widget_show (grid);
	gtk_box_append (GTK_BOX (vbox), grid);

	label = gtk_label_new_with_mnemonic (_("_Password:"));
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_widget_set_valign (label, GTK_ALIGN_CENTER);

	password_view->priv->password_entry = gtk_entry_new ();
	gtk_entry_set_visibility (GTK_ENTRY (password_view->priv->password_entry), FALSE);
	g_signal_connect (password_view->priv->password_entry, "changed",
			  G_CALLBACK (ev_password_dialog_entry_changed_cb),
			  dialog);
	g_signal_connect (password_view->priv->password_entry, "activate",
			  G_CALLBACK (ev_password_dialog_entry_activated_cb),
			  dialog);

	gtk_grid_attach (GTK_GRID (grid), label, 0, 0, 1, 1);
	gtk_widget_show (label);

	gtk_grid_attach (GTK_GRID (grid), password_view->priv->password_entry, 1, 0, 1, 1);
	gtk_widget_set_hexpand (password_view->priv->password_entry, TRUE);
	gtk_widget_show (password_view->priv->password_entry);

	gtk_label_set_mnemonic_widget (GTK_LABEL (label),
				       password_view->priv->password_entry);

	if (ev_keyring_is_available ()) {
		GtkWidget  *choice;
		GtkWidget  *remember_box;
		/* GSList *group; */

		remember_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
		gtk_box_append (GTK_BOX (vbox), remember_box);
		gtk_widget_show (remember_box);

		GtkWidget *choice1 = gtk_check_button_new_with_mnemonic (_("Forget password _immediately")); choice = choice1;
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (choice),
					      password_view->priv->password_save == G_PASSWORD_SAVE_NEVER);
		g_object_set_data (G_OBJECT (choice), "password-save",
				   GINT_TO_POINTER (G_PASSWORD_SAVE_NEVER));
		g_signal_connect (choice, "toggled",
				  G_CALLBACK (ev_password_dialog_remember_button_toggled),
				  password_view);
		gtk_box_append (GTK_BOX (remember_box), choice);
		gtk_widget_show (choice);

		
		choice = gtk_check_button_new_with_mnemonic (_("Remember password until you _log out")); gtk_check_button_set_group(GTK_CHECK_BUTTON(choice), GTK_CHECK_BUTTON(choice1));
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (choice),
					      password_view->priv->password_save == G_PASSWORD_SAVE_FOR_SESSION);
		g_object_set_data (G_OBJECT (choice), "password-save",
				   GINT_TO_POINTER (G_PASSWORD_SAVE_FOR_SESSION));
		g_signal_connect (choice, "toggled",
				  G_CALLBACK (ev_password_dialog_remember_button_toggled),
				  password_view);
		gtk_box_append (GTK_BOX (remember_box), choice);
		gtk_widget_show (choice);

		
		choice = gtk_check_button_new_with_mnemonic (_("Remember _forever")); gtk_check_button_set_group(GTK_CHECK_BUTTON(choice), GTK_CHECK_BUTTON(choice1));
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (choice),
					      password_view->priv->password_save == G_PASSWORD_SAVE_PERMANENTLY);
		g_object_set_data (G_OBJECT (choice), "password-save",
				   GINT_TO_POINTER (G_PASSWORD_SAVE_PERMANENTLY));
		g_signal_connect (choice, "toggled",
				  G_CALLBACK (ev_password_dialog_remember_button_toggled),
				  password_view);
		gtk_box_append (GTK_BOX (remember_box), choice);
		gtk_widget_show (choice);
	}

	g_signal_connect (dialog, "response",
			  G_CALLBACK (ev_password_dialog_got_response),
			  password_view);

	gtk_widget_show (GTK_WIDGET (dialog));
}

const gchar *
ev_password_view_get_password (EvPasswordView *password_view)
{
	return password_view->priv->password;
}

GPasswordSave
ev_password_view_get_password_save_flags (EvPasswordView *password_view)
{
	return password_view->priv->password_save;
}

GtkWidget *
ev_password_view_new (GtkWindow *parent)
{
	EvPasswordView *retval;

	retval = EV_PASSWORD_VIEW (g_object_new (EV_TYPE_PASSWORD_VIEW, NULL));

	retval->priv->parent_window = parent;

	return GTK_WIDGET (retval);
}

