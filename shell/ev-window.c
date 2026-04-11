/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; c-indent-level: 8 -*- */
/* this file is part of xreader, a generic document viewer
 *
 *  Copyright (C) 2009 Juanjo Marín <juanj.marin@juntadeandalucia.es>
 *  Copyright (C) 2008 Carlos Garcia Campos
 *  Copyright (C) 2004 Martin Kretzschmar
 *  Copyright (C) 2004 Red Hat, Inc.
 *  Copyright (C) 2000, 2001, 2002, 2003, 2004 Marco Pesenti Gritti
 *  Copyright © 2003, 2004, 2005, 2009 Christian Persch
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>

#include "eggfindbar.h"

#include "ev-zoom-action.h"
#include "ephy-zoom.h"

#include "ev-application.h"
#include "ev-document-factory.h"
#include "ev-document-find.h"
#include "ev-document-fonts.h"
#include "ev-document-images.h"
#include "ev-document-links.h"
#include "ev-document-thumbnails.h"
#include "ev-document-annotations.h"
#include "ev-document-type-builtins.h"
#include "ev-document-misc.h"
#include "ev-file-exporter.h"
#include "ev-file-helpers.h"
#include "ev-file-monitor.h"
#include "ev-history.h"
#include "ev-image.h"
#include "ev-job-scheduler.h"
#include "ev-jobs.h"
#include "ev-message-area.h"
#include "ev-metadata.h"
#include "ev-password-view.h"
#include "ev-properties-dialog.h"
#include "ev-preferences-dialog.h"
#include "ev-sidebar-annotations.h"
#include "ev-sidebar-attachments.h"
#include "ev-sidebar-bookmarks.h"
#include "ev-sidebar.h"
#include "ev-sidebar-links.h"
#include "ev-sidebar-page.h"
#include "ev-sidebar-thumbnails.h"
#include "ev-sidebar-layers.h"
#include "ev-utils.h"
#include "ev-keyring.h"
#include "ev-view.h"
#include "ev-view-presentation.h"
#if ENABLE_EPUB
#include "ev-web-view.h"
#endif
#include "ev-view-type-builtins.h"
#include "ev-window.h"
#include "ev-window-title.h"
#include "ev-print-operation.h"
#include "ev-progress-message-area.h"
#include "ev-annotation-properties-dialog.h"
#include "ev-annotations-toolbar.h"
#include "ev-toolbar.h"

#ifdef ENABLE_DBUS
#include "ev-gdbus-generated.h"
#endif /* ENABLE_DBUS */

typedef enum {
    PAGE_MODE_DOCUMENT,
    PAGE_MODE_PASSWORD
} EvWindowPageMode;

typedef enum {
    EV_CHROME_MENUBAR             = 1 << 0,
    EV_CHROME_TOOLBAR             = 1 << 1,
    EV_CHROME_FINDBAR             = 1 << 2,
    EV_CHROME_RAISE_TOOLBAR       = 1 << 3,
    EV_CHROME_FULLSCREEN_TOOLBAR  = 1 << 4,
    EV_CHROME_SIDEBAR             = 1 << 5,
    EV_CHROME_NORMAL              = EV_CHROME_MENUBAR | EV_CHROME_TOOLBAR | EV_CHROME_SIDEBAR
} EvChrome;

typedef enum {
    EV_SAVE_DOCUMENT,
    EV_SAVE_ATTACHMENT,
    EV_SAVE_IMAGE
} EvSaveType;

typedef enum {
    EV_MENUBAR_HIDE,
    EV_MENUBAR_SHOW,
    EV_MENUBAR_TOGGLE
} EvMenubarAction;


/* --- Multi-tab support --- */
typedef struct _EvTab EvTab;
struct _EvTab {
    GtkWidget       *view_box;
    GtkWidget       *scrolled_window;
    GtkWidget       *view;
    EvDocumentModel *model;
    EvDocument      *document;
    EvHistory       *history;
    EvMetadata      *metadata;
    EvBookmarks     *bookmarks;
    gchar           *uri;
    gchar           *local_uri;
    glong            uri_mtime;
    gboolean         in_reload;
    EvFileMonitor   *monitor;
    guint            setup_document_idle;
    EvLinkDest      *dest;
    gchar           *search_string;
    EvWindowRunMode  window_mode;
    EvJob           *load_job;
    EvJob           *reload_job;
    EvJob           *thumbnail_job;
    EvJob           *save_job;
    EvJob           *find_job;
    EvWindowPageMode page_mode;
};

