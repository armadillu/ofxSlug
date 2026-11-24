#pragma once
#include "ofMain.h"
#include "slughorn/slughorn.hpp"
#include <unordered_map>
#include <string>
#include <vector>

// Desktop OpenGL 3.3+. Coordinates use the usual OF baseline convention.
class ofxSlugFont {
  public:
	ofxSlugFont();
	~ofxSlugFont();
	ofxSlugFont(const ofxSlugFont &) = delete;
	ofxSlugFont &operator=(const ofxSlugFont &) = delete;
	// glyphs is UTF-8; an empty string loads U+0020 through U+00FF.
	bool load(const std::string &fontPath, float size, const std::string &glyphs = "");
	void clear();
	bool isLoaded() const;
	// Tight outline bounds in local drawing coordinates (excluding antialiasing).
	ofRectangle getStringBoundingBox(const std::string &text, float x, float baselineY) const;
	float stringWidth(const std::string &text) const;
	// Positioned glyphs share this font's atlas and are submitted in one draw.
	struct PositionedGlyph {
		std::string text;
		float x, baselineY, size;
		ofFloatColor color;
	};
	void drawGlyphs(const std::vector<PositionedGlyph> &glyphs) const;

  private:
	slughorn::Atlas atlas;
	std::unordered_map<uint32_t, ofRectangle> inkBounds;
	float size = 0;
	GLuint program = 0, curve = 0, band = 0, vao = 0, vbo = 0;
	bool loaded = false;
};
