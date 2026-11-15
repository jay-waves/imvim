# Bundled fallback fonts

These fonts supply missing glyphs; they do not replace the selected Latin monospace font.
Only requested glyphs are rasterized by ImGui. Keep the `fonts` directory beside `imvim.exe`.

- `SymbolsNerdFontMono-Regular.ttf`: Nerd Fonts Symbols Only v3.5.1, MIT license.
  Source: https://github.com/ryanoasis/nerd-fonts/releases/tag/v3.5.1
  Archive: `NerdFontsSymbolsOnly.zip`; see `NerdFonts-LICENSE.txt`.
- `DroidSansFallbackFull.ttf`: Android Droid fallback font, Apache-2.0.
  Copied without modification from Fedora's `google-droid-sans-fonts` package;
  upstream https://android.googlesource.com/platform/frameworks/base/ (historical `data/fonts`).
  SHA-256: `2392015530438bafc48edfc4aee6d9de2387f627a6134d8ab3dfcc99d21c8240`.
  See `DroidSans-NOTICE.txt` for copyright and the full license.

The default fallback covers common CJK and Nerd Font symbols. It is not a complete
Unicode collection; complex shaping, color emoji and CJK extension characters may
need additional support. `--fallback-font FILE` replaces the bundled CJK fallback.