/* Forward declarations for tab management */
static GtkWidget * ev_window_create_tab_label (EvWindow *ev_window, const gchar *title, GtkWidget *tab_page);
static EvTab * ev_window_create_new_tab (EvWindow *ev_window, const gchar *title);
static void ev_window_tab_close_button_clicked (GtkButton *button, gpointer user_data);
static void ev_window_close_tab_by_page (EvWindow *ev_window, GtkWidget *page_widget);
static void ev_window_update_notebook_visibility (EvWindow *ev_window);
static void ev_window_notebook_switch_page (GtkNotebook *notebook, GtkWidget *page, guint page_num, EvWindow *ev_window);
static GSettings * ev_window_ensure_settings (EvWindow *ev_window);
static void ev_window_cmd_new_tab (GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void ev_window_cmd_close_tab (GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void ev_window_cmd_next_tab (GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void ev_window_cmd_prev_tab (GSimpleAction *action, GVariant *parameter, gpointer user_data);

struct _EvWindowPrivate {
    /* UI */
    EvChrome chrome;

    GtkWidget *main_box;
    GtkWidget *menubar;
    GtkWidget *toolbar;
    GtkWidget *toolbar_revealer;
    GtkWidget *notebook;
    GtkWidget *hpaned;
    GList    *tabs;
    EvTab    *active_tab;
    GtkWidget *view_box;
    GtkWidget *sidebar;
    GtkWidget *find_bar;
    GtkWidget *scrolled_window;
    GtkWidget *view;
    GtkWidget *presentation_view;
    GtkWidget *message_area;
    GtkWidget *password_view;
    GtkWidget *sidebar_thumbs;
    GtkWidget *page_selector;
    GtkWidget *sidebar_links;
    GtkWidget *sidebar_attachments;
    GtkWidget *sidebar_layers;
    GtkWidget *sidebar_annots;
    GtkWidget *sidebar_bookmarks;
#if ENABLE_EPUB
    GtkWidget *webview;
#endif
    /* Settings */
    GSettings *settings;
    GSettings *default_settings;

    /* Menubar */
    guint           menubar_accel_keyval;
    GdkModifierType menubar_accel_modifier;
    gboolean        menubar_skip_release;
    gboolean        menubar_show_queued;

    /* Progress Messages */
    guint         progress_idle;
    GCancellable *progress_cancellable;

    /* Dialogs */
    GtkWidget *properties;
    GtkWidget *print_dialog;

    /* UI Builders */
    GtkBuilder       *builder;
    GtkRecentManager *recent_manager;
    guint             recent_ui_id;
    guint             bookmarks_ui_id;

    /* Popup view */
    GtkWidget    *view_popup;
    EvLink       *link;
    EvImage      *image;
    EvAnnotation *annot;

    /* Popup attachment */
    GtkWidget    *attachment_popup;
    GList        *attach_list;

    /* Document */
    EvDocumentModel *model;
    char            *uri;
    glong            uri_mtime;
    char            *local_uri;
    gboolean         in_reload;
    EvFileMonitor   *monitor;
    guint            setup_document_idle;

    EvDocument        *document;
    EvHistory         *history;
    EvWindowPageMode   page_mode;
    EvWindowTitle     *title;
    EvMetadata        *metadata;
    EvBookmarks       *bookmarks;

    /* Load params */
    EvLinkDest       *dest;
    gchar            *search_string;
    EvWindowRunMode   window_mode;

    EvJob            *load_job;
    EvJob            *reload_job;
    EvJob            *thumbnail_job;
    EvJob            *save_job;
    EvJob            *find_job;

    /* Printing */
    GQueue           *print_queue;
    GtkPrintSettings *print_settings;
    GtkPageSetup     *print_page_setup;
    gboolean          close_after_print;
    gboolean          close_after_save;

#ifdef ENABLE_DBUS
    /* DBus */
    EvXreaderWindow  *skeleton;
    gchar            *dbus_object_path;
#endif
};

#define EV_WINDOW_IS_PRESENTATION(w) (w->priv->presentation_view != NULL)

#define PAGE_SELECTOR_ACTION            "PageSelector"
#define HISTORY_ACTION                  "History"

#ifdef ENABLE_DBUS
#define EV_WINDOW_DBUS_OBJECT_PATH      "/org/x/reader/Window/%d"
#define EV_WINDOW_DBUS_INTERFACE        "org.x.reader.Window"
#endif

#define SIDEBAR_DEFAULT_SIZE            132
#define LINKS_SIDEBAR_ID                "links"
#define THUMBNAILS_SIDEBAR_ID           "thumbnails"
#define ATTACHMENTS_SIDEBAR_ID          "attachments"
#define LAYERS_SIDEBAR_ID               "layers"
#define ANNOTS_SIDEBAR_ID               "annotations"
#define BOOKMARKS_SIDEBAR_ID            "bookmarks"

#define EV_PRINT_SETTINGS_FILE          "print-settings"
#define EV_PRINT_SETTINGS_GROUP         "Print Settings"
#define EV_PAGE_SETUP_GROUP             "Page Setup"

#define MIN_SCALE 0.05409

#define MAX_RECENT_ITEM_LEN (40)

static const gchar *document_print_settings[] = {
        GTK_PRINT_SETTINGS_N_COPIES,
        GTK_PRINT_SETTINGS_COLLATE,
        GTK_PRINT_SETTINGS_REVERSE,
        GTK_PRINT_SETTINGS_NUMBER_UP,
        GTK_PRINT_SETTINGS_SCALE,
        GTK_PRINT_SETTINGS_PRINT_PAGES,
        GTK_PRINT_SETTINGS_PAGE_RANGES,
        GTK_PRINT_SETTINGS_PAGE_SET,
        GTK_PRINT_SETTINGS_OUTPUT_URI
};

static void     ev_window_update_actions                     (EvWindow         *ev_window);
static void     ev_window_sidebar_visibility_changed_cb      (EvSidebar        *ev_sidebar,
                                                              GParamSpec       *pspec,
                                                              EvWindow         *ev_window);
static void     ev_window_view_menubar_cb                    (GSimpleAction    *action,
                                                              GVariant         *state,
                                                              gpointer          user_data);
static void     ev_window_view_toolbar_cb                    (GSimpleAction    *action,
                                                              GVariant         *state,
                                                              gpointer          user_data);
static void     ev_window_set_page_mode                      (EvWindow         *window,
                                                              EvWindowPageMode  page_mode);
static void     ev_window_load_job_cb                        (EvJob            *job,
                                                              gpointer          data);
static void     ev_window_reload_document                    (EvWindow         *window,
                                                              EvLinkDest *dest);
static void     ev_window_reload_job_cb                      (EvJob            *job,
                                                              EvWindow         *window);
static void     ev_window_set_icon_from_thumbnail            (EvJobThumbnail   *job,
                                                              EvWindow         *ev_window);
static void     ev_window_save_job_cb                        (EvJob            *save,
                                                              EvWindow         *window);
static void     ev_window_sizing_mode_changed_cb             (EvDocumentModel  *model,
                                                              GParamSpec       *pspec,
                                                              EvWindow         *ev_window);
static void     ev_window_zoom_changed_cb                    (EvDocumentModel  *model,
                                                              GParamSpec       *pspec,
                                                              EvWindow         *ev_window);
static void     ev_window_add_recent                         (EvWindow         *window,
                                                              const char       *filename);
static void     ev_window_run_fullscreen                     (EvWindow         *window);
static void     ev_window_stop_fullscreen                    (EvWindow         *window,
                                                              gboolean          unfullscreen_window);
static void     ev_window_cmd_view_fullscreen                (GSimpleAction    *action,
                                                              GVariant         *state,
                                                              gpointer          user_data);
static void     ev_window_run_presentation                   (EvWindow         *window);
static void     ev_window_stop_presentation                  (EvWindow         *window,
                                                              gboolean          unfullscreen_window);
static void     ev_window_cmd_view_presentation              (GSimpleAction    *action,
                                                              GVariant         *state,
                                                              gpointer          user_data);
static void     ev_view_popup_cmd_open_link                  (GSimpleAction    *action,
                                                              GVariant         *parameter,
                                                              gpointer          user_data);
static void     ev_view_popup_cmd_open_link_new_window       (GSimpleAction    *action,
                                                              GVariant         *parameter,
                                                              gpointer          user_data);
static void     ev_view_popup_cmd_copy_link_address          (GSimpleAction    *action,
                                                              GVariant         *parameter,
                                                              gpointer          user_data);
static void     ev_view_popup_cmd_save_image_as              (GSimpleAction    *action,
                                                              GVariant         *parameter,
                                                              gpointer          user_data);
static void     ev_view_popup_cmd_copy_image                 (GSimpleAction    *action,
                                                              GVariant         *parameter,
                                                              gpointer          user_data);
static void     ev_view_popup_cmd_remove_annotation          (GSimpleAction    *action,
                                                              GVariant         *parameter,
                                                              gpointer          user_data);
static void     ev_view_popup_cmd_annot_properties           (GSimpleAction    *action,
                                                              GVariant         *parameter,
                                                              gpointer          user_data);
static void    ev_attachment_popup_cmd_open_attachment       (GSimpleAction    *action,
                                                              GVariant         *parameter,
                                                              gpointer          user_data);
static void    ev_attachment_popup_cmd_save_attachment_as    (GSimpleAction    *action,
                                                              GVariant         *parameter,
                                                              gpointer          user_data);
static void    ev_window_cmd_view_best_fit                   (GSimpleAction    *action,
                                                              GVariant         *state,
                                                              gpointer          user_data);
static void    ev_window_cmd_view_page_width                 (GSimpleAction    *action,
                                                              GVariant         *state,
                                                              gpointer          user_data);
static void    ev_window_cmd_view_expand_window              (GSimpleAction    *action,
                                                              GVariant         *parameter,
                                                              gpointer          user_data);
static void     ev_window_update_find_status_message         (EvWindow         *ev_window);
static void     ev_window_cmd_edit_find                      (GSimpleAction    *action,
                                                              GVariant         *parameter,
                                                              gpointer          user_data);
static void     find_bar_search_changed_cb                   (EggFindBar       *find_bar,
                                                              GParamSpec       *param,
                                                              EvWindow         *ev_window);
static void     view_external_link_cb                        (EvWindow         *window,
                                                              EvLinkAction     *action);
static void     ev_window_load_file_remote                   (EvWindow         *ev_window,
                                                              GFile            *source_file);
// ev_window_update_max_min_scale removed

#ifdef ENABLE_DBUS
static void     ev_window_emit_closed                        (EvWindow         *window);
static void     ev_window_emit_doc_loaded                    (EvWindow         *window);
#endif
static void     ev_window_save_remote                        (EvWindow         *ev_window,
                                                              EvSaveType        type,
                                                              GFile            *source_file,
                                                              GFile            *target_file);
static const gchar *ev_window_sidebar_get_current_page_id    (EvWindow         *ev_window);
static void     ev_window_set_document                       (EvWindow         *ev_window,
                                                              EvDocument       *document);
static void     ev_window_update_max_min_scale               (EvWindow         *window);
static void     ev_window_cmd_focus_page_selector            (GSimpleAction    *action,
                                                              GVariant         *parameter,
                                                              gpointer          user_data);
static void     ev_window_cmd_save_as_response_cb            (GtkDialog        *dialog,
                                                              gint              response_id,
                                                              gpointer          user_data);
static void     ev_window_check_document_modified_response_cb (GtkDialog        *dialog,
                                                              gint              response_id,
                                                              gpointer          user_data);
static void     ev_view_popup_cmd_annot_properties_response_cb (GtkDialog       *dialog,
                                                              gint              response_id,
                                                              gpointer          user_data);
static void     ev_window_setup_bookmarks                    (EvWindow         *window);

#define MIN_SCALE 0.05409
#define MAX_SCALE 4.0

static void    zoom_control_changed_cb                       (EvZoomAction *action,
                                                              float           zoom,
                                                              EvWindow       *ev_window);

static gboolean ev_window_close                              (EvWindow         *window);
G_DEFINE_TYPE_WITH_PRIVATE (EvWindow, ev_window, GTK_TYPE_APPLICATION_WINDOW)


static gchar *
sizing_mode_to_string (EvSizingMode mode)
{
    switch (mode) {
        case EV_SIZING_BEST_FIT:
            return "best-fit";
        case EV_SIZING_FIT_WIDTH:
            return "fit-width";
        case EV_SIZING_FREE:
            return "free";
	default:
	    g_assert_not_reached();
    }
}

static gint
sizing_mode_string_to_int (const gchar *string)
{
    if (g_strcmp0 (string, "best-fit") == 0){
        return 0;
    } else if (g_strcmp0 (string, "fit-width") == 0) {
        return 1;
    } else if (g_strcmp0 (string, "free") == 0) {
        return 2;
    }

    return 0;
}

static gdouble
get_screen_dpi (EvWindow *window)
{
    return ev_document_misc_get_screen_dpi_at_window (GTK_WINDOW(window));
}

static void
ev_window_set_action_sensitive (EvWindow   *ev_window,
                                const char *name,
                                gboolean    sensitive)
{
    GAction *action = g_action_map_lookup_action (G_ACTION_MAP (ev_window), name);
    if (action && G_IS_SIMPLE_ACTION (action))
        g_simple_action_set_enabled (G_SIMPLE_ACTION (action), sensitive);
}

static void
ev_window_setup_action_sensitivity (EvWindow *ev_window)
{
    EvDocument *document = ev_window->priv->document;
    const EvDocumentInfo *info = NULL;
    gboolean has_document = FALSE;
    gboolean ok_to_print = TRUE;
    gboolean ok_to_copy = TRUE;
    gboolean has_properties = TRUE;
    gboolean override_restrictions = TRUE;
    gboolean can_get_text = FALSE;
    gboolean has_pages = FALSE;
    gboolean can_find = FALSE;

    if (document) {
        has_document = TRUE;
        has_pages = ev_document_get_n_pages (document) > 0;
        info = ev_document_get_info (document);
    }

    if (!info || info->fields_mask == 0) {
        has_properties = FALSE;
    }

    if (has_document && EV_IS_SELECTION (document)) {
        can_get_text = TRUE;
    }
    else if (has_document && document->iswebdocument) {
        can_get_text = TRUE;
    }
    if (has_pages && EV_IS_DOCUMENT_FIND (document)) {
        can_find = TRUE;
    }

    if (has_document && ev_window->priv->settings) {
        override_restrictions =
                g_settings_get_boolean (ev_window->priv->settings,
                        GS_OVERRIDE_RESTRICTIONS);
    }

    if (!override_restrictions && info && info->fields_mask & EV_DOCUMENT_INFO_PERMISSIONS) {
        ok_to_print = (info->permissions & EV_DOCUMENT_PERMISSIONS_OK_TO_PRINT);
        ok_to_copy = (info->permissions & EV_DOCUMENT_PERMISSIONS_OK_TO_COPY);
    }

    if (has_document && !ev_print_operation_exists_for_document(document))
        ok_to_print = FALSE;

    /* File menu */
    ev_window_set_action_sensitive (ev_window, "FileOpenCopy", has_document);
    ev_window_set_action_sensitive (ev_window, "FileSaveAs", has_document && ok_to_copy);
    ev_window_set_action_sensitive (ev_window, "FilePrint", has_pages && ok_to_print);
    ev_window_set_action_sensitive (ev_window, "FileProperties", has_document && has_properties);

    /* Edit menu */
    ev_window_set_action_sensitive (ev_window, "EditSelectAll", has_pages && can_get_text);
    ev_window_set_action_sensitive (ev_window, "EditFind", can_find);
    ev_window_set_action_sensitive (ev_window, "Slash", can_find);
    ev_window_set_action_sensitive (ev_window, "EditRotateLeft", has_pages && !(document->iswebdocument));
    ev_window_set_action_sensitive (ev_window, "EditRotateRight", has_pages && !(document->iswebdocument));

    /* View menu */
    /*If it has pages it is a document, so our check for a webdocument won't lead to a crash. We need to switch these view modes off since more than one
     *webview is hard to manage, and would lead to unexpected behaviour in case the number of webviews gets too large.
     */
    ev_window_set_action_sensitive (ev_window, "ViewContinuous", has_pages && !(document->iswebdocument));
    ev_window_set_action_sensitive (ev_window, "ViewDual", has_pages && !(document->iswebdocument));
    ev_window_set_action_sensitive (ev_window, "ViewDualOddLeft", has_pages);
    ev_window_set_action_sensitive (ev_window, "ViewRtl", has_pages);
    ev_window_set_action_sensitive (ev_window, "ViewBestFit", has_pages && !(document->iswebdocument));
    ev_window_set_action_sensitive (ev_window, "ViewPageWidth", has_pages && !(document->iswebdocument));
    ev_window_set_action_sensitive (ev_window, "ViewReload", has_pages);
    ev_window_set_action_sensitive (ev_window, "ViewAutoscroll", has_pages && !(document->iswebdocument));
    ev_window_set_action_sensitive (ev_window, "ViewInvertedColors", has_pages);
    ev_window_set_action_sensitive (ev_window, "ViewExpandWindow", has_pages && !(document->iswebdocument));
    ev_window_set_action_sensitive (ev_window, "ViewZoomIn", has_pages && !(document->iswebdocument));
    ev_window_set_action_sensitive (ev_window, "ViewZoomOut", has_pages && !(document->iswebdocument));
    ev_window_set_action_sensitive (ev_window, "ViewZoomReset", has_pages && !(document->iswebdocument));
    ev_window_set_action_sensitive (ev_window, "ViewPresentation", has_pages && !(document->iswebdocument));

    /* Bookmarks menu */
    ev_window_set_action_sensitive (ev_window, "BookmarksAdd", has_pages && ev_window->priv->bookmarks);

    /* Toolbar-specific actions: */
    ev_window_set_action_sensitive (ev_window, PAGE_SELECTOR_ACTION, has_pages);
    ev_toolbar_set_preset_sensitivity (EV_TOOLBAR (ev_window->priv->toolbar), has_pages && !(document->iswebdocument));

    ev_window_update_actions (ev_window);
}

static void
ev_window_update_actions (EvWindow *ev_window)
{
#if ENABLE_EPUB
    EvWebView *webview = NULL;
#endif
    EvView *view = NULL;

    int n_pages = 0, page = -1;
    gboolean has_pages = FALSE;
    gboolean presentation_mode;
    gboolean can_find_in_page = FALSE;

    if (ev_window->priv->document) {
        page = ev_document_model_get_page (ev_window->priv->model);
        n_pages = ev_document_get_n_pages (ev_window->priv->document);
        has_pages = n_pages > 0;
    }
#if ENABLE_EPUB
    if (ev_window->priv->document && ev_window->priv->document->iswebdocument == TRUE ) {
        webview = EV_WEB_VIEW(ev_window->priv->webview);
    } else
#endif
    {
        view = EV_VIEW (ev_window->priv->view);
    }
    can_find_in_page = (ev_window->priv->find_job && ev_job_find_has_results (EV_JOB_FIND (ev_window->priv->find_job)));
    if (view) {
        ev_window_set_action_sensitive (ev_window, "EditCopy", has_pages && ev_view_get_has_selection (view));
    }
#if ENABLE_EPUB
    else if (webview) {
        /*
         * The webkit2 function for this is an asynchronous call,
         * so our only option is to set this to always on, and we'll take care of whether we can copy
         * or not when this command is actually given.
         */
        ev_window_set_action_sensitive (ev_window,"EditCopy", has_pages);
    }
#endif
    ev_window_set_action_sensitive (ev_window, "EditFindNext", has_pages && can_find_in_page);
    ev_window_set_action_sensitive (ev_window, "EditFindPrevious", has_pages && can_find_in_page);
    ev_window_set_action_sensitive (ev_window, "F3", has_pages && can_find_in_page);

    presentation_mode = EV_WINDOW_IS_PRESENTATION (ev_window);

    if (ev_window->priv->document && ev_window->priv->document->iswebdocument == FALSE ) {
        ev_window_set_action_sensitive (ev_window, "ViewZoomIn",
                                        has_pages && ev_view_can_zoom_in (view) && !presentation_mode);
        ev_window_set_action_sensitive (ev_window, "ViewZoomOut",
                                        has_pages && ev_view_can_zoom_out (view) && !presentation_mode);
        ev_window_set_action_sensitive (ev_window, "ViewZoomReset", has_pages && !presentation_mode);
    }
    /* Go menu */
    if (has_pages) {
        ev_window_set_action_sensitive (ev_window, "GoPreviousPage", page > 0);
        ev_window_set_action_sensitive (ev_window, "GoNextPage", page < n_pages - 1);
        ev_window_set_action_sensitive (ev_window, "GoFirstPage", page > 0);
        ev_window_set_action_sensitive (ev_window, "GoLastPage", page < n_pages - 1);
        ev_window_set_action_sensitive (ev_window, "GoPreviousHistory",
                                        ev_history_can_go_back (ev_window->priv->history));
        ev_window_set_action_sensitive (ev_window, "GoNextHistory",
                                        ev_history_can_go_forward (ev_window->priv->history));
    } else {
        ev_window_set_action_sensitive (ev_window, "GoFirstPage", FALSE);
        ev_window_set_action_sensitive (ev_window, "GoPreviousPage", FALSE);
        ev_window_set_action_sensitive (ev_window, "GoNextPage", FALSE);
        ev_window_set_action_sensitive (ev_window, "GoLastPage", FALSE);
        ev_window_set_action_sensitive (ev_window, "GoPreviousHistory", FALSE);
        ev_window_set_action_sensitive (ev_window, "GoNextHistory", FALSE);
    }
}



static void
set_widget_visibility (GtkWidget *widget,
                       gboolean visible)
{
    if (widget == NULL)
        return;

    g_assert (GTK_IS_WIDGET (widget));

    if (visible)
        gtk_widget_show (widget);
    else
        gtk_widget_hide (widget);
}

static void
update_chrome_visibility (EvWindow *window)
{
    EvWindowPrivate *priv = window->priv;
    gboolean menubar, toolbar, findbar, fullscreen_toolbar, sidebar;
    gboolean fullscreen_mode, presentation, fullscreen;

    presentation = EV_WINDOW_IS_PRESENTATION (window);
    fullscreen = ev_document_model_get_fullscreen (priv->model);
    fullscreen_mode = fullscreen || presentation;

    menubar = (priv->chrome & EV_CHROME_MENUBAR) != 0 && !fullscreen_mode;
    toolbar = ((priv->chrome & EV_CHROME_TOOLBAR) != 0  ||
                  (priv->chrome & EV_CHROME_RAISE_TOOLBAR) != 0) && !presentation && !fullscreen;
    fullscreen_toolbar = ((priv->chrome & EV_CHROME_FULLSCREEN_TOOLBAR) != 0 ||
                            (priv->chrome & EV_CHROME_RAISE_TOOLBAR) != 0) && fullscreen;
    findbar = (priv->chrome & EV_CHROME_FINDBAR) != 0;
    sidebar = (priv->chrome & EV_CHROME_SIDEBAR) != 0 && priv->document && !presentation;

    set_widget_visibility (priv->menubar, menubar);
    set_widget_visibility (priv->find_bar, findbar);
    set_widget_visibility (priv->sidebar, sidebar);
    gtk_revealer_set_reveal_child (GTK_REVEALER (priv->toolbar_revealer), toolbar);

    ev_toolbar_set_style (EV_TOOLBAR (priv->toolbar), fullscreen_toolbar);
}

static void
update_chrome_flag (EvWindow *window,
                    EvChrome flag,
                    gboolean active)
{
    EvWindowPrivate *priv = window->priv;

    if (active) {
        priv->chrome |= flag;
    } else {
        priv->chrome &= ~flag;
    }
}

/* update_sizing_buttons removed */

/* update_chrome_actions removed */

/**
 * ev_window_is_empty:
 * @ev_window: The instance of the #EvWindow.
 *
 * It does look if there is any document loaded or if there is any job to load
 * a document.
 *
 * Returns: %TRUE if there isn't any document loaded or any any documente to be
 *          loaded, %FALSE in other case.
 */
gboolean
ev_window_is_empty (const EvWindow *ev_window)
{
    g_return_val_if_fail (EV_IS_WINDOW (ev_window), FALSE);

    return (ev_window->priv->document == NULL) && (ev_window->priv->load_job == NULL);
}

static void
ev_window_set_message_area (EvWindow  *window,
                            GtkWidget *area)
{
    if (window->priv->message_area == area)
        return;

    if (window->priv->message_area)
        gtk_box_remove (GTK_BOX (window->priv->view_box), window->priv->message_area);
    window->priv->message_area = area;

    if (!area)
        return;

    gtk_box_prepend (GTK_BOX (window->priv->view_box), window->priv->message_area);
    g_object_add_weak_pointer (G_OBJECT (window->priv->message_area), (gpointer) &(window->priv->message_area));
}

static void
ev_window_message_area_response_cb (EvMessageArea *area,
                                    gint           response_id,
                                    EvWindow      *window)
{
    ev_window_set_message_area (window, NULL);
}

static void
ev_window_error_message (EvWindow    *window,
                         GError      *error,
                         const gchar *format,
                         ...)
{
    GtkWidget *area;
    va_list    args;
    gchar     *msg = NULL;

    if (window->priv->message_area)
        return;

    va_start (args, format);
    msg = g_strdup_vprintf (format, args);
    va_end (args);

    area = ev_message_area_new (GTK_MESSAGE_ERROR,
                                msg,
                                "window-close-symbolic",
                                GTK_RESPONSE_CLOSE,
                                NULL);
    g_free (msg);

    if (error)
        ev_message_area_set_secondary_text (EV_MESSAGE_AREA (area), error->message);
    g_signal_connect (area, "response", G_CALLBACK (ev_window_message_area_response_cb), window);
    gtk_widget_show (area);
    ev_window_set_message_area (window, area);
}

static void
ev_window_warning_message (EvWindow    *window,
                           const gchar *format,
                           ...)
{
    GtkWidget *area;
    va_list    args;
    gchar     *msg = NULL;

    if (window->priv->message_area)
        return;

    va_start (args, format);
    msg = g_strdup_vprintf (format, args);
    va_end (args);

    area = ev_message_area_new (GTK_MESSAGE_WARNING,
                                msg,
                                "window-close-symbolic",
                                GTK_RESPONSE_CLOSE,
                                NULL);
    g_free (msg);
    g_signal_connect (area, "response", G_CALLBACK (ev_window_message_area_response_cb), window);
    gtk_widget_show (area);
    ev_window_set_message_area (window, area);
}

typedef struct _LinkTitleData {
    EvLink      *link;
    const gchar *link_title;
} LinkTitleData;

/* ev_window_page_changed_cb removed */

static const gchar *
ev_window_sidebar_get_current_page_id (EvWindow *ev_window)
{
    GtkWidget   *current_page;
    const gchar *id;

    g_object_get (ev_window->priv->sidebar, "current_page", &current_page, NULL);

    if (current_page == ev_window->priv->sidebar_links) {
        id = LINKS_SIDEBAR_ID;
    } else if (current_page == ev_window->priv->sidebar_thumbs) {
        id = THUMBNAILS_SIDEBAR_ID;
    } else if (current_page == ev_window->priv->sidebar_attachments) {
        id = ATTACHMENTS_SIDEBAR_ID;
    } else if (current_page == ev_window->priv->sidebar_layers) {
        id = LAYERS_SIDEBAR_ID;
    } else if (current_page == ev_window->priv->sidebar_annots) {
        id = ANNOTS_SIDEBAR_ID;
    } else if (current_page == ev_window->priv->sidebar_bookmarks) {
        id = BOOKMARKS_SIDEBAR_ID;
    } else {
        g_assert_not_reached();
    }

    g_object_unref (current_page);

    return id;
}

static void
ev_window_sidebar_set_current_page (EvWindow    *window,
                                    const gchar *page_id)
{
    EvDocument *document = window->priv->document;
    EvSidebar  *sidebar = EV_SIDEBAR (window->priv->sidebar);
    GtkWidget  *links = window->priv->sidebar_links;
    GtkWidget  *thumbs = window->priv->sidebar_thumbs;
    GtkWidget  *attachments = window->priv->sidebar_attachments;
    GtkWidget  *annots = window->priv->sidebar_annots;
    GtkWidget  *layers = window->priv->sidebar_layers;
    GtkWidget  *bookmarks = window->priv->sidebar_bookmarks;

    if (strcmp (page_id, LINKS_SIDEBAR_ID) == 0 &&
            ev_sidebar_page_support_document (EV_SIDEBAR_PAGE (links), document)) {
        ev_sidebar_set_page (sidebar, links);
    } else if (strcmp (page_id, THUMBNAILS_SIDEBAR_ID) == 0 &&
            ev_sidebar_page_support_document (EV_SIDEBAR_PAGE (thumbs), document)) {
        ev_sidebar_set_page (sidebar, thumbs);
    } else if (strcmp (page_id, ATTACHMENTS_SIDEBAR_ID) == 0 &&
            ev_sidebar_page_support_document (EV_SIDEBAR_PAGE (attachments), document)) {
        ev_sidebar_set_page (sidebar, attachments);
    } else if (strcmp (page_id, LAYERS_SIDEBAR_ID) == 0 &&
            ev_sidebar_page_support_document (EV_SIDEBAR_PAGE (layers), document)) {
        ev_sidebar_set_page (sidebar, layers);
    } else if (strcmp (page_id, ANNOTS_SIDEBAR_ID) == 0 &&
            ev_sidebar_page_support_document (EV_SIDEBAR_PAGE (annots), document)) {
        ev_sidebar_set_page (sidebar, annots);
    } else if (strcmp (page_id, BOOKMARKS_SIDEBAR_ID) == 0 &&
            ev_sidebar_page_support_document (EV_SIDEBAR_PAGE (bookmarks), document)) {
        ev_sidebar_set_page (sidebar, bookmarks);
    }
}

static void
update_document_mode (EvWindow *window,
                      EvDocumentMode mode)
{
    if (mode == EV_DOCUMENT_MODE_PRESENTATION) {
        if (window->priv->document) {
            if (window->priv->document->iswebdocument) {
				ev_window_warning_message(window,_("Cannot enter presentation mode with ePub documents, use fullscreen mode instead."));
                return;
            }
        }
        ev_window_run_presentation (window);
    }
    else if (mode == EV_DOCUMENT_MODE_FULL_SCREEN) {
        ev_window_run_fullscreen (window);
    }
}

static void
ev_window_init_metadata_with_default_values (EvWindow *window)
{
    GSettings  *settings = window->priv->default_settings;
    EvMetadata *metadata = window->priv->metadata;

    /* Chrome */
    if (!ev_metadata_has_key (metadata, "sidebar_visibility")) {
        ev_metadata_set_boolean (metadata, "sidebar_visibility",
                g_settings_get_boolean (settings, "show-sidebar"));
    }

    /* Sidebar */
    if (!ev_metadata_has_key (metadata, "sidebar_size")) {
        ev_metadata_set_int (metadata, "sidebar_size",
                g_settings_get_int (settings, "sidebar-size"));
    }
    if (!ev_metadata_has_key (metadata, "thumbnails_size")) {
        ev_metadata_set_int (metadata, "thumbnails_size",
                g_settings_get_int (settings, "thumbnails-size"));
    }
    if (!ev_metadata_has_key (metadata, "sidebar_page")) {
        gchar *sidebar_page_id = g_settings_get_string (settings, "sidebar-page");

        ev_metadata_set_string (metadata, "sidebar_page", sidebar_page_id);
        g_free (sidebar_page_id);
    }

    /* Document model */
    if (!ev_metadata_has_key (metadata, "continuous")) {
        ev_metadata_set_boolean (metadata, "continuous", g_settings_get_boolean (settings, "continuous"));
    }
    if (!ev_metadata_has_key (metadata, "dual-page")) {
        ev_metadata_set_boolean (metadata, "dual-page", g_settings_get_boolean (settings, "dual-page"));
    }
    if (!ev_metadata_has_key (metadata, "dual-page-odd-left")) {
        ev_metadata_set_boolean (metadata, "dual-page-odd-left",
                                 g_settings_get_boolean (settings, "dual-page-odd-left"));
    }
    if (!ev_metadata_has_key (metadata, "rtl")) {
        ev_metadata_set_boolean (metadata, "rtl",
               gtk_widget_get_default_direction () == GTK_TEXT_DIR_RTL ? TRUE : FALSE);
    }
    if (!ev_metadata_has_key (metadata, "inverted-colors")) {
        ev_metadata_set_boolean (metadata, "inverted-colors", g_settings_get_boolean (settings, "inverted-colors"));
    }
    if (!ev_metadata_has_key (metadata, "sizing_mode")) {
        EvSizingMode mode = g_settings_get_enum (settings, "sizing-mode");

        ev_metadata_set_string (metadata, "sizing_mode", sizing_mode_to_string (mode));
    }

    if (!ev_metadata_has_key (metadata, "zoom")) {
        ev_metadata_set_double (metadata, "zoom", g_settings_get_double (settings, "zoom"));
    }

    if (!ev_metadata_has_key (metadata, "fullscreen")) {
        ev_metadata_set_boolean (metadata, "fullscreen", g_settings_get_boolean (settings, "fullscreen"));
    }
    if (!ev_metadata_has_key (metadata, "window_maximized")) {
        ev_metadata_set_boolean (metadata, "window_maximized", g_settings_get_boolean (settings, "window-maximized"));
    }
}

static void
setup_chrome_from_metadata (EvWindow *window)
{
    gboolean show_sidebar;

    if (!window->priv->metadata)
        return;

    if (ev_metadata_get_boolean (window->priv->metadata, "sidebar_visibility", &show_sidebar))
        update_chrome_flag (window, EV_CHROME_SIDEBAR, show_sidebar);

    update_chrome_visibility (window);
}

static void
setup_sidebar_from_metadata (EvWindow *window)
{
    gchar *page_id;
    gint   sidebar_size;
    gint   thumbnails_size;

    if (!window->priv->metadata)
        return;

    if (ev_metadata_get_int (window->priv->metadata, "sidebar_size", &sidebar_size))
        gtk_paned_set_position (GTK_PANED (window->priv->hpaned), sidebar_size);

    if (ev_metadata_get_int (window->priv->metadata, "thumbnails_size", &thumbnails_size))
        ev_sidebar_thumbnails_set_size (EV_SIDEBAR_THUMBNAILS (window->priv->sidebar_thumbs), thumbnails_size);

    if (ev_metadata_get_string (window->priv->metadata, "sidebar_page", &page_id))
        ev_window_sidebar_set_current_page (window, page_id);
}

static void
setup_model_from_metadata (EvWindow *window)
{
    gint     page;
    gchar   *sizing_mode;
    gdouble  zoom;
    gint     rotation;
    gboolean inverted_colors = FALSE;
    gboolean continuous = FALSE;
    gboolean dual_page = FALSE;
    gboolean dual_page_odd_left = FALSE;
    gboolean rtl = FALSE;
    gboolean fullscreen = FALSE;

    if (!window->priv->metadata)
        return;

    /* Current page */
    if (!window->priv->dest &&
            ev_metadata_get_int (window->priv->metadata, "page", &page)) {
        ev_document_model_set_page (window->priv->model, page);
    }

    /* Sizing mode */
    if (ev_metadata_get_string (window->priv->metadata, "sizing_mode", &sizing_mode)) {
        ev_document_model_set_sizing_mode (window->priv->model, sizing_mode_string_to_int (sizing_mode));
    }

    /* Zoom */
    if (ev_document_model_get_sizing_mode (window->priv->model) == EV_SIZING_FREE) {
        if (ev_metadata_get_double (window->priv->metadata, "zoom", &zoom)) {
            zoom *= get_screen_dpi (window) / 72.0;
            ev_document_model_set_scale (window->priv->model, zoom);
        }
    }

    /* Rotation */
    if (ev_metadata_get_int (window->priv->metadata, "rotation", &rotation)) {
        switch (rotation) {
        case 90:
            rotation = 90;
            break;
        case 180:
            rotation = 180;
            break;
        case 270:
            rotation = 270;
            break;
        default:
            rotation = 0;
            break;
        }
        ev_document_model_set_rotation (window->priv->model, rotation);
    }

    /* Inverted Colors */
    if (ev_metadata_get_boolean (window->priv->metadata, "inverted-colors", &inverted_colors)) {
        ev_document_model_set_inverted_colors (window->priv->model, inverted_colors);
    }

    /* Continuous */
    if (ev_metadata_get_boolean (window->priv->metadata, "continuous", &continuous)) {
        ev_document_model_set_continuous (window->priv->model, continuous);
    }

    /* Dual page */
    if (ev_metadata_get_boolean (window->priv->metadata, "dual-page", &dual_page)) {
        ev_document_model_set_dual_page (window->priv->model, dual_page);
    }

    /* Dual page odd pages left */
    if (ev_metadata_get_boolean (window->priv->metadata, "dual-page-odd-left", &dual_page_odd_left)) {
        ev_document_model_set_dual_page_odd_pages_left (window->priv->model, dual_page_odd_left);
    }

    /* Right to left document */
    if (ev_metadata_get_boolean (window->priv->metadata, "rtl", &rtl)) {
      ev_document_model_set_rtl (window->priv->model, rtl);
    }

    /* Fullscreen */
    if (ev_metadata_get_boolean (window->priv->metadata, "fullscreen", &fullscreen)) {
        if (fullscreen)
            ev_window_run_fullscreen (window);
    }
}

static void
setup_document_from_metadata (EvWindow *window)
{
    gint    width;
    gint    height;
    gdouble width_ratio;
    gdouble height_ratio;

    if (!window->priv->metadata)
        return;

    setup_sidebar_from_metadata (window);

    if (ev_metadata_get_int (window->priv->metadata, "window_width", &width) &&
            ev_metadata_get_int (window->priv->metadata, "window_height", &height))
        return; /* size was already set in setup_size_from_metadata */

    g_settings_get (window->priv->default_settings, "window-ratio", "(dd)", &width_ratio, &height_ratio);
    if (width_ratio > 0. && height_ratio > 0.) {
        gdouble    document_width;
        gdouble    document_height;
        gint       request_width;
        gint       request_height;

        ev_document_get_max_page_size (window->priv->document, &document_width, &document_height);

        request_width = (gint)(width_ratio * document_width + 0.5);
        request_height = (gint)(height_ratio * document_height + 0.5);

        if (request_width > 0 && request_height > 0) {
            gtk_window_set_default_size (GTK_WINDOW (window), request_width, request_height);
        }
    }
}

static void
setup_size_from_metadata (EvWindow *window)
{
    gint     width;
    gint     height;
    gboolean maximized;
    gint     x;
    gint     y;

    if (!window->priv->metadata)
        return;

    if (ev_metadata_get_boolean (window->priv->metadata, "window_maximized", &maximized)) {
        if (maximized) {
            gtk_window_maximize (GTK_WINDOW (window));
            return;
        } else {
            gtk_window_unmaximize (GTK_WINDOW (window));
        }
    }

    if (ev_metadata_get_int (window->priv->metadata, "window_x", &x) &&
            ev_metadata_get_int (window->priv->metadata, "window_y", &y)) {
        /* gtk_window_move removed in GTK4 */
        /* gtk_window_move (GTK_WINDOW (window), x, y); */
    }

    if (ev_metadata_get_int (window->priv->metadata, "window_width", &width) &&
            ev_metadata_get_int (window->priv->metadata, "window_height", &height)) {
        gtk_window_set_default_size (GTK_WINDOW (window), width, height);
    }
}

static void
setup_view_from_metadata (EvWindow *window)
{
    gboolean presentation;

    if (!window->priv->metadata)
        return;

    /* Presentation */
    if (ev_metadata_get_boolean (window->priv->metadata, "presentation", &presentation)) {
        if (presentation) {
            if (window->priv->document->iswebdocument == TRUE ) {
                return;
            }
            else {
                ev_window_run_presentation (window);
            }
        }
    }
}

static void
page_cache_size_changed (GSettings *settings,
                         gchar     *key,
                         EvWindow  *ev_window)
{
    guint page_cache_mb;

    page_cache_mb = g_settings_get_uint (settings, GS_PAGE_CACHE_SIZE);
    ev_view_set_page_cache_size (EV_VIEW (ev_window->priv->view), page_cache_mb * 1024 * 1024);
}

/* ev_window_setup_default removed */


static void
ev_window_clear_thumbnail_job (EvWindow *ev_window)
{
    if (ev_window->priv->thumbnail_job != NULL) {
        if (!ev_job_is_finished (ev_window->priv->thumbnail_job))
            ev_job_cancel (ev_window->priv->thumbnail_job);

        g_signal_handlers_disconnect_by_func (ev_window->priv->thumbnail_job,
                                              ev_window_set_icon_from_thumbnail,
                                              ev_window);
        g_object_unref (ev_window->priv->thumbnail_job);
        ev_window->priv->thumbnail_job = NULL;
    }
}

static void
ev_window_set_icon_from_thumbnail (EvJobThumbnail *job,
                                   EvWindow       *ev_window)
{
    if (job->thumbnail) {
        if (ev_document_model_get_inverted_colors (ev_window->priv->model))
            ev_document_misc_invert_pixbuf (job->thumbnail);
        /* gtk_window_set_icon removed in GTK4 */
        /* gtk_window_set_icon (GTK_WINDOW (ev_window), job->thumbnail); */
    }

    ev_window_clear_thumbnail_job (ev_window);
}

static void
ev_window_refresh_window_thumbnail (EvWindow *ev_window)
{
    gdouble page_width;
    gdouble scale;
    gint rotation;
    EvDocument *document = ev_window->priv->document;

    if (!document || ev_document_get_n_pages (document) <= 0 ||
            ev_document_get_n_pages (document) <= 0 ||
            !ev_document_check_dimensions (document) ||
            document->iswebdocument) {
        return;
    }

    ev_window_clear_thumbnail_job (ev_window);

    ev_document_get_page_size (document, 0, &page_width, NULL);
    scale = 128. / page_width;
    rotation = ev_document_model_get_rotation (ev_window->priv->model);

    ev_window->priv->thumbnail_job = ev_job_thumbnail_new (document, 0, rotation, scale);

    if (document->iswebdocument) {
        ev_job_set_run_mode(EV_JOB(ev_window->priv->thumbnail_job), EV_JOB_RUN_MAIN_LOOP);
    }
    g_signal_connect (ev_window->priv->thumbnail_job, "finished",
                      G_CALLBACK (ev_window_set_icon_from_thumbnail),
                      ev_window);
    ev_job_scheduler_push_job (ev_window->priv->thumbnail_job, EV_JOB_PRIORITY_NONE);
}

static void
override_restrictions_changed (GSettings *settings,
                               gchar     *key,
                               EvWindow  *ev_window)
{
    ev_window_setup_action_sensitivity (ev_window);
}

static GSettings *
ev_window_ensure_settings (EvWindow *ev_window)
{
    EvWindowPrivate *priv = ev_window->priv;

    if (priv->settings != NULL)
        return priv->settings;

    priv->settings = g_settings_new (GS_SCHEMA_NAME);
    g_signal_connect (priv->settings, "changed::"GS_OVERRIDE_RESTRICTIONS,
                      G_CALLBACK (override_restrictions_changed), ev_window);
    g_signal_connect (priv->settings, "changed::"GS_PAGE_CACHE_SIZE,
                      G_CALLBACK (page_cache_size_changed), ev_window);

    return priv->settings;
}

static gboolean
ev_window_setup_document (EvWindow *ev_window)
{
    const EvDocumentInfo *info;
    EvDocument *document = ev_window->priv->document;

    ev_window->priv->setup_document_idle = 0;
    ev_window_refresh_window_thumbnail (ev_window);

    ev_window_set_page_mode (ev_window, PAGE_MODE_DOCUMENT);

    ev_window_title_set_document (ev_window->priv->title, document);
    ev_window_title_set_uri (ev_window->priv->title, ev_window->priv->uri);

    /* Update notebook tab label for the active tab */
    if (ev_window->priv->notebook && ev_window->priv->active_tab) {
        gchar *basename = g_filename_display_basename (ev_window->priv->uri);
        GtkWidget *tab_label = ev_window_create_tab_label (ev_window, basename,
                ev_window->priv->active_tab->view_box);
        GtkWidget *close_btn = gtk_widget_get_last_child (tab_label);
        g_signal_connect (close_btn, "clicked",
                G_CALLBACK (ev_window_tab_close_button_clicked), ev_window);
        gtk_notebook_set_tab_label (GTK_NOTEBOOK (ev_window->priv->notebook),
                ev_window->priv->active_tab->view_box, tab_label);
        g_free (basename);

        /* Sync document state to active tab struct */
        ev_window->priv->active_tab->document = ev_window->priv->document;
        ev_window->priv->active_tab->uri = ev_window->priv->uri;
        ev_window->priv->active_tab->metadata = ev_window->priv->metadata;
        ev_window->priv->active_tab->bookmarks = ev_window->priv->bookmarks;
        ev_window->priv->active_tab->page_mode = ev_window->priv->page_mode;
    }

    ev_window_ensure_settings (ev_window);
    ev_window_setup_action_sensitivity (ev_window);

    if (ev_window->priv->properties) {
        ev_properties_dialog_set_document (EV_PROPERTIES_DIALOG (ev_window->priv->properties),
                                           ev_window->priv->uri,
                                           ev_window->priv->document);
    }

    info = ev_document_get_info (document);
    update_document_mode (ev_window, info->mode);

    if (EV_IS_DOCUMENT_FIND (document)) {
        if (ev_window->priv->search_string && !EV_WINDOW_IS_PRESENTATION (ev_window)) {
            ev_window_cmd_edit_find (NULL, NULL, ev_window);
            egg_find_bar_set_search_string (EGG_FIND_BAR (ev_window->priv->find_bar), ev_window->priv->search_string);
        }

        g_clear_pointer (&ev_window->priv->search_string, g_free);
    }

    /*FIXME*/
    if (EV_WINDOW_IS_PRESENTATION (ev_window) && document->iswebdocument == FALSE)
        gtk_widget_grab_focus (ev_window->priv->presentation_view);
    else if (!gtk_widget_get_visible (ev_window->priv->find_bar)) {
        if ( document->iswebdocument == FALSE )
            gtk_widget_grab_focus (ev_window->priv->view);
#if ENABLE_EPUB
        else
            gtk_widget_grab_focus (ev_window->priv->webview);
#endif

    }
    return FALSE;
}

static void
ev_window_set_document (EvWindow   *ev_window,
                        EvDocument *document)
{
    if (ev_window->priv->document == document)
        return;

    if (ev_window->priv->document) {
        g_object_unref (ev_window->priv->document);
    }

    if (document) {
        ev_window->priv->document = g_object_ref (document);
        ev_sidebar_set_model (EV_SIDEBAR (ev_window->priv->sidebar),
                              ev_window->priv->model);
    } else {
        ev_window->priv->document = NULL;
    }
}

static void
ev_window_document_changed (EvWindow *ev_window,
                            gpointer  user_data)
{
    if (ev_window->priv->settings &&
            g_settings_get_boolean (ev_window->priv->settings, GS_AUTO_RELOAD))
        ev_window_reload_document (ev_window, NULL);
}

/* ev_window_password_view_unlock removed */

static void
ev_window_clear_load_job (EvWindow *ev_window)
{
    if (ev_window->priv->load_job != NULL) {
        if (!ev_job_is_finished (ev_window->priv->load_job))
            ev_job_cancel (ev_window->priv->load_job);

        g_signal_handlers_disconnect_by_func (ev_window->priv->load_job, ev_window_load_job_cb, ev_window);
        g_object_unref (ev_window->priv->load_job);
        ev_window->priv->load_job = NULL;
    }
}

static void
ev_window_clear_reload_job (EvWindow *ev_window)
{
    if (ev_window->priv->reload_job != NULL) {
        if (!ev_job_is_finished (ev_window->priv->reload_job))
            ev_job_cancel (ev_window->priv->reload_job);

        g_signal_handlers_disconnect_by_func (ev_window->priv->reload_job, ev_window_reload_job_cb, ev_window);
        g_object_unref (ev_window->priv->reload_job);
        ev_window->priv->reload_job = NULL;
    }
}

static void
ev_window_clear_local_uri (EvWindow *ev_window)
{
    if (ev_window->priv->local_uri) {
        ev_tmp_uri_unlink (ev_window->priv->local_uri);
        g_free (ev_window->priv->local_uri);
        ev_window->priv->local_uri = NULL;
    }
}

static void
ev_window_handle_link (EvWindow *ev_window,
                       EvLinkDest *dest)
{
    if (ev_window->priv->document->iswebdocument == TRUE ) {
        return;
    }
    if (dest) {
        EvLink *link;
        EvLinkAction *link_action;

        link_action = ev_link_action_new_dest (dest);
        link = ev_link_new (NULL, link_action);
        ev_view_handle_link (EV_VIEW (ev_window->priv->view), link);
        g_object_unref (link);
    }
}

/* This callback will executed when load job will be finished.
 *
 * Since the flow of the error dialog is very confusing, we assume that both
 * document and uri will go away after this function is called, and thus we need
 * to ref/dup them.  Additionally, it needs to clear
 * ev_window->priv->password_{uri,document}, and thus people who call this
 * function should _not_ necessarily expect those to exist after being
 * called. */
static void
ev_window_load_job_cb (EvJob *job,
                       gpointer data)
{
    EvWindow *ev_window = EV_WINDOW (data);
    EvDocument *document = EV_JOB (job)->document;
    EvJobLoad *job_load = EV_JOB_LOAD (job);

    g_assert (job_load->uri);

    ev_view_set_loading (EV_VIEW (ev_window->priv->view), FALSE);
    /* Success! */
    if (!ev_job_is_failed (job)) {
        ev_document_model_set_document (ev_window->priv->model, document);

#ifdef ENABLE_DBUS
        ev_window_emit_doc_loaded (ev_window);
#endif
        setup_chrome_from_metadata (ev_window);
        /* residue */
        setup_document_from_metadata (ev_window);
        setup_view_from_metadata (ev_window);
        ev_window_setup_document (ev_window);

        ev_window_add_recent (ev_window, ev_window->priv->uri);

        ev_window_title_set_type (ev_window->priv->title, EV_WINDOW_TITLE_DOCUMENT);
        if (job_load->password) {
            GPasswordSave flags;

            flags = ev_password_view_get_password_save_flags (EV_PASSWORD_VIEW (ev_window->priv->password_view));
            ev_keyring_save_password (ev_window->priv->uri, job_load->password, flags);
        }

        ev_window_handle_link (ev_window, ev_window->priv->dest);
        g_clear_object (&ev_window->priv->dest);

        switch (ev_window->priv->window_mode) {
        case EV_WINDOW_MODE_FULLSCREEN:
            ev_window_run_fullscreen (ev_window);
            break;
        case EV_WINDOW_MODE_PRESENTATION:
            ev_window_run_presentation (ev_window);
            break;
        default:
            break;
        }

        /* Create a monitor for the document */
        ev_window->priv->monitor = ev_file_monitor_new (ev_window->priv->uri);
        g_signal_connect_swapped (ev_window->priv->monitor, "changed",
                                  G_CALLBACK (ev_window_document_changed),
                                  ev_window);

        ev_window_clear_load_job (ev_window);
        return;
    }

    if (g_error_matches (job->error, EV_DOCUMENT_ERROR, EV_DOCUMENT_ERROR_ENCRYPTED)) {
        gchar *password;

        setup_view_from_metadata (ev_window);

        /* First look whether password is in keyring */
        password = ev_keyring_lookup_password (ev_window->priv->uri);
        if (password) {
            if (job_load->password && strcmp (password, job_load->password) == 0) {
                /* Password in kering is wrong */
                ev_job_load_set_password (job_load, NULL);
                /* FIXME: delete password from keyring? */
            } else {
                ev_job_load_set_password (job_load, password);
                ev_job_scheduler_push_job (job, EV_JOB_PRIORITY_NONE);
                g_free (password);
                return;
            }

            g_free (password);
        }

        /* We need to ask the user for a password */
        ev_window_title_set_uri (ev_window->priv->title,
                                 ev_window->priv->uri);
        ev_window_title_set_type (ev_window->priv->title,
                                  EV_WINDOW_TITLE_PASSWORD);

        ev_password_view_set_uri (EV_PASSWORD_VIEW (ev_window->priv->password_view),
                                  job_load->uri);

        ev_window_set_page_mode (ev_window, PAGE_MODE_PASSWORD);

        ev_job_load_set_password (job_load, NULL);
        ev_password_view_ask_password (EV_PASSWORD_VIEW (ev_window->priv->password_view));
    } else {
        ev_window_error_message (ev_window, job->error,
                                 "%s", _("Unable to open document"));
        ev_window_clear_load_job (ev_window);
    }
}

static void
ev_window_reload_job_cb (EvJob    *job,
                         EvWindow *ev_window)
{
    GtkWidget *widget;

    if (ev_job_is_failed (job)) {
        ev_window_clear_reload_job (ev_window);
        ev_window->priv->in_reload = FALSE;
        if (ev_window->priv->dest) {
            g_object_unref (ev_window->priv->dest);
            ev_window->priv->dest = NULL;
        }

        return;
    }

    ev_document_model_set_document (ev_window->priv->model,
                                    job->document);
    if (ev_window->priv->dest) {
        ev_window_handle_link (ev_window, ev_window->priv->dest);
        g_clear_object (&ev_window->priv->dest);
    }

    /* Restart the search after reloading */
    widget = gtk_window_get_focus (GTK_WINDOW (ev_window));
    if (widget && gtk_widget_get_ancestor (widget, EGG_TYPE_FIND_BAR)) {
        find_bar_search_changed_cb (EGG_FIND_BAR (ev_window->priv->find_bar),
                NULL, ev_window);
    }

    ev_window_clear_reload_job (ev_window);
    ev_window->priv->in_reload = FALSE;
}

/**
 * ev_window_get_uri:
 * @ev_window: The instance of the #EvWindow.
 *
 * It returns the uri of the document showed in the #EvWindow.
 *
 * Returns: the uri of the document showed in the #EvWindow.
 */
const char *
ev_window_get_uri (EvWindow *ev_window)
{
    return ev_window->priv->uri;
}

/**
 * ev_window_close_dialogs:
 * @ev_window: The window where dialogs will be closed.
 *
 * It looks for password, print and properties dialogs and closes them and
 * frees them from memory. If there is any print job it does free it too.
 */
static void
ev_window_close_dialogs (EvWindow *ev_window)
{
    if (ev_window->priv->print_dialog)
        gtk_window_destroy (GTK_WINDOW (ev_window->priv->print_dialog));
    ev_window->priv->print_dialog = NULL;

    if (ev_window->priv->properties)
        gtk_window_destroy (GTK_WINDOW (ev_window->priv->properties));
    ev_window->priv->properties = NULL;
}

static void
ev_window_clear_progress_idle (EvWindow *ev_window)
{
    if (ev_window->priv->progress_idle > 0)
        g_source_remove (ev_window->priv->progress_idle);
    ev_window->priv->progress_idle = 0;
}

static void
reset_progress_idle (EvWindow *ev_window)
{
    ev_window->priv->progress_idle = 0;
}

static void
ev_window_show_progress_message (EvWindow   *ev_window,
                                 guint       interval,
                                 GSourceFunc function)
{
    if (ev_window->priv->progress_idle > 0)
        g_source_remove (ev_window->priv->progress_idle);
    ev_window->priv->progress_idle =
            g_timeout_add_seconds_full (G_PRIORITY_DEFAULT,
                    interval, function,
                    ev_window,
                    (GDestroyNotify)reset_progress_idle);
}

static void
ev_window_reset_progress_cancellable (EvWindow *ev_window)
{
    if (ev_window->priv->progress_cancellable)
        g_cancellable_reset (ev_window->priv->progress_cancellable);
    else
        ev_window->priv->progress_cancellable = g_cancellable_new ();
}

static void
ev_window_progress_response_cb (EvProgressMessageArea *area,
                                gint                   response,
                                EvWindow              *ev_window)
{
    if (response == GTK_RESPONSE_CANCEL)
        g_cancellable_cancel (ev_window->priv->progress_cancellable);
    ev_window_set_message_area (ev_window, NULL);
}

static gboolean
show_loading_progress (EvWindow *ev_window)
{
    GtkWidget *area;
    gchar     *text;
    gchar       *display_name;

    if (ev_window->priv->message_area)
        return FALSE;

    text = g_uri_unescape_string (ev_window->priv->uri, NULL);
    display_name = g_markup_escape_text (text, -1);
    g_free (text);
    text = g_strdup_printf (_("Loading document from “%s”"),
            display_name);

    area = ev_progress_message_area_new ("document-open-symbolic",
            text,
            _("_Close"),
            GTK_RESPONSE_CLOSE,
            _("_Cancel"),
            GTK_RESPONSE_CANCEL,
            NULL);
    g_signal_connect (area, "response",
            G_CALLBACK (ev_window_progress_response_cb),
            ev_window);
    gtk_widget_show (area);
    ev_window_set_message_area (ev_window, area);

    g_free (text);
    g_free (display_name);

    return FALSE;
}

static void
ev_window_load_remote_failed (EvWindow *ev_window,
                              GError   *error)
{
    if ( !ev_window->priv->view ) return;

    ev_view_set_loading (EV_VIEW (ev_window->priv->view), FALSE);
    ev_window->priv->in_reload = FALSE;
    ev_window_error_message (ev_window, error,
            "%s", _("Unable to open document"));
    g_free (ev_window->priv->local_uri);
    ev_window->priv->local_uri = NULL;
    ev_window->priv->uri_mtime = 0;
}

static void
set_uri_mtime (GFile        *source,
               GAsyncResult *async_result,
               EvWindow     *ev_window)
{
    GFileInfo *info;
    GError *error = NULL;

    info = g_file_query_info_finish (source, async_result, &error);

    if (error) {
        ev_window->priv->uri_mtime = 0;
        g_error_free (error);
    } else {
        GTimeVal mtime;

        g_file_info_get_modification_time (info, &mtime);
        ev_window->priv->uri_mtime = mtime.tv_sec;
        g_object_unref (info);
    }

    g_object_unref (source);
}

static void
mount_volume_ready_cb (GFile        *source,
                       GAsyncResult *async_result,
                       EvWindow     *ev_window)
{
    GError *error = NULL;

    g_file_mount_enclosing_volume_finish (source, async_result, &error);

    if (error) {
        ev_window_load_remote_failed (ev_window, error);
        g_object_unref (source);
        g_error_free (error);
    } else {
        /* Volume successfully mounted,
           try opening the file again */
        ev_window_load_file_remote (ev_window, source);
    }
}

static void
window_open_file_copy_ready_cb (GFile        *source,
                                GAsyncResult *async_result,
                                EvWindow     *ev_window)
{
    GError *error = NULL;

    ev_window_clear_progress_idle (ev_window);
    ev_window_set_message_area (ev_window, NULL);

    g_file_copy_finish (source, async_result, &error);
    if (!error) {
        ev_job_scheduler_push_job (ev_window->priv->load_job, EV_JOB_PRIORITY_NONE);
        g_file_query_info_async (source,
                                 G_FILE_ATTRIBUTE_TIME_MODIFIED,
                                 0, G_PRIORITY_DEFAULT,
                                 NULL,
                                 (GAsyncReadyCallback)set_uri_mtime,
                                 ev_window);
        return;
    }

    if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_MOUNTED)) {
        GMountOperation *operation;

        operation = gtk_mount_operation_new (GTK_WINDOW (ev_window));
        g_file_mount_enclosing_volume (source,
                                       G_MOUNT_MOUNT_NONE,
                                       operation, NULL,
                                       (GAsyncReadyCallback)mount_volume_ready_cb,
                                       ev_window);
        g_object_unref (operation);
    } else if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        ev_window_clear_load_job (ev_window);
        ev_window_clear_local_uri (ev_window);
        g_free (ev_window->priv->uri);
        ev_window->priv->uri = NULL;
        g_object_unref (source);

        ev_view_set_loading (EV_VIEW (ev_window->priv->view), FALSE);
    } else {
        ev_window_load_remote_failed (ev_window, error);
        g_object_unref (source);
    }

    g_error_free (error);
}

