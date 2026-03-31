/* this file is part of xreader, a generic document viewer
 *
 *  Copyright (C) 2004 Martin Kretzschmar
 *  Copyright © 2010 Christian Persch
 *
 *  Author:
 *    Martin Kretzschmar <martink@gnome.org>
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
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "totem-scrsaver.h"

#include "ev-application.h"
#include "ev-file-helpers.h"
#include "ev-stock-icons.h"

#ifdef ENABLE_DBUS
#include "ev-gdbus-generated.h"
#endif /* ENABLE_DBUS */

gchar **supported_mimetypes;

struct _EvApplication {
	GtkApplication         base_instance;

	gchar                 *uri;
	gchar                 *dot_dir;

#ifdef ENABLE_DBUS
    GDBusConnection       *connection;
    EvXreaderApplication  *skeleton;
    gboolean               doc_registered;
#endif

	TotemScrsaver         *scr_saver;
};

struct _EvApplicationClass {
	GtkApplicationClass   base_class;
};

G_DEFINE_TYPE (EvApplication, ev_application, GTK_TYPE_APPLICATION)

#ifdef ENABLE_DBUS
#define APPLICATION_DBUS_OBJECT_PATH  "/org/x/reader/Xreader"
#define APPLICATION_DBUS_INTERFACE    "org.x.reader.Application"

#define XREADER_DAEMON_SERVICE        "org.x.reader.Daemon"
#define XREADER_DAEMON_OBJECT_PATH    "/org/x/reader/Daemon"
#define XREADER_DAEMON_INTERFACE      "org.x.reader.Daemon"
#endif

static void _ev_application_open_uri_at_dest (EvApplication   *application,
                                              const gchar     *uri,
                                              GdkDisplay      *display,
                                              EvLinkDest      *dest,
                                              EvWindowRunMode  mode,
                                              const gchar     *search_string,
                                              guint            timestamp);
static void ev_application_open_uri_in_window (EvApplication  *application,
                                               const char     *uri,
                                               EvWindow       *ev_window,
                                               GdkDisplay     *display,
                                               EvLinkDest     *dest,
                                               EvWindowRunMode mode,
                                               const gchar    *search_string,
                                               guint           timestamp);

/**
 * ev_application_new:
 *
 * Creates a new #EvApplication instance.
 *
 * Returns: (transfer full): a newly created #EvApplication
 */
EvApplication *
ev_application_new (void)
{
  const GApplicationFlags flags = G_APPLICATION_NON_UNIQUE;

  return g_object_new (EV_TYPE_APPLICATION,
                       "application-id", NULL,
                       "flags", flags,
                       NULL);
}

/* Session */
gboolean
ev_application_load_session (EvApplication *application)
{
    /* GTK4: Session management is handled by GtkApplication.
     * The old EggSMClient X11-based session saving is removed.
     */
    return FALSE;
}

static void
ev_application_init_session (EvApplication *application)
{
    /* GTK4: No-op. Session management handled by GtkApplication. */
}

#ifdef ENABLE_DBUS
/**
 * ev_display_open_if_needed:
 * @name: the name of the display to be open if it's needed.
 *
 * Search among all the open displays if any of them have the same name as the
 * passed name. If the display isn't found it tries the open it.
 *
 * Returns: a #GdkDisplay of the display with the passed name.
 */
static GdkDisplay *
ev_display_open_if_needed (const gchar *name)
{
    GSList     *displays;
    GSList     *l;
    GdkDisplay *display = NULL;

    displays = gdk_display_manager_list_displays (gdk_display_manager_get ());

    for (l = displays; l != NULL; l = l->next) {
        const gchar *display_name = gdk_display_get_name ((GdkDisplay *) l->data);

        if (g_ascii_strcasecmp (display_name, name) == 0) {
            display = l->data;
            break;
        }
    }

    g_slist_free (displays);

    return display != NULL ? display : gdk_display_open (name);
}
#endif /* ENABLE_DBUS */

