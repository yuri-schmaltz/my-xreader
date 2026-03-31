/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Stock icons for Xreader
 *
 * Copyright (C) 2003 Martin Kretzschmar
 *
 * Author:
 *   Martin Kretzschmar <Martin.Kretzschmar@inf.tu-dresden.de>
 *
 * Xreader is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xreader is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
 * License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <config.h>

#include <gtk/gtk.h>

#include "ev-stock-icons.h"

static gchar *ev_icons_path;

/**
 * ev_stock_icons_init:
 *
 * GTK4: GtkIconFactory is removed. We simply add our custom icon
 * search path to GtkIconTheme so named icons can be found.
 */
void
ev_stock_icons_init (void)
{
	GtkIconTheme *icon_theme;

	ev_icons_path = g_build_filename (XREADERDATADIR, "icons", NULL);

	icon_theme = gtk_icon_theme_get_for_display (gdk_display_get_default ());
	if (icon_theme) {
		gtk_icon_theme_add_search_path (icon_theme, ev_icons_path);
	}
}

void
ev_stock_icons_shutdown (void)
{
	g_free (ev_icons_path);
	ev_icons_path = NULL;
}