static void
window_open_file_copy_progress_cb (goffset   n_bytes,
                                   goffset   total_bytes,
                                   EvWindow *ev_window)
{
    gchar *status;
    gdouble fraction;

    if (!ev_window->priv->message_area)
        return;

    if (total_bytes <= 0)
        return;

    fraction = n_bytes / (gdouble)total_bytes;
    status = g_strdup_printf (_("Downloading document (%d%%)"),
            (gint)(fraction * 100));

    ev_progress_message_area_set_status (EV_PROGRESS_MESSAGE_AREA (ev_window->priv->message_area),
            status);
    ev_progress_message_area_set_fraction (EV_PROGRESS_MESSAGE_AREA (ev_window->priv->message_area),
            fraction);

    g_free (status);
}

static void
ev_window_load_file_remote (EvWindow *ev_window,
                            GFile    *source_file)
{
    GFile *target_file;

    if (!ev_window->priv->local_uri) {
        char *base_name, *template;
        GFile *tmp_file;
        GError *err = NULL;

        /* We'd like to keep extension of source uri since
         * it helps to resolve some mime types, say cbz.
         */
        base_name = g_file_get_basename (source_file);
        template = g_strdup_printf ("document.XXXXXX-%s", base_name);
        g_free (base_name);

        tmp_file = ev_mkstemp_file (template, &err);
        g_free (template);
        if (tmp_file == NULL) {
            ev_window_error_message (ev_window, err,
                    "%s", _("Failed to load remote file."));
            g_error_free (err);
            return;
        }

        ev_window->priv->local_uri = g_file_get_uri (tmp_file);
        g_object_unref (tmp_file);

        ev_job_load_set_uri (EV_JOB_LOAD (ev_window->priv->load_job),
                ev_window->priv->local_uri);
    }

    ev_window_reset_progress_cancellable (ev_window);

    target_file = g_file_new_for_uri (ev_window->priv->local_uri);
    g_file_copy_async (source_file, target_file,
            G_FILE_COPY_OVERWRITE,
            G_PRIORITY_DEFAULT,
            ev_window->priv->progress_cancellable,
            (GFileProgressCallback)window_open_file_copy_progress_cb,
            ev_window,
            (GAsyncReadyCallback)window_open_file_copy_ready_cb,
            ev_window);
    g_object_unref (target_file);

    ev_window_show_progress_message (ev_window, 1,
            (GSourceFunc)show_loading_progress);
}

void
ev_window_open_uri (EvWindow       *ev_window,
                    const char     *uri,
                    EvLinkDest     *dest,
                    EvWindowRunMode mode,
                    const gchar    *search_string)
{
    GFile *source_file;

    /* If a document is already loaded, open in a new tab */
    if (ev_window->priv->document != NULL && uri != NULL) {
        EvTab *new_tab = ev_window_create_new_tab (ev_window, NULL);

        /* Update priv pointers to the new tab */
        ev_window->priv->view = new_tab->view;
        ev_window->priv->scrolled_window = new_tab->scrolled_window;
        ev_window->priv->view_box = new_tab->view_box;
        ev_window->priv->model = new_tab->model;
        ev_window->priv->document = NULL;
        ev_window->priv->uri = NULL;
        ev_window->priv->metadata = NULL;
        ev_window->priv->bookmarks = NULL;
        ev_window->priv->history = new_tab->history;
        ev_window->priv->in_reload = FALSE;
        ev_window->priv->page_mode = PAGE_MODE_DOCUMENT;

        /* Reconnect sidebar */
        ev_sidebar_set_model (EV_SIDEBAR (ev_window->priv->sidebar),
                              new_tab->model);
    }

    ev_window->priv->in_reload = FALSE;

    if (ev_window->priv->uri &&
            g_ascii_strcasecmp (ev_window->priv->uri, uri) == 0) {
        ev_window_reload_document (ev_window, dest);
        return;
    }

    if (ev_window->priv->monitor) {
        g_object_unref (ev_window->priv->monitor);
        ev_window->priv->monitor = NULL;
    }

    ev_window_close_dialogs (ev_window);
    ev_window_clear_load_job (ev_window);
    ev_window_clear_local_uri (ev_window);

    ev_window->priv->window_mode = mode;

    if (ev_window->priv->uri)
        g_free (ev_window->priv->uri);
    ev_window->priv->uri = g_strdup (uri);

    /* Sync URI to active tab */
    if (ev_window->priv->active_tab)
        ev_window->priv->active_tab->uri = ev_window->priv->uri;

    if (ev_window->priv->metadata)
        g_object_unref (ev_window->priv->metadata);
    if (ev_window->priv->bookmarks)
        g_object_unref (ev_window->priv->bookmarks);

    source_file = g_file_new_for_uri (uri);
    if (ev_is_metadata_supported_for_file (source_file)) {
        ev_window->priv->metadata = ev_metadata_new (source_file);
        ev_window_init_metadata_with_default_values (ev_window);
    } else {
        ev_window->priv->metadata = NULL;
    }

    if (ev_window->priv->metadata) {
        ev_window->priv->bookmarks = ev_bookmarks_new (ev_window->priv->metadata);
        ev_sidebar_bookmarks_set_bookmarks (EV_SIDEBAR_BOOKMARKS (ev_window->priv->sidebar_bookmarks),
                ev_window->priv->bookmarks);
        g_signal_connect_swapped (ev_window->priv->bookmarks, "changed",
                G_CALLBACK (ev_window_setup_bookmarks),
                ev_window);
    } else {
        ev_window->priv->bookmarks = NULL;
    }

    if (ev_window->priv->search_string)
        g_free (ev_window->priv->search_string);
    ev_window->priv->search_string = search_string ?
            g_strdup (search_string) : NULL;

    if (ev_window->priv->dest)
        g_object_unref (ev_window->priv->dest);
    ev_window->priv->dest = dest ? g_object_ref (dest) : NULL;

    setup_size_from_metadata (ev_window);
    setup_model_from_metadata (ev_window);
    ev_window_setup_bookmarks (ev_window);

    ev_window->priv->load_job = ev_job_load_new (uri);
    g_signal_connect (ev_window->priv->load_job,
            "finished",
            G_CALLBACK (ev_window_load_job_cb),
            ev_window);

    if (!g_file_is_native (source_file) && !ev_window->priv->local_uri) {
        ev_window_load_file_remote (ev_window, source_file);
    } else {
        ev_view_set_loading (EV_VIEW (ev_window->priv->view), TRUE);
        g_object_unref (source_file);
        ev_job_scheduler_push_job (ev_window->priv->load_job, EV_JOB_PRIORITY_NONE);
    }
}

void
ev_window_open_document (EvWindow       *ev_window,
                         EvDocument     *document,
                         EvLinkDest     *dest,
                         EvWindowRunMode mode,
                         const gchar    *search_string)
{
    ev_window_close_dialogs (ev_window);
    ev_window_clear_load_job (ev_window);
    ev_window_clear_local_uri (ev_window);

    if (ev_window->priv->monitor) {
        g_object_unref (ev_window->priv->monitor);
        ev_window->priv->monitor = NULL;
    }

    if (ev_window->priv->uri)
        g_free (ev_window->priv->uri);
    ev_window->priv->uri = g_strdup (ev_document_get_uri (document));

    setup_size_from_metadata (ev_window);
    setup_model_from_metadata (ev_window);

    ev_document_model_set_document (ev_window->priv->model, document);

    setup_document_from_metadata (ev_window);
    setup_view_from_metadata (ev_window);

    if (dest && document->iswebdocument == FALSE) {
        EvLink *link;
        EvLinkAction *link_action;

        link_action = ev_link_action_new_dest (dest);
        link = ev_link_new (NULL, link_action);
        ev_view_handle_link (EV_VIEW (ev_window->priv->view), link);
        g_object_unref (link_action);
        g_object_unref (link);
    }

    switch (mode) {
    case EV_WINDOW_MODE_FULLSCREEN:
        ev_window_run_fullscreen (ev_window);
        break;
    case EV_WINDOW_MODE_PRESENTATION:
        ev_window_run_presentation (ev_window);
        break;
    default:
        break;
    }

    if (search_string && EV_IS_DOCUMENT_FIND (document) &&
            mode != EV_WINDOW_MODE_PRESENTATION) {
        ev_window_cmd_edit_find (NULL, NULL, ev_window);
        egg_find_bar_set_search_string (EGG_FIND_BAR (ev_window->priv->find_bar),
                search_string);
    }

    /* Create a monitor for the document */
    ev_window->priv->monitor = ev_file_monitor_new (ev_window->priv->uri);
    g_signal_connect_swapped (ev_window->priv->monitor, "changed",
            G_CALLBACK (ev_window_document_changed),
            ev_window);
}

static void
ev_window_reload_local (EvWindow *ev_window)
{
    const gchar *uri;

    uri = ev_window->priv->local_uri ? ev_window->priv->local_uri : ev_window->priv->uri;
    ev_window->priv->reload_job = ev_job_load_new (uri);
    g_signal_connect (ev_window->priv->reload_job, "finished",
            G_CALLBACK (ev_window_reload_job_cb),
            ev_window);
    ev_job_scheduler_push_job (ev_window->priv->reload_job, EV_JOB_PRIORITY_NONE);
}

static gboolean
show_reloading_progress (EvWindow *ev_window)
{
    GtkWidget *area;
    gchar     *text;

    if (ev_window->priv->message_area)
        return FALSE;

    text = g_strdup_printf (_("Reloading document from %s"),
            ev_window->priv->uri);
    area = ev_progress_message_area_new ("view-refresh-symbolic",
            text,
            _("_Close"),
            GTK_RESPONSE_CLOSE,
            _("_Cancel"),
            GTK_RESPONSE_CANCEL,
            NULL);
    g_signal_connect (area, "response",
            G_CALLBACK (ev_window_progress_response_cb),
            ev_window);
    gtk_widget_show (area);
    ev_window_set_message_area (ev_window, area);
    g_free (text);

    return FALSE;
}

static void
reload_remote_copy_ready_cb (GFile        *remote,
                             GAsyncResult *async_result,
                             EvWindow     *ev_window)
{
    GError *error = NULL;

    ev_window_clear_progress_idle (ev_window);

    g_file_copy_finish (remote, async_result, &error);
    if (error) {
        if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            ev_window_error_message (ev_window, error,
                    "%s", _("Failed to reload document."));
        g_error_free (error);
    } else {
        ev_window_reload_local (ev_window);
    }

    g_object_unref (remote);
}

static void
reload_remote_copy_progress_cb (goffset   n_bytes,
                                goffset   total_bytes,
                                EvWindow *ev_window)
{
    gchar *status;
    gdouble fraction;

    if (!ev_window->priv->message_area)
        return;

    if (total_bytes <= 0)
        return;

    fraction = n_bytes / (gdouble)total_bytes;
    status = g_strdup_printf (_("Downloading document (%d%%)"),
            (gint)(fraction * 100));

    ev_progress_message_area_set_status (EV_PROGRESS_MESSAGE_AREA (ev_window->priv->message_area),
            status);
    ev_progress_message_area_set_fraction (EV_PROGRESS_MESSAGE_AREA (ev_window->priv->message_area),
            fraction);

    g_free (status);
}

static void
query_remote_uri_mtime_cb (GFile        *remote,
                           GAsyncResult *async_result,
                           EvWindow     *ev_window)
{
    GFileInfo *info;
    GTimeVal   mtime;
    GError    *error = NULL;

    info = g_file_query_info_finish (remote, async_result, &error);
    if (error) {
        g_error_free (error);
        g_object_unref (remote);
        ev_window_reload_local (ev_window);

        return;
    }

    g_file_info_get_modification_time (info, &mtime);
    if (ev_window->priv->uri_mtime != mtime.tv_sec) {
        GFile *target_file;

        /* Remote file has changed */
        ev_window->priv->uri_mtime = mtime.tv_sec;

        ev_window_reset_progress_cancellable (ev_window);

        target_file = g_file_new_for_uri (ev_window->priv->local_uri);
        g_file_copy_async (remote, target_file,
                G_FILE_COPY_OVERWRITE,
                G_PRIORITY_DEFAULT,
                ev_window->priv->progress_cancellable,
                (GFileProgressCallback)reload_remote_copy_progress_cb,
                ev_window,
                (GAsyncReadyCallback)reload_remote_copy_ready_cb,
                ev_window);
        g_object_unref (target_file);
        ev_window_show_progress_message (ev_window, 1,
                (GSourceFunc)show_reloading_progress);
    } else {
        g_object_unref (remote);
        ev_window_reload_local (ev_window);
    }

    g_object_unref (info);
}

static void
ev_window_reload_remote (EvWindow *ev_window)
{
    GFile *remote;

    remote = g_file_new_for_uri (ev_window->priv->uri);
    /* Reload the remote uri only if it has changed */
    g_file_query_info_async (remote,
            G_FILE_ATTRIBUTE_TIME_MODIFIED,
            0, G_PRIORITY_DEFAULT,
            NULL,
            (GAsyncReadyCallback)query_remote_uri_mtime_cb,
            ev_window);
}

static void
ev_window_reload_document (EvWindow *ev_window,
                           EvLinkDest *dest)
{
    ev_window_clear_reload_job (ev_window);
    ev_window->priv->in_reload = TRUE;

    if (ev_window->priv->dest)
        g_object_unref (ev_window->priv->dest);
    ev_window->priv->dest = dest ? g_object_ref (dest) : NULL;

    if (ev_window->priv->local_uri) {
        ev_window_reload_remote (ev_window);
    } else {
        ev_window_reload_local (ev_window);
    }
}

static const gchar *
get_settings_key_for_directory (GUserDirectory directory)
{
    switch (directory) {
    case G_USER_DIRECTORY_PICTURES:
        return GS_LAST_PICTURES_DIRECTORY;
    case G_USER_DIRECTORY_DOCUMENTS:
    default:
        return GS_LAST_DOCUMENT_DIRECTORY;
    }
}

static void
ev_window_file_chooser_restore_folder (EvWindow       *window,
                                       GtkFileChooser *file_chooser,
                                       const gchar    *uri,
                                       GUserDirectory  directory)
{
    gchar *folder_uri = NULL;
    GFile *folder = NULL;

    g_settings_get (ev_window_ensure_settings (window),
            get_settings_key_for_directory (directory),
            "ms", &folder_uri);

    if (folder_uri == NULL && uri != NULL) {
        GFile *file, *parent;

        file = g_file_new_for_uri (uri);
        parent = g_file_get_parent (file);
        g_object_unref (file);
        if (parent) {
            folder_uri = g_file_get_uri (parent);
            g_object_unref (parent);
        }
    }

    if (folder_uri) {
        folder = g_file_new_for_uri (folder_uri);
    } else {
        const gchar *dir = g_get_user_special_dir (directory);
        folder = g_file_new_for_path (dir ? dir : g_get_home_dir ());
    }

    if (folder) {
        gtk_file_chooser_set_current_folder (file_chooser, folder, NULL);
        g_object_unref (folder);
    }

    g_free (folder_uri);
}

static void
ev_window_file_chooser_save_folder (EvWindow       *window,
                                    GtkFileChooser *file_chooser,
                                    GUserDirectory  directory)
{
    gchar *uri = NULL;
    GFile *folder;

    folder = gtk_file_chooser_get_current_folder (file_chooser);
    if (folder) {
        GFile *default_folder = g_file_new_for_path (g_get_user_special_dir (directory));
        if (!g_file_equal (folder, default_folder)) {
            uri = g_file_get_uri (folder);
        }
        g_object_unref (default_folder);
        g_object_unref (folder);
    }

    g_settings_set (ev_window_ensure_settings (window),
            get_settings_key_for_directory (directory),
            "ms", uri);
    g_free (uri);
}

static void
file_open_dialog_response_cb (GtkWidget *chooser,
                              gint       response_id,
                              EvWindow  *ev_window)
{
    if (response_id == GTK_RESPONSE_OK) {
        GSList *uris = NULL;

        ev_window_file_chooser_save_folder (ev_window,
                GTK_FILE_CHOOSER (chooser),
                G_USER_DIRECTORY_DOCUMENTS);

        GListModel *files;
        guint n_files, i;

        files = gtk_file_chooser_get_files (GTK_FILE_CHOOSER (chooser));
        n_files = g_list_model_get_n_items (files);
        for (i = 0; i < n_files; i++) {
            GFile *file = g_list_model_get_item (files, i);
            uris = g_slist_prepend (uris, g_file_get_uri (file));
            g_object_unref (file);
        }
        g_object_unref (files);

        ev_application_open_uri_list (EV_APP, uris,
                gtk_widget_get_display (GTK_WIDGET (ev_window)),
                GDK_CURRENT_TIME);

        g_slist_free_full (uris, g_free);

    }

    gtk_window_destroy (GTK_WINDOW (chooser));
}

static void
ev_window_cmd_file_open (GSimpleAction *action,
                         GVariant      *parameter,
                         gpointer       user_data)
{
    EvWindow    *window = EV_WINDOW (user_data);
    GtkWidget   *chooser;

    chooser = gtk_file_chooser_dialog_new (_("Open Document"),
            GTK_WINDOW (window),
            GTK_FILE_CHOOSER_ACTION_OPEN,
            _("_Cancel"), GTK_RESPONSE_CANCEL,
            _("_Open"), GTK_RESPONSE_OK,
            NULL);

    ev_document_factory_add_filters (chooser, NULL);
    gtk_file_chooser_set_select_multiple (GTK_FILE_CHOOSER (chooser), TRUE);

    ev_window_file_chooser_restore_folder (window,
            GTK_FILE_CHOOSER (chooser),
            NULL,
            G_USER_DIRECTORY_DOCUMENTS);

    g_signal_connect (chooser, "response",
            G_CALLBACK (file_open_dialog_response_cb),
            window);

    gtk_widget_show (chooser);
}

static void
ev_window_open_copy_at_dest (EvWindow   *window,
                             EvLinkDest *dest)
{
    EvWindow *new_window = EV_WINDOW (ev_window_new ());


    if (window->priv->metadata)
        new_window->priv->metadata = g_object_ref (window->priv->metadata);
    ev_window_open_document (new_window,
            window->priv->document,
            dest, 0, NULL);
    gtk_window_present (GTK_WINDOW (new_window));
}

static void
ev_window_cmd_file_open_copy (GSimpleAction *action,
                              GVariant      *parameter,
                              gpointer       user_data)
{
    EvWindow *window = EV_WINDOW (user_data);
    ev_window_open_copy_at_dest (window, NULL);
}

/* ev_window_cmd_file_activate removed */

/* ev_window_open_recent_action_item_activated */


G_GNUC_UNUSED static void
ev_window_open_recent_action_item_activated (GSimpleAction *action,
                                             GVariant      *parameter,
                                             gpointer       user_data)
{
    EvWindow   *window = EV_WINDOW (user_data);
    const char *uri;

    uri = g_variant_get_string (parameter, NULL);
    if (uri) {
        ev_application_open_uri_at_dest (EV_APP, uri,
                                        gtk_widget_get_display (GTK_WIDGET (window)),
                                        NULL, 0, NULL, GDK_CURRENT_TIME);
    }
}

static void
ev_window_add_recent (EvWindow *window,
                      const char *filename)
{
    gtk_recent_manager_add_item (window->priv->recent_manager, filename);
}



/*
 * Doubles underscore to avoid spurious menu accels.
 */
/* ev_window_get_menu_file_label removed */

static void
ev_window_setup_favorites (EvWindow *ev_window)
{
    /* TODO: Implement dynamic GMenu for favorites in GTK4 */
}

/* show_saving_progress removed */

/* window_save_file_copy_ready_cb removed */

/* window_save_file_copy_progress_cb removed */

/* ev_window_save_remote removed */

static void
ev_window_clear_save_job (EvWindow *ev_window)
{
    if (ev_window->priv->save_job != NULL) {
        if (!ev_job_is_finished (ev_window->priv->save_job))
            ev_job_cancel (ev_window->priv->save_job);

        g_signal_handlers_disconnect_by_func (ev_window->priv->save_job,
                ev_window_save_job_cb,
                ev_window);
        g_object_unref (ev_window->priv->save_job);
        ev_window->priv->save_job = NULL;
    }
}

static void
ev_window_save_job_cb (EvJob     *job,
                       EvWindow  *window)
{
    if (ev_job_is_failed (job)) {
        ev_window_error_message (window, job->error,
                _("The file could not be saved as “%s”."),
                EV_JOB_SAVE (job)->uri);
    } else {
        ev_window_add_recent (window, EV_JOB_SAVE (job)->uri);

        GFile *dst = g_file_new_for_uri (EV_JOB_SAVE (job)->uri);
	    if (dst && ev_is_metadata_supported_for_file (dst))
	        ev_metadata_copy_to (window->priv->metadata, dst);

	    g_object_unref (dst);
    }

    ev_window_clear_save_job (window);

    if (window->priv->close_after_save) {
        gtk_window_destroy (GTK_WINDOW (window));
    }
}