static void
ev_spawn (const char     *uri,
          GdkDisplay     *display,
          EvLinkDest     *dest,
          EvWindowRunMode mode,
          const gchar    *search_string,
          guint           timestamp)
{
    GString *cmd;
    gchar *path, *cmdline;
    GAppInfo *app;
    GdkAppLaunchContext *ctx;
    GError  *error = NULL;

    cmd = g_string_new (NULL);
    path = g_build_filename (BINDIR, "xreader", NULL);
    g_string_append_printf (cmd, " %s", path);
    g_free (path);

    /* Page label or index */
    if (dest) {
        switch (ev_link_dest_get_dest_type (dest)) {
            case EV_LINK_DEST_TYPE_PAGE_LABEL:
                g_string_append_printf (cmd, " --page-label=%s",
                                        ev_link_dest_get_page_label (dest));
                break;
            case EV_LINK_DEST_TYPE_PAGE:
                g_string_append_printf (cmd, " --page-index=%d",
                                        ev_link_dest_get_page (dest) + 1);
                break;
            case EV_LINK_DEST_TYPE_NAMED:
                g_string_append_printf (cmd, " --named-dest=%s",
                                        ev_link_dest_get_named_dest (dest));
                break;
            default:
                break;
        }
    }

    /* Find string */
    if (search_string) {
        g_string_append_printf (cmd, " --find=%s", search_string);
    }

    /* Mode */
    switch (mode) {
        case EV_WINDOW_MODE_FULLSCREEN:
            g_string_append (cmd, " -f");
            break;
        case EV_WINDOW_MODE_PRESENTATION:
            g_string_append (cmd, " -s");
            break;
        default:
            break;
    }

    cmdline = g_string_free (cmd, FALSE);
    app = g_app_info_create_from_commandline (cmdline, NULL, G_APP_INFO_CREATE_SUPPORTS_URIS, &error);

    if (app != NULL) {
        GList uri_list;
        GList *uris = NULL;

        if (!display)
            display = gdk_display_get_default ();
        ctx = gdk_display_get_app_launch_context (display);
        gdk_app_launch_context_set_timestamp (ctx, timestamp);

        if (uri) {
            uri_list.data = (gchar *)uri;
            uri_list.prev = uri_list.next = NULL;
            uris = &uri_list;
        }
        g_app_info_launch_uris (app, uris, G_APP_LAUNCH_CONTEXT (ctx), &error);

        g_object_unref (app);
        g_object_unref (ctx);
    }

    if (error != NULL) {
		g_printerr ("Error launching xreader %s: %s\n", uri, error->message);
        g_error_free (error);
    }

    g_free (cmdline);
}

static EvWindow *
ev_application_get_empty_window (EvApplication *application,
                                 GdkDisplay    *display)
{
    EvWindow *empty_window = NULL;
	GList    *windows;
    GList    *l;

    windows = gtk_application_get_windows (GTK_APPLICATION (application));
    for (l = windows; l != NULL; l = l->next) {
		EvWindow *window;

        if (!EV_IS_WINDOW (l->data))
            continue;

        window = EV_WINDOW (l->data);

        if (ev_window_is_empty (window) &&
            gtk_widget_get_display (GTK_WIDGET (window)) == display) {
            empty_window = window;
            break;
        }
    }

    return empty_window;
}


#ifdef ENABLE_DBUS
typedef struct {
    gchar          *uri;
    GdkDisplay     *display;
    EvLinkDest     *dest;
    EvWindowRunMode mode;
    gchar          *search_string;
    guint           timestamp;
} EvRegisterDocData;

static void
ev_register_doc_data_free (EvRegisterDocData *data)
{
    if (!data)
        return;

    g_free (data->uri);
    if (data->search_string)
        g_free (data->search_string);
    if (data->dest)
        g_object_unref (data->dest);

    g_free (data);
}

static void
on_reload_cb (GObject      *source_object,
              GAsyncResult *res,
              gpointer      user_data)
{
    GDBusConnection *connection = G_DBUS_CONNECTION (source_object);
    GVariant        *value;
    GError          *error = NULL;

    g_application_release (g_application_get_default ());

    value = g_dbus_connection_call_finish (connection, res, &error);
    if (value != NULL) {
        g_variant_unref (value);
    } else {
        g_printerr ("Failed to Reload: %s\n", error->message);
        g_error_free (error);
    }

    /* We did not open a window, so manually clear the startup
     * notification. */
}

