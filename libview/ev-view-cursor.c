/* this file is part of xreader, a mate document viewer
 *
 *  Copyright (C) 2004 Red Hat, Inc
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

#include "ev-view-cursor.h"

GdkCursor *
ev_view_cursor_new (GdkDisplay  *display,
		    EvViewCursor new_cursor)
{
	GdkCursor *cursor = NULL;

	switch (new_cursor) {
	case EV_VIEW_CURSOR_NORMAL:
		break;
	case EV_VIEW_CURSOR_IBEAM:
		cursor = gdk_cursor_new_from_name ("text", NULL);
		break;
	case EV_VIEW_CURSOR_LINK:
		cursor = gdk_cursor_new_from_name ("pointer", NULL);
		break;
	case EV_VIEW_CURSOR_WAIT:
		cursor = gdk_cursor_new_from_name ("wait", NULL);
		break;
	case EV_VIEW_CURSOR_HIDDEN:
		cursor = gdk_cursor_new_from_name ("none", NULL);
		break;
	case EV_VIEW_CURSOR_DRAG:
		cursor = gdk_cursor_new_from_name ("move", NULL);
		break;
	case EV_VIEW_CURSOR_AUTOSCROLL:
		cursor = gdk_cursor_new_from_name ("n-resize", NULL);
		break;
	case EV_VIEW_CURSOR_ADD:
		cursor = gdk_cursor_new_from_name ("crosshair", NULL);
		break;
	}

	return cursor;
}