static void
ev_window_save_as (EvWindow *ev_window,
		   gchar* uri)
{
    /* FIXME: remote copy should be done here rather than in the save job,
     * so that we can track progress and cancel the operation
     */
    ev_window_clear_save_job (ev_window);
    ev_window->priv->save_job = ev_job_save_new (ev_window->priv->document,
            uri, ev_window->priv->uri);
    g_signal_connect (ev_window->priv->save_job, "finished",
            G_CALLBACK (ev_window_save_job_cb),
            ev_window);
    /* The priority doesn't matter for this job */
    ev_job_scheduler_push_job (ev_window->priv->save_job, EV_JOB_PRIORITY_NONE);
}

static void
ev_window_save (EvWindow *ev_window)
{
    ev_window_save_as (ev_window, ev_window->priv->uri);
}

static void
ev_window_cmd_save_as_response_cb (GtkDialog *dialog,
                                  gint       response_id,
                                  gpointer   user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);

    if (response_id == GTK_RESPONSE_ACCEPT) {
        GFile *file = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (dialog));
        char *uri = g_file_get_uri (file);
        
        ev_window_file_chooser_save_folder (ev_window,
                GTK_FILE_CHOOSER (dialog),
                G_USER_DIRECTORY_DOCUMENTS);

        ev_window_save_as (ev_window, uri);

        g_free (uri);
        g_object_unref (file);

        if (ev_window->priv->close_after_save) {
            gtk_window_destroy (GTK_WINDOW (ev_window));
            return;
        }
    }
    gtk_window_destroy (GTK_WINDOW (dialog));
}

static void
ev_window_cmd_save_as (GSimpleAction *action,
                       GVariant      *parameter,
                       gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    GtkWidget *fc;
    gchar *base_name;
    GFile *file;

    fc = gtk_file_chooser_dialog_new (
            _("Save a Copy"),
            GTK_WINDOW (ev_window), GTK_FILE_CHOOSER_ACTION_SAVE,
            _("_Cancel"), GTK_RESPONSE_CANCEL,
            _("_Save"), GTK_RESPONSE_ACCEPT,
            NULL);

    ev_document_factory_add_filters (fc, ev_window->priv->document);
    gtk_dialog_set_default_response (GTK_DIALOG (fc), GTK_RESPONSE_ACCEPT);

    file = g_file_new_for_uri (ev_window->priv->uri);
    base_name = g_file_get_basename (file);
    gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (fc), base_name);

    g_object_unref (file);
    g_free (base_name);

    ev_window_file_chooser_restore_folder (ev_window,
            GTK_FILE_CHOOSER (fc),
            ev_window->priv->uri,
            G_USER_DIRECTORY_DOCUMENTS);

    g_signal_connect (fc, "response", G_CALLBACK (ev_window_cmd_save_as_response_cb), ev_window);
    gtk_widget_show (fc);
}

static GKeyFile *
get_print_settings_file (void)
{
    GKeyFile *print_settings_file;
    gchar    *filename;
    GError *error = NULL;

    print_settings_file = g_key_file_new ();

    filename = g_build_filename (ev_application_get_dot_dir (EV_APP, FALSE),
            EV_PRINT_SETTINGS_FILE, NULL);
    if (!g_key_file_load_from_file (print_settings_file,
            filename,
            G_KEY_FILE_KEEP_COMMENTS |
            G_KEY_FILE_KEEP_TRANSLATIONS,
            &error)) {

        /* Don't warn if the file simply doesn't exist */
        if (!g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
            g_warning ("%s", error->message);

        g_error_free (error);
    }

    g_free (filename);

    return print_settings_file;
}

static void
save_print_setting_file (GKeyFile *key_file)
{
    gchar  *filename;
    gchar  *data;
    gsize  data_length;
    GError *error = NULL;

    filename = g_build_filename (ev_application_get_dot_dir (EV_APP, TRUE),
            EV_PRINT_SETTINGS_FILE, NULL);
    data = g_key_file_to_data (key_file, &data_length, NULL);
    g_file_set_contents (filename, data, data_length, &error);
    if (error) {
        g_warning ("Failed to save print settings: %s", error->message);
        g_error_free (error);
    }
    g_free (data);
    g_free (filename);
}

static void
ev_window_save_print_settings (EvWindow         *window,
                               GtkPrintSettings *print_settings)
{
    GKeyFile *key_file;
    gint      i;

    key_file = get_print_settings_file ();
    gtk_print_settings_to_key_file (print_settings, key_file, EV_PRINT_SETTINGS_GROUP);

	/* Always Remove n_copies from global settings */
	g_key_file_remove_key (key_file, EV_PRINT_SETTINGS_GROUP, GTK_PRINT_SETTINGS_N_COPIES, NULL);

    /* Save print settings that are specific to the document */
    for (i = 0; i < G_N_ELEMENTS (document_print_settings); i++) {
        /* Remove it from global settings */
        g_key_file_remove_key (key_file, EV_PRINT_SETTINGS_GROUP,
                document_print_settings[i], NULL);

        if (window->priv->metadata) {
            const gchar *value;

            value = gtk_print_settings_get (print_settings,
                    document_print_settings[i]);
            ev_metadata_set_string (window->priv->metadata,
                    document_print_settings[i], value);
        }
    }

    save_print_setting_file (key_file);
    g_key_file_free (key_file);
}

static void
ev_window_save_print_page_setup (EvWindow     *window,
                                 GtkPageSetup *page_setup)
{
    GKeyFile *key_file;

    key_file = get_print_settings_file ();
    gtk_page_setup_to_key_file (page_setup, key_file, EV_PAGE_SETUP_GROUP);

    /* Do not save document settings in global file */
    g_key_file_remove_key (key_file, EV_PAGE_SETUP_GROUP,
            "page-setup-orientation", NULL);
    g_key_file_remove_key (key_file, EV_PAGE_SETUP_GROUP,
            "page-setup-margin-top", NULL);
    g_key_file_remove_key (key_file, EV_PAGE_SETUP_GROUP,
            "page-setup-margin-bottom", NULL);
    g_key_file_remove_key (key_file, EV_PAGE_SETUP_GROUP,
            "page-setup-margin-left", NULL);
    g_key_file_remove_key (key_file, EV_PAGE_SETUP_GROUP,
            "page-setup-margin-right", NULL);

    save_print_setting_file (key_file);
    g_key_file_free (key_file);

    if (!window->priv->metadata)
        return;

    /* Save page setup options that are specific to the document */
    ev_metadata_set_int (window->priv->metadata, "page-setup-orientation",
            gtk_page_setup_get_orientation (page_setup));
    ev_metadata_set_double (window->priv->metadata, "page-setup-margin-top",
            gtk_page_setup_get_top_margin (page_setup, GTK_UNIT_MM));
    ev_metadata_set_double (window->priv->metadata, "page-setup-margin-bottom",
            gtk_page_setup_get_bottom_margin (page_setup, GTK_UNIT_MM));
    ev_metadata_set_double (window->priv->metadata, "page-setup-margin-left",
            gtk_page_setup_get_left_margin (page_setup, GTK_UNIT_MM));
    ev_metadata_set_double (window->priv->metadata, "page-setup-margin-right",
            gtk_page_setup_get_right_margin (page_setup, GTK_UNIT_MM));
}

static void
ev_window_load_print_settings_from_metadata (EvWindow         *window,
                                             GtkPrintSettings *print_settings)
{
    gint i;

    if (!window->priv->metadata)
        return;

    /* Load print setting that are specific to the document */
    for (i = 0; i < G_N_ELEMENTS (document_print_settings); i++) {
        gchar *value = NULL;

        ev_metadata_get_string (window->priv->metadata,
                document_print_settings[i], &value);
        gtk_print_settings_set (print_settings,
                document_print_settings[i], value);
    }
}

static void
ev_window_load_print_page_setup_from_metadata (EvWindow     *window,
                                               GtkPageSetup *page_setup)
{
    gint          int_value;
    gdouble       double_value;
    GtkPaperSize *paper_size = gtk_page_setup_get_paper_size (page_setup);

    /* Load page setup options that are specific to the document */
    if (window->priv->metadata &&
            ev_metadata_get_int (window->priv->metadata, "page-setup-orientation", &int_value)) {
        gtk_page_setup_set_orientation (page_setup, int_value);
    } else {
        gtk_page_setup_set_orientation (page_setup, GTK_PAGE_ORIENTATION_PORTRAIT);
    }

    if (window->priv->metadata &&
            ev_metadata_get_double (window->priv->metadata, "page-setup-margin-top", &double_value)) {
        gtk_page_setup_set_top_margin (page_setup, double_value, GTK_UNIT_MM);
    } else {
        gtk_page_setup_set_top_margin (page_setup,
                gtk_paper_size_get_default_top_margin (paper_size, GTK_UNIT_MM),
                GTK_UNIT_MM);
    }

    if (window->priv->metadata &&
            ev_metadata_get_double (window->priv->metadata, "page-setup-margin-bottom", &double_value)) {
        gtk_page_setup_set_bottom_margin (page_setup, double_value, GTK_UNIT_MM);
    } else {
        gtk_page_setup_set_bottom_margin (page_setup,
                gtk_paper_size_get_default_bottom_margin (paper_size, GTK_UNIT_MM),
                GTK_UNIT_MM);
    }

    if (window->priv->metadata &&
            ev_metadata_get_double (window->priv->metadata, "page-setup-margin-left", &double_value)) {
        gtk_page_setup_set_left_margin (page_setup, double_value, GTK_UNIT_MM);
    } else {
        gtk_page_setup_set_left_margin (page_setup,
                gtk_paper_size_get_default_left_margin (paper_size, GTK_UNIT_MM),
                GTK_UNIT_MM);
    }

    if (window->priv->metadata &&
            ev_metadata_get_double (window->priv->metadata, "page-setup-margin-right", &double_value)) {
        gtk_page_setup_set_right_margin (page_setup, double_value, GTK_UNIT_MM);
    } else {
        gtk_page_setup_set_right_margin (page_setup,
                gtk_paper_size_get_default_right_margin (paper_size, GTK_UNIT_MM),
                GTK_UNIT_MM);
    }
}

static GtkPrintSettings *
get_print_settings (GKeyFile *key_file)
{
    GtkPrintSettings *print_settings;

    print_settings = g_key_file_has_group (key_file, EV_PRINT_SETTINGS_GROUP) ?
            gtk_print_settings_new_from_key_file (key_file, EV_PRINT_SETTINGS_GROUP, NULL) :
            gtk_print_settings_new ();

    return print_settings ? print_settings : gtk_print_settings_new ();
}

static GtkPageSetup *
get_print_page_setup (GKeyFile *key_file)
{
    GtkPageSetup *page_setup;

    page_setup = g_key_file_has_group (key_file, EV_PAGE_SETUP_GROUP) ?
            gtk_page_setup_new_from_key_file (key_file, EV_PAGE_SETUP_GROUP, NULL) :
            gtk_page_setup_new ();

    return page_setup ? page_setup : gtk_page_setup_new ();
}

static void
ev_window_print_cancel (EvWindow *ev_window)
{
    EvPrintOperation *op;

    if (!ev_window->priv->print_queue)
        return;

    while ((op = g_queue_peek_tail (ev_window->priv->print_queue))) {
        ev_print_operation_cancel (op);
    }
}

static void
ev_window_print_update_pending_jobs_message (EvWindow *ev_window,
                                             gint      n_jobs)
{
    gchar *text = NULL;

    if (!EV_IS_PROGRESS_MESSAGE_AREA (ev_window->priv->message_area) ||
            !ev_window->priv->print_queue)
        return;

    if (n_jobs == 0) {
        ev_window_set_message_area (ev_window, NULL);
        return;
    }

    if (n_jobs > 1) {
        text = g_strdup_printf (ngettext ("%d pending job in queue",
                "%d pending jobs in queue",
                n_jobs - 1), n_jobs - 1);
    }

    ev_message_area_set_secondary_text (EV_MESSAGE_AREA (ev_window->priv->message_area),
            text);
    g_free (text);
}

static gboolean
destroy_window (GtkWidget *window)
{
    gtk_window_destroy (GTK_WINDOW (window));

    return FALSE;
}

static void
ev_window_print_operation_done (EvPrintOperation       *op,
                                GtkPrintOperationResult result,
                                EvWindow               *ev_window)
{
    gint n_jobs;

    switch (result) {
    case GTK_PRINT_OPERATION_RESULT_APPLY: {
        GtkPrintSettings *print_settings;

        print_settings = ev_print_operation_get_print_settings (op);
        ev_window_save_print_settings (ev_window, print_settings);

        if (ev_print_operation_get_embed_page_setup (op)) {
            GtkPageSetup *page_setup;

            page_setup = ev_print_operation_get_default_page_setup (op);
            ev_window_save_print_page_setup (ev_window, page_setup);
        }
    }

    break;
    case GTK_PRINT_OPERATION_RESULT_ERROR: {
        GtkWidget *dialog;
        GError    *error = NULL;


        ev_print_operation_get_error (op, &error);

        /* The message area is already used by
         * the printing progress, so it's better to
         * use a popup dialog in this case
         */
        dialog = gtk_message_dialog_new (GTK_WINDOW (ev_window),
                GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_ERROR,
                GTK_BUTTONS_CLOSE,
                "%s", _("Failed to print document"));
        gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                "%s", error->message);
        g_signal_connect (dialog, "response",
                G_CALLBACK (gtk_window_destroy),
                NULL);
        gtk_widget_show (dialog);

        g_error_free (error);
    }
    break;
    case GTK_PRINT_OPERATION_RESULT_CANCEL:
    default:
        break;
    }

    g_queue_remove (ev_window->priv->print_queue, op);
    g_object_unref (op);
    n_jobs = g_queue_get_length (ev_window->priv->print_queue);
    ev_window_print_update_pending_jobs_message (ev_window, n_jobs);

    if (n_jobs == 0 && ev_window->priv->close_after_print)
        g_idle_add ((GSourceFunc)destroy_window,
                ev_window);
}

static void
ev_window_print_progress_response_cb (EvProgressMessageArea *area,
                                      gint                   response,
                                      EvWindow              *ev_window)
{
    if (response == GTK_RESPONSE_CANCEL) {
        EvPrintOperation *op;

        op = g_queue_peek_tail (ev_window->priv->print_queue);
        ev_print_operation_cancel (op);
    } else {
        gtk_widget_hide (GTK_WIDGET (area));
    }
}

static void
ev_window_print_operation_status_changed (EvPrintOperation *op,
                                          EvWindow         *ev_window)
{
    const gchar *status;
    gdouble      fraction;

    status = ev_print_operation_get_status (op);
    fraction = ev_print_operation_get_progress (op);

    if (!ev_window->priv->message_area) {
        GtkWidget   *area;
        const gchar *job_name;
        gchar       *text;

        job_name = ev_print_operation_get_job_name (op);
        text = g_strdup_printf (_("Printing job “%s”"), job_name);

        area = ev_progress_message_area_new ("printer-symbolic",
                text,
                _("_Close"),
                GTK_RESPONSE_CLOSE,
                _("_Cancel"),
                GTK_RESPONSE_CANCEL,
                NULL);
        ev_window_print_update_pending_jobs_message (ev_window, 1);
        g_signal_connect (area, "response",
                G_CALLBACK (ev_window_print_progress_response_cb),
                ev_window);
        gtk_widget_show (area);
        ev_window_set_message_area (ev_window, area);
        g_free (text);
    }

    ev_progress_message_area_set_status (EV_PROGRESS_MESSAGE_AREA (ev_window->priv->message_area),
            status);
    ev_progress_message_area_set_fraction (EV_PROGRESS_MESSAGE_AREA (ev_window->priv->message_area),
            fraction);
}

static void
ev_window_print_operation_begin_print (EvPrintOperation *op,
                                       EvWindow         *ev_window)
{
    if (!ev_window->priv->print_queue)
        ev_window->priv->print_queue = g_queue_new ();

    g_queue_push_head (ev_window->priv->print_queue, op);
    ev_window_print_update_pending_jobs_message (ev_window,
            g_queue_get_length (ev_window->priv->print_queue));
}

void
ev_window_print_range (EvWindow *ev_window,
                       gint      first_page,
                       gint      last_page)
{
    EvPrintOperation *op;
    GKeyFile         *print_settings_file;
    GtkPrintSettings *print_settings;
    GtkPageSetup     *print_page_setup;
    gint              current_page;
    gint              document_last_page;

    g_return_if_fail (EV_IS_WINDOW (ev_window));
    g_return_if_fail (ev_window->priv->document != NULL);

    if (!ev_window->priv->print_queue)
        ev_window->priv->print_queue = g_queue_new ();

    op = ev_print_operation_new (ev_window->priv->document);
    if (!op) {
        g_warning ("%s", "Printing is not supported for document\n");
        return;
    }

    g_signal_connect (op, "begin_print",
            G_CALLBACK (ev_window_print_operation_begin_print),
            (gpointer)ev_window);
    g_signal_connect (op, "status_changed",
            G_CALLBACK (ev_window_print_operation_status_changed),
            (gpointer)ev_window);
    g_signal_connect (op, "done",
            G_CALLBACK (ev_window_print_operation_done),
            (gpointer)ev_window);

    current_page = ev_document_model_get_page (ev_window->priv->model);
    document_last_page = ev_document_get_n_pages (ev_window->priv->document);

    print_settings_file = get_print_settings_file ();

    print_settings = get_print_settings (print_settings_file);
    ev_window_load_print_settings_from_metadata (ev_window, print_settings);

    print_page_setup = get_print_page_setup (print_settings_file);
    ev_window_load_print_page_setup_from_metadata (ev_window, print_page_setup);

    if (first_page != 1 || last_page != document_last_page) {
        GtkPageRange range;

        /* Ranges in GtkPrint are 0 - N */
        range.start = first_page - 1;
        range.end = last_page - 1;

        gtk_print_settings_set_print_pages (print_settings,
                GTK_PRINT_PAGES_RANGES);
        gtk_print_settings_set_page_ranges (print_settings,
                &range, 1);
    }

    ev_print_operation_set_job_name (op, gtk_window_get_title (GTK_WINDOW (ev_window)));
    ev_print_operation_set_current_page (op, current_page);
    ev_print_operation_set_print_settings (op, print_settings);
    ev_print_operation_set_default_page_setup (op, print_page_setup);
    ev_print_operation_set_embed_page_setup (op, TRUE);

    g_object_unref (print_settings);
    g_object_unref (print_page_setup);
    g_key_file_free (print_settings_file);

    ev_print_operation_run (op, GTK_WINDOW (ev_window));
}

static void
ev_window_print (EvWindow *window)
{
    ev_window_print_range (window, 1,
            ev_document_get_n_pages (window->priv->document));
}

static void
ev_window_cmd_file_print (GSimpleAction *action,
                          GVariant      *parameter,
                          gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    ev_window_print (ev_window);
}

static void
ev_window_cmd_file_properties (GSimpleAction *action,
                               GVariant      *parameter,
                               gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    if (ev_window->priv->properties == NULL) {
        ev_window->priv->properties = ev_properties_dialog_new ();
        ev_properties_dialog_set_document (EV_PROPERTIES_DIALOG (ev_window->priv->properties),
                ev_window->priv->uri,
                ev_window->priv->document);
        g_object_add_weak_pointer (G_OBJECT (ev_window->priv->properties),
                (gpointer) &(ev_window->priv->properties));
        gtk_window_set_transient_for (GTK_WINDOW (ev_window->priv->properties),
                GTK_WINDOW (ev_window));
    }

    ev_document_fc_mutex_lock ();
    gtk_widget_show (ev_window->priv->properties);
    ev_document_fc_mutex_unlock ();
}

static void
ev_window_cmd_edit_preferences (GSimpleAction *action,
                                GVariant      *parameter,
                                gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    ev_preferences_dialog_show(ev_window);
}

static void
ev_window_check_document_modified_response_cb (GtkDialog *dialog,
                                               gint       response_id,
                                               gpointer   user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);

    if (response_id == GTK_RESPONSE_YES) {
        ev_window->priv->close_after_save = TRUE;
        ev_window_cmd_save_as (NULL, NULL, ev_window);
    } else if (response_id == GTK_RESPONSE_NO) {
        ev_window->priv->close_after_save = FALSE;
        gtk_window_destroy (GTK_WINDOW (ev_window));
    } else if (response_id == GTK_RESPONSE_ACCEPT) {
        ev_window->priv->close_after_save = TRUE;
        ev_window_save (ev_window);
    }
    gtk_window_destroy (GTK_WINDOW (dialog));
}

static gboolean
ev_window_check_document_modified (EvWindow *ev_window)
{
    EvDocument  *document = ev_window->priv->document;
    GtkWidget   *dialog;
    GtkWidget   *button;
    gchar       *text, *markup;
    const gchar *secondary_text;

    if (!document)
        return FALSE;

    if (EV_IS_DOCUMENT_FORMS (document) &&
            ev_document_forms_document_is_modified (EV_DOCUMENT_FORMS (document))) {
        secondary_text = _("Document contains form fields that have been filled out. "
                "If you don't save a copy, changes will be permanently lost.");
    } else if (EV_IS_DOCUMENT_ANNOTATIONS (document) &&
            ev_document_annotations_document_is_modified (EV_DOCUMENT_ANNOTATIONS (document))) {
        secondary_text = _("Document contains new or modified annotations. "
                "If you don't save a copy, changes will be permanently lost.");
    } else {
        return FALSE;
    }


    text = g_markup_printf_escaped (_("Save a copy of document “%s” before closing?"),
            gtk_window_get_title (GTK_WINDOW (ev_window)));

    dialog = gtk_message_dialog_new (GTK_WINDOW (ev_window),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_QUESTION,
            GTK_BUTTONS_NONE,
            NULL);

    markup = g_strdup_printf ("<b>%s</b>", text);
    g_free (text);

    gtk_message_dialog_set_markup (GTK_MESSAGE_DIALOG (dialog), markup);
    g_free (markup);

    gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
            "%s", secondary_text);

    button = gtk_dialog_add_button (GTK_DIALOG (dialog), _("Save a _Copy"), GTK_RESPONSE_YES);
    gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Save"), GTK_RESPONSE_ACCEPT);
    gtk_dialog_add_button (GTK_DIALOG (dialog), _("Close _without Saving"), GTK_RESPONSE_NO);
    gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Cancel"), GTK_RESPONSE_CANCEL);

    gtk_style_context_add_class (gtk_widget_get_style_context (button),
                                 "suggested-action");
    gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_YES);

    g_signal_connect (dialog, "response", G_CALLBACK (ev_window_check_document_modified_response_cb), ev_window);
    gtk_widget_show (dialog);

    return TRUE;
}

static void
print_jobs_confirmation_dialog_response (GtkDialog *dialog,
                                         gint       response,
                                         EvWindow  *ev_window)
{
    gtk_window_destroy (GTK_WINDOW (dialog));

    switch (response) {
    case GTK_RESPONSE_YES:
        if (!ev_window->priv->print_queue ||
                g_queue_is_empty (ev_window->priv->print_queue))
            gtk_window_destroy (GTK_WINDOW (ev_window));
        else
            ev_window->priv->close_after_print = TRUE;
        break;
    case GTK_RESPONSE_NO:
        ev_window->priv->close_after_print = TRUE;
        if (ev_window->priv->print_queue &&
                !g_queue_is_empty (ev_window->priv->print_queue)) {
            gtk_widget_set_sensitive (GTK_WIDGET (ev_window), FALSE);
            ev_window_print_cancel (ev_window);
        } else {
            gtk_window_destroy (GTK_WINDOW (ev_window));
        }
        break;
    case GTK_RESPONSE_CANCEL:
    default:
        ev_window->priv->close_after_print = FALSE;
    }
}

static gboolean
ev_window_check_print_queue (EvWindow *ev_window)
{
    GtkWidget *dialog;
    GtkWidget *button;
    gchar     *text, *markup;
    gint       n_print_jobs;

    n_print_jobs = ev_window->priv->print_queue ?
            g_queue_get_length (ev_window->priv->print_queue) : 0;

    if (n_print_jobs == 0)
        return FALSE;

    dialog = gtk_message_dialog_new (GTK_WINDOW (ev_window),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_QUESTION,
            GTK_BUTTONS_NONE,
            NULL);
    if (n_print_jobs == 1) {
        EvPrintOperation *op;
        const gchar      *job_name;

        op = g_queue_peek_tail (ev_window->priv->print_queue);
        job_name = ev_print_operation_get_job_name (op);

        text = g_strdup_printf (_("Wait until print job “%s” finishes before closing?"),
                job_name);
    } else {
        /* TRANS: the singular form is not really used as n_print_jobs > 1
              but some languages distinguish between different plurals forms,
              so the ngettext is needed. */
        text = g_strdup_printf (ngettext("There is %d print job active. "
                "Wait until print finishes before closing?",
                "There are %d print jobs active. "
                "Wait until print finishes before closing?",
                n_print_jobs),
                n_print_jobs);
    }

    markup = g_strdup_printf ("<b>%s</b>", text);
    g_free (text);

    gtk_message_dialog_set_markup (GTK_MESSAGE_DIALOG (dialog), markup);
    g_free (markup);

    gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), "%s",
            _("If you close the window, pending print "
                    "jobs will not be printed."));

    gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Cancel _print and Close"), GTK_RESPONSE_NO);
    gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Cancel"), GTK_RESPONSE_CANCEL);
    button = gtk_dialog_add_button (GTK_DIALOG (dialog), _("Close _after Printing"), GTK_RESPONSE_YES);

    gtk_style_context_add_class (gtk_widget_get_style_context (button),
                                 "destructive-action");
    gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_YES);

    g_signal_connect (dialog, "response",
            G_CALLBACK (print_jobs_confirmation_dialog_response),
            ev_window);
    gtk_widget_show (dialog);

    return TRUE;
}

static gboolean
ev_window_close (EvWindow *ev_window)
{
    if (EV_WINDOW_IS_PRESENTATION (ev_window)) {
        gint current_page;

        /* Save current page */
        current_page = ev_view_presentation_get_current_page (
                EV_VIEW_PRESENTATION (ev_window->priv->presentation_view));
        ev_document_model_set_page (ev_window->priv->model, current_page);
    }

    if (ev_window_check_document_modified (ev_window))
        return FALSE;

    if (ev_window_check_print_queue (ev_window))
        return FALSE;

    return !ev_window->priv->close_after_save;
}

static void
ev_window_cmd_file_close_window (GSimpleAction *action,
                                 GVariant      *parameter,
                                 gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    if (ev_window_close (ev_window))
        gtk_window_destroy (GTK_WINDOW (ev_window));
}

static void
ev_window_cmd_file_close_all_windows (GSimpleAction *action,
                                      GVariant      *parameter,
                                      gpointer       user_data)
{
    GList *l, *windows;

    windows = g_list_copy (gtk_application_get_windows (GTK_APPLICATION (EV_APP)));
    for (l = windows; l != NULL; l = l->next) {
        EvWindow *win = EV_WINDOW (l->data);
        ev_window_cmd_file_close_window (action, parameter, win);
    }
    g_list_free (windows);
}

static void
ev_window_cmd_focus_page_selector (GSimpleAction *action,
                                   GVariant      *parameter,
                                   gpointer       user_data)
{
    EvWindow *window = EV_WINDOW (user_data);

    if (window->priv->page_selector)
        gtk_widget_grab_focus (window->priv->page_selector);
}

static void
ev_window_cmd_scroll_forward (GSimpleAction *action,
                              GVariant      *parameter,
                              gpointer       user_data)
{
    EvWindow *window = EV_WINDOW (user_data);

    /* If the webview is occupying the window */
    if (window->priv->document && window->priv->document->iswebdocument == TRUE)
        return;

    ev_view_scroll (EV_VIEW (window->priv->view), GTK_SCROLL_PAGE_FORWARD, FALSE);
}

static void
ev_window_cmd_scroll_backward (GSimpleAction *action,
                               GVariant      *parameter,
                               gpointer       user_data)
{
    EvWindow *window = EV_WINDOW (user_data);

    /* If the webview is occupying the window */
    if (window->priv->document && window->priv->document->iswebdocument == TRUE)
        return;

    ev_view_scroll (EV_VIEW (window->priv->view), GTK_SCROLL_PAGE_BACKWARD, FALSE);
}

static void
ev_window_cmd_continuous (GSimpleAction *action,
                          GVariant      *state,
                          gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    gboolean continuous = g_variant_get_boolean (state);

    g_simple_action_set_state (action, state);
    ev_window_stop_presentation (ev_window, TRUE);
    ev_document_model_set_continuous (ev_window->priv->model, continuous);
}