static void
on_register_uri_cb (GObject      *source_object,
                    GAsyncResult *res,
                    gpointer      user_data)
{
    GDBusConnection   *connection = G_DBUS_CONNECTION (source_object);
    EvRegisterDocData *data = (EvRegisterDocData *)user_data;
    EvApplication     *application = EV_APP;
    GVariant          *value;
    const gchar       *owner;
    GVariantBuilder    builder;
    GError            *error = NULL;

    g_application_release (G_APPLICATION (application));

    value = g_dbus_connection_call_finish (connection, res, &error);
    if (!value) {
		g_printerr ("Error registering document: %s\n", error->message);
        g_error_free (error);

        _ev_application_open_uri_at_dest (application,
                                          data->uri,
                                          data->display,
                                          data->dest,
                                          data->mode,
                                          data->search_string,
                                          data->timestamp);
        ev_register_doc_data_free (data);

        return;
    }

    g_variant_get (value, "(&s)", &owner);

    /* This means that the document wasn't already registered; go
     * ahead with opening it.
     */
    if (owner[0] == '\0') {
        g_variant_unref (value);

        application->doc_registered = TRUE;

        _ev_application_open_uri_at_dest (application,
                                          data->uri,
                                          data->display,
                                          data->dest,
                                          data->mode,
                                          data->search_string,
                                          data->timestamp);
        ev_register_doc_data_free (data);

        return;
    }

    /* Already registered */
    g_variant_builder_init (&builder, G_VARIANT_TYPE ("(a{sv}u)"));
    g_variant_builder_open (&builder, G_VARIANT_TYPE ("a{sv}"));
    g_variant_builder_add (&builder, "{sv}",
                           "display",
                           g_variant_new_string (gdk_display_get_name (data->display)));
    if (data->dest) {
        switch (ev_link_dest_get_dest_type (data->dest)) {
            case EV_LINK_DEST_TYPE_PAGE_LABEL:
                g_variant_builder_add (&builder, "{sv}", "page-label",
                                       g_variant_new_string (ev_link_dest_get_page_label (data->dest)));
                break;
            case EV_LINK_DEST_TYPE_PAGE:
                g_variant_builder_add (&builder, "{sv}", "page-index",
                                       g_variant_new_uint32 (ev_link_dest_get_page (data->dest)));
                break;
            case EV_LINK_DEST_TYPE_NAMED:
                g_variant_builder_add (&builder, "{sv}", "named-dest",
                                       g_variant_new_string (ev_link_dest_get_named_dest (data->dest)));
                break;
            default:
                break;
        }
    }
    if (data->search_string) {
        g_variant_builder_add (&builder, "{sv}",
                               "find-string",
                               g_variant_new_string (data->search_string));
    }
    if (data->mode != EV_WINDOW_MODE_NORMAL) {
        g_variant_builder_add (&builder, "{sv}",
                               "mode",
                               g_variant_new_uint32 (data->mode));
    }
    g_variant_builder_close (&builder);

    g_variant_builder_add (&builder, "u", data->timestamp);

    g_dbus_connection_call (connection,
                            owner,
                            APPLICATION_DBUS_OBJECT_PATH,
                            APPLICATION_DBUS_INTERFACE,
                            "Reload",
                            g_variant_builder_end (&builder),
                            NULL,
                            G_DBUS_CALL_FLAGS_NONE,
                            -1,
                            NULL,
                            on_reload_cb,
                            NULL);
    g_application_hold (G_APPLICATION (application));
    g_variant_unref (value);
    ev_register_doc_data_free (data);
}

/*
 * ev_application_register_uri:
 * @application:
 * @uri:
 * @screen:
 * @dest:
 * @mode:
 * @search_string:
 * @timestamp:
 *
 * Registers @uri with xreader-daemon.
 *
 */
static void
ev_application_register_uri (EvApplication  *application,
                             const gchar    *uri,
                             GdkDisplay     *display,
                             EvLinkDest     *dest,
                             EvWindowRunMode mode,
                             const gchar    *search_string,
                             guint           timestamp)
{
    EvRegisterDocData *data;

    if (!application->skeleton)
        return;

    if (application->doc_registered) {
        /* Already registered, reload */
        GList *windows, *l;

        windows = gtk_application_get_windows (GTK_APPLICATION (application));
        for (l = windows; l != NULL; l = g_list_next (l)) {
            if (!EV_IS_WINDOW (l->data))
                continue;

            ev_application_open_uri_in_window (application, uri,
                                               EV_WINDOW (l->data),
                                               display, dest, mode,
                                               search_string,
                                               timestamp);
        }

        return;
    }

    data = g_new (EvRegisterDocData, 1);
    data->uri = g_strdup (uri);
    data->display = display;
    data->dest = dest ? g_object_ref (dest) : NULL;
    data->mode = mode;
    data->search_string = search_string ? g_strdup (search_string) : NULL;
    data->timestamp = timestamp;

    g_dbus_connection_call (g_application_get_dbus_connection (G_APPLICATION (application)),
                            XREADER_DAEMON_SERVICE,
                            XREADER_DAEMON_OBJECT_PATH,
                            XREADER_DAEMON_INTERFACE,
                            "RegisterDocument",
                            g_variant_new ("(s)", uri),
                            G_VARIANT_TYPE ("(s)"),
                            G_DBUS_CALL_FLAGS_NONE,
                            -1,
                            NULL,
                            on_register_uri_cb,
                            data);

    g_application_hold (G_APPLICATION (application));
}

