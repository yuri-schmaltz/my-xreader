/* ev-message-area.c
 *  this file is part of xreader, a mate document viewer
 *
 * Copyright (C) 2007 Carlos Garcia Campos
 *
 * Author:
 *   Carlos Garcia Campos <carlosgc@gnome.org>
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

#include "ev-message-area.h"

struct _EvMessageAreaPrivate {
	GtkWidget *main_box;
	GtkWidget *action_area;
	GtkWidget *image;
	GtkWidget *label;
	GtkWidget *secondary_label;
	
	guint      message_type : 3;
};

enum {
	PROP_0,
	PROP_TEXT,
	PROP_SECONDARY_TEXT,
	PROP_IMAGE
};

enum {
	RESPONSE,
	LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

static void ev_message_area_set_property (GObject      *object,
					  guint         prop_id,
					  const GValue *value,
					  GParamSpec   *pspec);
static void ev_message_area_get_property (GObject      *object,
					  guint         prop_id,
					  GValue       *value,
					  GParamSpec   *pspec);

G_DEFINE_TYPE_WITH_PRIVATE (EvMessageArea, ev_message_area, GTK_TYPE_BOX)

static void
ev_message_area_class_init (EvMessageAreaClass *class)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (class);

	gobject_class->set_property = ev_message_area_set_property;
	gobject_class->get_property = ev_message_area_get_property;

	g_object_class_install_property (gobject_class,
					 PROP_TEXT,
					 g_param_spec_string ("text",
							      "Text",
							      "The primary text of the message dialog",
							      NULL,
							      G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
					 PROP_SECONDARY_TEXT,
					 g_param_spec_string ("secondary-text",
							      "Secondary Text",
							      "The secondary text of the message dialog",
							      NULL,
							      G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
					 PROP_IMAGE,
					 g_param_spec_object ("image",
							      "Image",
							      "The image",
							      GTK_TYPE_WIDGET,
							      G_PARAM_READWRITE));

	signals[RESPONSE] =
		g_signal_new ("response",
			      G_TYPE_FROM_CLASS (gobject_class),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      g_cclosure_marshal_VOID__INT,
			      G_TYPE_NONE, 1,
			      G_TYPE_INT);
}

static void
ev_message_area_init (EvMessageArea *area)
{
	GtkWidget *hbox, *vbox;
	GtkWidget *content_area;

	area->priv = ev_message_area_get_instance_private (area);

	area->priv->main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
	vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);

	area->priv->label = gtk_label_new (NULL);
	gtk_label_set_use_markup (GTK_LABEL (area->priv->label), TRUE);
	gtk_label_set_wrap (GTK_LABEL (area->priv->label), TRUE);
	gtk_label_set_selectable (GTK_LABEL (area->priv->label), TRUE);
	gtk_widget_set_halign (area->priv->label, GTK_ALIGN_START);
	gtk_widget_set_valign (area->priv->label, GTK_ALIGN_CENTER);
	gtk_widget_set_focusable (area->priv->label, TRUE);
	gtk_widget_set_vexpand (area->priv->label, TRUE);
	gtk_box_append (GTK_BOX (vbox), area->priv->label);

	area->priv->secondary_label = gtk_label_new (NULL);
	gtk_label_set_use_markup (GTK_LABEL (area->priv->secondary_label), TRUE);
	gtk_label_set_wrap (GTK_LABEL (area->priv->secondary_label), TRUE);
	gtk_label_set_selectable (GTK_LABEL (area->priv->secondary_label), TRUE);
	gtk_widget_set_halign (area->priv->secondary_label, GTK_ALIGN_START);
	gtk_widget_set_valign (area->priv->secondary_label, GTK_ALIGN_CENTER);
	gtk_widget_set_focusable (area->priv->secondary_label, TRUE);
	gtk_widget_set_vexpand (area->priv->secondary_label, TRUE);
	gtk_box_append (GTK_BOX (vbox), area->priv->secondary_label);

	area->priv->image = gtk_image_new_from_icon_name (NULL);
	gtk_widget_set_halign (area->priv->image, GTK_ALIGN_CENTER);
	gtk_widget_set_valign (area->priv->image, GTK_ALIGN_START);
	gtk_box_append (GTK_BOX (hbox), area->priv->image);

	gtk_widget_set_hexpand (vbox, TRUE);
	gtk_box_append (GTK_BOX (hbox), vbox);

	gtk_widget_set_hexpand (hbox, TRUE);
	gtk_box_append (GTK_BOX (area->priv->main_box), hbox);

	
}

static void
ev_message_area_set_image_for_type (EvMessageArea *area,
				    GtkMessageType type)
{
	const gchar *icon_name = NULL;

	switch (type) {
	case GTK_MESSAGE_INFO:
		icon_name = "dialog-information";
		break;
	case GTK_MESSAGE_QUESTION:
		icon_name = "dialog-question";
		break;
	case GTK_MESSAGE_WARNING:
		icon_name = "dialog-warning";
		break;
	case GTK_MESSAGE_ERROR:
		icon_name = "dialog-error";
		break;
	case GTK_MESSAGE_OTHER:
		break;
	default:
		g_warning ("Unknown GtkMessageType %u", type);
		break;
	}

	if (icon_name)
		gtk_image_set_from_icon_name (GTK_IMAGE (area->priv->image), icon_name);
}

static void
ev_message_area_set_property (GObject      *object,
			      guint         prop_id,
			      const GValue *value,
			      GParamSpec   *pspec)
{
	EvMessageArea *area = EV_MESSAGE_AREA (object);

	switch (prop_id) {
	case PROP_TEXT:
		ev_message_area_set_text (area, g_value_get_string (value));
		break;
	case PROP_SECONDARY_TEXT:
		ev_message_area_set_secondary_text (area, g_value_get_string (value));
		break;
	case PROP_IMAGE:
		ev_message_area_set_image (area, (GtkWidget *)g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
ev_message_area_get_property (GObject     *object,
			      guint        prop_id,
			      GValue      *value,
			      GParamSpec  *pspec)
{
	EvMessageArea *area = EV_MESSAGE_AREA (object);

	switch (prop_id) {
	case PROP_TEXT:
		g_value_set_string (value, gtk_label_get_label (GTK_LABEL (area->priv->label)));
		break;
	case PROP_SECONDARY_TEXT:
		g_value_set_string (value, gtk_label_get_label (GTK_LABEL (area->priv->secondary_label)));
		break;
	case PROP_IMAGE:
		g_value_set_object (value, area->priv->image);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void button_clicked (GtkButton *button, EvMessageArea *area) {
    int response_id = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "response-id"));
    g_signal_emit(area, signals[RESPONSE], 0, response_id);
}

void
_ev_message_area_add_buttons_valist (EvMessageArea *area,
				     const gchar   *first_button_text,
				     va_list        args)
{
	const gchar* text;
	gint response_id;

	if (first_button_text == NULL)
		return;

	text = first_button_text;
	response_id = va_arg (args, gint);

	while (text != NULL) {
		GtkWidget *button = gtk_button_new_with_label(text);
		g_object_set_data(G_OBJECT(button), "response-id", GINT_TO_POINTER(response_id));
		g_signal_connect(button, "clicked", G_CALLBACK(button_clicked), area);
		gtk_box_append(GTK_BOX(area->priv->action_area), button);
        gtk_widget_show(button);

		text = va_arg (args, gchar*);
		if (text == NULL)
			break;

		response_id = va_arg (args, int);
	}
}

GtkWidget *
_ev_message_area_get_main_box (EvMessageArea *area)
{
	return area->priv->main_box;
}

GtkWidget *
ev_message_area_new (GtkMessageType type,
		     const gchar   *text,
		     const gchar   *first_button_text,
		     ...)
{
	GtkWidget *widget;

	widget = g_object_new (EV_TYPE_MESSAGE_AREA,
			       "message-type", type,
			       "text", text,
			       NULL);
	ev_message_area_set_image_for_type (EV_MESSAGE_AREA (widget), type);
	if (first_button_text) {
		va_list args;

		va_start (args, first_button_text);
		_ev_message_area_add_buttons_valist (EV_MESSAGE_AREA (widget),
						     first_button_text, args);
		va_end (args);
	}

	return widget;
}

void
ev_message_area_set_image (EvMessageArea *area,
			   GtkWidget     *image)
{
	GtkWidget *parent;

	g_return_if_fail (EV_IS_MESSAGE_AREA (area));

	area->priv->message_type = GTK_MESSAGE_OTHER;

	parent = gtk_widget_get_parent (area->priv->image);
        gtk_box_remove (GTK_BOX (parent), area->priv->image);
        gtk_box_prepend (GTK_BOX (parent), image);

	area->priv->image = image;

	g_object_notify (G_OBJECT (area), "image");
}

void
ev_message_area_set_image_from_icon_name (EvMessageArea *area,
					  const gchar   *icon_name)
{
	g_return_if_fail (EV_IS_MESSAGE_AREA (area));
	g_return_if_fail (icon_name != NULL);
	
	gtk_image_set_from_icon_name (GTK_IMAGE (area->priv->image),
				  icon_name);
}

void
ev_message_area_set_text (EvMessageArea *area,
			  const gchar   *str)
{
	g_return_if_fail (EV_IS_MESSAGE_AREA (area));

	if (str) {
		gchar *msg;

		msg = g_strdup_printf ("<b>%s</b>", str);
		gtk_label_set_markup (GTK_LABEL (area->priv->label), msg);
		g_free (msg);
	} else {
		gtk_label_set_markup (GTK_LABEL (area->priv->label), NULL);
	}

	g_object_notify (G_OBJECT (area), "text");
}

void
ev_message_area_set_secondary_text (EvMessageArea *area,
				    const gchar   *str)
{
	g_return_if_fail (EV_IS_MESSAGE_AREA (area));

	if (str) {
		gchar *msg;

		msg = g_strdup_printf ("<small>%s</small>", str);
		gtk_label_set_markup (GTK_LABEL (area->priv->secondary_label), msg);
		g_free (msg);
		gtk_widget_show (area->priv->secondary_label);
	} else {
		gtk_label_set_markup (GTK_LABEL (area->priv->secondary_label), NULL);
		gtk_widget_hide (area->priv->secondary_label);
	}

	g_object_notify (G_OBJECT (area), "secondary-text");
}
