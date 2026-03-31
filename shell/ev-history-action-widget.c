/* ev-history-action-widget.c
 *  this file is part of xreader, a document viewer
 */

#include "config.h"
#include "ev-history-action-widget.h"
#include <glib/gi18n.h>
#include <math.h>

struct _EvHistoryActionWidget
{
    GtkWidget parent_instance;

    GtkWidget *back_button;
    GtkWidget *forward_button;

    EvHistory *history;
    gboolean popup_shown;
};

enum
{
    PROP_0,
    PROP_POPUP_SHOWN
};

typedef enum
{
    EV_HISTORY_ACTION_BUTTON_BACK,
    EV_HISTORY_ACTION_BUTTON_FORWARD
} EvHistoryActionButton;

G_DEFINE_TYPE (EvHistoryActionWidget, ev_history_action_widget, GTK_TYPE_WIDGET)

static void
ev_history_action_widget_finalize (GObject *object)
{
    EvHistoryActionWidget *control = EV_HISTORY_ACTION_WIDGET (object);

    ev_history_action_widget_set_history (control, NULL);

    G_OBJECT_CLASS (ev_history_action_widget_parent_class)->finalize (object);
}

static void
ev_history_action_widget_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
    EvHistoryActionWidget *history_widget = EV_HISTORY_ACTION_WIDGET (object);

    switch (prop_id)
    {
        case PROP_POPUP_SHOWN:
            g_value_set_boolean (value, history_widget->popup_shown);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
ev_history_action_widget_set_popup_shown (EvHistoryActionWidget *history_widget,
                                          gboolean               popup_shown)
{
    if (history_widget->popup_shown == popup_shown)
    {
        return;
    }

    history_widget->popup_shown = popup_shown;
    g_object_notify (G_OBJECT (history_widget), "popup-shown");
}

static void
history_button_link_activated (GtkButton             *btn,
                               EvHistoryActionWidget *history_widget)
{
    EvLink *link = EV_LINK (g_object_get_data (G_OBJECT (btn), "ev-history-button-link"));
    if (link)
        ev_history_go_to_link (history_widget->history, link);

    GtkWidget *popover = g_object_get_data (G_OBJECT (history_widget), "popup-popover");
    if (popover)
        gtk_popover_popdown (GTK_POPOVER (popover));
}

static void
popover_closed_cb (GtkPopover *popover, EvHistoryActionWidget *history_widget)
{
    ev_history_action_widget_set_popup_shown (history_widget, FALSE);
    g_object_set_data (G_OBJECT (history_widget), "popup-popover", NULL);
}

static void
ev_history_action_widget_show_popup (EvHistoryActionWidget *history_widget,
                                     EvHistoryActionButton  action_button,
                                     GtkWidget             *parent_button)
{
    GtkWidget *popover;
    GtkWidget *box;
    GList *list = NULL;
    GList *l;

    switch (action_button)
    {
        case EV_HISTORY_ACTION_BUTTON_BACK:
            list = ev_history_get_back_list (history_widget->history);
            break;
        case EV_HISTORY_ACTION_BUTTON_FORWARD:
            list = ev_history_get_forward_list (history_widget->history);
            break;
    }

    if (!list)
        return;

    popover = gtk_popover_new ();
    gtk_widget_set_parent (popover, parent_button);
    box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_popover_set_child (GTK_POPOVER (popover), box);

    for (l = list; l; l = g_list_next (l))
    {
        EvLink    *link = EV_LINK (l->data);
        GtkWidget *btn = gtk_button_new_with_label (ev_link_get_title (link));
        gtk_button_set_has_frame (GTK_BUTTON (btn), FALSE);
        g_object_set_data_full (G_OBJECT (btn), "ev-history-button-link",
                                g_object_ref (link), (GDestroyNotify)g_object_unref);
        g_signal_connect (btn, "clicked",
                          G_CALLBACK (history_button_link_activated), history_widget);
        gtk_box_append (GTK_BOX (box), btn);
    }
    g_list_free (list);

    g_object_set_data (G_OBJECT (history_widget), "popup-popover", popover);
    g_signal_connect (popover, "closed", G_CALLBACK (popover_closed_cb), history_widget);
    ev_history_action_widget_set_popup_shown (history_widget, TRUE);
    gtk_popover_popup (GTK_POPOVER (popover));
}

static void
button_clicked (GtkWidget             *button,
                EvHistoryActionWidget *history_widget)
{
    if (button == history_widget->back_button)
    {
        ev_history_go_back (history_widget->history);
    }
    else if (button == history_widget->forward_button)
    {
        ev_history_go_forward (history_widget->history);
    }
}

static void
right_clicked_cb (GtkGestureClick *gesture,
                  gint n_press, gdouble x, gdouble y,
                  EvHistoryActionWidget *history_widget)
{
    GtkWidget *button = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
    ev_history_action_widget_show_popup (history_widget,
                                         button == history_widget->back_button ?
                                         EV_HISTORY_ACTION_BUTTON_BACK :
                                         EV_HISTORY_ACTION_BUTTON_FORWARD,
                                         button);
}

static void
long_pressed_cb (GtkGestureLongPress *gesture,
                 gdouble x, gdouble y,
                 EvHistoryActionWidget *history_widget)
{
    GtkWidget *button = gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (gesture));
    ev_history_action_widget_show_popup (history_widget,
                                         button == history_widget->back_button ?
                                         EV_HISTORY_ACTION_BUTTON_BACK :
                                         EV_HISTORY_ACTION_BUTTON_FORWARD,
                                         button);
}

