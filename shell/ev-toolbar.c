/* ev-toolbar.c
 *  this file is part of xreader, a document viewer
 */

#include "config.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "ev-toolbar.h"
#include "ev-document-model.h"
#include "ev-zoom-action.h"
#include "ev-page-action-widget.h"

struct _EvToolbar
{
    GtkBox parent_instance;

    GtkWidget *fullscreen_group;
    GtkWidget *preset_group;
    GtkWidget *expand_window_button;
    GtkWidget *zoom_action;
    GtkWidget *page_preset_button;
    GtkWidget *reader_preset_button;
    GtkWidget *history_group;

    GSettings *settings;

    EvWindow *window;
    EvDocumentModel *model;
};

enum
{
    PROP_0,
    PROP_WINDOW
};

G_DEFINE_TYPE (EvToolbar, ev_toolbar, GTK_TYPE_BOX)

static void
ev_toolbar_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
    EvToolbar *ev_toolbar = EV_TOOLBAR (object);

    switch (prop_id)
    {
        case PROP_WINDOW:
            ev_toolbar->window = g_value_get_object (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
ev_toolbar_document_model_changed_cb (EvDocumentModel *model,
                                      GParamSpec      *pspec,
                                      EvToolbar       *ev_toolbar)
{
    EvSizingMode sizing_mode;
    gboolean continuous;
    gboolean best_fit;
    gboolean page_width;

    sizing_mode = ev_document_model_get_sizing_mode (model);
    continuous = ev_document_model_get_continuous (model);

    switch (sizing_mode)
    {
        case EV_SIZING_BEST_FIT:
            best_fit = TRUE;
            page_width = FALSE;
            break;
        case EV_SIZING_FIT_WIDTH:
            best_fit = FALSE;
            page_width = TRUE;
            break;
        default:
            best_fit = page_width = FALSE;
            break;
    }

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ev_toolbar->page_preset_button),
                                  !continuous && best_fit);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ev_toolbar->reader_preset_button),
                                  continuous && page_width);
}

static void
on_page_preset_toggled (GtkToggleButton *button,
                        gpointer         user_data)
{
    EvToolbar *ev_toolbar = EV_TOOLBAR (user_data);

    if (gtk_toggle_button_get_active (button))
    {
        ev_document_model_set_continuous (ev_toolbar->model, FALSE);
        ev_document_model_set_sizing_mode (ev_toolbar->model, EV_SIZING_BEST_FIT);
    }
}

static void
on_reader_preset_toggled (GtkToggleButton *button,
                          gpointer         user_data)
{
    EvToolbar *ev_toolbar = EV_TOOLBAR (user_data);

    if (gtk_toggle_button_get_active (button))
    {
        ev_document_model_set_continuous (ev_toolbar->model, TRUE);
        ev_document_model_set_sizing_mode (ev_toolbar->model, EV_SIZING_FIT_WIDTH);
    }
}

static GtkWidget *
setup_preset_buttons (EvToolbar *ev_toolbar)
{
    GtkWidget *box;
    GtkWidget *image;

    box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);

    /* Page preset button */
    ev_toolbar->page_preset_button = gtk_toggle_button_new ();
    gtk_widget_set_valign (ev_toolbar->page_preset_button, GTK_ALIGN_CENTER);
    image = gtk_image_new_from_icon_name ("xsi-view-paged-symbolic");
    gtk_widget_add_css_class (ev_toolbar->page_preset_button, "flat");
    gtk_widget_set_focusable (GTK_WIDGET (ev_toolbar->page_preset_button), FALSE);

    gtk_button_set_child (GTK_BUTTON (ev_toolbar->page_preset_button), image);
    gtk_widget_set_tooltip_text (ev_toolbar->page_preset_button,
                                 _("Page View\nNon-Continuous + Best Fit\nCtrl+2"));
    gtk_box_append (GTK_BOX (box), ev_toolbar->page_preset_button);
    g_signal_connect (ev_toolbar->page_preset_button, "toggled",
                      G_CALLBACK (on_page_preset_toggled), ev_toolbar);

    /* Reader preset button */
    ev_toolbar->reader_preset_button = gtk_toggle_button_new ();
    gtk_widget_set_valign (ev_toolbar->reader_preset_button, GTK_ALIGN_CENTER);
    image = gtk_image_new_from_icon_name ("xsi-view-continuous-symbolic");
    gtk_widget_add_css_class (ev_toolbar->reader_preset_button, "flat");
    gtk_widget_set_focusable (GTK_WIDGET (ev_toolbar->reader_preset_button), FALSE);

    gtk_button_set_child (GTK_BUTTON (ev_toolbar->reader_preset_button), image);
    gtk_widget_set_tooltip_text (ev_toolbar->reader_preset_button,
                                 _("Reader View\nContinuous + Fit Page Width\nCtrl+1"));
    gtk_box_append (GTK_BOX (box), ev_toolbar->reader_preset_button);
    g_signal_connect (ev_toolbar->reader_preset_button, "toggled",
                      G_CALLBACK (on_reader_preset_toggled), ev_toolbar);

    return box;
}

