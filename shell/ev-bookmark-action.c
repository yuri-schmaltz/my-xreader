/* ev-bookmark-action.c
 *  this file is part of evince, a gnome document viewer
 *
 * Copyright (C) 2010 Carlos Garcia Campos  <carlosgc@gnome.org>
 *
 * Evince is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Evince is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "config.h"

#include "ev-bookmark-action.h"

enum {
        PROP_0,
        PROP_TITLE,
        PROP_PAGE
};

struct _EvBookmarkAction {
        GObject base;

        gchar    *title;
        guint     page;
};

struct _EvBookmarkActionClass {
        GObjectClass base_class;
};

G_DEFINE_TYPE (EvBookmarkAction, ev_bookmark_action, G_TYPE_OBJECT)

static void
ev_bookmark_action_init (EvBookmarkAction *action)
{
}

static void
ev_bookmark_action_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
        EvBookmarkAction *action = EV_BOOKMARK_ACTION (object);

        switch (prop_id) {
        case PROP_TITLE:
                g_free (action->title);
                action->title = g_value_dup_string (value);
                break;
        case PROP_PAGE:
                action->page = g_value_get_uint (value);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
}

static void
ev_bookmark_action_get_property (GObject      *object,
                                 guint         prop_id,
                                 GValue *value,
                                 GParamSpec   *pspec)
{
        EvBookmarkAction *action = EV_BOOKMARK_ACTION (object);

        switch (prop_id) {
        case PROP_TITLE:
                g_value_set_string (value, action->title);
                break;
        case PROP_PAGE:
                g_value_set_uint (value, action->page);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
}

static void
ev_bookmark_action_finalize (GObject *object)
{
        EvBookmarkAction *action = EV_BOOKMARK_ACTION (object);

        g_free (action->title);

        G_OBJECT_CLASS (ev_bookmark_action_parent_class)->finalize (object);
}

static void
ev_bookmark_action_class_init (EvBookmarkActionClass *klass)
{
        GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

        gobject_class->set_property = ev_bookmark_action_set_property;
        gobject_class->get_property = ev_bookmark_action_get_property;
        gobject_class->finalize = ev_bookmark_action_finalize;

        g_object_class_install_property (gobject_class,
                                         PROP_TITLE,
                                         g_param_spec_string ("title",
                                                              "Title",
                                                              "The bookmark title",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        g_object_class_install_property (gobject_class,
                                         PROP_PAGE,
                                         g_param_spec_uint ("page",
                                                            "Page",
                                                            "The bookmark page",
                                                            0, G_MAXUINT, 0,
                                                            G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE));
}

EvBookmarkAction *
ev_bookmark_action_new (EvBookmark *bookmark)
{
        EvBookmarkAction *action;

        g_return_val_if_fail (bookmark->title != NULL, NULL);

        action = g_object_new (EV_TYPE_BOOKMARK_ACTION,
                               "title", bookmark->title,
                               "page", bookmark->page,
                               NULL);

        return action;
}

guint
ev_bookmark_action_get_page (EvBookmarkAction *action)
{
        g_return_val_if_fail (EV_IS_BOOKMARK_ACTION (action), 0);

        return action->page;
}
