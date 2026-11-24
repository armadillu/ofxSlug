# ofxSlug

An openFrameworks addon for rendering font outlines on the GPU with the Slug algorithm. It uses [slughorn](https://github.com/AlphaPixel/slughorn) to convert FreeType outlines into curve and band textures and adapts slughorn's OpenGL example shader. Slug shader code Copyright 2017 by Eric Lengyel. slughorn is Copyright AlphaPixel and contributors; its MIT license is in [SLUGHORN-LICENSE](SLUGHORN-LICENSE).

## Requirements and setup

- Desktop OpenGL 3.3 or newer and the OF programmable renderer. Request it before creating the window (`settings.setGLVersion(3, 3)`). OpenGL ES and the legacy renderer are not supported.
- C++20 and a recent openFrameworks with FreeType. `addon_config.mk` requests C++20 for supported make-based projects. Xcode and other project generators may need the C++ language standard set to C++20 manually.
- Add `ofxSlug` to the project using the project generator or `addons.make`. Install or symlink this directory into your OF `addons` directory if the generator cannot find external addons.

## Usage

`ofxSlug` manages named fonts and styles and offers `draw`, `drawFormatted`, `drawColumn`, and `drawFormattedColumn`.

```cpp
ofxSlug fonts;
fonts.setup();
fonts.addFont("regular", "regular.ttf");
fonts.addFont("bold", "bold.ttf");
fonts.addStyle("body", ofxSlug::Style("regular", 24, ofColor::white));
fonts.addStyle("bodyBold", ofxSlug::Style("bold", 24, ofColor::white));
fonts.setDefaultStyle("body");

// In draw():
ofRectangle bounds = fonts.drawFormattedColumn(
    "<body>Hello <bodyBold>world</bodyBold>. "
    "<style color='#ff8844'>Orange</style><br/>Next line.</body>",
    40, 40, 500, OF_ALIGN_HORZ_LEFT);
```

Named style tags may be nested. `<style id="styleName">` also selects a registered style. A `<style>` or named tag accepts `font`, `size`, `color` (`#rgb`, `#rgba`, `#rrggbb`, `#rrggbbaa`), and `spacing` attributes. Text uses XML syntax: line breaks are `<br/>`, and literal `&` and `<` must be escaped. `<p>` starts/ends a line. `drawColumn` and `drawFormattedColumn` take the column x position and the first line baseline as y, including when the first line has mixed font sizes. Both return the line-box rectangle, whose top may be above the supplied baseline. Single-line `draw` uses a baseline. Alignment may be left, center, or right. Tabs advance to the next tab stop, including at the start of a line; `fonts.tabWidth` sets the spacing in space advances (default 4). By default fonts include U+0020–U+00FF; pass a UTF-8 string containing all needed glyphs as the optional third argument to `addFont` to choose a different set.

This is a compatible subset rather than a drop-in replacement: NanoVG access, blur, explicit GPU batch APIs and full vertical metrics are not implemented. Layout does not provide kerning, shaping, or bidirectional text, and batches consecutive glyphs using the same font. Use a separate font for each weight or italic style. Call `fonts.clear()` while the OpenGL context is active, such as in `exit()`.

The example uses the adjacent `ofxTimeMeasurements` addon for CPU and GPU timing. Press `D` to toggle layout guides and `M` to turn timing collection and its overlay on or off. Press `T` to enter the timing menu, use the arrow keys to select a sample, and Enter to enable or disable its section. In debug mode, the magenta rectangle is the returned paragraph line-box bounds, white boxes are individual line boxes, orange lines are baselines, red blocks show word advances, cyan blocks show spaces, and vertical guides mark the requested column width. These are layout measurements, not tight glyph-ink bounds. Pass `true` as the final `debug` argument to `drawColumn` or `drawFormattedColumn` to display the guides in your own app. The ofxSlug addon itself does not depend on ofxTimeMeasurements.

The example follows the main FontStash2 demo: move the mouse left, center, or right to change column alignment while column widths animate. It demonstrates plain and styled text, with debug guides and timing. The sample needs `font-unicode.ttf`, `font-mono.ttf` and `NotoEmoji-Regular.ttf` in `bin/data`. The original sample text is reproduced, but its DIY sections use the three-stage API; blur and color emoji are not implemented.

## Fallback fonts and emoji

A global fallback can supply missing glyphs, including monochrome outline emoji:

```cpp
fonts.addFont("emoji", "NotoEmoji-Regular.ttf", "😀💩🐙🐵");
fonts.setGlobalFallbackFont("emoji");
```

Register the fallback first, and include all desired characters in its glyph string: like every font in this addon, it loads U+0020–U+00FF by default. Fallback is used when the primary font lacks a loaded glyph; measurement, line wrapping, bounds and rendering all use the selected font. Pass an empty ID to disable it. Changes to fonts or fallback require cached layouts to be rebuilt.

Emoji use the text style's color. This is monochrome outline rendering; color emoji fonts, variation selectors, skin-tone combinations, flags and zero-width-joiner sequences are not shaped or supported as combined emoji. The two examples link to the Noto Emoji font in `AddonsExamples/ofxFontStash2_example/bin/data/fonts`.

## Inspecting and reusing layout

The convenience drawing methods remain available. To inspect or edit a layout before drawing it, use the three stages:

```cpp
// Parse and lay out when the text, styles, or column width change.
auto items = fonts.parseStyledText("<body>Hello <bodyBold>world</bodyBold></body>");
auto layout = fonts.layoutLines(items, 500, OF_ALIGN_HORZ_LEFT, 3);
if(layout.truncated) {
    // More than three lines were needed.
}

for(size_t lineNumber = 0; lineNumber < layout.lines.size(); ++lineNumber) {
    for(const auto &item : layout.lines[lineNumber].items) {
        if(item.wordIndex != ofxSlug::noWord) {
            // Characters with the same wordIndex belong to the same word,
            // even across style changes or a line break within a long word.
        }
    }
}

// In draw(): x is the column origin; y is the first line's baseline.
fonts.drawLines(layout, 40, 40);
```

`parseText(text, style)` provides the same first stage for plain text. Parsed items contain UTF-8 codepoints and their styles. `layoutLines` remeasures and splits the text of its input items, so you can edit their text or styles before layout. Its final argument limits lines; zero means unlimited. The returned layout stores the column width, alignment, line metrics, item advances and `truncated` status. Word indices are zero-based, assigned in text order, and whitespace uses `noWord`; punctuation stays part of the word. They identify words in the decoded text, not byte offsets in the original XML.

You may inspect or modify the returned items, line metrics, width and alignment before drawing. Changing colors needs no relayout. If you change text, font, size or spacing, edit the parsed items and call `layoutLines` again; changing advances or line contents directly also requires updating the corresponding line metrics. Reuse a layout until text, metrics, loaded fonts, line-height settings or column width change. Drawing still builds and uploads glyph batches each time.

## Text bounds

`fonts.getTextBounds(text, style, x, baselineY)` returns tight outline bounds in local coordinates for `draw()`, excluding antialiasing. Column methods return line-box bounds instead.

## Migration

The former `ofxSlug::Fonts` is now `ofxSlug`; replace `ofxSlug::Fonts fonts;` with `ofxSlug fonts;`. The old single-font `load`, `drawString`, `getStringBoundingBox`, `stringWidth`, and `drawParagraph` interface has been removed. Register fonts with `addFont` and use a `Style` with `draw` or `getTextBounds`. For formatted paragraphs, register styles and use `drawFormattedColumn`; its XML markup differs from the old HTML-like paragraph syntax.

Slughorn revision: `1e09656e5fe2da7a24c164fe8697187ebf28f673`. Its FreeType implementation is enabled in `slughorn.cpp`. The addon logs through the `ofxSlug` channel; set it to `OF_LOG_WARNING` to suppress routine notices.
