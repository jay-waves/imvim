#!/usr/bin/env python3
"""Check config-driven font lookup, dimensions and command-line precedence."""
import json
import pathlib
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix='imvim-config-gui-') as directory:
    config = pathlib.Path(directory) / 'config.json'
    screenshot = pathlib.Path(directory) / 'screen.ppm'
    config.write_text(json.dumps({
        'font': {'family': 'monospace', 'fallback_families': ['sans-serif:charset=2713'],
                 'size': 15, 'cjk_scale': 1.1, 'line_spacing': 1, 'letter_spacing': 0.25},
        'window': {'width': 840, 'height': 520},
        'nvim': {'path': 'nvim', 'args': ['--clean', '-n']}
    }))
    subprocess.run([sys.argv[1], '--config', str(config), '--smoke-seconds', '2', '--screenshot', str(screenshot)], check=True, timeout=15)
    with screenshot.open('rb') as file:
        assert file.readline().strip() == b'P6'
        assert file.readline().strip() == b'840 520', 'Configured client dimensions not applied'
    # A CLI-selected family must replace a missing configured font file.
    config.write_text(json.dumps({'font': {'file': 'missing-font.ttf'}}))
    subprocess.run([sys.argv[1], '--config', str(config), '--font-family', 'monospace', '--smoke-seconds', '1'], check=True, timeout=15)
    print('PASS: config-driven fonts, dimensions and command-line precedence')
