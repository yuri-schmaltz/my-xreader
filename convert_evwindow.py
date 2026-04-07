import re

with open('shell/ev-window.c', 'r') as f:
    c = f.read()

# Change parent class
c = c.replace('GTK_TYPE_APPLICATION_WINDOW', 'GTK_TYPE_BOX')

# Create helper macro for toplevel
helper = "#define _ev_window_get_toplevel(w) GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(w)))\n"
c = c.replace('#define EV_WINDOW_IS_PRESENTATION(w) (w->priv->presentation_view != NULL)', 
              helper + '#define EV_WINDOW_IS_PRESENTATION(w) (w->priv->presentation_view != NULL)')

# Replace GTK_WINDOW(ev_window) and GTK_WINDOW(window)
# Be careful not to replace it if it's already using the helper.
c = re.sub(r'GTK_WINDOW\s*\(\s*ev_window\s*\)', '_ev_window_get_toplevel (ev_window)', c)
c = re.sub(r'GTK_WINDOW\s*\(\s*window\s*\)', '_ev_window_get_toplevel (window)', c)

# Fix gtk_window_set_child to just gtk_box_append
c = c.replace('gtk_window_set_child (_ev_window_get_toplevel (ev_window), ev_window->priv->main_box);',
              'gtk_box_append (GTK_BOX (ev_window), ev_window->priv->main_box);')

# Replace GTK_APPLICATION_WINDOW (ev_window)
c = re.sub(r'GTK_APPLICATION_WINDOW\s*\(\s*ev_window\s*\)', 'GTK_APPLICATION_WINDOW (gtk_widget_get_root(GTK_WIDGET(ev_window)))', c)

# In ev_window_new, remove the "application" property
new_func_old = """GtkWidget *
ev_window_new (void)
{
    return GTK_WIDGET (g_object_new (EV_TYPE_WINDOW,
                                     "application", g_application_get_default (),
                                     NULL));
}"""

new_func_new = """GtkWidget *
ev_window_new (void)
{
    return GTK_WIDGET (g_object_new (EV_TYPE_WINDOW,
                                     "orientation", GTK_ORIENTATION_VERTICAL,
                                     NULL));
}"""
c = c.replace(new_func_old, new_func_new)

with open('shell/ev-window.c', 'w') as f:
    f.write(c)

