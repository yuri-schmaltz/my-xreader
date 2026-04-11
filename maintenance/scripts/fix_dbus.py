import re

with open('shell/ev-application.c', 'r') as f:
    c = f.read()

# Fix handle_get_window_list_cb
ol5 = """        windows = gtk_application_get_windows (GTK_APPLICATION (application));
        for (l = windows; l; l = g_list_next (l)) {
            if (!EV_IS_WINDOW (l->data))
                continue;

            g_ptr_array_add (paths, (gpointer) ev_window_get_dbus_object_path (EV_WINDOW (l->data)));
        }"""
nl5 = """        windows = gtk_application_get_windows (GTK_APPLICATION (application));
        for (l = windows; l; l = g_list_next (l)) {
            if (!XREADER_IS_WINDOW (l->data))
                continue;

            EvWindow *ew = xreader_window_get_active_tab(XREADER_WINDOW(l->data));
            if (ew) {
                g_ptr_array_add (paths, (gpointer) ev_window_get_dbus_object_path (ew));
            }
        }"""
c = c.replace(ol5, nl5)

with open('shell/ev-application.c', 'w') as f:
    f.write(c)