static void
ev_window_cmd_dual (GSimpleAction *action,
                    GVariant      *state,
                    gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    gboolean dual_page = g_variant_get_boolean (state);

    g_simple_action_set_state (action, state);
    ev_window_stop_presentation (ev_window, TRUE);
    ev_document_model_set_dual_page (ev_window->priv->model, dual_page);
}

static void
ev_window_cmd_dual_odd_pages_left (GSimpleAction *action,
                                   GVariant      *state,
                                   gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    gboolean dual_page_odd_left = g_variant_get_boolean (state);

    g_simple_action_set_state (action, state);
    ev_window_stop_presentation (ev_window, TRUE);
    ev_document_model_set_dual_page_odd_pages_left (ev_window->priv->model,
            dual_page_odd_left);
}

static void
ev_window_cmd_rtl (GSimpleAction *action,
                   GVariant      *state,
                   gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    gboolean rtl = g_variant_get_boolean (state);

    g_simple_action_set_state (action, state);
    ev_document_model_set_rtl (ev_window->priv->model, rtl);
}

static void
ev_window_cmd_view_best_fit (GSimpleAction *action,
                             GVariant      *state,
                             gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    gboolean active = g_variant_get_boolean (state);

    g_simple_action_set_state (action, state);
    ev_window_stop_presentation (ev_window, TRUE);

    if (active) {
        ev_document_model_set_sizing_mode (ev_window->priv->model, EV_SIZING_BEST_FIT);
    } else {
        ev_document_model_set_sizing_mode (ev_window->priv->model, EV_SIZING_FREE);
    }
    ev_window_update_actions (ev_window);
}

static void
ev_window_cmd_view_page_width (GSimpleAction *action,
                               GVariant      *state,
                               gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    gboolean active = g_variant_get_boolean (state);

    g_simple_action_set_state (action, state);
    ev_window_stop_presentation (ev_window, TRUE);

    if (active) {
        ev_document_model_set_sizing_mode (ev_window->priv->model, EV_SIZING_FIT_WIDTH);
    } else {
        ev_document_model_set_sizing_mode (ev_window->priv->model, EV_SIZING_FREE);
    }
    ev_window_update_actions (ev_window);
}


static void
ev_window_cmd_edit_select_all (GSimpleAction *action,
                               GVariant      *parameter,
                               gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    g_return_if_fail (EV_IS_WINDOW (ev_window));

    /*
     * If the find bar is open, select all applies to the find field contents.
     * If the zoom level selector is active, select all applies to its entry
     * field.Otherwise it applies to the viewing window's contents.
     */
    if (ev_window->priv->chrome & EV_CHROME_FINDBAR) {
        egg_find_bar_grab_focus(ev_window->priv->find_bar);
    } else if (ev_window->priv->chrome & EV_CHROME_TOOLBAR
        && ev_toolbar_zoom_action_get_focused (EV_TOOLBAR (ev_window->priv->toolbar))) {
        ev_toolbar_zoom_action_select_all (EV_TOOLBAR (ev_window->priv->toolbar));
    } else if (ev_window->priv->document->iswebdocument == FALSE ) {
        ev_view_select_all (EV_VIEW (ev_window->priv->view));
    }
#if ENABLE_EPUB
    else {
        ev_web_view_select_all(EV_WEB_VIEW(ev_window->priv->webview));
    }
#endif
}

static void
ev_window_cmd_edit_find (GSimpleAction *action,
                         GVariant      *parameter,
                         gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    if (ev_window->priv->document == NULL || !EV_IS_DOCUMENT_FIND (ev_window->priv->document)) {
        g_error ("Find action should be insensitive since document doesn't support find");
        return;
    }

    if (EV_WINDOW_IS_PRESENTATION (ev_window))
        return;

    update_chrome_flag (ev_window, EV_CHROME_FINDBAR, TRUE);
    update_chrome_visibility (ev_window);
    gtk_widget_grab_focus (ev_window->priv->find_bar);
}

static void
ev_window_cmd_edit_find_next (GSimpleAction *action,
                              GVariant      *parameter,
                              gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    if (EV_WINDOW_IS_PRESENTATION (ev_window))
        return;

    update_chrome_flag (ev_window, EV_CHROME_FINDBAR, TRUE);
    update_chrome_visibility (ev_window);
    gtk_widget_grab_focus (ev_window->priv->find_bar);
    if (ev_window->priv->document->iswebdocument == FALSE) {
        ev_view_find_next (EV_VIEW (ev_window->priv->view));
    }
#if ENABLE_EPUB
    else {
        ev_web_view_find_next(EV_WEB_VIEW(ev_window->priv->webview));
    }
#endif
}

static void
ev_window_cmd_edit_find_previous (GSimpleAction *action,
                                  GVariant      *parameter,
                                  gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    if (EV_WINDOW_IS_PRESENTATION (ev_window))
        return;

    update_chrome_flag (ev_window, EV_CHROME_FINDBAR, TRUE);
    update_chrome_visibility (ev_window);
    gtk_widget_grab_focus (ev_window->priv->find_bar);
    if (ev_window->priv->document->iswebdocument == FALSE) {
        ev_view_find_previous (EV_VIEW (ev_window->priv->view));
    }
#if ENABLE_EPUB
    else {
        ev_web_view_find_previous(EV_WEB_VIEW(ev_window->priv->webview));
    }
#endif
}

static void
ev_window_cmd_edit_copy (GSimpleAction *action,
                         GVariant      *parameter,
                         gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    g_return_if_fail (EV_IS_WINDOW (ev_window));
#if ENABLE_EPUB
    if (ev_window->priv->document->iswebdocument) {
        ev_web_view_copy(EV_WEB_VIEW(ev_window->priv->webview));
    } else
#endif
    {
        ev_view_copy (EV_VIEW (ev_window->priv->view));
    }
}



/* ev_window_update_fullscreen_action removed */

static void
ev_window_run_fullscreen (EvWindow *window)
{
    gboolean fullscreen_window = TRUE;

    if (ev_document_model_get_fullscreen (window->priv->model))
        return;

    if (EV_WINDOW_IS_PRESENTATION (window)) {
        ev_window_stop_presentation (window, FALSE);
        fullscreen_window = FALSE;
    }

    ev_document_model_set_fullscreen (window->priv->model, TRUE);
    /* residue */

    /* Don't show the fullscreen toolbar
     */
    update_chrome_flag (window, EV_CHROME_FULLSCREEN_TOOLBAR, FALSE);
    update_chrome_visibility (window);

    if (fullscreen_window)
        gtk_window_fullscreen (GTK_WINDOW (window));
    if (window->priv->view) {
        gtk_widget_grab_focus (window->priv->view);
    }
#if ENABLE_EPUB
    else  {
        gtk_widget_grab_focus(window->priv->webview);
    }
#endif
    if (window->priv->metadata && !ev_window_is_empty (window))
        ev_metadata_set_boolean (window->priv->metadata, "fullscreen", TRUE);
}

static void
ev_window_stop_fullscreen (EvWindow *window,
        gboolean  unfullscreen_window)
{
    if (!ev_document_model_get_fullscreen (window->priv->model))
        return;

    ev_document_model_set_fullscreen (window->priv->model, FALSE);
    /* residue */
    update_chrome_flag (window, EV_CHROME_FULLSCREEN_TOOLBAR, FALSE);
    update_chrome_visibility (window);
    if (unfullscreen_window)
        gtk_window_unfullscreen (GTK_WINDOW (window));

    if (window->priv->metadata && !ev_window_is_empty (window))
        ev_metadata_set_boolean (window->priv->metadata, "fullscreen", FALSE);
}

static void
ev_window_cmd_view_fullscreen (GSimpleAction *action,
                               GVariant      *state,
                               gpointer       user_data)
{
    EvWindow *window = EV_WINDOW (user_data);
    gboolean fullscreen;

    fullscreen = g_variant_get_boolean (state);
    if (fullscreen) {
        ev_window_run_fullscreen (window);
    } else {
        ev_window_stop_fullscreen (window, TRUE);
    }

    g_simple_action_set_state (action, state);
}

/* ev_window_update_presentation_action removed */

static void
ev_window_view_presentation_finished (EvWindow *window)
{
    ev_window_stop_presentation (window, TRUE);
}

static gboolean
ev_window_view_presentation_focus_in (EvWindow *window)
{
    ev_application_screensaver_disable (EV_APP);

    return FALSE;
}

static gboolean
ev_window_view_presentation_focus_out (EvWindow *window)
{
    ev_application_screensaver_enable (EV_APP);

    return FALSE;
}

static void
ev_window_run_presentation (EvWindow *window)
{
    gboolean fullscreen_window = TRUE;
    guint    current_page;
    guint    rotation;
    gboolean inverted_colors;
    gboolean rtl;

    if (EV_WINDOW_IS_PRESENTATION (window))
        return;

    if (window->priv->document->iswebdocument) {
        ev_window_warning_message(window,_("Presentation mode is not supported for ePub documents"));
        return;
    }
    if (ev_document_model_get_fullscreen (window->priv->model)) {
        ev_window_stop_fullscreen (window, FALSE);
        fullscreen_window = FALSE;
    }

    current_page = ev_document_model_get_page (window->priv->model);
    rotation = ev_document_model_get_rotation (window->priv->model);
    inverted_colors = ev_document_model_get_inverted_colors (window->priv->model);
    rtl = ev_document_model_get_rtl (window->priv->model);
    window->priv->presentation_view = ev_view_presentation_new (window->priv->document,
            current_page,
            rotation,
            inverted_colors);
    ev_view_presentation_set_rtl (EV_VIEW_PRESENTATION (window->priv->presentation_view), rtl);
    g_signal_connect_swapped (window->priv->presentation_view, "finished",
            G_CALLBACK (ev_window_view_presentation_finished),
            window);

    g_signal_connect_swapped (window->priv->presentation_view, "external-link",
            G_CALLBACK (view_external_link_cb),
            window);
    g_signal_connect_swapped (window->priv->presentation_view, "focus-in-event",
            G_CALLBACK (ev_window_view_presentation_focus_in),
            window);
    g_signal_connect_swapped (window->priv->presentation_view, "focus-out-event",
            G_CALLBACK (ev_window_view_presentation_focus_out),
            window);

    gtk_box_append (GTK_BOX (window->priv->main_box),
            window->priv->presentation_view);

    gtk_widget_hide (window->priv->hpaned);
    /* residue */
    update_chrome_visibility (window);

    gtk_widget_grab_focus (window->priv->presentation_view);
    if (fullscreen_window)
        gtk_window_fullscreen (GTK_WINDOW (window));

    gtk_widget_show (window->priv->presentation_view);

    ev_application_screensaver_disable (EV_APP);

    if (window->priv->metadata && !ev_window_is_empty (window))
        ev_metadata_set_boolean (window->priv->metadata, "presentation", TRUE);
}

static void
ev_window_stop_presentation (EvWindow *window,
                             gboolean  unfullscreen_window)
{
    guint current_page;
    guint rotation;

    if (!EV_WINDOW_IS_PRESENTATION (window))
        return;

    current_page = ev_view_presentation_get_current_page (EV_VIEW_PRESENTATION (window->priv->presentation_view));
    ev_document_model_set_page (window->priv->model, current_page);
    rotation = ev_view_presentation_get_rotation (EV_VIEW_PRESENTATION (window->priv->presentation_view));
    ev_document_model_set_rotation (window->priv->model, rotation);

    gtk_box_remove (GTK_BOX (window->priv->main_box),
            window->priv->presentation_view);
    window->priv->presentation_view = NULL;

    gtk_widget_show (window->priv->hpaned);
    /* residue */
    update_chrome_visibility (window);
    if (unfullscreen_window)
        gtk_window_unfullscreen (GTK_WINDOW (window));

    if (window->priv->view) {
        gtk_widget_grab_focus (window->priv->view);
    }
#if ENABLE_EPUB
    else {
        gtk_widget_grab_focus (window->priv->webview);
    }
#endif
    ev_application_screensaver_enable (EV_APP);

    if (window->priv->metadata && !ev_window_is_empty (window))
        ev_metadata_set_boolean (window->priv->metadata, "presentation", FALSE);
}

static void
ev_window_cmd_view_presentation (GSimpleAction *action,
                                 GVariant      *state,
                                 gpointer       user_data)
{
    EvWindow *window = EV_WINDOW (user_data);
    gboolean presentation;

    presentation = g_variant_get_boolean (state);
    if (presentation) {
        ev_window_run_presentation (window);
    }

    g_simple_action_set_state (action, state);
}

static void
ev_window_setup_gtk_settings (EvWindow *window)
{
    /* GTK4: "gtk-menu-bar-accel" property no longer exists.
     * Use F10 as the default menubar accelerator. */
    gtk_accelerator_parse ("F10",
            &window->priv->menubar_accel_keyval,
            &window->priv->menubar_accel_modifier);
}

static void
ev_window_update_max_min_scale (EvWindow *window)
{
    gdouble dpi = ev_document_misc_get_screen_dpi_at_window (GTK_WINDOW (window));
    ev_document_model_set_min_scale (window->priv->model, MIN_SCALE * dpi / 72.0);
    ev_document_model_set_max_scale (window->priv->model, MAX_SCALE * dpi / 72.0);
}

/* ev_window_screen_changed removed */

/* ev_window_state_event removed */

static void
ev_window_set_page_mode (EvWindow         *window,
                         EvWindowPageMode  page_mode)
{
    GtkWidget *child = NULL;
    GtkWidget *real_child;

    if (window->priv->page_mode == page_mode)
        return;

    window->priv->page_mode = page_mode;

    switch (page_mode) {
    case PAGE_MODE_DOCUMENT:
        if ( window->priv->document && window->priv->document->iswebdocument == FALSE ) {
            child = window->priv->view;
        }
#if ENABLE_EPUB
else {
    child=window->priv->webview;
}
#endif
break;
    case PAGE_MODE_PASSWORD:
        child = window->priv->password_view;
        break;
    default:
        g_assert_not_reached ();
    }

    real_child = gtk_scrolled_window_get_child (GTK_SCROLLED_WINDOW (window->priv->scrolled_window));
    if (child != real_child) {
        if (real_child)
            gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (window->priv->scrolled_window), NULL);
        gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (window->priv->scrolled_window),
                child);
    }
    ev_window_update_actions (window);
}


static void
ev_window_cmd_edit_rotate_left (GSimpleAction *action,
                                GVariant      *parameter,
                                gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    gint rotation;

    if (EV_WINDOW_IS_PRESENTATION (ev_window)) {
        rotation = ev_view_presentation_get_rotation (EV_VIEW_PRESENTATION (ev_window->priv->presentation_view));
        ev_view_presentation_set_rotation (EV_VIEW_PRESENTATION (ev_window->priv->presentation_view),
                rotation - 90);
    } else {
        rotation = ev_document_model_get_rotation (ev_window->priv->model);

        ev_document_model_set_rotation (ev_window->priv->model, rotation - 90);
    }
}

static void
ev_window_cmd_edit_rotate_right (GSimpleAction *action,
                                 GVariant      *parameter,
                                 gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    gint rotation;

    if (EV_WINDOW_IS_PRESENTATION (ev_window)) {
        rotation = ev_view_presentation_get_rotation (EV_VIEW_PRESENTATION (ev_window->priv->presentation_view));
        ev_view_presentation_set_rotation (EV_VIEW_PRESENTATION (ev_window->priv->presentation_view),
                rotation + 90);
    } else {
        rotation = ev_document_model_get_rotation (ev_window->priv->model);

        ev_document_model_set_rotation (ev_window->priv->model, rotation + 90);
    }
}

static void
ev_window_cmd_view_inverted_colors (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    gboolean inverted_colors = ev_document_model_get_inverted_colors (ev_window->priv->model);

    ev_document_model_set_inverted_colors (ev_window->priv->model, !inverted_colors);
}

static void
ev_window_cmd_edit_save_settings (GSimpleAction *action,
                                  GVariant      *parameter,
                                  gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    EvWindowPrivate *priv = ev_window->priv;
    EvDocumentModel *model = priv->model;
    GSettings       *settings = priv->default_settings;
    EvSizingMode     sizing_mode;

    g_settings_set_boolean (settings, "continuous",
            ev_document_model_get_continuous (model));
    g_settings_set_boolean (settings, "dual-page",
            ev_document_model_get_dual_page (model));
    g_settings_set_boolean (settings, "dual-page-odd-left",
            ev_document_model_get_dual_page_odd_pages_left (model));
    g_settings_set_boolean (settings, "fullscreen",
            ev_document_model_get_fullscreen (model));
    g_settings_set_boolean (settings, "inverted-colors",
            ev_document_model_get_inverted_colors (model));
    sizing_mode = ev_document_model_get_sizing_mode (model);
    g_settings_set_enum (settings, "sizing-mode", sizing_mode);
    if (sizing_mode == EV_SIZING_FREE) {
        gdouble zoom = ev_document_model_get_scale (model);

        zoom *= 72.0 / get_screen_dpi (ev_window);
        g_settings_set_double (settings, "zoom", zoom);
    }
    g_settings_set_boolean (settings, "show-sidebar",
            gtk_widget_get_visible (priv->sidebar));
    g_settings_set_int (settings, "sidebar-size",
            gtk_paned_get_position (GTK_PANED (priv->hpaned)));
    g_settings_set_string (settings, "sidebar-page",
            ev_window_sidebar_get_current_page_id (ev_window));
    g_settings_apply (settings);
}

static void
ev_window_cmd_view_zoom_in (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    g_return_if_fail (EV_IS_WINDOW (ev_window));

    ev_document_model_set_sizing_mode (ev_window->priv->model, EV_SIZING_FREE);
#if ENABLE_EPUB
    if (ev_window->priv->document->iswebdocument) {
        ev_web_view_zoom_in(EV_WEB_VIEW(ev_window->priv->webview));
    }
    else
#endif
    {
        ev_view_zoom_in (EV_VIEW (ev_window->priv->view));
    }
}

static void
ev_window_cmd_view_zoom_out (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    g_return_if_fail (EV_IS_WINDOW (ev_window));

    ev_document_model_set_sizing_mode (ev_window->priv->model, EV_SIZING_FREE);
#if ENABLE_EPUB
    if ( ev_window->priv->document->iswebdocument)  {
        ev_web_view_zoom_out(EV_WEB_VIEW(ev_window->priv->webview));
    }
    else
#endif
    {
        ev_view_zoom_out (EV_VIEW (ev_window->priv->view));
    }
}

static void
ev_window_cmd_view_zoom_reset (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    g_return_if_fail (EV_IS_WINDOW (ev_window));

    ev_document_model_set_sizing_mode (ev_window->priv->model, EV_SIZING_FREE);
#if ENABLE_EPUB
    if ( ev_window->priv->document->iswebdocument)  {
        ev_web_view_zoom_reset(EV_WEB_VIEW(ev_window->priv->webview));
    }
    else
#endif
    {
        ev_document_model_set_scale (ev_window->priv->model, get_screen_dpi (ev_window) / 72.0);
    }
}

static void
ev_window_cmd_view_zoom (GSimpleAction *action,
			 GVariant      *parameter,
			 gpointer       user_data)
{
    EvWindow *ev_window = user_data;
    EvWindowPrivate *priv = ev_window_get_instance_private (ev_window);
    gdouble zoom = g_variant_get_double (parameter);

    ev_document_model_set_sizing_mode (priv->model, EV_SIZING_FREE);
    ev_document_model_set_scale (priv->model,
                                 zoom * get_screen_dpi (ev_window) / 72.0);
}

static void
ev_window_cmd_go_previous_history (GSimpleAction *action,
                                   GVariant      *parameter,
                                   gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    g_return_if_fail (EV_IS_WINDOW (ev_window));
    ev_history_go_back (ev_window->priv->history);
}

static void
ev_window_cmd_go_next_history (GSimpleAction *action,
                               GVariant      *parameter,
                               gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    g_return_if_fail (EV_IS_WINDOW (ev_window));
    ev_history_go_forward (ev_window->priv->history);
}

static void
ev_window_cmd_go_previous_page (GSimpleAction *action,
                                GVariant      *parameter,
                                gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    g_return_if_fail (EV_IS_WINDOW (ev_window));
#if ENABLE_EPUB
    if ( ev_window->priv->document->iswebdocument == TRUE ) {
        ev_web_view_previous_page(EV_WEB_VIEW(ev_window->priv->webview));
    }
    else
#endif
    {
        ev_view_previous_page (EV_VIEW (ev_window->priv->view));
    }
}

static void
ev_window_cmd_go_next_page (GSimpleAction *action,
                            GVariant      *parameter,
                            gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    g_return_if_fail (EV_IS_WINDOW (ev_window));
#if ENABLE_EPUB
    if ( ev_window->priv->document->iswebdocument == TRUE ) {
        ev_web_view_next_page(EV_WEB_VIEW(ev_window->priv->webview));
    } else
#endif
    {
        ev_view_next_page (EV_VIEW (ev_window->priv->view));
    }
}

static void
ev_window_cmd_go_first_page (GSimpleAction *action,
                             GVariant      *parameter,
                             gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    g_return_if_fail (EV_IS_WINDOW (ev_window));

    ev_document_model_set_page (ev_window->priv->model, 0);
}

static void
ev_window_cmd_go_last_page (GSimpleAction *action,
                            GVariant      *parameter,
                            gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    g_return_if_fail (EV_IS_WINDOW (ev_window));

    ev_document_model_set_page (ev_window->priv->model,
            ev_document_get_n_pages (ev_window->priv->document) - 1);
}

/* ev_window_cmd_go_forward removed */

/* ev_window_cmd_go_backward removed */

static void
ev_window_cmd_reader_view (GSimpleAction *action,
                           GVariant      *parameter,
                           gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    g_return_if_fail (EV_IS_WINDOW (ev_window));

    ev_toolbar_activate_reader_view (EV_TOOLBAR (ev_window->priv->toolbar));
}

static void
ev_window_cmd_page_view (GSimpleAction *action,
                         GVariant      *parameter,
                         gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    g_return_if_fail (EV_IS_WINDOW (ev_window));

    ev_toolbar_activate_page_view (EV_TOOLBAR (ev_window->priv->toolbar));
}

static void
ev_window_cmd_bookmark_activate (GSimpleAction *action,
                                 GVariant      *parameter,
                                 gpointer       user_data)
{
    /* Bookmark activation stub for GTK4 */
}

/* compare_bookmarks removed */

static void
ev_window_setup_bookmarks (EvWindow *window)
{
    /* FIXME: Implement bookmark menu update using GMenu and GAction.
     * Legacy GtkUIManager and GtkAction are removed in GTK4.
     */
}

static void
ev_window_cmd_bookmarks_add (GSimpleAction *action,
                             GVariant      *parameter,
                             gpointer       user_data)
{
    EvWindow *window = EV_WINDOW (user_data);
    EvBookmark bm;
    gchar     *page_label;

    bm.page = ev_document_model_get_page (window->priv->model);
    page_label = ev_document_get_page_label (window->priv->document, bm.page);
    bm.title = g_strdup_printf (_("Page %s"), page_label);
    g_free (page_label);

    /* EvBookmarks takes ownership of bookmark */
    ev_bookmarks_add (window->priv->bookmarks, &bm);
}

static void
ev_window_cmd_view_reload (GSimpleAction *action,
                           GVariant      *parameter,
                           gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    ev_window_reload_document (ev_window, NULL);
}

static void
ev_window_cmd_view_expand_window (GSimpleAction *action,
                                  GVariant      *parameter,
                                  gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    g_return_if_fail (EV_IS_WINDOW (ev_window));

    zoom_control_changed_cb (NULL, EPHY_ZOOM_EXPAND_WINDOW_TO_FIT, ev_window);
}

static void
ev_window_cmd_view_autoscroll (GSimpleAction *action,
                               GVariant      *parameter,
                               gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    EvDocument* document = ev_window->priv->document;
    if (document->iswebdocument == TRUE ) {
        return ;
    }else {
        ev_view_autoscroll_start (EV_VIEW (ev_window->priv->view));
    }
}

#define EV_HELP "help:xreader"

static void
ev_window_cmd_help_contents (GSimpleAction *action,
                             GVariant      *parameter,
                             gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    ev_window_show_help(ev_window, NULL);
}

void
ev_window_show_help (EvWindow *ev_window,
                     const gchar *uri)
{
    GError  *error       = NULL;
    gchar   *help_page   = EV_HELP;

    if (uri) {
        help_page = g_strdup_printf ("%s:%s", EV_HELP, uri);
    }
    gtk_show_uri(GTK_WINDOW(ev_window), help_page, GDK_CURRENT_TIME);
    if (error) {
        ev_window_error_message(ev_window, error, "%s", _("There was an error displaying help"));
        g_error_free (error);
    }
}

static void
ev_window_cmd_leave_fullscreen (GSimpleAction *action,
                                GVariant      *parameter,
                                gpointer       user_data)
{
    EvWindow *window = EV_WINDOW (user_data);
    ev_window_stop_fullscreen (window, TRUE);
}

static void
ev_window_cmd_start_presentation (GSimpleAction *action,
                                  GVariant      *parameter,
                                  gpointer       user_data)
{
    EvWindow *window = EV_WINDOW (user_data);
    ev_window_run_presentation (window);
}

static void
ev_window_cmd_escape (GSimpleAction *action,
                      GVariant      *parameter,
                      gpointer       user_data)
{
    EvWindow *window = EV_WINDOW (user_data);
    GtkWidget *widget;

    if (window->priv->document && !window->priv->document->iswebdocument && window->priv->view)
        ev_view_autoscroll_stop (EV_VIEW (window->priv->view));

    if (gtk_widget_is_visible (window->priv->find_bar)) {
        update_chrome_flag (window, EV_CHROME_FINDBAR, FALSE);
        update_chrome_visibility (window);
        return;
    }

    widget = gtk_window_get_focus (GTK_WINDOW (window));
    if (widget && gtk_widget_get_ancestor (widget, EGG_TYPE_FIND_BAR)) {
        update_chrome_flag (window, EV_CHROME_FINDBAR, FALSE);
        update_chrome_visibility (window);

        if (window->priv->view)
            gtk_widget_grab_focus (window->priv->view);
#if ENABLE_EPUB
        else
            gtk_widget_grab_focus (window->priv->webview);
#endif
    } else {
        gboolean fullscreen;

        fullscreen = ev_document_model_get_fullscreen (window->priv->model);

        if (fullscreen) {
            ev_window_stop_fullscreen (window, TRUE);
        } else if (EV_WINDOW_IS_PRESENTATION (window)) {
            ev_window_stop_presentation (window, TRUE);
            gtk_widget_grab_focus (window->priv->view);
        } else {
            if (window->priv->view)
                gtk_widget_grab_focus (window->priv->view);
#if ENABLE_EPUB
            else
                gtk_widget_grab_focus (window->priv->webview);
#endif
        }

        if (fullscreen && EV_WINDOW_IS_PRESENTATION (window))
            g_warning ("Both fullscreen and presentation set somehow");
    }
}

static void
save_sizing_mode (EvWindow *window)
{
    EvSizingMode mode;

    if (!window->priv->metadata || ev_window_is_empty (window))
        return;

    mode = ev_document_model_get_sizing_mode (window->priv->model);
    ev_metadata_set_string (window->priv->metadata, "sizing_mode",
            sizing_mode_to_string (mode));
}

static void
ev_window_document_changed_cb (EvDocumentModel *model,
                               GParamSpec      *pspec,
                               EvWindow        *ev_window)
{
    ev_window_set_document (ev_window,
            ev_document_model_get_document (model));
}

static void
ev_window_sizing_mode_changed_cb (EvDocumentModel *model,
                                  GParamSpec      *pspec,
                                  EvWindow        *ev_window)
{
    EvSizingMode sizing_mode = ev_document_model_get_sizing_mode (model);

    g_object_set (ev_window->priv->scrolled_window,
            "hscrollbar-policy",
            sizing_mode == EV_SIZING_FREE ?
                    GTK_POLICY_AUTOMATIC : GTK_POLICY_NEVER,
                    "vscrollbar-policy", GTK_POLICY_AUTOMATIC,
                    NULL);

    /* residue */
    save_sizing_mode (ev_window);
}

