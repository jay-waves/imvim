#!/usr/bin/env python3
"""Exercise native key events through the GUI, then verify Neovim saved the file.
Run with: xvfb-run -a python3 tests/gui_x11.py build/linux/imvim
Requires system libX11 and libXtst; no Python packages.
"""
import ctypes as c
import ctypes.util
import pathlib
import subprocess
import sys
import tempfile
import time

x11 = c.CDLL(ctypes.util.find_library('X11'))
xtst = c.CDLL(ctypes.util.find_library('Xtst'))
x11.XOpenDisplay.argtypes = [c.c_char_p]
x11.XOpenDisplay.restype = c.c_void_p
x11.XDefaultRootWindow.argtypes = [c.c_void_p]
x11.XDefaultRootWindow.restype = c.c_ulong
x11.XQueryTree.argtypes = [c.c_void_p, c.c_ulong, c.POINTER(c.c_ulong), c.POINTER(c.c_ulong), c.POINTER(c.POINTER(c.c_ulong)), c.POINTER(c.c_uint)]
x11.XFetchName.argtypes = [c.c_void_p, c.c_ulong, c.POINTER(c.c_void_p)]
x11.XFree.argtypes = [c.c_void_p]
x11.XSetInputFocus.argtypes = [c.c_void_p, c.c_ulong, c.c_int, c.c_ulong]
x11.XKeysymToKeycode.argtypes = [c.c_void_p, c.c_ulong]
x11.XKeysymToKeycode.restype = c.c_uint
x11.XFlush.argtypes = [c.c_void_p]
x11.XCloseDisplay.argtypes = [c.c_void_p]
xtst.XTestFakeKeyEvent.argtypes = [c.c_void_p, c.c_uint, c.c_int, c.c_ulong]
display = x11.XOpenDisplay(None)
if not display:
    raise RuntimeError('No X display; run under xvfb-run')

def find_window():
    root = x11.XDefaultRootWindow(display)
    parent, root_out, count = c.c_ulong(), c.c_ulong(), c.c_uint()
    children = c.POINTER(c.c_ulong)()
    x11.XQueryTree(display, root, c.byref(root_out), c.byref(parent), c.byref(children), c.byref(count))
    result = None
    for i in range(count.value):
        name = c.c_void_p()
        if x11.XFetchName(display, children[i], c.byref(name)) and name.value:
            if b'imvim' in c.string_at(name):
                result = children[i]
            x11.XFree(name)
    if children:
        x11.XFree(children)
    return result

def key(symbol, down):
    code = x11.XKeysymToKeycode(display, symbol)
    if not code:
        raise RuntimeError(f'No keycode for {symbol}')
    xtst.XTestFakeKeyEvent(display, code, int(down), 0)
    x11.XFlush(display)
    time.sleep(0.035)

def tap(symbol):
    key(symbol, True)
    key(symbol, False)

try:
    with tempfile.TemporaryDirectory(prefix='imvim-gui-') as directory:
        target = pathlib.Path(directory) / 'keyboard test.txt'
        process = subprocess.Popen([sys.argv[1], '--clean', '--', str(target)])
        try:
            deadline = time.monotonic() + 8
            window = None
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    raise RuntimeError('GUI exited before opening a window')
                window = find_window()
                if window:
                    break
                time.sleep(0.05)
            if not window:
                raise RuntimeError('GUI window did not appear')
            x11.XSetInputFocus(display, window, 1, 0)
            x11.XFlush(display)
            time.sleep(0.5)
            for char in 'ihello from native keys':
                tap(ord(char))
            tap(0xff1b)  # Escape
            key(0xffe1, True)  # Shift
            tap(ord('z'))
            tap(ord('z'))  # ZZ: save and exit, through the GUI
            key(0xffe1, False)
            if process.wait(timeout=8) != 0:
                raise RuntimeError('GUI returned a failure')
            if target.read_text() != 'hello from native keys\n':
                raise RuntimeError(f'Unexpected saved text: {target.read_text()!r}')
            print('PASS: X11 key events -> ImGui -> Neovim -> saved file -> clean exit')
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)
finally:
    x11.XCloseDisplay(display)
