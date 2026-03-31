/* ev-history-action-widget.h
 *  this file is part of xreader, a document viewer
 *
 * Copied and modified from evince/shell/ev-history-action-widget.h
 */

#ifndef EV_HISTORY_ACTION_WIDGET_H
#define EV_HISTORY_ACTION_WIDGET_H

#include <gtk/gtk.h>
#include "ev-history.h"

G_BEGIN_DECLS

#define EV_TYPE_HISTORY_ACTION_WIDGET (ev_history_action_widget_get_type())
G_DECLARE_FINAL_TYPE (EvHistoryActionWidget, ev_history_action_widget, EV, HISTORY_ACTION_WIDGET, GtkWidget)

void  ev_history_action_widget_set_history (EvHistoryActionWidget *history_widget,
                                            EvHistory             *history);

G_END_DECLS

#endif /* EV_HISTORY_ACTION_WIDGET_H */