static void
ev_window_zoom_changed_cb (EvDocumentModel *model,
                           GParamSpec *pspec,
                           EvWindow *ev_window)
{
    ev_window_update_actions (ev_window);

    if (!ev_window->priv->metadata)
        return;

    if (ev_document_model_get_sizing_mode (model) == EV_SIZING_FREE && !ev_window_is_empty (ev_window)) {
        gdouble zoom;

        zoom = ev_document_model_get_scale (model);
        zoom *= 72.0 / get_screen_dpi (ev_window);
        ev_metadata_set_double (ev_window->priv->metadata, "zoom", zoom);
    }
}

/* ev_window_update_continuous_action removed */

static void
ev_window_continuous_changed_cb (EvDocumentModel *model,
                                 GParamSpec      *pspec,
                                 EvWindow        *ev_window)
{
    /* residue */

    if (ev_window->priv->metadata && !ev_window_is_empty (ev_window))
        ev_metadata_set_boolean (ev_window->priv->metadata, "continuous",
                ev_document_model_get_continuous (model));
}

static void
ev_window_rotation_changed_cb (EvDocumentModel *model,
                               GParamSpec      *pspec,
                               EvWindow        *window)
{
    gint rotation = ev_document_model_get_rotation (model);

    if (window->priv->metadata && !ev_window_is_empty (window))
        ev_metadata_set_int (window->priv->metadata, "rotation",
                rotation);

    ev_window_update_max_min_scale (window);
    ev_window_refresh_window_thumbnail (window);
}

/* ev_window_update_inverted_colors_action removed */

static void
ev_window_inverted_colors_changed_cb (EvDocumentModel *model,
                                      GParamSpec      *pspec,
                                      EvWindow        *window)
{
    gboolean inverted_colors = ev_document_model_get_inverted_colors (model);

    /* residue */

    if (window->priv->metadata && !ev_window_is_empty (window))
        ev_metadata_set_boolean (window->priv->metadata, "inverted-colors",
                inverted_colors);

    ev_window_refresh_window_thumbnail (window);
}

/* ev_window_update_dual_page_action removed */

static void
ev_window_dual_mode_changed_cb (EvDocumentModel *model,
                                GParamSpec      *pspec,
                                EvWindow        *ev_window)
{
    /* residue */

    if (ev_window->priv->metadata && !ev_window_is_empty (ev_window))
        ev_metadata_set_boolean (ev_window->priv->metadata, "dual-page",
                ev_document_model_get_dual_page (model));
}

/* ev_window_update_dual_page_odd_pages_left_action removed */

/* ev_window_update_rtl_action removed */

static void
ev_window_dual_mode_odd_pages_left_changed_cb (EvDocumentModel *model,
                                               GParamSpec      *pspec,
                                               EvWindow        *ev_window)
{
    /* residue */

    if (ev_window->priv->metadata && !ev_window_is_empty (ev_window))
        ev_metadata_set_boolean (ev_window->priv->metadata, "dual-page-odd-left",
                ev_document_model_get_dual_page_odd_pages_left (model));
}

static void
ev_window_direction_changed_cb (EvDocumentModel *model,
                          GParamSpec      *pspec,
                          EvWindow        *ev_window)
{
    /* residue */

    if (ev_window->priv->metadata && !ev_window_is_empty (ev_window))
      ev_metadata_set_boolean (ev_window->priv->metadata, "rtl",
             ev_document_model_get_rtl (model));
}

static void
ev_window_cmd_help_about (GSimpleAction *action,
                          GVariant      *parameter,
                          gpointer       user_data)
{
    EvWindow  *ev_window = EV_WINDOW (user_data);
    const char *license[] = {
            N_("Xreader is free software; you can redistribute it and/or modify "
                    "it under the terms of the GNU General Public License as published by "
                    "the Free Software Foundation; either version 2 of the License, or "
                    "(at your option) any later version.\n"),
            N_("Xreader is distributed in the hope that it will be useful, "
                    "but WITHOUT ANY WARRANTY; without even the implied warranty of "
                    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the "
                    "GNU General Public License for more details.\n"),
            N_("You should have received a copy of the GNU General Public License "
                    "along with Xreader; if not, write to the Free Software Foundation, Inc., "
                    "51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA\n")
    };

    char *license_trans;
    license_trans = g_strconcat (_(license[0]), "\n", _(license[1]), "\n",
            _(license[2]), "\n", NULL);

    gtk_show_about_dialog (
            GTK_WINDOW (ev_window),
            "program-name", _("Xreader"),
            "version", VERSION,
            "license", license_trans,
            "website", "http://www.github.com/linuxmint/xreader/",
            "logo-icon-name", "xreader",
            "wrap-license", TRUE,
            NULL);

    g_free (license_trans);
}

static void
ev_window_view_menubar_cb (GSimpleAction *action,
                           GVariant      *state,
                           gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    gboolean active = g_variant_get_boolean (state);

    g_simple_action_set_state (action, state);
    g_settings_set_boolean (ev_window->priv->settings, "show-menubar", active);
    update_chrome_flag (ev_window, EV_CHROME_MENUBAR, active);
    update_chrome_visibility (ev_window);
}



static void
ev_window_view_toolbar_cb (GSimpleAction *action,
                           GVariant      *state,
                           gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    gboolean active = g_variant_get_boolean (state);

    g_settings_set_boolean (ev_window->priv->settings, "show-toolbar", active);
    update_chrome_flag (ev_window, EV_CHROME_TOOLBAR, active);
    update_chrome_visibility (ev_window);
}

static void
ev_window_view_sidebar_cb (GSimpleAction *action,
                           GVariant      *state,
                           gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    gboolean active = g_variant_get_boolean (state);

    if (EV_WINDOW_IS_PRESENTATION (ev_window))
        return;

    update_chrome_flag (ev_window, EV_CHROME_SIDEBAR, active);
    update_chrome_visibility (ev_window);
}

static void
ev_window_sidebar_current_page_changed_cb (EvSidebar  *ev_sidebar,
                                           GParamSpec *pspec,
                                           EvWindow   *ev_window)
{
    if (ev_window->priv->metadata && !ev_window_is_empty (ev_window)) {
        ev_metadata_set_string (ev_window->priv->metadata,
                "sidebar_page",
                ev_window_sidebar_get_current_page_id (ev_window));
    }
}

static void
ev_window_sidebar_visibility_changed_cb (EvSidebar  *ev_sidebar,
                                         GParamSpec *pspec,
                                         EvWindow   *ev_window)
{
    if (!EV_WINDOW_IS_PRESENTATION (ev_window)) {
        gboolean visible = gtk_widget_get_visible (GTK_WIDGET (ev_sidebar));
        GAction *action = g_action_map_lookup_action (G_ACTION_MAP (ev_window), "ViewSidebar");

        if (action)
            g_simple_action_set_state (G_SIMPLE_ACTION (action), g_variant_new_boolean (visible));

        if (ev_window->priv->metadata)
            ev_metadata_set_boolean (ev_window->priv->metadata, "sidebar_visibility",
                    visible);
        if (!visible)
            gtk_widget_grab_focus (ev_window->priv->view);
    }
}





static gboolean
attachment_bar_menu_popup_cb (EvSidebarAttachments *attachbar,
                              GList           *attach_list,
                              EvWindow        *ev_window)
{
    /* residue */

    g_assert (attach_list != NULL);

    if (ev_window->priv->attach_list) {
        g_list_foreach (ev_window->priv->attach_list,
                (GFunc) g_object_unref, NULL);
        g_list_free (ev_window->priv->attach_list);
    }

    ev_window->priv->attach_list = attach_list;

    /* residue */

    /* gtk_menu_popup removed (GTK3 residue) */

    return TRUE;
}

static void
ev_window_update_find_status_message (EvWindow *ev_window)
{
    gchar *message;

    if (!ev_window->priv->find_job)
        return;

    if (ev_job_is_finished (ev_window->priv->find_job)) {
        EvJobFind *job_find = EV_JOB_FIND (ev_window->priv->find_job);

        if (ev_job_find_has_results (job_find)) {
            gint n_results;

            n_results = ev_job_find_get_n_results (job_find,
                    ev_document_model_get_page (ev_window->priv->model));
            /* TRANS: Sometimes this could be better translated as
               "%d hit(s) on this page".  Therefore this string
               contains plural cases. */
            message = g_strdup_printf (ngettext ("%d found on this page (%d total)",
                             "%d found on this page (%d total)",
                             n_results),
                             n_results, job_find->total_count);
        } else {
            message = g_strdup (_("Not found"));
        }
    } else {
        gdouble percent;

        percent = ev_job_find_get_progress (EV_JOB_FIND (ev_window->priv->find_job));
        message = g_strdup_printf (_("%3d%% remaining to search"),
                (gint) ((1.0 - percent) * 100));
    }

    egg_find_bar_set_status_text (EGG_FIND_BAR (ev_window->priv->find_bar), message);
    g_free (message);
}

static void
ev_window_find_job_finished_cb (EvJobFind *job,
                                EvWindow  *ev_window)
{
    ev_window_update_find_status_message (ev_window);
}

static void
ev_window_find_job_updated_cb (EvJobFind *job,
                               gint       page,
                               EvWindow  *ev_window)
{
    ev_window_update_actions (ev_window);
#if ENABLE_EPUB
if (ev_window->priv->document->iswebdocument == TRUE ) {
    ev_web_view_find_changed(EV_WEB_VIEW(ev_window->priv->webview),
            job->results,job->text, job->case_sensitive);
}
else
#endif
{
    ev_view_find_changed (EV_VIEW (ev_window->priv->view),
            ev_job_find_get_results (job),
            page);
}
ev_window_update_find_status_message (ev_window);
}

static void
ev_window_clear_find_job (EvWindow *ev_window)
{
    if (ev_window->priv->find_job != NULL) {
        if (!ev_job_is_finished (ev_window->priv->find_job))
            ev_job_cancel (ev_window->priv->find_job);

        g_signal_handlers_disconnect_by_func (ev_window->priv->find_job,
                ev_window_find_job_finished_cb,
                ev_window);
        g_signal_handlers_disconnect_by_func (ev_window->priv->find_job,
                ev_window_find_job_updated_cb,
                ev_window);
        g_object_unref (ev_window->priv->find_job);
        ev_window->priv->find_job = NULL;
    }
}

static void
find_bar_previous_cb (EggFindBar *find_bar,
                      EvWindow   *ev_window)
{
#if ENABLE_EPUB
    if (ev_window->priv->document->iswebdocument == TRUE ) {
        ev_web_view_find_previous(EV_WEB_VIEW(ev_window->priv->webview));
    }else
#endif
    {
        ev_view_find_previous (EV_VIEW (ev_window->priv->view));
    }
}

static void
find_bar_next_cb (EggFindBar *find_bar,
                  EvWindow   *ev_window)
{
#if ENABLE_EPUB
    if (ev_window->priv->document->iswebdocument == TRUE ) {
        ev_web_view_find_next(EV_WEB_VIEW(ev_window->priv->webview));
    } else
#endif
    {
        ev_view_find_next (EV_VIEW (ev_window->priv->view));
    }
}

static void
find_bar_close_cb (EggFindBar *find_bar,
                   EvWindow   *ev_window)
{
#if ENABLE_EPUB
    if (ev_window->priv->document->iswebdocument == TRUE ) {
        ev_web_view_find_cancel(EV_WEB_VIEW(ev_window->priv->webview));
    }
    else
#endif
    {
        ev_view_find_cancel (EV_VIEW (ev_window->priv->view));
    }
    ev_window_clear_find_job (ev_window);
    update_chrome_flag (ev_window, EV_CHROME_FINDBAR, FALSE);
    update_chrome_visibility (ev_window);
}

static void
ev_window_search_start (EvWindow *ev_window)
{
    EggFindBar *find_bar = EGG_FIND_BAR (ev_window->priv->find_bar);
    const char *search_string;

    if (!ev_window->priv->document || !EV_IS_DOCUMENT_FIND (ev_window->priv->document))
        return;

    search_string = egg_find_bar_get_search_string (find_bar);
    gboolean has_string = (search_string && search_string[0]);

    if (has_string && ev_window->priv->find_job != NULL && ev_job_is_finished (ev_window->priv->find_job)) {
        EvJobFind *job_find = EV_JOB_FIND (ev_window->priv->find_job);
        const gchar *searched_string = ev_job_find_get_text (job_find);

        if (!ev_job_find_has_results (job_find) && strstr (search_string, searched_string)) {
            egg_find_bar_set_status_text (find_bar, _("Not found"));
            return;
        }
    }

    ev_window_clear_find_job (ev_window);

    if (has_string) {
        ev_window->priv->find_job = ev_job_find_new (ev_window->priv->document,
                                                     ev_document_model_get_page (ev_window->priv->model),
                                                     ev_document_get_n_pages (ev_window->priv->document),
                                                     search_string,
                                                     egg_find_bar_get_case_sensitive (find_bar));

        g_signal_connect (ev_window->priv->find_job, "finished",
                          G_CALLBACK (ev_window_find_job_finished_cb),
                          ev_window);
        g_signal_connect (ev_window->priv->find_job, "updated",
                          G_CALLBACK (ev_window_find_job_updated_cb),
                          ev_window);
        ev_job_scheduler_push_job (ev_window->priv->find_job, EV_JOB_PRIORITY_NONE);
    } else {
        ev_window_update_actions (ev_window);
        egg_find_bar_set_status_text (find_bar, NULL);
        if (ev_window->priv->document->iswebdocument == FALSE) {
            gtk_widget_queue_draw (GTK_WIDGET (ev_window->priv->view));
        }
    }
}

static void
find_bar_search_changed_cb (EggFindBar *find_bar,
                            GParamSpec *param,
                            EvWindow *ev_window)
{
    /* Either the string or case sensitivity could have changed. */
#if ENABLE_EPUB
    if (ev_window->priv->document->iswebdocument) {
        ev_web_view_find_search_changed(EV_WEB_VIEW (ev_window->priv->webview));
    } else
#endif
    {
        ev_view_find_search_changed (EV_VIEW (ev_window->priv->view));
    }

    ev_window_search_start (ev_window);
}

static void
find_bar_visibility_changed_cb (EggFindBar *find_bar,
                                GParamSpec *param,
                                EvWindow   *ev_window)
{
    gboolean visible;
    visible = gtk_widget_get_visible (GTK_WIDGET (find_bar));

    if (ev_window->priv->document && EV_IS_DOCUMENT_FIND (ev_window->priv->document)) {

        if (!ev_window->priv->document->iswebdocument) {
            ev_view_find_set_highlight_search (EV_VIEW (ev_window->priv->view), visible);
        }
#if ENABLE_EPUB
        else {
            ev_web_view_find_search_changed(EV_WEB_VIEW(ev_window->priv->webview));
            ev_web_view_set_handler(EV_WEB_VIEW(ev_window->priv->webview),visible);
        }
#endif
        ev_window_update_actions (ev_window);

        if (visible)
            ev_window_search_start (ev_window);
        else
        {
            egg_find_bar_set_status_text (EGG_FIND_BAR (ev_window->priv->find_bar), NULL);
            egg_find_bar_set_search_string (EGG_FIND_BAR (ev_window->priv->find_bar), NULL);
        }
    }
}

static void
find_bar_scroll (EggFindBar   *find_bar,
                 GtkScrollType scroll,
                 EvWindow     *ev_window)
{
    if (ev_window->priv->document->iswebdocument == TRUE)
        return;
    ev_view_scroll (EV_VIEW (ev_window->priv->view), scroll, FALSE);
}

static void
zoom_control_changed_cb (EvZoomAction *action,
                         float           zoom,
                         EvWindow       *ev_window)
{
    EvSizingMode mode;
    GtkWindow *window;
    gdouble doc_width, doc_height, scale;
    gint old_width, old_height;
    gint new_width, new_height;

    if (zoom == EPHY_ZOOM_EXPAND_WINDOW_TO_FIT) {
        window = GTK_WINDOW (ev_window);

        ev_document_get_max_page_size (ev_window->priv->document, &doc_width, &doc_height);
        scale = ev_document_model_get_scale (ev_window->priv->model);

        new_width = (gint)(doc_width * scale);
        new_height = (gint)(doc_height * scale);

        /*
         * If the sidebar, menu bar, or tool bars are open,
         * we must account for their sizes in calculating
         * the new expanded window size.
         */


        if (ev_window->priv->chrome & EV_CHROME_SIDEBAR)
        {
            GtkAllocation alloc;
            gtk_widget_get_allocation(ev_window->priv->sidebar_thumbs, &alloc);
            new_width += alloc.width;
        }
        if (ev_window->priv->chrome & EV_CHROME_TOOLBAR)
        {
            GtkAllocation alloc;
            gtk_widget_get_allocation(GTK_WIDGET(ev_window->priv->toolbar), &alloc);
            new_height += alloc.height;
        }
        if (ev_window->priv->chrome & EV_CHROME_MENUBAR)
        {
            GtkAllocation alloc;
            gtk_widget_get_allocation(GTK_WIDGET(ev_window->priv->menubar), &alloc);
            new_height += alloc.height;
        }

        /*
         * Add a little slack
         */
        new_width += 50;
        new_height += 50;

        /*
         * Only resize if the old window isn't already
         * big enough.
         */
        gtk_window_get_default_size(window, &old_width, &old_height);
        if (!(old_width >= new_width && old_height >= new_height))
            gtk_window_set_default_size (GTK_WINDOW (window), new_width, new_height);

        return;
    }

    if (zoom == EPHY_ZOOM_BEST_FIT) {
        mode = EV_SIZING_BEST_FIT;
    } else if (zoom == EPHY_ZOOM_FIT_WIDTH) {
        mode = EV_SIZING_FIT_WIDTH;
    } else {
        mode = EV_SIZING_FREE;
    }

    ev_document_model_set_sizing_mode (ev_window->priv->model, mode);

    if (mode == EV_SIZING_FREE) {
        ev_document_model_set_scale (ev_window->priv->model,
                zoom);
    }
}

/* ev_window_drag_data_received removed */

static void
ev_window_dispose (GObject *object)
{
    EvWindow *window = EV_WINDOW (object);
    EvWindowPrivate *priv = window->priv;

#ifdef ENABLE_DBUS
    if (priv->skeleton != NULL) {
        ev_window_emit_closed (window);

        g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (priv->skeleton));
        g_object_unref (priv->skeleton);
        priv->skeleton = NULL;
        g_free (priv->dbus_object_path);
        priv->dbus_object_path = NULL;
    }
#endif /* ENABLE_DBUS */

    if (priv->bookmarks) {
        g_object_unref (priv->bookmarks);
        priv->bookmarks = NULL;
    }

    if (priv->metadata) {
        g_object_unref (priv->metadata);
        priv->metadata = NULL;
    }

    if (priv->setup_document_idle > 0) {
        g_source_remove (priv->setup_document_idle);
        priv->setup_document_idle = 0;
    }

    if (priv->monitor) {
        g_object_unref (priv->monitor);
        priv->monitor = NULL;
    }

    if (priv->title) {
        ev_window_title_free (priv->title);
        priv->title = NULL;
    }

    /* ui_manager block removed */

    /* action_group block removed */

    /* action group residue */

    /* action group residue */

    /* recent_action_group residue */

    /* bookmarks_action_group residue */

    if (priv->recent_manager) {
        g_signal_handlers_disconnect_by_func (priv->recent_manager,
                NULL, /* removed */
                window);
        priv->recent_manager = NULL;
    }


    if (priv->settings) {
        g_object_unref (priv->settings);
        priv->settings = NULL;
    }

    if (priv->default_settings) {
        g_settings_apply (priv->default_settings);
        g_object_unref (priv->default_settings);
        priv->default_settings = NULL;
    }

    priv->recent_ui_id = 0;

    if (priv->model) {
        g_signal_handlers_disconnect_by_func (priv->model,
                NULL, /* removed */
                window);
        g_object_unref (priv->model);
        priv->model = NULL;
    }

    if (priv->document) {
        g_object_unref (priv->document);
        priv->document = NULL;
    }

    if (priv->view) {
        g_object_unref (priv->view);
        priv->view = NULL;
    }

    if (priv->password_view) {
        g_object_unref (priv->password_view);
        priv->password_view = NULL;
    }

    if (priv->load_job) {
        ev_window_clear_load_job (window);
    }

    if (priv->reload_job) {
        ev_window_clear_reload_job (window);
    }

    if (priv->save_job) {
        ev_window_clear_save_job (window);
    }

    if (priv->thumbnail_job) {
        ev_window_clear_thumbnail_job (window);
    }

    if (priv->find_job) {
        ev_window_clear_find_job (window);
    }

    if (priv->local_uri) {
        ev_window_clear_local_uri (window);
        priv->local_uri = NULL;
    }

    ev_window_clear_progress_idle (window);
    if (priv->progress_cancellable) {
        g_object_unref (priv->progress_cancellable);
        priv->progress_cancellable = NULL;
    }

    ev_window_close_dialogs (window);

    if (priv->link) {
        g_object_unref (priv->link);
        priv->link = NULL;
    }

    if (priv->image) {
        g_object_unref (priv->image);
        priv->image = NULL;
    }

    if (priv->annot) {
        g_object_unref (priv->annot);
        priv->annot = NULL;
    }

    if (priv->attach_list) {
        g_list_foreach (priv->attach_list,
                (GFunc) g_object_unref,
                NULL);
        g_list_free (priv->attach_list);
        priv->attach_list = NULL;
    }

    if (priv->find_bar) {
        g_signal_handlers_disconnect_by_func
        (window->priv->find_bar,
                G_CALLBACK (find_bar_close_cb),
                window);
        priv->find_bar = NULL;
    }

    if (priv->uri) {
        g_free (priv->uri);
        priv->uri = NULL;
    }

    if (priv->search_string) {
        g_free (priv->search_string);
        priv->search_string = NULL;
    }

    if (priv->dest) {
        g_object_unref (priv->dest);
        priv->dest = NULL;
    }

    if (priv->history) {
        g_object_unref (priv->history);
        priv->history = NULL;
    }

    if (priv->print_queue) {
        g_queue_free (priv->print_queue);
        priv->print_queue = NULL;
    }

    G_OBJECT_CLASS (ev_window_parent_class)->dispose (object);
}

static void
menubar_deactivate_cb (GtkWidget *menubar,
                       EvWindow  *window)
{
    g_signal_handlers_disconnect_by_func (menubar,
            G_CALLBACK (menubar_deactivate_cb),
            window);

    /* gtk_menu_shell_deselect (Obsolete) */

    update_chrome_visibility (window);
}

/* Function is_alt_key_event removed (GTK3 residue) */

/*
 * GtkWindow catches keybindings for the menu items _before_ passing them to
 * the focused widget. This is unfortunate and means that pressing Ctrl+a,
 * Ctrl+left or Ctrl+right in the search bar ends up selecting text in the EvView
 * or rotating it.
 * Here we override GtkWindow's handler to do the same things that it
 * does, but in the opposite order and then we chain up to the grand
 * parent handler, skipping gtk_window_key_press_event.
 */
/* Removed legacy GdkEventKey function */

/* Removed legacy GdkEventKey function */

G_GNUC_UNUSED static gboolean
ev_window_delete_event (GtkWindow *window, gpointer user_data)
{
    return !ev_window_close (EV_WINDOW (window));
}

static void
ev_window_class_init (EvWindowClass *ev_window_class)
{
    GObjectClass *g_object_class = G_OBJECT_CLASS (ev_window_class);
    /* GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (ev_window_class); */

    g_object_class->dispose = ev_window_dispose;

    /* widget_class->delete_event = ev_window_delete_event; (GTK3 residue) */
    /* widget_class->key_press_event = ev_window_key_press_event; (GTK3 residue) */
    /* widget_class->key_release_event = ev_window_key_release_event; (GTK3 residue) */
    /* widget_class->screen_changed = ev_window_screen_changed; (GTK3 residue) */
    /* widget_class->window_state_event = ev_window_state_event; (GTK3 residue) */
    /* widget_class->drag_data_received = ev_window_drag_data_received; (GTK3 residue) */
}

/* Normal items */
static const GActionEntry entries[] = {
    { "FileOpen", ev_window_cmd_file_open },
    { "FileOpenCopy", ev_window_cmd_file_open_copy },
    { "FileSaveAs", ev_window_cmd_save_as },
    { "FilePrint", ev_window_cmd_file_print },
    { "FileProperties", ev_window_cmd_file_properties },
    { "FileCloseAllWindows", ev_window_cmd_file_close_all_windows },
    { "FileCloseWindow", ev_window_cmd_file_close_window },
    { "TabNew", ev_window_cmd_new_tab },
    { "TabClose", ev_window_cmd_close_tab },
    { "TabNext", ev_window_cmd_next_tab },
    { "TabPrev", ev_window_cmd_prev_tab },
    { "EditCopy", ev_window_cmd_edit_copy },
    { "EditSelectAll", ev_window_cmd_edit_select_all },
    { "EditFind", ev_window_cmd_edit_find },
    { "EditFindNext", ev_window_cmd_edit_find_next },
    { "EditFindPrevious", ev_window_cmd_edit_find_previous },
    { "EditRotateLeft", ev_window_cmd_edit_rotate_left },
    { "EditRotateRight", ev_window_cmd_edit_rotate_right },
    { "EditSaveSettings", ev_window_cmd_edit_save_settings },
    { "EditPreferences", ev_window_cmd_edit_preferences },
    { "ViewZoomReset", ev_window_cmd_view_zoom_reset },
    { "ViewZoomIn", ev_window_cmd_view_zoom_in },
    { "ViewZoomOut", ev_window_cmd_view_zoom_out },
    { "ViewZoom", ev_window_cmd_view_zoom, "d" },
    { "ViewReload", ev_window_cmd_view_reload },
    { "ViewExpandWindow", ev_window_cmd_view_expand_window },
    { "ViewAutoscroll", ev_window_cmd_view_autoscroll },
    { "GoPreviousPage", ev_window_cmd_go_previous_page },
    { "GoNextPage", ev_window_cmd_go_next_page },
    { "GoFirstPage", ev_window_cmd_go_first_page },
    { "GoLastPage", ev_window_cmd_go_last_page },
    { "GoPreviousHistory", ev_window_cmd_go_previous_history },
    { "GoNextHistory", ev_window_cmd_go_next_history },
    { "BookmarksAdd", ev_window_cmd_bookmarks_add },
    { "HelpContents", ev_window_cmd_help_contents },
    { "HelpAbout", ev_window_cmd_help_about },
    { "LeaveFullscreen", ev_window_cmd_leave_fullscreen },
    { "StartPresentation", ev_window_cmd_start_presentation },
    { "ViewReaderView", ev_window_cmd_reader_view },
    { "ViewPageView", ev_window_cmd_page_view },
    { "ViewBookmark", ev_window_cmd_bookmark_activate, "u" },

    /* Accels and misc */
    { "Escape", ev_window_cmd_escape },
    { "Slash", ev_window_cmd_edit_find },
    { "F3", ev_window_cmd_edit_find_next },
    { "PageDown", ev_window_cmd_scroll_forward },
    { "PageUp", ev_window_cmd_scroll_backward },
    { "Space", ev_window_cmd_scroll_forward },
    { "ShiftSpace", ev_window_cmd_scroll_backward },
    { "BackSpace", ev_window_cmd_scroll_backward },
    { "ShiftBackSpace", ev_window_cmd_scroll_forward },
    { "Return", ev_window_cmd_scroll_forward },
    { "ShiftReturn", ev_window_cmd_scroll_backward },

    /* Toggle items */
    { "ViewMenubar", NULL, NULL, "true", ev_window_view_menubar_cb },
    { "ViewToolbar", NULL, NULL, "true", ev_window_view_toolbar_cb },
    { "ViewSidebar", NULL, NULL, "true", ev_window_view_sidebar_cb },
    { "ViewContinuous", NULL, NULL, "true", ev_window_cmd_continuous },
    { "ViewDual", NULL, NULL, "false", ev_window_cmd_dual },
    { "ViewDualOddLeft", NULL, NULL, "false", ev_window_cmd_dual_odd_pages_left },
    { "ViewRtl", NULL, NULL, "false", ev_window_cmd_rtl },
    { "ViewFullscreen", NULL, NULL, "false", ev_window_cmd_view_fullscreen },
    { "ViewPresentation", NULL, NULL, "false", ev_window_cmd_view_presentation },
    { "ViewBestFit", NULL, NULL, "false", ev_window_cmd_view_best_fit },
    { "ViewPageWidth", NULL, NULL, "false", ev_window_cmd_view_page_width },
    { "ViewInvertedColors", NULL, NULL, "false", ev_window_cmd_view_inverted_colors }
};