static void
ev_application_unregister_uri (EvApplication *application,
                               const gchar   *uri)
{
    GVariant *value;
    GError   *error = NULL;

    if (!application->doc_registered)
        return;

    /* This is called from ev_application_shutdown(),
     * so it's safe to use the sync api
     */
    value = g_dbus_connection_call_sync (g_application_get_dbus_connection (G_APPLICATION (application)),
                                         XREADER_DAEMON_SERVICE,
                                         XREADER_DAEMON_OBJECT_PATH,
                                         XREADER_DAEMON_INTERFACE,
                                         "UnregisterDocument",
                                         g_variant_new ("(s)", uri),
                                         NULL,
                                         G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                         -1,
                                         NULL,
                                         &error);
    if (value == NULL) {
		g_printerr ("Error unregistering document: %s\n", error->message);
        g_error_free (error);
    } else {
        g_variant_unref (value);
    }
}
#endif /* ENABLE_DBUS */

static void
ev_application_open_uri_in_window (EvApplication  *application,
                                   const char     *uri,
                                   EvWindow       *ev_window,
                                   GdkDisplay     *display,
                                   EvLinkDest     *dest,
                                   EvWindowRunMode mode,
                                   const gchar    *search_string,
                                   guint           timestamp)
{
    if (uri == NULL)
        uri = application->uri;

    /* We need to load uri before showing the window, so
       we can restore window size without flickering */
    if (!gtk_widget_get_realized (GTK_WIDGET (ev_window))) {
        gtk_widget_set_visible (GTK_WIDGET (ev_window), FALSE);
        gtk_widget_realize (GTK_WIDGET (ev_window));
    }
    ev_window_open_uri (ev_window, uri, dest, mode, search_string);
    gtk_widget_set_visible (GTK_WIDGET (ev_window), TRUE);

    gtk_window_present_with_time (GTK_WINDOW (ev_window), timestamp);
}

static void
_ev_application_open_uri_at_dest (EvApplication  *application,
                                  const gchar    *uri,
                                  GdkDisplay     *display,
                                  EvLinkDest     *dest,
                                  EvWindowRunMode mode,
                                  const gchar    *search_string,
                                  guint           timestamp)
{
    EvWindow *ev_window;

    ev_window = ev_application_get_empty_window (application, display);
    if (!ev_window)
        ev_window = EV_WINDOW (ev_window_new ());

    ev_application_open_uri_in_window (application, uri, ev_window,
                                       display, dest, mode,
                                       search_string,
                                       timestamp);
}

/**
 * ev_application_open_uri_at_dest:
 * @application: The instance of the application.
 * @uri: The uri to be opened.
 * @screen: Thee screen where the link will be shown.
 * @dest: The #EvLinkDest of the document.
 * @mode: The run mode of the window.
 * @timestamp: Current time value.
 */
void
ev_application_open_uri_at_dest (EvApplication  *application,
                                 const char     *uri,
                                 GdkDisplay     *display,
                                 EvLinkDest     *dest,
                                 EvWindowRunMode mode,
                                 const gchar    *search_string,
                                 guint           timestamp)
{
    g_return_if_fail (uri != NULL);

    if (application->uri && strcmp (application->uri, uri) != 0) {
        /* spawn a new xreader process */
        ev_spawn (uri, display, dest, mode, search_string, timestamp);
        return;
    } else if (!application->uri) {
        application->uri = g_strdup (uri);
    }

#ifdef ENABLE_DBUS
    /* Register the uri or send Reload to
     * remote instance if already registered
     */
    ev_application_register_uri (application, uri, display, dest, mode, search_string, timestamp);
#else
    _ev_application_open_uri_at_dest (application, uri, display, dest, mode, search_string, timestamp);
#endif /* ENABLE_DBUS */
}

/**
 * ev_application_open_window:
 * @application: The instance of the application.
 * @timestamp: Current time value.
 *
 * Creates a new window
 */