static GtkWidget *
ev_history_action_widget_create_button (EvHistoryActionWidget *history_widget,
                                        EvHistoryActionButton  action_button)
{
    GtkWidget *button;
    GtkWidget *image;
    const gchar *icon_name = NULL;
    const gchar *tooltip_text = NULL;

    button = gtk_button_new ();
    gtk_widget_set_valign (button, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class (button, "flat");

    g_signal_connect (button, "clicked",
                      G_CALLBACK (button_clicked), history_widget);
                      
    GtkEventController *right_click = GTK_EVENT_CONTROLLER (gtk_gesture_click_new ());
    gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (right_click), GDK_BUTTON_SECONDARY);
    g_signal_connect (right_click, "pressed", G_CALLBACK (right_clicked_cb), history_widget);
    gtk_widget_add_controller (button, right_click);

    GtkEventController *long_press = GTK_EVENT_CONTROLLER (gtk_gesture_long_press_new ());
    g_signal_connect (long_press, "pressed", G_CALLBACK (long_pressed_cb), history_widget);
    gtk_widget_add_controller (button, long_press);

    switch (action_button)
    {
        case EV_HISTORY_ACTION_BUTTON_BACK:
            icon_name = "xsi-go-previous-symbolic";
            tooltip_text = _("Go to previous history item");
            break;
        case EV_HISTORY_ACTION_BUTTON_FORWARD:
            icon_name = "xsi-go-next-symbolic";
            tooltip_text = _("Go to next history item");
            break;
    }

    image = gtk_image_new_from_icon_name (icon_name);
    gtk_button_set_child (GTK_BUTTON (button), image);
    gtk_widget_set_tooltip_text (button, tooltip_text);
    gtk_widget_set_focusable (button, FALSE);

    return button;
}

static void
ev_history_action_widget_init (EvHistoryActionWidget *history_widget)
{
    GtkWidget *box;

    box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_parent (box, GTK_WIDGET (history_widget));

    history_widget->back_button = ev_history_action_widget_create_button (history_widget, EV_HISTORY_ACTION_BUTTON_BACK);
    gtk_box_append (GTK_BOX (box), history_widget->back_button);

    history_widget->forward_button = ev_history_action_widget_create_button (history_widget, EV_HISTORY_ACTION_BUTTON_FORWARD);
    gtk_box_append (GTK_BOX (box), history_widget->forward_button);

    gtk_widget_set_sensitive (history_widget->back_button, FALSE);
    gtk_widget_set_sensitive (history_widget->forward_button, FALSE);
}

static void
ev_history_action_widget_dispose (GObject *object)
{
	EvHistoryActionWidget *history_widget = EV_HISTORY_ACTION_WIDGET (object);
	GtkWidget *child;

	while ((child = gtk_widget_get_first_child (GTK_WIDGET (history_widget))))
		gtk_widget_unparent (child);

	G_OBJECT_CLASS (ev_history_action_widget_parent_class)->dispose (object);
}

static void
ev_history_action_widget_class_init (EvHistoryActionWidgetClass *klass)
{
    GObjectClass   *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    object_class->get_property = ev_history_action_widget_get_property;
    object_class->finalize = ev_history_action_widget_finalize;
    object_class->dispose = ev_history_action_widget_dispose;

    g_object_class_install_property (object_class,
                                     PROP_POPUP_SHOWN,
                                     g_param_spec_boolean ("popup-shown",
                                                           "Popup shown",
                                                           "Whether the history's dropdown is shown",
                                                           FALSE,
                                                           G_PARAM_READABLE |
                                                           G_PARAM_STATIC_STRINGS));

    gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
}

static void
history_changed_cb (EvHistory             *history,
                    EvHistoryActionWidget *history_widget)
{
    gtk_widget_set_sensitive (history_widget->back_button, ev_history_can_go_back (history));
    gtk_widget_set_sensitive (history_widget->forward_button, ev_history_can_go_forward (history));
}

void
ev_history_action_widget_set_history (EvHistoryActionWidget *history_widget,
                                      EvHistory             *history)
{
    g_return_if_fail (EV_IS_HISTORY_ACTION_WIDGET (history_widget));

    if (history_widget->history == history)
    {
        return;
    }

    if (history_widget->history)
    {
        g_object_remove_weak_pointer (G_OBJECT (history_widget->history),
                                      (gpointer)&history_widget->history);
        g_signal_handlers_disconnect_by_func (history_widget->history,
                                              G_CALLBACK (history_changed_cb), history_widget);
    }

    history_widget->history = history;
    if (!history)
    {
        return;
    }

    g_object_add_weak_pointer (G_OBJECT (history), (gpointer)&history_widget->history);

    g_signal_connect (history, "changed",
                      G_CALLBACK (history_changed_cb), history_widget);
}