static const GActionEntry view_popup_entries [] = {
	{ "open-link", ev_view_popup_cmd_open_link },
	{ "go-link", ev_view_popup_cmd_open_link },
	{ "open-link-new-window", ev_view_popup_cmd_open_link_new_window },
	{ "copy-link-address", ev_view_popup_cmd_copy_link_address },
	{ "save-image-as", ev_view_popup_cmd_save_image_as },
	{ "copy-image", ev_view_popup_cmd_copy_image },
	{ "annot-properties", ev_view_popup_cmd_annot_properties },
	{ "remove-annotation", ev_view_popup_cmd_remove_annotation }
};

static const GActionEntry attachment_popup_entries [] = {
	{ "open-attachment", ev_attachment_popup_cmd_open_attachment },
	{ "save-attachment-as", ev_attachment_popup_cmd_save_attachment_as }
};



static void
sidebar_links_link_activated_cb (EvSidebarLinks *sidebar_links,
                                 EvLink         *link,
                                 EvWindow       *window)
{
    if (window->priv->document->iswebdocument == FALSE ) {
        ev_view_handle_link (EV_VIEW (window->priv->view), link);
    }
#if ENABLE_EPUB
    else {
        ev_web_view_handle_link(EV_WEB_VIEW(window->priv->webview), link);
    }
#endif
}

static void
history_changed_cb (EvHistory *history,
                    EvWindow  *window)
{
    ev_window_update_actions(window);
}

static void
activate_link_cb (GObject  *object,
                  EvLink   *link,
                  EvWindow *window)
{
    if (window->priv->view) {
        ev_view_handle_link (EV_VIEW (window->priv->view), link);
        gtk_widget_grab_focus (window->priv->view);
    }
#if ENABLE_EPUB
    else {
        ev_web_view_handle_link (EV_WEB_VIEW (window->priv->webview), link);
        gtk_widget_grab_focus (window->priv->webview);
    }
#endif
}

static void
sidebar_layers_visibility_changed (EvSidebarLayers *layers,
                                   EvWindow        *window)
{
    if (window->priv->document->iswebdocument == FALSE ) {
        ev_view_reload (EV_VIEW (window->priv->view));
    }
#if ENABLE_EPUB
    else
    {
        ev_web_view_reload(EV_WEB_VIEW(window->priv->webview));
    }
#endif
}

static void
sidebar_thumbnails_size_changed   (EvSidebarThumbnails  *sidebar_thumbnails,
                                   gint                  new_size,
                                   EvWindow             *window)
{
	if (window->priv->metadata)
		ev_metadata_set_int (window->priv->metadata, "thumbnails_size", new_size);
}

static void
sidebar_annots_annot_activated_cb (EvSidebarAnnotations *sidebar_annots,
                                   EvMapping            *annot_mapping,
                                   EvWindow             *window)
{
    if (window->priv->document->iswebdocument == TRUE ) return;
    ev_view_focus_annotation (EV_VIEW (window->priv->view), annot_mapping);
}

static void
ev_window_begin_add_annot (EvSidebarAnnotations *sidebar_annots,
		EvAnnotationType annot_type,
		EvWindow        *window)
{
    if (window->priv->document->iswebdocument == TRUE ) return;
    ev_view_begin_add_annotation (EV_VIEW (window->priv->view), annot_type);
}



static void
ev_window_cancel_add_annot(EvSidebarAnnotations *sidebar_annots, EvWindow *window)
{
    if (window->priv->document->iswebdocument == TRUE ) return;
    ev_view_cancel_add_annotation (EV_VIEW (window->priv->view));
}

static void
sidebar_bookmarks_add_bookmark (EvSidebarBookmarks *sidebar_bookmarks,
                                EvWindow           *window)
{
    ev_window_cmd_bookmarks_add (NULL, NULL, window);
}




static void
sidebar_widget_model_set (EvSidebarLinks *ev_sidebar_links,
                          GParamSpec     *pspec,
                          EvWindow       *ev_window)
{
    /* Obsolete in GTK4/GAction migration for now */
}





static void
sidebar_page_main_widget_update_cb (GObject      *ev_sidebar_page,
                                    GParamSpec   *pspec,
                                    EvWindow     *ev_window)
{
    GtkWidget *widget;

    g_object_get (ev_sidebar_page, "main_widget", &widget, NULL);
}







static void
launch_action (EvWindow     *window,
               EvLinkAction *action)
{
    const char *filename = ev_link_action_get_filename (action);
    GAppInfo *app_info;
    GFile *file;
    GList file_list = {NULL};
    GdkAppLaunchContext *context;
    GdkDisplay          *display;
    GError *error = NULL;

    if (filename == NULL)
        return;

    if (g_path_is_absolute (filename)) {
        file = g_file_new_for_path (filename);
    } else {
        GFile *base_file;
        gchar *dir;

        dir = g_path_get_dirname (window->priv->uri);
        base_file = g_file_new_for_uri (dir);
        g_free (dir);

        file = g_file_resolve_relative_path (base_file, filename);
        g_object_unref (base_file);
    }

    app_info = g_file_query_default_handler (file, NULL, &error);
    if (!app_info) {
        ev_window_error_message (window, error,
                "%s",
                _("Unable to launch external application."));
        g_object_unref (file);
        g_error_free (error);

        return;
    }

    display = gtk_widget_get_display (GTK_WIDGET (window));
    context = gdk_display_get_app_launch_context (display);
    gdk_app_launch_context_set_timestamp (context, GDK_CURRENT_TIME);


    file_list.data = file;
    if (!g_app_info_launch (app_info, &file_list, G_APP_LAUNCH_CONTEXT (context), &error)) {
        ev_window_error_message (window, error,
                "%s",
                _("Unable to launch external application."));
        g_error_free (error);
    }

    g_object_unref (app_info);
    g_object_unref (file);
    /* FIXMEchpe: unref launch context? */

    /* According to the PDF spec filename can be an executable. I'm not sure
       allowing to launch executables is a good idea though. -- marco */
}

static void
launch_external_uri (EvWindow     *window,
                     EvLinkAction *action)
{
    const gchar *uri = ev_link_action_get_uri (action);
    GError *error = NULL;
    gboolean ret;
    GdkAppLaunchContext *context;
    GdkDisplay *display;

    display = gtk_widget_get_display (GTK_WIDGET (window));
    context = gdk_display_get_app_launch_context (display);
    gdk_app_launch_context_set_timestamp (context, GDK_CURRENT_TIME);


    if (!g_strstr_len (uri, strlen (uri), "://") &&
            !g_str_has_prefix (uri, "mailto:")) {
        gchar *new_uri;

        /* Not a valid uri, assume http if it starts with www */
        if (g_str_has_prefix (uri, "www.")) {
            new_uri = g_strdup_printf ("http://%s", uri);
        } else {
            GFile *file, *parent;

            file = g_file_new_for_uri (window->priv->uri);
            parent = g_file_get_parent (file);
            g_object_unref (file);
            if (parent) {
                gchar *parent_uri = g_file_get_uri (parent);

                new_uri = g_build_filename (parent_uri, uri, NULL);
                g_free (parent_uri);
                g_object_unref (parent);
            } else {
                new_uri = g_strdup_printf ("file:///%s", uri);
            }
        }
        ret = g_app_info_launch_default_for_uri (new_uri, G_APP_LAUNCH_CONTEXT (context), &error);
        g_free (new_uri);
    } else {
        ret = g_app_info_launch_default_for_uri (uri, G_APP_LAUNCH_CONTEXT (context), &error);
    }

    if (ret == FALSE) {
        ev_window_error_message (window, error,
                "%s", _("Unable to open external link"));
        g_error_free (error);
    }

    /* FIXMEchpe: unref launch context? */
}

static void
open_remote_link (EvWindow      *window,
                  EvLinkAction  *action)
{
    gchar *uri;
    gchar *dir;

    dir = g_path_get_dirname (window->priv->uri);

    uri = g_build_filename (dir, ev_link_action_get_filename (action),
            NULL);
    g_free (dir);

    ev_application_open_uri_at_dest (EV_APP, uri,
            gtk_widget_get_display (GTK_WIDGET (window)),
            ev_link_action_get_dest (action),
            0,
            NULL,
            GDK_CURRENT_TIME);

    g_free (uri);
}

static void
do_action_named (EvWindow       *window,
                 EvLinkAction   *action)
{
    const gchar *name = ev_link_action_get_name (action);

    if (g_ascii_strcasecmp (name, "FirstPage") == 0) {
        ev_window_cmd_go_first_page (NULL, NULL, window);
    } else if (g_ascii_strcasecmp (name, "PrevPage") == 0) {
        ev_window_cmd_go_previous_page (NULL, NULL, window);
    } else if (g_ascii_strcasecmp (name, "NextPage") == 0) {
        ev_window_cmd_go_next_page (NULL, NULL, window);
    } else if (g_ascii_strcasecmp (name, "LastPage") == 0) {
        ev_window_cmd_go_last_page (NULL, NULL, window);
    } else if (g_ascii_strcasecmp (name, "GoToPage") == 0) {
        ev_window_cmd_focus_page_selector (NULL, NULL, window);
    } else if (g_ascii_strcasecmp (name, "Find") == 0) {
        ev_window_cmd_edit_find (NULL, NULL, window);
    } else if (g_ascii_strcasecmp (name, "Close") == 0) {
        ev_window_cmd_file_close_window (NULL, NULL, window);
    } else if (g_ascii_strcasecmp (name, "Print") == 0) {
        ev_window_cmd_file_print (NULL, NULL, window);
    } else if (g_ascii_strcasecmp (name, "Properties") == 0) {
        ev_window_cmd_file_properties (NULL, NULL, window);
    } else {
        g_warning ("Unimplemented named action: %s, please post a "
                "bug report on Xreader bug tracker "
                "(https://github.com/linuxmint/xreader/issues) with a testcase.",
                name);
    }
}

static void
view_external_link_cb (EvWindow     *window,
                       EvLinkAction *action)
{
    switch (ev_link_action_get_action_type (action)) {
    case EV_LINK_ACTION_TYPE_GOTO_DEST: {
        EvLinkDest *dest;

        dest = ev_link_action_get_dest (action);
        if (!dest)
            return;

        ev_window_open_copy_at_dest (window, dest);
    }
    break;
    case EV_LINK_ACTION_TYPE_EXTERNAL_URI:
        launch_external_uri (window, action);
        break;
    case EV_LINK_ACTION_TYPE_LAUNCH:
        launch_action (window, action);
        break;
    case EV_LINK_ACTION_TYPE_GOTO_REMOTE:
        open_remote_link (window, action);
        break;
    case EV_LINK_ACTION_TYPE_NAMED:
        do_action_named (window, action);
        break;
    default:
        g_assert_not_reached ();
    }
}

static void
ev_view_popup_cmd_open_link (GSimpleAction *action,
                             GVariant      *parameter,
                             gpointer       user_data)
{
    EvWindow *window = EV_WINDOW (user_data);
    if (window->priv->document->iswebdocument == TRUE ) return;
    ev_view_handle_link (EV_VIEW (window->priv->view), window->priv->link);
}

static void
ev_view_popup_cmd_open_link_new_window (GSimpleAction *action,
                                        GVariant      *parameter,
                                        gpointer       user_data)
{
    EvWindow *window = EV_WINDOW (user_data);
    EvLinkAction *ev_action = NULL;
    EvLinkDest   *dest;

    ev_action = ev_link_get_action (window->priv->link);
    if (!ev_action)
        return;

    dest = ev_link_action_get_dest (ev_action);
    if (!dest)
        return;

    ev_window_open_copy_at_dest (window, dest);
}

static void
ev_view_popup_cmd_copy_link_address (GSimpleAction *action,
                                     GVariant      *parameter,
                                     gpointer       user_data)
{
    EvWindow *window = EV_WINDOW (user_data);
    EvLinkAction *ev_action;
    if (window->priv->document->iswebdocument == TRUE ) return;
    ev_action = ev_link_get_action (window->priv->link);
    if (!ev_action)
        return;

    ev_view_copy_link_address (EV_VIEW (window->priv->view),
            ev_action);
}

static GFile *
create_file_from_uri_for_format (const gchar     *uri,
                                 GdkPixbufFormat *format)
{
    GFile  *target_file;
    gchar **extensions;
    gchar  *uri_extension;
    gint    i;

    extensions = gdk_pixbuf_format_get_extensions (format);
    for (i = 0; extensions[i]; i++) {
        if (g_str_has_suffix (uri, extensions[i])) {
            g_strfreev (extensions);
            return g_file_new_for_uri (uri);
        }
    }

    uri_extension = g_strconcat (uri, ".", extensions[0], NULL);
    target_file = g_file_new_for_uri (uri_extension);
    g_free (uri_extension);
    g_strfreev (extensions);

    return target_file;
}

static void
image_save_dialog_response_cb (GtkWidget *fc,
                               gint       response_id,
                               EvWindow  *ev_window)
{
    GFile           *target_file;
    gboolean         is_native;
    GError          *error = NULL;
    GdkPixbuf       *pixbuf;
    gchar           *uri;
    gchar           *filename;
    gchar           *file_format;
    GdkPixbufFormat *format;
    GtkFileFilter   *filter;

    if (response_id != GTK_RESPONSE_OK) {
        gtk_window_destroy (GTK_WINDOW (fc));
        return;
    }

    ev_window_file_chooser_save_folder (ev_window,
            GTK_FILE_CHOOSER (fc),
            G_USER_DIRECTORY_PICTURES);

    uri = g_file_get_uri (gtk_file_chooser_get_file (GTK_FILE_CHOOSER (fc)));
    filter = gtk_file_chooser_get_filter (GTK_FILE_CHOOSER (fc));
    format = g_object_get_data (G_OBJECT (filter), "pixbuf-format");

    if (format == NULL) {
        format = get_gdk_pixbuf_format_by_extension (uri);
    }

    if (format == NULL && g_strrstr (uri, ".") == NULL) {
        /* no extension found and no extension provided within uri */
        format = get_gdk_pixbuf_format_by_extension (".png");
        if (format == NULL) {
            /* no .png support, try .jpeg */
            format = get_gdk_pixbuf_format_by_extension (".jpeg");
        }
    }

    if (format == NULL) {
        ev_window_error_message (ev_window, NULL,
                "%s",
                _("Couldn't find appropriate format to save image"));
        g_free (uri);
        gtk_window_destroy (GTK_WINDOW (fc));

        return;
    }

    target_file = create_file_from_uri_for_format (uri, format);
    g_free (uri);

    is_native = g_file_is_native (target_file);
    if (is_native) {
        filename = g_file_get_path (target_file);
    } else {
        /* Create a temporary local file to save to */
        if (ev_mkstemp ("saveimage.XXXXXX", &filename, &error) == -1)
            goto has_error;
    }

    ev_document_doc_mutex_lock ();
    pixbuf = ev_document_images_get_image (EV_DOCUMENT_IMAGES (ev_window->priv->document),
            ev_window->priv->image);
    ev_document_doc_mutex_unlock ();

    file_format = gdk_pixbuf_format_get_name (format);
    gdk_pixbuf_save (pixbuf, filename, file_format, &error, NULL);
    g_free (file_format);
    g_object_unref (pixbuf);
    has_error:
    if (error) {
        ev_window_error_message (ev_window, error,
                "%s", _("The image could not be saved."));
        g_error_free (error);
        g_free (filename);
        g_object_unref (target_file);
        gtk_window_destroy (GTK_WINDOW (fc));

        return;
    }

    if (!is_native) {
        GFile *source_file;

        source_file = g_file_new_for_path (filename);

        ev_window_save_remote (ev_window, EV_SAVE_IMAGE,
                source_file, target_file);
        g_object_unref (source_file);
    }

    g_free (filename);
    g_object_unref (target_file);
    gtk_window_destroy (GTK_WINDOW (fc));
}

static void
ev_window_save_remote (EvWindow *ev_window,
                       EvSaveType type,
                       GFile      *source_file,
                       GFile      *target_file)
{
    GError *error = NULL;
    g_file_copy (source_file, target_file, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error);
    if (error) {
        ev_window_error_message (ev_window, error, "%s", _("The image could not be saved."));
        g_error_free (error);
    }
}

static void
ev_view_popup_cmd_save_image_as (GSimpleAction *action,
                                 GVariant      *parameter,
                                 gpointer       user_data)
{
    EvWindow *window = EV_WINDOW (user_data);
    GtkWidget *fc;

    if (!window->priv->image)
        return;

    fc = gtk_file_chooser_dialog_new (_("Save Image"),
            GTK_WINDOW (window),
            GTK_FILE_CHOOSER_ACTION_SAVE,
            _("_Cancel"),
            GTK_RESPONSE_CANCEL,
            _("_Save"), GTK_RESPONSE_OK,
            NULL);

    gtk_dialog_set_default_response (GTK_DIALOG (fc), GTK_RESPONSE_OK);


    file_chooser_dialog_add_writable_pixbuf_formats    (GTK_FILE_CHOOSER (fc));

    ev_window_file_chooser_restore_folder (window,
            GTK_FILE_CHOOSER (fc),
            NULL,
            G_USER_DIRECTORY_PICTURES);

    g_signal_connect (fc, "response",
            G_CALLBACK (image_save_dialog_response_cb),
            window);

    gtk_widget_show (fc);
}

static void
ev_view_popup_cmd_copy_image (GSimpleAction *action,
                              GVariant      *parameter,
                              gpointer       user_data)
{
    EvWindow     *window = EV_WINDOW (user_data);
    GdkClipboard *clipboard;
    GdkPixbuf    *pixbuf;
    GdkTexture   *texture;

    if (!window->priv->image)
        return;

    clipboard = gtk_widget_get_clipboard (GTK_WIDGET (window));
    ev_document_doc_mutex_lock ();
    pixbuf = ev_document_images_get_image (EV_DOCUMENT_IMAGES (window->priv->document),
            window->priv->image);
    ev_document_doc_mutex_unlock ();

    texture = gdk_texture_new_for_pixbuf (pixbuf);
    gdk_clipboard_set_texture (clipboard, texture);
    g_object_unref (texture);
    g_object_unref (pixbuf);
}

static void
ev_view_popup_cmd_remove_annotation (GSimpleAction *action,
                                     GVariant      *parameter,
                                     gpointer       user_data)
{
        EvWindow *window = EV_WINDOW (user_data);
    ev_view_remove_annotation (EV_VIEW (window->priv->view),
                               window->priv->annot);
}

static void
ev_view_popup_cmd_annot_properties_response_cb (GtkDialog *dialog,
                                               gint       response_id,
                                               gpointer   user_data)
{
    EvWindow *window = EV_WINDOW (user_data);
    EvAnnotation *annot = window->priv->annot;
    EvAnnotationsSaveMask mask = EV_ANNOTATIONS_SAVE_NONE;

    if (response_id == GTK_RESPONSE_APPLY) {
        const gchar *author;
        GdkRGBA rgba;
        gdouble opacity;
        gboolean popup_is_open;

        author = ev_annotation_properties_dialog_get_author (EV_ANNOTATION_PROPERTIES_DIALOG (dialog));
        if (ev_annotation_markup_set_label (EV_ANNOTATION_MARKUP (annot), author))
            mask |= EV_ANNOTATIONS_SAVE_LABEL;

        ev_annotation_properties_dialog_get_rgba (EV_ANNOTATION_PROPERTIES_DIALOG (dialog), &rgba);
        if (ev_annotation_set_rgba (annot, &rgba))
            mask |= EV_ANNOTATIONS_SAVE_COLOR;

        opacity = ev_annotation_properties_dialog_get_opacity (EV_ANNOTATION_PROPERTIES_DIALOG (dialog));
        if (ev_annotation_markup_set_opacity (EV_ANNOTATION_MARKUP (annot), opacity))
            mask |= EV_ANNOTATIONS_SAVE_OPACITY;

        popup_is_open = ev_annotation_properties_dialog_get_popup_is_open (EV_ANNOTATION_PROPERTIES_DIALOG (dialog));
        if (ev_annotation_markup_set_popup_is_open (EV_ANNOTATION_MARKUP (annot), popup_is_open))
            mask |= EV_ANNOTATIONS_SAVE_POPUP_IS_OPEN;

        if (EV_IS_ANNOTATION_TEXT (annot)) {
            EvAnnotationTextIcon icon;
            icon = ev_annotation_properties_dialog_get_text_icon (EV_ANNOTATION_PROPERTIES_DIALOG (dialog));
            if (ev_annotation_text_set_icon (EV_ANNOTATION_TEXT (annot), icon))
                mask |= EV_ANNOTATIONS_SAVE_TEXT_ICON;
        }

        if (EV_IS_ANNOTATION_TEXT_MARKUP (annot)) {
            EvAnnotationTextMarkupType markup_type;
            markup_type = ev_annotation_properties_dialog_get_text_markup_type (EV_ANNOTATION_PROPERTIES_DIALOG (dialog));
            if (ev_annotation_text_markup_set_markup_type (EV_ANNOTATION_TEXT_MARKUP (annot), markup_type))
                mask |= EV_ANNOTATIONS_SAVE_TEXT_MARKUP_TYPE;
        }

        if (mask != EV_ANNOTATIONS_SAVE_NONE) {
            ev_document_doc_mutex_lock ();
            ev_document_annotations_save_annotation (EV_DOCUMENT_ANNOTATIONS (window->priv->document),
                    window->priv->annot,
                    mask);
            ev_document_doc_mutex_unlock ();
            ev_view_reload (EV_VIEW (window->priv->view));
        }
    }
    gtk_window_destroy (GTK_WINDOW (dialog));
}

static void
ev_view_popup_cmd_annot_properties (GSimpleAction *action,
                                    GVariant      *parameter,
                                    gpointer       user_data)
{
    EvWindow *window = EV_WINDOW (user_data);
    GtkWidget *dialog;

    if (!window->priv->annot)
        return;

    dialog = ev_annotation_properties_dialog_new_with_annotation (window->priv->annot);
    gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (window));
    g_signal_connect (dialog, "response",
                      G_CALLBACK (ev_view_popup_cmd_annot_properties_response_cb),
                      window);
    gtk_widget_show (dialog);
}

static void
ev_attachment_popup_cmd_open_attachment (GSimpleAction *action,
                                         GVariant      *parameter,
                                         gpointer       user_data)
{
    EvWindow *window = EV_WINDOW (user_data);
    GList     *l;
    for (l = window->priv->attach_list; l && l->data; l = g_list_next (l)) {
        EvAttachment *attachment;
        GError       *error = NULL;

        attachment = (EvAttachment *) l->data;

        ev_attachment_open (attachment,
                           gtk_widget_get_display (GTK_WIDGET (window)),
                           GDK_CURRENT_TIME,
                           &error);

        if (error) {
            ev_window_error_message (window, error,
                    "%s", _("Unable to open attachment"));
            g_error_free (error);
        }
    }
}

static void
attachment_save_dialog_response_cb (GtkWidget *fc,
                                    gint       response_id,
                                    EvWindow  *ev_window)
{
    GFile                *target_file;
    gchar                *uri;
    GList                *l;
    GtkFileChooserAction  fc_action;
    gboolean              is_dir;
    gboolean              is_native;

    if (response_id != GTK_RESPONSE_OK) {
        gtk_window_destroy (GTK_WINDOW (fc));
        return;
    }

    ev_window_file_chooser_save_folder (ev_window,
            GTK_FILE_CHOOSER (fc),
            G_USER_DIRECTORY_DOCUMENTS);

    uri = g_file_get_uri (gtk_file_chooser_get_file (GTK_FILE_CHOOSER (fc)));
    target_file = g_file_new_for_uri (uri);
    g_object_get (G_OBJECT (fc), "action", &fc_action, NULL);
    is_dir = (fc_action == GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
    is_native = g_file_is_native (target_file);

    for (l = ev_window->priv->attach_list; l && l->data; l = g_list_next (l)) {
        EvAttachment *attachment;
        GFile        *save_to = NULL;
        GError       *error = NULL;

        attachment = (EvAttachment *) l->data;

        if (is_native) {
            if (is_dir) {
                save_to = g_file_get_child (target_file,
                        /* FIXMEchpe: file name encoding! */
                        ev_attachment_get_name (attachment));
            } else {
                save_to = g_object_ref (target_file);
            }
        } else {
            save_to = ev_mkstemp_file ("saveattachment.XXXXXX", &error);
        }

        if (save_to)
            ev_attachment_save (attachment, save_to, &error);

        if (error) {
            ev_window_error_message (ev_window, error,
                    "%s", _("The attachment could not be saved."));
            g_error_free (error);
            g_object_unref (save_to);

            continue;
        }

        if (!is_native) {
            GFile *dest_file;

            if (is_dir) {
                dest_file = g_file_get_child (target_file,
                        ev_attachment_get_name (attachment));
            } else {
                dest_file = g_object_ref (target_file);
            }

            ev_window_save_remote (ev_window, EV_SAVE_ATTACHMENT,
                    save_to, dest_file);

            g_object_unref (dest_file);
        }

        g_object_unref (save_to);
    }

    g_free (uri);
    g_object_unref (target_file);

    gtk_window_destroy (GTK_WINDOW (fc));
}

static void
ev_attachment_popup_cmd_save_attachment_as (GSimpleAction *action,
                                            GVariant      *parameter,
                                            gpointer       user_data)
{
    EvWindow *window = EV_WINDOW (user_data);
    GtkWidget    *fc;
    EvAttachment *attachment = NULL;

    if (!window->priv->attach_list)
        return;

    if (g_list_length (window->priv->attach_list) == 1)
        attachment = (EvAttachment *) window->priv->attach_list->data;

    fc = gtk_file_chooser_dialog_new (
            _("Save Attachment"),
            GTK_WINDOW (window),
            attachment ? GTK_FILE_CHOOSER_ACTION_SAVE : GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
            _("_Cancel"),
                    GTK_RESPONSE_CANCEL,
                    _("_Save"), GTK_RESPONSE_OK,
                    NULL);

    gtk_dialog_set_default_response (GTK_DIALOG (fc), GTK_RESPONSE_OK);


    if (attachment)
        gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (fc),
                ev_attachment_get_name (attachment));

    ev_window_file_chooser_restore_folder (window,
            GTK_FILE_CHOOSER (fc),
            NULL,
            G_USER_DIRECTORY_DOCUMENTS);

    g_signal_connect (fc, "response",
            G_CALLBACK (attachment_save_dialog_response_cb),
            window);

    gtk_widget_show (fc);
}

#ifdef ENABLE_DBUS
static void
ev_window_sync_source (EvWindow     *window,
                       EvSourceLink *link)
{
    guint32         timestamp;
    gchar        *uri_input;
    GFile        *input_gfile;

    if (window->priv->skeleton == NULL)
        return;

    timestamp = GDK_CURRENT_TIME;
    if (g_path_is_absolute (link->filename)) {
        input_gfile = g_file_new_for_path (link->filename);
    } else {
        GFile *gfile, *parent_gfile;

        gfile = g_file_new_for_uri (window->priv->uri);
        parent_gfile = g_file_get_parent (gfile);

        /* parent_gfile should never be NULL */
        if (parent_gfile == NULL) {
            g_printerr ("Document URI is '/'\n");
            return;
        }

        input_gfile = g_file_get_child (parent_gfile, link->filename);
        g_object_unref (parent_gfile);
        g_object_unref (gfile);
    }

    uri_input = g_file_get_uri (input_gfile);
    g_object_unref (input_gfile);

    ev_xreader_window_emit_sync_source (window->priv->skeleton,
            uri_input,
            g_variant_new ("(ii)", link->line, link->col),
            timestamp);
    g_free (uri_input);
}

static void
ev_window_emit_closed (EvWindow *window)
{
    if (window->priv->skeleton == NULL)
        return;

    ev_xreader_window_emit_closed (window->priv->skeleton);

    /* If this is the last window call g_dbus_connection_flush_sync()
     * to make sure the signal is emitted.
     */
    if (ev_application_get_n_windows (EV_APP) == 1)
        g_dbus_connection_flush_sync (g_application_get_dbus_connection (g_application_get_default ()), NULL, NULL);
}

static void
ev_window_emit_doc_loaded (EvWindow *window)
{
    if (window->priv->skeleton == NULL)
        return;

    ev_xreader_window_emit_document_loaded (window->priv->skeleton, window->priv->uri);
}

static gboolean
handle_sync_view_cb (EvXreaderWindow        *object,
                     GDBusMethodInvocation *invocation,
                     const gchar           *source_file,
                     GVariant              *source_point,
                     guint                  timestamp,
                     EvWindow              *window)
{
    if (window->priv->document && ev_document_has_synctex (window->priv->document)) {
        EvSourceLink link;

        link.filename = (char *) source_file;
        g_variant_get (source_point, "(ii)", &link.line, &link.col);
        ev_view_highlight_forward_search (EV_VIEW (window->priv->view), &link);
        gtk_window_present_with_time (GTK_WINDOW (window), timestamp);
    }

    ev_xreader_window_complete_sync_view (object, invocation);

    return TRUE;
}
#endif /* ENABLE_DBUS */



