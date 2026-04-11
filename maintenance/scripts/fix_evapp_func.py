import re

with open('shell/ev-application.c', 'r') as f:
    c = f.read()

# Fix ev_application_open_uri_in_window body
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

    gtk_window_present_with_time (GTK_WINDOW (window), timestamp);
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

# Actually, the file has:
#         gtk_widget_realize (GTK_WIDGET (ev_window));

c = re.sub(r'static void\nev_application_open_uri_in_window[\s\S]*?gtk_window_present_with_time \(GTK_WINDOW \(window\), timestamp\);\n\}', nl2, c)
# if it fails:
c = re.sub(r'if \(!gtk_widget_get_realized.*?gtk_widget_set_visible.*?TRUE\);', 'xreader_window_open_document (window, uri, dest, mode, search_string);', c, flags=re.DOTALL)


# Fix handle_reload_cb which passes EV_WINDOW(l->data)
c = re.sub(r'ev_application_open_uri_in_window \(application, NULL,\n\s*EV_WINDOW \(l->data\),',
           r'ev_application_open_uri_in_window (application, NULL,\n                                           XREADER_WINDOW (l->data),', c)

# Fix ev_application_get_empty_window return
c = re.sub(r'EvWindow \*empty_window = NULL;\n\tGList', r'XreaderWindow *empty_window = NULL;\n\tGList', c)

with open('shell/ev-application.c', 'w') as f:
    f.write(c)

