/* ev-toolbar.h
 *  this file is part of xreader, a document viewer
 */

#ifndef __EV_TOOLBAR_H__
#define __EV_TOOLBAR_H__

#include <gtk/gtk.h>
#include "ev-window.h"

G_BEGIN_DECLS

#define EV_TYPE_TOOLBAR (ev_toolbar_get_type())
G_DECLARE_FINAL_TYPE (EvToolbar, ev_toolbar, EV, TOOLBAR, GtkBox)

GtkWidget *ev_toolbar_new (EvWindow *window);

void ev_toolbar_set_style (EvToolbar *ev_toolbar,
                           gboolean   is_fullscreen);

void ev_toolbar_set_preset_sensitivity (EvToolbar *ev_toolbar,
                                        gboolean   sensitive);

void ev_toolbar_activate_reader_view (EvToolbar *ev_toolbar);
void ev_toolbar_activate_page_view (EvToolbar *ev_toolbar);

gboolean ev_toolbar_zoom_action_get_focused (EvToolbar *ev_toolbar);
void ev_toolbar_zoom_action_select_all (EvToolbar *ev_toolbar);

G_END_DECLS

#endif /* __EV_TOOLBAR_H__ */
