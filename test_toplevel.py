import re

with open('shell/ev-window.c', 'r') as f:
    content = f.read()

count_window = len(re.findall(r'GTK_WINDOW\s*\([^)]*window\)', content))
count_appwin = len(re.findall(r'GTK_APPLICATION_WINDOW\s*\([^)]*window\)', content))

print(f"GTK_WINDOW casting count: {count_window}")
print(f"GTK_APPLICATION_WINDOW casting count: {count_appwin}")