static GtkWidget *
create_sidepane_button (const gchar *action_name, const gchar *tooltip)
{
    GtkWidget *button = gtk_toggle_button_new();
    gtk_widget_set_valign (button, GTK_ALIGN_CENTER);
    GtkWidget *image = gtk_image_new_from_icon_name ("xsi-view-left-pane-symbolic");

    gtk_button_set_child (GTK_BUTTON (button), image);
    gtk_widget_add_css_class (button, "flat");
    gtk_button_set_has_frame(GTK_BUTTON(button), FALSE);
    if (action_name) gtk_actionable_set_action_name (GTK_ACTIONABLE (button), action_name);
    gtk_widget_set_tooltip_text (button, tooltip);

    return button;
}

static GtkWidget *
create_button (const gchar *action_name, const gchar *icon_name, const gchar *tooltip)
{
    GtkWidget *button = gtk_button_new_from_icon_name (icon_name);
    gtk_widget_set_valign (button, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class (button, "flat");
    gtk_button_set_has_frame(GTK_BUTTON(button), FALSE);
    if (action_name) gtk_actionable_set_action_name (GTK_ACTIONABLE (button), action_name);
    gtk_widget_set_tooltip_text (button, tooltip);
    return button;
}

gboolean
ev_toolbar_zoom_action_get_focused (EvToolbar *ev_toolbar)
{
    return ev_zoom_action_get_focused (EV_ZOOM_ACTION (ev_toolbar->zoom_action));
}

void
ev_toolbar_zoom_action_select_all (EvToolbar *ev_toolbar)
{
    ev_zoom_action_select_all (EV_ZOOM_ACTION (ev_toolbar->zoom_action));
}

static void
ev_toolbar_constructed (GObject *object)
{
    EvToolbar *ev_toolbar = EV_TOOLBAR (object);
    GMenu *zoom_menu;
    GtkWidget *box;
    GtkWidget *button;
    GtkWidget *separator;
    GtkWidget *page_selector;

    G_OBJECT_CLASS (ev_toolbar_parent_class)->constructed (object);

    ev_toolbar->model = ev_window_get_document_model (ev_toolbar->window);

    gtk_widget_add_css_class (GTK_WIDGET (ev_toolbar), "primary-toolbar");

    /* Using a single horizontal box for the toolbar contents */
    gtk_orientable_set_orientation (GTK_ORIENTABLE (ev_toolbar), GTK_ORIENTATION_HORIZONTAL);

    box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_append (GTK_BOX (ev_toolbar), box);

    button = create_button ("win.go-previous-page", "go-previous-symbolic", _("Previous page"));
    gtk_box_append (GTK_BOX (box), button);

    button = create_button ("win.go-next-page", "go-next-symbolic", _("Next page"));
    gtk_box_append (GTK_BOX (box), button);

    /* Page Selector */
    page_selector = ev_page_action_widget_new ();
    ev_page_action_widget_set_model (EV_PAGE_ACTION_WIDGET(page_selector), ev_toolbar->model);
    gtk_box_append (GTK_BOX (ev_toolbar), page_selector);

    /* History Navigation */
    ev_toolbar->history_group = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_append (GTK_BOX (ev_toolbar), ev_toolbar->history_group);

    button = create_button ("win.go-back-history", "go-previous-symbolic", _("Go back in history"));
    gtk_box_append (GTK_BOX (ev_toolbar->history_group), button);

    button = create_button ("win.go-forward-history", "go-next-symbolic", _("Go forward in history"));
    gtk_box_append (GTK_BOX (ev_toolbar->history_group), button);

    /* Zoom box (expanding) */
    box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand (box, TRUE);
    gtk_box_append (GTK_BOX (ev_toolbar), box);

    button = create_sidepane_button ("win.toggle-sidebar", _("Side pane"));
    gtk_widget_set_halign(button, GTK_ALIGN_END);
    gtk_box_append (GTK_BOX (box), button);

    button = create_button ("win.print", "printer-symbolic", _("Print"));
    gtk_widget_set_halign(button, GTK_ALIGN_END);
    gtk_box_append (GTK_BOX (box), button);

    separator = gtk_separator_new (GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_margin_start (separator, 6);
    gtk_widget_set_margin_end (separator, 6);
    gtk_widget_set_halign(separator, GTK_ALIGN_END);
    gtk_box_append (GTK_BOX (box), separator);

    button = create_button ("win.zoom-out", "zoom-out-symbolic", _("Zoom Out"));
    gtk_widget_set_halign(button, GTK_ALIGN_END);
    gtk_box_append (GTK_BOX (box), button);

    button = create_button ("win.zoom-in", "zoom-in-symbolic", _("Zoom In"));
    gtk_widget_set_halign(button, GTK_ALIGN_END);
    gtk_box_append (GTK_BOX (box), button);

    button = create_button ("win.zoom-default", "zoom-original-symbolic", _("Zoom Reset"));
    gtk_widget_set_halign(button, GTK_ALIGN_END);
    gtk_box_append (GTK_BOX (box), button);

    ev_toolbar->expand_window_button = create_button ("win.toggle-fullscreen", "view-fullscreen-symbolic", _("Fullscreen"));
    gtk_widget_set_halign(ev_toolbar->expand_window_button, GTK_ALIGN_END);
    gtk_box_append (GTK_BOX (box), ev_toolbar->expand_window_button);

    zoom_menu = g_menu_new();
    ev_toolbar->zoom_action = ev_zoom_action_new
        (ev_window_get_document_model (ev_toolbar->window), zoom_menu);
    g_object_unref (zoom_menu);
    gtk_widget_set_tooltip_text (ev_toolbar->zoom_action,
                                 _("Select or set the zoom level of the document"));
    gtk_widget_set_margin_start(ev_toolbar->zoom_action, 2);
    gtk_widget_set_halign(ev_toolbar->zoom_action, GTK_ALIGN_END);
    gtk_box_append (GTK_BOX (box), ev_toolbar->zoom_action);

    separator = gtk_separator_new (GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_margin_start (separator, 6);
    gtk_widget_set_margin_end (separator, 6);
    gtk_widget_set_halign(separator, GTK_ALIGN_END);
    gtk_box_append (GTK_BOX (box), separator);

    /* View preset button */
    ev_toolbar->preset_group = setup_preset_buttons (ev_toolbar);
    gtk_widget_set_halign(ev_toolbar->preset_group, GTK_ALIGN_END);
    gtk_box_append (GTK_BOX (ev_toolbar), ev_toolbar->preset_group);

    /* Setup the buttons we only want to show when fullscreened */
    ev_toolbar->fullscreen_group = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_halign(ev_toolbar->fullscreen_group, GTK_ALIGN_END);
    gtk_box_append (GTK_BOX (ev_toolbar), ev_toolbar->fullscreen_group);

    button = create_button ("win.presentation", "media-playback-start-symbolic", _("Presentation"));
    gtk_box_append (GTK_BOX (ev_toolbar->fullscreen_group), button);

    separator = gtk_separator_new (GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_margin_start (separator, 6);
    gtk_widget_set_margin_end (separator, 6);
    gtk_box_append (GTK_BOX (ev_toolbar->fullscreen_group), separator);

    button = create_button ("win.unfullscreen", "view-restore-symbolic", _("Leave Fullscreen"));
    gtk_box_append (GTK_BOX (ev_toolbar->fullscreen_group), button);

    g_signal_connect (ev_toolbar->model, "notify::continuous",
                      G_CALLBACK (ev_toolbar_document_model_changed_cb), ev_toolbar);
    g_signal_connect (ev_toolbar->model, "notify::sizing-mode",
                      G_CALLBACK (ev_toolbar_document_model_changed_cb), ev_toolbar);

    /* Toolbar buttons visibility bindings */
    g_settings_bind (ev_toolbar->settings, GS_SHOW_EXPAND_WINDOW,
                     ev_toolbar->expand_window_button, "visible",
                     G_SETTINGS_BIND_DEFAULT);
    g_settings_bind (ev_toolbar->settings, GS_SHOW_ZOOM_ACTION,
                     ev_toolbar->zoom_action, "visible",
                     G_SETTINGS_BIND_DEFAULT);
    g_settings_bind (ev_toolbar->settings, GS_SHOW_HISTORY_BUTTONS,
                     ev_toolbar->history_group, "visible",
                     G_SETTINGS_BIND_DEFAULT);

    ev_toolbar_document_model_changed_cb (ev_toolbar->model, NULL, ev_toolbar);
}

static void
ev_toolbar_finalize (GObject *object)
{
    EvToolbar *ev_toolbar = EV_TOOLBAR (object);

    g_clear_object (&ev_toolbar->settings);

    G_OBJECT_CLASS (ev_toolbar_parent_class)->finalize (object);
}

static void
ev_toolbar_class_init (EvToolbarClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->set_property = ev_toolbar_set_property;
    object_class->constructed = ev_toolbar_constructed;
    object_class->finalize = ev_toolbar_finalize;

    g_object_class_install_property (object_class,
                                     PROP_WINDOW,
                                     g_param_spec_object ("window",
                                                          "Window",
                                                          "The parent xreader window",
                                                          EV_TYPE_WINDOW,
                                                          G_PARAM_WRITABLE |
                                                          G_PARAM_CONSTRUCT_ONLY |
                                                          G_PARAM_STATIC_STRINGS));
}

static void
ev_toolbar_init (EvToolbar *ev_toolbar)
{
    ev_toolbar->settings = g_settings_new (GS_SCHEMA_NAME_TOOLBAR);
}

GtkWidget *
ev_toolbar_new (EvWindow *window)
{
    g_return_val_if_fail (EV_IS_WINDOW (window), NULL);

    return GTK_WIDGET (g_object_new (EV_TYPE_TOOLBAR, "window", window, NULL));
}

void
ev_toolbar_set_style (EvToolbar *ev_toolbar,
                      gboolean   is_fullscreen)
{
    g_return_if_fail (EV_IS_TOOLBAR (ev_toolbar));

    if (is_fullscreen)
    {
        gtk_widget_remove_css_class (GTK_WIDGET (ev_toolbar), "primary-toolbar");
        gtk_widget_set_visible (ev_toolbar->fullscreen_group, TRUE);
        gtk_widget_set_visible (ev_toolbar->expand_window_button, FALSE);
    }
    else
    {
        gtk_widget_add_css_class (GTK_WIDGET (ev_toolbar), "primary-toolbar");
        gtk_widget_set_visible (ev_toolbar->fullscreen_group, FALSE);
        gtk_widget_set_visible (ev_toolbar->expand_window_button, TRUE);
    }
}

void
ev_toolbar_set_preset_sensitivity (EvToolbar *ev_toolbar,
                                   gboolean   sensitive)
{
    g_return_if_fail (EV_IS_TOOLBAR (ev_toolbar));

    gtk_widget_set_sensitive (ev_toolbar->preset_group, sensitive);
}

void
ev_toolbar_activate_reader_view (EvToolbar *ev_toolbar)
{
    g_return_if_fail (EV_IS_TOOLBAR (ev_toolbar));

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ev_toolbar->reader_preset_button), TRUE);
}

void
ev_toolbar_activate_page_view (EvToolbar *ev_toolbar)
{
    g_return_if_fail (EV_IS_TOOLBAR (ev_toolbar));

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ev_toolbar->page_preset_button), TRUE);
}
