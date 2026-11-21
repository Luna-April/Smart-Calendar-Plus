"""Embed the offline editor in firmware. Run after editing web/album.html."""
from pathlib import Path
import sys
root = Path(__file__).resolve().parents[1]
html = (root / 'web/album.html').read_text()
output = '#pragma once\n#include <Arduino.h>\nstatic const char ALBUM_HTML[] PROGMEM = R"ALBUM(' + html + ')ALBUM";\n'
header = root / 'include/album_page.h'
if '--check' in sys.argv:
    assert header.read_text() == output, 'Run python tools/embed_album.py'
else:
    header.write_text(output)
