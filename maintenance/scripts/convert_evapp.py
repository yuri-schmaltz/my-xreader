import re

with open('shell/ev-application.c', 'r') as f:
    c = f.read()

c = c.replace('#include "ev-application.h"', '#include "ev-application.h"\n#include "xreader-window.h"')

# Fix ev_application_get_empty_window to return XreaderWindow
c = c.replace('static EvWindow *\nev_application_get_empty_window', 'static XreaderWindow *\nev_application_get_empty_window')

# Replace EvWindow with XreaderWindow in that block
ol1 = """    EvWindow *empty_window = NULL;
GList    *windows;
    GList    *l;

    windows = gtk_application_get_windows (GTK_APPLICATION (application));
    for (l = windows; l != NULL; l = l->next) {
dow *window;

        if (!EV_IS_WINDOW (l->data))
            continue;

        window = EV_WINDOW (l->data);

        if (ev_window_is_empty (window) &&
            gtk_widget_get_display (GTK_WIDGET (window)) == display) {"""

nl1 = """    XreaderWindow *empty_window = NULL;
GList    *windows;
    GList    *l;

    windows = gtk_application_get_windows (GTK_APPLICATION (application));
    for (l = windows; l != NULL; l = l->next) {
dow *window;

        if (!XREADER_IS_WINDOW (l->data))
            continue;

        window = XREADER_WINDOW (l->data);
        EvWindow *ev_window = xreader_window_get_active_tab(window);

        if (ev_window && ev_window_is_empty (ev_window) &&
            gtk_widget_get_display (GTK_WIDGET (window)) == display) {"""

c = c.replace(ol1, nl1)


# Fix ev_application_open_uri_in_window
ol2 = """static void
ev_application_open_uri_in_window (EvApplication  *application,
                                   const char     *uri,
                                   XreaderWindow  *window,
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
}"""

nl2 = """static void
ev_application_open_uri_in_window (EvApplication  *application,
                                   const char     *uri,
                                   XreaderWindow  *window,
                                   GdkDisplay     *display,
                                   EvLinkDest     *dest,
                                   EvWindowRunMode mode,
                                   const gchar    *search_string,
                                   guint           timestamp)
{
    if (uri == NULL)
        uri = application->uri;

    xreader_window_open_document (window, uri, dest, mode, search_string);
    
    gtk_window_present_with_time (GTK_WINDOW (window), timestamp);
}"""

c = c.replace(ol2, nl2)


# Fix _ev_application_open_uri_at_dest
ol3 = """    EvWindow *ev_window;

    ev_window = ev_application_get_empty_window (application, display);
    if (!ev_window)
        ev_window = EV_WINDOW (ev_window_new ());

    ev_application_open_uri_in_window (application, uri, ev_window,"""

nl3 = """    XreaderWindow *window;

    window = ev_application_get_empty_window (application, display);
    if (!window) {
        window = XREADER_WINDOW (xreader_window_new (GTK_APPLICATION (application)));
        gtk_window_present(GTK_WINDOW(window));
    }
    
    ev_application_open_uri_in_window (application, uri, window,"""
c = c.replace(ol3, nl3)

# Fix D-Bus handle_reload_cb / ev_application_register_uri iterates existing windows

ol4 = """            if (!EV_IS_WINDOW (l->data))
                continue;

            ev_application_open_uri_in_window (application, uri,
                                               EV_WINDOW (l->data),"""
nl4 = """            if (!XREADER_IS_WINDOW (l->data))
                continue;

            ev_application_open_uri_in_window (application, uri,
                                               XREADER_WINDOW (l->data),"""
c = c.replace(ol4, nl4)


with open('shell/ev-application.c', 'w') as f:
    f.write(c)