void
ev_application_open_window (EvApplication *application,
                            GdkDisplay    *display,
                            guint32        timestamp)
{
    GtkWidget *new_window = ev_window_new ();

    if (!gtk_widget_get_realized (new_window))
        gtk_widget_realize (new_window);

    gtk_window_present_with_time (GTK_WINDOW (new_window), timestamp);
}

#ifdef ENABLE_DBUS
static gboolean
handle_get_window_list_cb (EvXreaderApplication   *object,
                           GDBusMethodInvocation *invocation,
                           EvApplication         *application)
{
        GList     *windows, *l;
        GPtrArray *paths;

        paths = g_ptr_array_new ();

        windows = gtk_application_get_windows (GTK_APPLICATION (application));
        for (l = windows; l; l = g_list_next (l)) {
            if (!EV_IS_WINDOW (l->data))
                continue;

            g_ptr_array_add (paths, (gpointer) ev_window_get_dbus_object_path (EV_WINDOW (l->data)));
        }

        g_ptr_array_add (paths, NULL);
        ev_xreader_application_complete_get_window_list (object, invocation,
                                                        (const char * const *) paths->pdata);

        g_ptr_array_free (paths, TRUE);

        return TRUE;
}

static gboolean
handle_reload_cb (EvXreaderApplication   *object,
                  GDBusMethodInvocation *invocation,
                  GVariant              *args,
                  guint                  timestamp,
                  EvApplication         *application)
{
    GList           *windows, *l;
    GVariantIter     iter;
    const gchar     *key;
    GVariant        *value;
    GdkDisplay      *display = NULL;
    EvLinkDest      *dest = NULL;
    EvWindowRunMode  mode = EV_WINDOW_MODE_NORMAL;
    const gchar     *search_string = NULL;

    g_variant_iter_init (&iter, args);

    while (g_variant_iter_loop (&iter, "{&sv}", &key, &value)) {
        if (strcmp (key, "display") == 0 && g_variant_classify (value) == G_VARIANT_CLASS_STRING) {
            display = ev_display_open_if_needed (g_variant_get_string (value, NULL));
        } else if (strcmp (key, "mode") == 0 && g_variant_classify (value) == G_VARIANT_CLASS_UINT32) {
            mode = g_variant_get_uint32 (value);
        } else if (strcmp (key, "page-label") == 0 && g_variant_classify (value) == G_VARIANT_CLASS_STRING) {
            dest = ev_link_dest_new_page_label (g_variant_get_string (value, NULL));
        } else if (strcmp (key, "named-dest") == 0 && g_variant_classify (value) == G_VARIANT_CLASS_STRING) {
            dest = ev_link_dest_new_named (g_variant_get_string (value, NULL));
        } else if (strcmp (key, "page-index") == 0 && g_variant_classify (value) == G_VARIANT_CLASS_UINT32) {
            dest = ev_link_dest_new_page (g_variant_get_uint32 (value));
        } else if (strcmp (key, "find-string") == 0 && g_variant_classify (value) == G_VARIANT_CLASS_STRING) {
            search_string = g_variant_get_string (value, NULL);
        }
    }

    if (!display)
        display = gdk_display_get_default ();

    windows = gtk_application_get_windows (GTK_APPLICATION ((application)));
    for (l = windows; l != NULL; l = g_list_next (l)) {
         if (!EV_IS_WINDOW (l->data))
             continue;

        ev_application_open_uri_in_window (application, NULL,
                                           EV_WINDOW (l->data),
                                           display, dest, mode,
                                           search_string,
                                           timestamp);
    }

    if (dest)
        g_object_unref (dest);

    ev_xreader_application_complete_reload (object, invocation);

    return TRUE;
}
#endif /* ENABLE_DBUS */

void
ev_application_open_uri_list (EvApplication *application,
                              GSList        *uri_list,
                              GdkDisplay    *display,
                              guint          timestamp)
{
    GSList *l;

    for (l = uri_list; l != NULL; l = l->next) {
        ev_application_open_uri_at_dest (application, (char *)l->data,
                                         display, NULL, 0, NULL,
                                         timestamp);
    }
}


static void
ev_application_shutdown (GApplication *gapplication)
{
    EvApplication *application = EV_APPLICATION (gapplication);

    if (application->uri) {
#ifdef ENABLE_DBUS
        ev_application_unregister_uri (application,
                                       application->uri);
#endif
        g_free (application->uri);
        application->uri = NULL;
    }

    g_object_unref (application->scr_saver);
    application->scr_saver = NULL;

    g_free (application->dot_dir);
    application->dot_dir = NULL;

    g_clear_pointer (&supported_mimetypes, g_strfreev);

    G_APPLICATION_CLASS (ev_application_parent_class)->shutdown (gapplication);
}

