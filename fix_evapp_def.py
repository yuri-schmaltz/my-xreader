import re

with open('shell/ev-application.c', 'r') as f:
    c = f.read()

# Fix the return variable in ev_application_get_empty_window
ol1 = """    return empty_window;"""
nl1 = """    return empty_window;"""
c = c.replace("    EvWindow *empty_window = NULL;", "    XreaderWindow *empty_window = NULL;") # Oh wait, we already did it, we just need to fix the return in ev_application_get_empty_window if we left one empty_window pointer type mismatched.
# Actually, the file has: return empty_window; where empty_window is declared as XreaderWindow *, but wait... why did gcc complain?
# "warning: returning ‘EvWindow *’ from a function with incompatible return type ‘XreaderWindow *’"
# Ah! Inside the function `ev_application_get_empty_window`, there might be another `empty_window` or we didn't replace it properly.
ol_empty = """    XreaderWindow *empty_window = NULL;""" # actually we did replace it, let's see.

# Fix the parameter in ev_application_open_uri_in_window definition:
c = re.sub(r'ev_application_open_uri_in_window\s*\(\s*EvApplication\s*\*application,\s*const\s*char\s*\*uri,\s*EvWindow\s*\*ev_window,',
           r'ev_application_open_uri_in_window (EvApplication  *application,\n                                   const char     *uri,\n                                   XreaderWindow  *window,', c)

# remove ev_spawn
c = re.sub(r'static void\nev_spawn[\s\S]*?gnome_desktop_app_launch_context_set_timestamp[\s\S]*?\}', '', c)


with open('shell/ev-application.c', 'w') as f:
    f.write(c)
