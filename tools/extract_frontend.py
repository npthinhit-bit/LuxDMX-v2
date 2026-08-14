#!/usr/bin/env python3
"""Extract page bodies, CSS, and JS from existing HTML files into frontend .h files."""
import re
import os

SRC = os.path.join(os.path.dirname(__file__), '..', 'src')
PAGES = os.path.join(SRC, 'pages')
FRONTEND = os.path.join(SRC, 'frontend')

def read(name):
    with open(os.path.join(PAGES, name), 'r', encoding='utf-8') as f:
        return f.read()

def write_h(path, content):
    with open(path, 'w', encoding='utf-8') as f:
        f.write(content)

# Extract CSS from <style>...</style> in <head>
def extract_css(html):
    m = re.search(r'<style>(.*?)</style>', html, re.DOTALL)
    return m.group(1).strip() if m else ''

<<<<<<< ours
<<<<<<< ours
# Extract body content between <!--NAVBAR--> and </body>
def extract_body(html):
    m = re.search(r'<!--NAVBAR-->(.*)</body>', html, re.DOTALL)
    return m.group(1).strip() if m else ''
=======
=======
>>>>>>> theirs
# Extract body content between <!--NAVBAR--> and the first <script> or footer
def extract_body(html):
    m = re.search(r'<!--NAVBAR-->(.*?)(?:<footer|</div>\s*</div>\s*<script|$)', html, re.DOTALL)
    if m:
        body = m.group(1).strip()
        # Remove trailing closing divs if any
        body = re.sub(r'\s*</div>\s*$', '', body)
        return body.strip()
    return ''
<<<<<<< ours
>>>>>>> theirs
=======
>>>>>>> theirs

# Extract all <script>...</script> blocks
def extract_scripts(html):
    return re.findall(r'<script>(.*?)</script>', html, re.DOTALL)

def make_h(name, content):
    return f'#pragma once\n#include <Arduino.h>\n\nstatic const char {name}[] PROGMEM = R"=====(\n{content}\n)=====";\n'

# --- index.html ---
idx = read('index.html')
idx_css = extract_css(idx)
idx_body = extract_body(idx)
idx_scripts = extract_scripts(idx)
idx_js = '\n'.join(idx_scripts)

write_h(os.path.join(FRONTEND, 'pages', 'index_page.h'), make_h('INDEX_PAGE_BODY', idx_body))
write_h(os.path.join(FRONTEND, 'pages', 'index_css.h'), make_h('INDEX_PAGE_CSS', idx_css))
write_h(os.path.join(FRONTEND, 'scripts', 'index_js.h'), make_h('INDEX_PAGE_JS', idx_js))

# --- config.html ---
cfg = read('config.html')
cfg_css = extract_css(cfg)
cfg_body = extract_body(cfg)
cfg_scripts = extract_scripts(cfg)
cfg_js = '\n'.join(cfg_scripts)

write_h(os.path.join(FRONTEND, 'pages', 'config_page.h'), make_h('CONFIG_PAGE_BODY', cfg_body))
write_h(os.path.join(FRONTEND, 'pages', 'config_css.h'), make_h('CONFIG_PAGE_CSS', cfg_css))
write_h(os.path.join(FRONTEND, 'scripts', 'config_js.h'), make_h('CONFIG_PAGE_JS', cfg_js))

# --- rdm.html ---
rdm = read('rdm.html')
rdm_css = extract_css(rdm)
rdm_body = extract_body(rdm)
rdm_scripts = extract_scripts(rdm)
rdm_js = '\n'.join(rdm_scripts)

write_h(os.path.join(FRONTEND, 'pages', 'rdm_page.h'), make_h('RDM_PAGE_BODY', rdm_body))
write_h(os.path.join(FRONTEND, 'pages', 'rdm_css.h'), make_h('RDM_PAGE_CSS', rdm_css))
write_h(os.path.join(FRONTEND, 'scripts', 'rdm_js.h'), make_h('RDM_PAGE_JS', rdm_js))

# --- Utility pages ---
for name, sym in [
    ('setup.html', 'SETUP_PAGE'),
    ('setup_done.html', 'SETUP_DONE_PAGE'),
    ('reset.html', 'RESET_PAGE'),
    ('reset_done.html', 'RESET_DONE_PAGE'),
    ('ota_progress.html', 'OTA_PROGRESS_PAGE'),
    ('ota_done.html', 'OTA_DONE_PAGE'),
    ('config_saved.html', 'CONFIG_SAVED_PAGE'),
]:
    html = read(name)
    body = extract_body(html)
    if not body:
        body = html
    write_h(os.path.join(FRONTEND, 'pages', name.replace('.html', '_page.h')), make_h(sym, body))

print('Extracted frontend pages from src/pages/*.html')
