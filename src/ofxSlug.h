#pragma once
#include "ofMain.h"
#include "ofxSlugFont.h"
#include "pugixml.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Named fonts and styles for Slughorn-based text rendering.
class ofxSlug {

  public:
	struct Style {
		std::string fontID;
		float fontSize = 12.f;
		ofColor color = ofColor::white;
		float lineHeightMult = 1.f;
		float spacing = 0.f;
		Style() = default;
		Style(const std::string &id, float size, const ofColor &c = ofColor::white) : fontID(id), fontSize(size), color(c) {
		}
	};

	ofxSlug();
	~ofxSlug();
	ofxSlug(const ofxSlug &) = delete;
	ofxSlug &operator=(const ofxSlug &) = delete;
	void setup(bool debug = false);
	void clear();
	bool addFont(const std::string &fontID, const std::string &fontFile, const std::string &glyphs = "");
	bool isFontLoaded(const std::string &fontID) const;
	std::vector<std::string> getFontIDs() const;
	bool addStyle(const std::string &styleID, const Style &style);
	bool removeStyle(const std::string &styleID);
	bool styleExists(const std::string &styleID) const;
	Style getStyle(const std::string &styleID, bool *exists = nullptr) const;
	std::unordered_map<std::string, Style> &getStyles();
	void setDefaultStyle(const std::string &styleID);
	void setLineHeightMult(float multiplier);
	// Number of space advances between tab stops.
	int tabWidth = 4;
	float draw(const std::string &text, const Style &style, float x, float y);
	// Tight outline bounds for draw(), in local coordinates; excludes antialiasing.
	ofRectangle getTextBounds(const std::string &text, const Style &style, float x, float y) const;
	float drawFormatted(const std::string &text, float x, float y);
	// x is the column origin; y is the first line baseline. Returns line-box bounds.
	ofRectangle drawColumn(const std::string &text, const Style &style, float x, float y, float width,
						   ofAlignHorz align = OF_ALIGN_HORZ_LEFT, bool debug = false);
	// x is the column origin; y is the first line baseline. Returns line-box bounds.
	ofRectangle drawFormattedColumn(const std::string &text, float x, float y, float width, ofAlignHorz align = OF_ALIGN_HORZ_LEFT,
									bool debug = false);

  private:
	struct Item {
		std::string text;
		Style style;
		float advance = 0;
	};
	struct Line {
		std::vector<Item> items;
		float width = 0, height = 0, maxSize = 0;
	};

	std::unordered_map<std::string, std::unique_ptr<ofxSlugFont>> fonts;
	std::unordered_map<std::string, Style> styles;
	std::string defaultStyle;
	float lineHeight = 1.f;
	bool debug = false;

	Style defaultValue() const;
	float measure(const std::string &text, const Style &style) const;
	void parseNode(const pugi::xml_node &node, const Style &inherited, std::vector<Item> &out) const;
	std::vector<Item> formatted(const std::string &text) const;
	std::vector<Item> plain(const std::string &text, const Style &style) const;
	std::vector<Line> layout(const std::vector<Item> &items, float width, int tabWidth) const;
	ofRectangle inkBounds(const std::vector<Item> &items, float x, float y, int tabWidth) const;
	ofRectangle render(const std::vector<Item> &items, float x, float y, float width, ofAlignHorz align, bool debugDraw, int tabWidth,
					   bool baseline = false) const;
};