static gboolean
ev_window_drop_cb (GtkDropTarget *target,
                   const GValue  *value,
                   double         x,
                   double         y,
                   gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    GdkFileList *file_list;
    GSList *files;
    GSList *l;

    if (!G_TYPE_CHECK_INSTANCE_TYPE (g_value_get_object (value), GDK_TYPE_FILE_LIST))
        return FALSE;

    file_list = g_value_get_object (value);
    files = gdk_file_list_get_files (file_list);

    for (l = files; l; l = l->next) {
        GFile *file = l->data;
        char *uri = g_file_get_uri (file);

        ev_window_open_uri (ev_window, uri, NULL, EV_WINDOW_MODE_NORMAL, NULL);
        g_free (uri);
    }

    return TRUE;
}


/* --- Tab management --- */

static void
ev_window_update_notebook_visibility (EvWindow *ev_window)
{
    gint n_pages = gtk_notebook_get_n_pages (GTK_NOTEBOOK (ev_window->priv->notebook));
    gtk_notebook_set_show_tabs (GTK_NOTEBOOK (ev_window->priv->notebook), n_pages > 1);
}

static GtkWidget *
ev_window_create_tab_label (EvWindow *ev_window, const gchar *title, GtkWidget *tab_page)
{
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *label = gtk_label_new (title ? title : _("New Document"));
    GtkWidget *close_btn = gtk_button_new_from_icon_name ("window-close-symbolic");

    gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars (GTK_LABEL (label), 20);
    gtk_widget_set_hexpand (label, TRUE);
    gtk_button_set_has_frame (GTK_BUTTON (close_btn), FALSE);
    gtk_widget_set_valign (close_btn, GTK_ALIGN_CENTER);

    /* Store a reference to the page widget so close button knows which tab to close */
    g_object_set_data (G_OBJECT (close_btn), "tab-page", tab_page);
    g_object_set_data (G_OBJECT (box), "tab-label", label);

    gtk_box_append (GTK_BOX (box), label);
    gtk_box_append (GTK_BOX (box), close_btn);

    return box;
}

static void
ev_window_close_tab_by_page (EvWindow *ev_window, GtkWidget *page_widget)
{
    GList *l;
    EvTab *tab_to_remove = NULL;

    for (l = ev_window->priv->tabs; l; l = l->next) {
        EvTab *tab = (EvTab *) l->data;
        if (tab->view_box == page_widget) {
            tab_to_remove = tab;
            break;
        }
    }

    if (!tab_to_remove)
        return;

    gint page_num = gtk_notebook_page_num (GTK_NOTEBOOK (ev_window->priv->notebook), page_widget);
    if (page_num < 0)
        return;

    ev_window->priv->tabs = g_list_remove (ev_window->priv->tabs, tab_to_remove);

    /* Free tab resources */
    g_clear_object (&tab_to_remove->model);
    g_clear_object (&tab_to_remove->document);
    g_clear_object (&tab_to_remove->history);
    g_clear_object (&tab_to_remove->metadata);
    g_clear_object (&tab_to_remove->bookmarks);
    g_clear_object (&tab_to_remove->monitor);
    g_free (tab_to_remove->uri);
    g_free (tab_to_remove->local_uri);
    g_free (tab_to_remove->search_string);
    if (tab_to_remove->load_job) ev_job_cancel (tab_to_remove->load_job);
    if (tab_to_remove->find_job) ev_job_cancel (tab_to_remove->find_job);
    g_free (tab_to_remove);

    gtk_notebook_remove_page (GTK_NOTEBOOK (ev_window->priv->notebook), page_num);

    if (g_list_length (ev_window->priv->tabs) == 0) {
        gtk_window_destroy (GTK_WINDOW (ev_window));
        return;
    }

    ev_window_update_notebook_visibility (ev_window);
}

static void
ev_window_tab_close_button_clicked (GtkButton *button, gpointer user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    GtkWidget *page = g_object_get_data (G_OBJECT (button), "tab-page");
    ev_window_close_tab_by_page (ev_window, page);
}

static void
ev_window_notebook_switch_page (GtkNotebook *notebook, GtkWidget *page,
                                guint page_num, EvWindow *ev_window)
{
    GList *l;

    for (l = ev_window->priv->tabs; l; l = l->next) {
        EvTab *tab = (EvTab *) l->data;
        if (tab->view_box == page) {
            ev_window->priv->active_tab = tab;

            /* Update priv pointers to current tab */
            ev_window->priv->view = tab->view;
            ev_window->priv->scrolled_window = tab->scrolled_window;
            ev_window->priv->model = tab->model;
            ev_window->priv->document = tab->document;
            ev_window->priv->uri = tab->uri;
            ev_window->priv->metadata = tab->metadata;
            ev_window->priv->bookmarks = tab->bookmarks;
            ev_window->priv->history = tab->history;
            ev_window->priv->page_mode = tab->page_mode;

            /* Reconnect sidebar to new model */
            ev_sidebar_set_model (EV_SIDEBAR (ev_window->priv->sidebar),
                                  tab->model);

            /* Update window title */
            if (tab->uri) {
                ev_window_title_set_uri (ev_window->priv->title, tab->uri);
            }
            if (tab->document) {
                ev_window_title_set_document (ev_window->priv->title, tab->document);
            }

            ev_window_setup_action_sensitivity (ev_window);
            break;
        }
    }
}

static EvTab *
ev_window_create_new_tab (EvWindow *ev_window, const gchar *title)
{
    EvTab *tab = g_new0 (EvTab, 1);
    guint page_cache_mb;

    tab->model = ev_document_model_new ();
    tab->page_mode = PAGE_MODE_DOCUMENT;
    tab->window_mode = EV_WINDOW_MODE_NORMAL;

    tab->view_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand (tab->view_box, TRUE);
    gtk_widget_set_hexpand (tab->view_box, TRUE);

    tab->scrolled_window = GTK_WIDGET (g_object_new (GTK_TYPE_SCROLLED_WINDOW, NULL));
    gtk_widget_set_vexpand (tab->scrolled_window, TRUE);
    gtk_box_append (GTK_BOX (tab->view_box), tab->scrolled_window);
    gtk_widget_show (tab->scrolled_window);

    tab->view = ev_view_new ();
    page_cache_mb = g_settings_get_uint (ev_window_ensure_settings (ev_window),
                                          GS_PAGE_CACHE_SIZE);
    ev_view_set_page_cache_size (EV_VIEW (tab->view),
                                  page_cache_mb * 1024 * 1024);
    ev_view_set_model (EV_VIEW (tab->view), tab->model);

    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (tab->scrolled_window), tab->view);
    gtk_widget_show (tab->view);
    gtk_widget_show (tab->view_box);

    tab->history = ev_history_new (tab->model);

    /* Connect view signals */
    g_signal_connect_swapped (tab->view, "external-link",
                              G_CALLBACK (view_external_link_cb),
                              ev_window);

    /* Tab label */
    GtkWidget *tab_label = ev_window_create_tab_label (ev_window, title, tab->view_box);
    GtkWidget *close_btn = gtk_widget_get_last_child (tab_label);
    g_signal_connect (close_btn, "clicked",
                      G_CALLBACK (ev_window_tab_close_button_clicked), ev_window);

    gint page_num = gtk_notebook_append_page (GTK_NOTEBOOK (ev_window->priv->notebook),
                                               tab->view_box, tab_label);
    gtk_notebook_set_tab_reorderable (GTK_NOTEBOOK (ev_window->priv->notebook),
                                      tab->view_box, TRUE);

    ev_window->priv->tabs = g_list_append (ev_window->priv->tabs, tab);
    ev_window->priv->active_tab = tab;

    /* Switch to the new tab */
    gtk_notebook_set_current_page (GTK_NOTEBOOK (ev_window->priv->notebook), page_num);

    ev_window_update_notebook_visibility (ev_window);

    return tab;
}

static void
ev_window_cmd_new_tab (GSimpleAction *action,
                       GVariant      *parameter,
                       gpointer       user_data)
{
    EvWindow  *window = EV_WINDOW (user_data);
    GtkWidget *chooser;

    chooser = gtk_file_chooser_dialog_new (_("Open Document in New Tab"),
            GTK_WINDOW (window),
            GTK_FILE_CHOOSER_ACTION_OPEN,
            _("_Cancel"), GTK_RESPONSE_CANCEL,
            _("_Open"), GTK_RESPONSE_OK,
            NULL);

    ev_document_factory_add_filters (chooser, NULL);
    gtk_file_chooser_set_select_multiple (GTK_FILE_CHOOSER (chooser), FALSE);

    ev_window_file_chooser_restore_folder (window,
            GTK_FILE_CHOOSER (chooser),
            NULL,
            G_USER_DIRECTORY_DOCUMENTS);

    g_signal_connect (chooser, "response",
            G_CALLBACK (file_open_dialog_response_cb),
            window);

    gtk_widget_show (chooser);
}

static void
ev_window_cmd_close_tab (GSimpleAction *action,
                         GVariant      *parameter,
                         gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    gint current = gtk_notebook_get_current_page (GTK_NOTEBOOK (ev_window->priv->notebook));

    if (current < 0)
        return;

    GtkWidget *page = gtk_notebook_get_nth_page (GTK_NOTEBOOK (ev_window->priv->notebook), current);
    ev_window_close_tab_by_page (ev_window, page);
}

static void
ev_window_cmd_next_tab (GSimpleAction *action,
                        GVariant      *parameter,
                        gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    gtk_notebook_next_page (GTK_NOTEBOOK (ev_window->priv->notebook));
}

static void
ev_window_cmd_prev_tab (GSimpleAction *action,
                        GVariant      *parameter,
                        gpointer       user_data)
{
    EvWindow *ev_window = EV_WINDOW (user_data);
    gtk_notebook_prev_page (GTK_NOTEBOOK (ev_window->priv->notebook));
}

static void
ev_window_init (EvWindow *ev_window)
{
    GError *error = NULL;
    GtkWidget *sidebar_widget;
    GtkStyleContext *context;
#ifdef ENABLE_DBUS
    GDBusConnection *connection;
    static gint window_id = 0;
#endif


    ev_window->priv = ev_window_get_instance_private (ev_window);

#ifdef ENABLE_DBUS
    connection = g_application_get_dbus_connection (g_application_get_default ());
    if (connection) {
        EvXreaderWindow *skeleton;

        ev_window->priv->dbus_object_path = g_strdup_printf (EV_WINDOW_DBUS_OBJECT_PATH, window_id++);

        skeleton = ev_xreader_window_skeleton_new ();
        if (g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (skeleton),
                connection,
                ev_window->priv->dbus_object_path,
                &error)) {
            ev_window->priv->skeleton = skeleton;
            g_signal_connect (skeleton, "handle-sync-view",
                    G_CALLBACK (handle_sync_view_cb),
                    ev_window);
        } else {
            g_printerr ("Failed to register bus object %s: %s\n",
                    ev_window->priv->dbus_object_path, error->message);
            g_error_free (error);
            g_free (ev_window->priv->dbus_object_path);
            ev_window->priv->dbus_object_path = NULL;
            error = NULL;

            g_object_unref (skeleton);
            ev_window->priv->skeleton = NULL;
        }
    }
#endif /* ENABLE_DBUS */

    /* GTK4: enable the application menubar */
	gtk_application_window_set_show_menubar (GTK_APPLICATION_WINDOW (ev_window), TRUE);

    ev_window->priv->menubar_skip_release = FALSE;
    ev_window->priv->menubar_show_queued = FALSE;

    ev_window->priv->model = ev_document_model_new ();

    ev_window->priv->page_mode = PAGE_MODE_DOCUMENT;
    ev_window->priv->chrome = EV_CHROME_NORMAL;
    ev_window->priv->title = ev_window_title_new (ev_window);

    context = gtk_widget_get_style_context (GTK_WIDGET (ev_window));
    gtk_style_context_add_class (context, "xreader-window");

    ev_window->priv->history = ev_history_new (ev_window->priv->model);
    g_signal_connect (ev_window->priv->history, "activate-link",
            G_CALLBACK (activate_link_cb), ev_window);
    g_signal_connect (ev_window->priv->history, "changed",
            G_CALLBACK (history_changed_cb), ev_window);

    ev_window->priv->main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child (GTK_WINDOW (ev_window), ev_window->priv->main_box);

    g_action_map_add_action_entries (G_ACTION_MAP (ev_window),
                                     entries, G_N_ELEMENTS (entries),
                                     ev_window);

    GSimpleActionGroup *view_popup_group = g_simple_action_group_new ();
    g_action_map_add_action_entries (G_ACTION_MAP (view_popup_group),
                                     view_popup_entries, G_N_ELEMENTS (view_popup_entries),
                                     ev_window);
    gtk_widget_insert_action_group (GTK_WIDGET (ev_window), "view-popup", G_ACTION_GROUP (view_popup_group));
    g_object_unref (view_popup_group);

    GSimpleActionGroup *attachment_popup_group = g_simple_action_group_new ();
    g_action_map_add_action_entries (G_ACTION_MAP (attachment_popup_group),
                                     attachment_popup_entries, G_N_ELEMENTS (attachment_popup_entries),
                                     ev_window);
    gtk_widget_insert_action_group (GTK_WIDGET (ev_window), "attachment-popup", G_ACTION_GROUP (attachment_popup_group));
    g_object_unref (attachment_popup_group);

    /* Tab keyboard accelerators */
    {
        GtkApplication *app = GTK_APPLICATION (g_application_get_default ());
        const gchar *accel_new_tab[]  = { "<Primary>t", NULL };
        const gchar *accel_close_tab[] = { "<Primary>w", NULL };
        const gchar *accel_next_tab[] = { "<Primary>Tab", "<Primary>Page_Down", NULL };
        const gchar *accel_prev_tab[] = { "<Primary><Shift>Tab", "<Primary>Page_Up", NULL };
        gtk_application_set_accels_for_action (app, "win.TabNew", accel_new_tab);
        gtk_application_set_accels_for_action (app, "win.TabClose", accel_close_tab);
        gtk_application_set_accels_for_action (app, "win.TabNext", accel_next_tab);
        gtk_application_set_accels_for_action (app, "win.TabPrev", accel_prev_tab);
    }

    ev_window->priv->builder = gtk_builder_new_from_resource ("/org/x/reader/shell/ui/xreader-menus.ui");
    GMenuModel *menubar = G_MENU_MODEL (gtk_builder_get_object (ev_window->priv->builder, "menubar"));
    gtk_application_set_menubar (GTK_APPLICATION (g_application_get_default ()), menubar);

    ev_window->priv->toolbar_revealer = gtk_revealer_new ();
    gtk_box_append (GTK_BOX (ev_window->priv->main_box), ev_window->priv->toolbar_revealer);
    gtk_revealer_set_transition_duration (GTK_REVEALER (ev_window->priv->toolbar_revealer), 175);
    gtk_widget_show (ev_window->priv->toolbar_revealer);

    ev_window->priv->toolbar = ev_toolbar_new (ev_window);
    gtk_revealer_set_child (GTK_REVEALER (ev_window->priv->toolbar_revealer), ev_window->priv->toolbar);
    gtk_widget_show (ev_window->priv->toolbar);

    /* Paned: sidebar (shared) | notebook (tabs) */
    ev_window->priv->hpaned = gtk_paned_new (GTK_ORIENTATION_HORIZONTAL);
    g_signal_connect (ev_window->priv->hpaned,
            "notify::position",
            G_CALLBACK (ev_window_sidebar_visibility_changed_cb),
            ev_window);
    gtk_paned_set_position (GTK_PANED (ev_window->priv->hpaned), SIDEBAR_DEFAULT_SIZE);
    gtk_widget_set_vexpand (ev_window->priv->hpaned, TRUE);
    gtk_box_append (GTK_BOX (ev_window->priv->main_box), ev_window->priv->hpaned);
    gtk_widget_show (ev_window->priv->hpaned);

    ev_window->priv->sidebar = ev_sidebar_new ();
    ev_sidebar_set_model (EV_SIDEBAR (ev_window->priv->sidebar),
            ev_window->priv->model);
    gtk_paned_set_start_child (GTK_PANED (ev_window->priv->hpaned),
            ev_window->priv->sidebar);
    gtk_widget_show (ev_window->priv->sidebar);

    /* Stub sidebar, for now */

    sidebar_widget = ev_sidebar_thumbnails_new ();
    ev_window->priv->sidebar_thumbs = sidebar_widget;
    g_signal_connect (sidebar_widget,
            "notify::main-widget",
            G_CALLBACK (sidebar_page_main_widget_update_cb),
            ev_window);
    g_signal_connect (sidebar_widget,
            "size_changed",
            G_CALLBACK (sidebar_thumbnails_size_changed),
            ev_window);
    sidebar_page_main_widget_update_cb (G_OBJECT (sidebar_widget), NULL, ev_window);
    gtk_widget_show (sidebar_widget);
    ev_sidebar_add_page (EV_SIDEBAR (ev_window->priv->sidebar),
            sidebar_widget);

    sidebar_widget = ev_sidebar_links_new ();
    ev_window->priv->sidebar_links = sidebar_widget;
    g_signal_connect (sidebar_widget,
            "notify::model",
            G_CALLBACK (sidebar_widget_model_set),
            ev_window);
    g_signal_connect (sidebar_widget,
            "link_activated",
            G_CALLBACK (sidebar_links_link_activated_cb),
            ev_window);
    sidebar_page_main_widget_update_cb (G_OBJECT (sidebar_widget), NULL, ev_window);
    gtk_widget_show (sidebar_widget);
    ev_sidebar_add_page (EV_SIDEBAR (ev_window->priv->sidebar),
            sidebar_widget);

    sidebar_widget = ev_sidebar_attachments_new ();
    ev_window->priv->sidebar_attachments = sidebar_widget;
    g_signal_connect_object (sidebar_widget,
            "popup",
            G_CALLBACK (attachment_bar_menu_popup_cb),
            ev_window, 0);
    gtk_widget_show (sidebar_widget);
    ev_sidebar_add_page (EV_SIDEBAR (ev_window->priv->sidebar),
            sidebar_widget);

    sidebar_widget = ev_sidebar_layers_new ();
    ev_window->priv->sidebar_layers = sidebar_widget;
    g_signal_connect (sidebar_widget,
            "layers_visibility_changed",
            G_CALLBACK (sidebar_layers_visibility_changed),
            ev_window);
    gtk_widget_show (sidebar_widget);
    ev_sidebar_add_page (EV_SIDEBAR (ev_window->priv->sidebar),
            sidebar_widget);

    sidebar_widget = ev_sidebar_annotations_new ();
    EvAnnotationsToolbar *annot_toolbar = ev_sidebar_annotations_get_toolbar(EV_SIDEBAR_ANNOTATIONS(sidebar_widget));
    ev_window->priv->sidebar_annots = sidebar_widget;
    g_signal_connect (sidebar_widget,
            "annot_activated",
            G_CALLBACK (sidebar_annots_annot_activated_cb),
            ev_window);
    g_signal_connect (annot_toolbar,
            "begin-add-annot",
            G_CALLBACK (ev_window_begin_add_annot),
            ev_window);
    g_signal_connect (annot_toolbar,
            "cancel-add-annot",
            G_CALLBACK (ev_window_cancel_add_annot),
            ev_window);
    gtk_widget_show (sidebar_widget);
    ev_sidebar_add_page (EV_SIDEBAR (ev_window->priv->sidebar),
            sidebar_widget);

    sidebar_widget = ev_sidebar_bookmarks_new ();
    ev_window->priv->sidebar_bookmarks = sidebar_widget;
    g_signal_connect (sidebar_widget,
            "add-bookmark",
            G_CALLBACK (sidebar_bookmarks_add_bookmark),
            ev_window);
    gtk_widget_show (sidebar_widget);
    ev_sidebar_add_page (EV_SIDEBAR (ev_window->priv->sidebar),
            sidebar_widget);

    /* Notebook for tabs - placed as end child of hpaned */
    ev_window->priv->notebook = gtk_notebook_new ();
    gtk_notebook_set_scrollable (GTK_NOTEBOOK (ev_window->priv->notebook), TRUE);
    gtk_notebook_set_show_tabs (GTK_NOTEBOOK (ev_window->priv->notebook), FALSE);
    gtk_notebook_set_show_border (GTK_NOTEBOOK (ev_window->priv->notebook), FALSE);
    gtk_widget_set_vexpand (ev_window->priv->notebook, TRUE);
    gtk_widget_set_hexpand (ev_window->priv->notebook, TRUE);
    gtk_paned_set_end_child (GTK_PANED (ev_window->priv->hpaned),
            ev_window->priv->notebook);
    gtk_widget_show (ev_window->priv->notebook);

    g_signal_connect (ev_window->priv->notebook, "switch-page",
            G_CALLBACK (ev_window_notebook_switch_page), ev_window);

    /* Create initial tab - reuse the model already created in this init */
    {
        EvTab *initial_tab = ev_window_create_new_tab (ev_window, NULL);
        /* Replace the tab's own model with the one init already created,
         * so that sidebar/history/etc. all share the same model. */
        g_object_unref (initial_tab->model);
        initial_tab->model = ev_window->priv->model;
        ev_view_set_model (EV_VIEW (initial_tab->view), ev_window->priv->model);

        ev_window->priv->view_box = initial_tab->view_box;
        ev_window->priv->scrolled_window = initial_tab->scrolled_window;
        ev_window->priv->view = initial_tab->view;
    }

    /* Note: ev_view_new() is now called inside ev_window_create_new_tab */
    /* The EvView from the initial tab is referenced via priv->view */

#if ENABLE_EPUB
    ev_window->priv->webview = ev_web_view_new();
    ev_web_view_set_model(EV_WEB_VIEW(ev_window->priv->webview),ev_window->priv->model);
#endif
    /* page_cache and model are already set in ev_window_create_new_tab */

	ev_window->priv->password_view = ev_password_view_new (GTK_WINDOW (ev_window));
	/* Note: "unlock" handler removed - was NULL stub from GTK3 migration */
	g_signal_connect_swapped (ev_window->priv->view, "external-link",
				  G_CALLBACK (view_external_link_cb),
				  ev_window);
#ifdef ENABLE_DBUS
    g_signal_connect_swapped (ev_window->priv->view, "sync-source",
            G_CALLBACK (ev_window_sync_source),
            ev_window);
#endif
    gtk_widget_show (ev_window->priv->view);
    gtk_widget_show (ev_window->priv->password_view);

    /* Find Bar */
    ev_window->priv->find_bar = egg_find_bar_new ();
    gtk_box_append (GTK_BOX (ev_window->priv->main_box),
            ev_window->priv->find_bar);

    /* We own a ref on these widgets, as we can swap them in and out */
    g_object_ref (ev_window->priv->view);
    g_object_ref (ev_window->priv->password_view);

    /* scrolled_window child is already set in ev_window_create_new_tab */

    /* Connect to model signals */
    /* Note: "page-changed" handler removed - was NULL stub from GTK3 migration */
    g_signal_connect (ev_window->priv->model,
            "notify::document",
            G_CALLBACK (ev_window_document_changed_cb),
            ev_window);
    g_signal_connect (ev_window->priv->model,
            "notify::scale",
            G_CALLBACK (ev_window_zoom_changed_cb),
            ev_window);
    g_signal_connect (ev_window->priv->model,
            "notify::sizing-mode",
            G_CALLBACK (ev_window_sizing_mode_changed_cb),
            ev_window);
    g_signal_connect (ev_window->priv->model,
            "notify::rotation",
            G_CALLBACK (ev_window_rotation_changed_cb),
            ev_window);
    g_signal_connect (ev_window->priv->model,
            "notify::continuous",
            G_CALLBACK (ev_window_continuous_changed_cb),
            ev_window);
    g_signal_connect (ev_window->priv->model,
            "notify::dual-page",
            G_CALLBACK (ev_window_dual_mode_changed_cb),
            ev_window);
    g_signal_connect (ev_window->priv->model,
            "notify::dual-odd-left",
            G_CALLBACK (ev_window_dual_mode_odd_pages_left_changed_cb),
            ev_window);
    g_signal_connect (ev_window->priv->model,
            "notify::rtl",
            G_CALLBACK (ev_window_direction_changed_cb),
            ev_window);
    g_signal_connect (ev_window->priv->model,
            "notify::inverted-colors",
            G_CALLBACK (ev_window_inverted_colors_changed_cb),
            ev_window);

    /* Connect sidebar signals */
    g_signal_connect (ev_window->priv->sidebar,
            "notify::visible",
            G_CALLBACK (ev_window_sidebar_visibility_changed_cb),
            ev_window);
    g_signal_connect (ev_window->priv->sidebar,
            "notify::current-page",
            G_CALLBACK (ev_window_sidebar_current_page_changed_cb),
            ev_window);

    /* Connect to find bar signals */
    g_signal_connect (ev_window->priv->find_bar,
            "previous",
            G_CALLBACK (find_bar_previous_cb),
            ev_window);
    g_signal_connect (ev_window->priv->find_bar,
            "next",
            G_CALLBACK (find_bar_next_cb),
            ev_window);
    g_signal_connect (ev_window->priv->find_bar,
            "close",
            G_CALLBACK (find_bar_close_cb),
            ev_window);
    g_signal_connect (ev_window->priv->find_bar,
            "notify::search-string",
            G_CALLBACK (find_bar_search_changed_cb),
            ev_window);
    g_signal_connect (ev_window->priv->find_bar,
            "notify::case-sensitive",
            G_CALLBACK (find_bar_search_changed_cb),
            ev_window);
    g_signal_connect (ev_window->priv->find_bar,
            "notify::visible",
            G_CALLBACK (find_bar_visibility_changed_cb),
            ev_window);
    g_signal_connect (ev_window->priv->find_bar,
            "scroll",
            G_CALLBACK (find_bar_scroll),
            ev_window);

    /* Popups */
    ev_window->priv->view_popup = GTK_WIDGET (gtk_builder_get_object (ev_window->priv->builder, "DocumentPopup"));
    ev_window->priv->link = NULL;

    ev_window->priv->attachment_popup = GTK_WIDGET (gtk_builder_get_object (ev_window->priv->builder, "AttachmentPopup"));
    ev_window->priv->attach_list = NULL;

    /* Give focus to the document view */
    gtk_widget_grab_focus (ev_window->priv->view);

    ev_window->priv->default_settings = g_settings_new (GS_SCHEMA_NAME_DEFAULT);
    g_settings_delay (ev_window->priv->default_settings);
    /* residue */
    /* residue */

    /* Set it user interface params */
    /* residue */
    ev_window_setup_favorites (ev_window);

    ev_window_setup_gtk_settings (ev_window);

    gtk_window_set_default_size (GTK_WINDOW (ev_window), 600, 600);

    ev_window_sizing_mode_changed_cb (ev_window->priv->model, NULL, ev_window);
    ev_window_setup_action_sensitivity (ev_window);

    /* Drag and Drop */
    GtkDropTarget *target = gtk_drop_target_new (GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
    g_signal_connect (target, "drop", G_CALLBACK (ev_window_drop_cb), ev_window);
    gtk_widget_add_controller (GTK_WIDGET (ev_window), GTK_EVENT_CONTROLLER (target));
}

/**
 * ev_window_new:
 *
 * Creates a #GtkWidget that represents the window.
 *
 * Returns: the #GtkWidget that represents the window.
 */
GtkWidget *
ev_window_new (void)
{
    GtkWidget *ev_window;

    ev_window = GTK_WIDGET (g_object_new (EV_TYPE_WINDOW,
                            "application", g_application_get_default (),
                            NULL));

    return ev_window;
}

const gchar *
ev_window_get_dbus_object_path (EvWindow *ev_window)
{
#ifdef ENABLE_DBUS
    return ev_window->priv->dbus_object_path;
#else
    return NULL;
#endif
}

GActionGroup *
ev_window_get_main_action_group (EvWindow *ev_window)
{
    g_return_val_if_fail (EV_WINDOW (ev_window), NULL);

    return G_ACTION_GROUP (ev_window);
}

EvDocumentModel *
ev_window_get_document_model (EvWindow *ev_window)
{
    g_return_val_if_fail (EV_WINDOW (ev_window), NULL);

    return ev_window->priv->model;
}