static void
ev_application_activate (GApplication *gapplication)
{
        EvApplication *application = EV_APPLICATION (gapplication);
        GList *windows, *l;

        windows = gtk_application_get_windows (GTK_APPLICATION (application));
        for (l = windows; l != NULL; l = l->next) {
                if (!EV_IS_WINDOW (l->data))
                        continue;

                gtk_window_present (GTK_WINDOW (l->data));
    }
    }

#ifdef ENABLE_DBUS
static gboolean
ev_application_dbus_register (GApplication    *gapplication,
                              GDBusConnection *connection,
                              const gchar     *object_path,
                              GError         **error)
{
        EvApplication *application = EV_APPLICATION (gapplication);
        EvXreaderApplication *skeleton;

        if (!G_APPLICATION_CLASS (ev_application_parent_class)->dbus_register (gapplication,
                                                                               connection,
                                                                               object_path,
                                                                               error))
                return FALSE;

        skeleton = ev_xreader_application_skeleton_new ();
        if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (skeleton),
                                               connection,
                                              APPLICATION_DBUS_OBJECT_PATH,
                                               error)) {
                g_object_unref (skeleton);

                return FALSE;
        }

        application->skeleton = skeleton;
                g_signal_connect (skeleton, "handle-get-window-list",
                                  G_CALLBACK (handle_get_window_list_cb),
                                  application);
                g_signal_connect (skeleton, "handle-reload",
                                  G_CALLBACK (handle_reload_cb),
                                  application);

        return TRUE;
        }

static void
ev_application_dbus_unregister (GApplication    *gapplication,
                                GDBusConnection *connection,
                                const gchar     *object_path)
{
        EvApplication *application = EV_APPLICATION (gapplication);

        if (application->skeleton != NULL) {
                g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (application->skeleton));
                g_object_unref (application->skeleton);
                application->skeleton = NULL;
        }

        G_APPLICATION_CLASS (ev_application_parent_class)->dbus_unregister (gapplication,
                                                                            connection,
                                                                            object_path);
    }

#endif /* ENABLE_DBUS */

static void
ev_application_class_init (EvApplicationClass *ev_application_class)
{
        GApplicationClass *g_application_class = G_APPLICATION_CLASS (ev_application_class);

        g_application_class->activate = ev_application_activate;
        g_application_class->shutdown = ev_application_shutdown;

#ifdef ENABLE_DBUS
        g_application_class->dbus_register = ev_application_dbus_register;
        g_application_class->dbus_unregister = ev_application_dbus_unregister;
#endif
        }

static void
parse_mimetypes (void)
{
    supported_mimetypes = g_strsplit (SUPPORTED_MIMETYPES, ";", -1);
}

static void
ev_application_init (EvApplication *ev_application)
{
    ev_application->dot_dir = g_build_filename (g_get_user_config_dir (), "xreader", NULL);

    ev_application_init_session (ev_application);

    parse_mimetypes ();

    ev_application->scr_saver = totem_scrsaver_new ();
    g_object_set (ev_application->scr_saver,
                  "reason", _("Running in presentation mode"),
                  NULL);
}

gboolean
ev_application_has_window (EvApplication *application)
{
    GList *l, *windows;

    windows = gtk_application_get_windows (GTK_APPLICATION (application));
    for (l = windows; l != NULL; l = l->next) {
        if (!EV_IS_WINDOW (l->data))
            continue;

        return TRUE;
    }

    return FALSE;
}

guint
ev_application_get_n_windows (EvApplication *application)
{
    GList *l, *windows;
    guint  retval = 0;

        windows = gtk_application_get_windows (GTK_APPLICATION (application));
        for (l = windows; l != NULL && !retval; l = l->next) {
                if (!EV_IS_WINDOW (l->data))
                        continue;

            retval++;
    }

    return retval;
}

const gchar *
ev_application_get_uri (EvApplication *application)
{
    return application->uri;
}

void
ev_application_screensaver_enable (EvApplication *application)
{
    totem_scrsaver_enable (application->scr_saver);
}

void
ev_application_screensaver_disable (EvApplication *application)
{
    totem_scrsaver_disable (application->scr_saver);
}

const gchar *
ev_application_get_dot_dir (EvApplication *application,
                            gboolean create)
{
    if (create)
        g_mkdir_with_parents (application->dot_dir, 0700);

    return application->dot_dir;
}

