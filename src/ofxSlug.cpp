#include "ofxSlug.h"
#include "ofxSlugFont.h"
#include "pugixml.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr float baseSize = 64.f;
bool colorValue(const std::string &raw, ofColor &out) {
	if(raw.empty() || raw[0] != '#')
		return false;
	auto s = raw.substr(1);
	if(s.size() == 3 || s.size() == 4) {
		std::string t;
		for(char c : s) {
			t += c;
			t += c;
		}

		s = t;
	}

	if((s.size() != 6 && s.size() != 8) || s.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
		return false;
	auto n = std::stoul(s, nullptr, 16);
	if(s.size() == 6)
		out = ofColor((n >> 16) & 255, (n >> 8) & 255, n & 255);
	else
		out = ofColor((n >> 24) & 255, (n >> 16) & 255, (n >> 8) & 255, n & 255);
	return true;
}

std::vector<std::string> codepoints(const std::string &s) {
	std::vector<std::string> result;
	for(size_t i = 0; i < s.size();) {
		unsigned char c = s[i];
		size_t n = c < 0x80 ? 1 : (c >= 0xc2 && c <= 0xdf ? 2 : (c >= 0xe0 && c <= 0xef ? 3 : (c >= 0xf0 && c <= 0xf4 ? 4 : 1)));
		n = std::min(n, s.size() - i);
		result.push_back(s.substr(i, n));
		i += n;
	}

	return result;
}

} // namespace

ofxSlug::Style ofxSlug::defaultValue() const {
	auto it = styles.find(defaultStyle);
	if(it != styles.end())
		return it->second;
	return styles.empty() ? Style{} : styles.begin()->second;
}

const ofxSlugFont *ofxSlug::fontForGlyph(const std::string &fontID, const std::string &text) const {
	auto primary = fonts.find(fontID);
	if(primary == fonts.end()) return nullptr;
	if(primary->second->hasGlyph(text)) return primary->second.get();
	auto fallback = fonts.find(globalFallbackFont);
	if(fallback != fonts.end() && fallback->second->hasGlyph(text)) return fallback->second.get();
	return primary->second.get();
}

float ofxSlug::measure(const std::string &text, const Style &s) const {
	const auto *font = fontForGlyph(s.fontID, text);
	return font ? font->stringWidth(text) * s.fontSize / baseSize + (text.empty() ? 0 : s.spacing) : 0;
}

void ofxSlug::parseNode(const pugi::xml_node &node, const Style &inherited, std::vector<Item> &out) const {
	if(node.type() == pugi::node_pcdata || node.type() == pugi::node_cdata) {
		for(auto &c : codepoints(node.value()))
			out.push_back({c, inherited, measure(c, inherited)});
		return;
	}

	if(node.type() != pugi::node_element && node.type() != pugi::node_document)
		return;
	Style style = inherited;
	std::string name = node.name();
	if(name == "br") {
		out.push_back({"\n", style, 0});
		return;
	}

	bool para = name == "p";
	if(para && !out.empty() && out.back().text != "\n")
		out.push_back({"\n", style, 0});
	auto found = styles.find(name);
	if(found != styles.end())
		style = found->second;
	if(name == "style") {
		if(auto a = node.attribute("id")) {
			auto named = styles.find(a.value());
			if(named != styles.end())
				style = named->second;
			else
				ofLogWarning("ofxSlug") << "Unknown style: " << a.value();
		}
	}

	if(auto a = node.attribute("font"))
		style.fontID = a.value();
	if(auto a = node.attribute("size")) {
		float value = a.as_float();
		if(value > 0)
			style.fontSize = value;
	}

	if(auto a = node.attribute("color")) {
		if(!colorValue(a.value(), style.color))
			ofLogWarning("ofxSlug") << "Unsupported color: " << a.value();
	}

	if(auto a = node.attribute("spacing"))
		style.spacing = a.as_float();
	for(auto child : node.children())
		parseNode(child, style, out);
	if(para && !out.empty() && out.back().text != "\n")
		out.push_back({"\n", style, 0});
}

std::vector<ofxSlug::Item> ofxSlug::formatted(const std::string &text) const {
	std::vector<Item> result;
	pugi::xml_document doc;
	auto status = doc.load_string(text.c_str(), pugi::parse_default | pugi::parse_fragment | pugi::parse_ws_pcdata);
	if(!status) {
		ofLogError("ofxSlug") << "Invalid formatted text: " << status.description();
		return result;
	}

	parseNode(doc, defaultValue(), result);
	return result;
}

std::vector<ofxSlug::Item> ofxSlug::plain(const std::string &text, const Style &s) const {
	std::vector<Item> items;
	for(auto &c : codepoints(text))
		items.push_back({c, s, measure(c, s)});
	return items;
}

std::vector<ofxSlug::Line> ofxSlug::layout(const std::vector<Item> &items, float width, int tabWidth) const {
	if(items.empty())
		return {};
	std::vector<Line> lines(1);
	bool pending = false;
	Item space;
	auto add = [&](const Item &item) {
		auto &line = lines.back();
		line.items.push_back(item);
		line.width += item.advance;
		line.maxSize = std::max(line.maxSize, item.style.fontSize);
		line.height = std::max(line.height, item.style.fontSize * 1.2f * item.style.lineHeightMult * lineHeight);
	};
	auto next = [&](const Style &s) {
		if(lines.back().height == 0) {
			lines.back().height = s.fontSize * 1.2f * s.lineHeightMult * lineHeight;
			lines.back().maxSize = s.fontSize;
		}

		lines.emplace_back();
	};
	for(size_t i = 0; i < items.size();) {
		if(items[i].text == "\n") {
			next(items[i].style);
			pending = false;
			++i;
			continue;
		}

		if(items[i].text == "\t") {
			if(pending) {
				add(space);
				pending = false;
			}

			Item tab = items[i];
			const float step = measure(" ", tab.style) * std::max(1, tabWidth);
			if(step > 0) {
				tab.advance = (std::floor(lines.back().width / step + 0.0001f) + 1) * step - lines.back().width;
				if(!lines.back().items.empty() && lines.back().width + tab.advance > width) {
					next(tab.style);
					tab.advance = step;
				}

				add(tab);
			}

			++i;
			continue;
		}

		if(items[i].text == " " || items[i].text == "\r") {
			if(!lines.back().items.empty()) {
				space = items[i];
				pending = true;
			}

			++i;
			continue;
		}

		size_t end = i;
		float word = 0;
		while(end < items.size() && items[end].text != "\n" && items[end].text != " " && items[end].text != "\t" && items[end].text != "\r")
			word += items[end++].advance;
		if(!lines.back().items.empty() && lines.back().width + (pending ? space.advance : 0) + word > width) {
			next(items[i].style);
			pending = false;
		}

		if(pending)
			add(space);
		pending = false;
		for(; i < end; ++i) {
			if(!lines.back().items.empty() && lines.back().width + items[i].advance > width)
				next(items[i].style);
			add(items[i]);
		}
	}

	if(lines.back().height == 0) {
		auto s = items.back().style;
		lines.back().height = s.fontSize * 1.2f * s.lineHeightMult * lineHeight;
		lines.back().maxSize = s.fontSize;
	}

	return lines;
}

ofRectangle ofxSlug::inkBounds(const std::vector<Item> &items, float x, float y, int tabWidth) const {
	auto lines = layout(items, std::numeric_limits<float>::infinity(), tabWidth);
	float top = 0, left = 0, upper = 0, right = 0, bottom = 0;
	bool hasInk = false;
	for(const auto &line : lines) {
		float pen = x;
		for(const auto &item : line.items) {
			const auto *font = fontForGlyph(item.style.fontID, item.text);
			if(font && item.text != " " && item.text != "\t") {
				auto b = font->getStringBoundingBox(item.text, 0, 0);
				if(b.width > 0 && b.height > 0) {
					float scale = item.style.fontSize / baseSize;
					float l = pen + b.x * scale, t = y + top + line.maxSize + b.y * scale;
					float r = l + b.width * scale, bot = t + b.height * scale;
					if(!hasInk) {
						left = l;
						upper = t;
						right = r;
						bottom = bot;
						hasInk = true;
					} else {
						left = std::min(left, l);
						upper = std::min(upper, t);
						right = std::max(right, r);
						bottom = std::max(bottom, bot);
					}
				}
			}

			pen += item.advance;
		}

		top += line.height;
	}

	return hasInk ? ofRectangle(left, upper, right - left, bottom - upper) : ofRectangle(x, y, 0, 0);
}

ofRectangle ofxSlug::render(const std::vector<Item> &items, float x, float y, float width, ofAlignHorz align, bool debugDraw, int tabWidth,
							bool baseline) const {
	return renderLines(layout(items, width, tabWidth), x, y, width, align, debugDraw, baseline);
}

ofRectangle ofxSlug::renderLines(const std::vector<Line> &lines, float x, float y, float width, ofAlignHorz align, bool debugDraw,
								 bool baseline) const {
	if(lines.empty())
		return ofRectangle(x, y, 0, 0);
	if(baseline)
		y -= lines.front().maxSize;
	float top = 0, minX = x, maxX = x;
	ofPushStyle();
	const ofxSlugFont *batchFont = nullptr;
	std::vector<ofxSlugFont::PositionedGlyph> batch;
	auto flush = [&]() {
		if(batchFont && !batch.empty())
			batchFont->drawGlyphs(batch);
		batch.clear();
	};
	for(const auto &line : lines) {
		float offset = 0;
		if(std::isfinite(width)) {
			if(align == OF_ALIGN_HORZ_CENTER)
				offset = (width - line.width) * 0.5f;
			else if(align == OF_ALIGN_HORZ_RIGHT)
				offset = width - line.width;
		}

		float pen = x + offset;
		if(&line == &lines.front()) {
			minX = pen;
			maxX = pen + line.width;
		} else {
			minX = std::min(minX, pen);
			maxX = std::max(maxX, pen + line.width);
		}

		for(const auto &item : line.items) {
			const auto *font = fontForGlyph(item.style.fontID, item.text);
			if(font && item.text != " " && item.text != "\t") {
				if(batchFont && batchFont != font)
					flush();
				batchFont = font;
				batch.push_back({item.text, pen, y + top + line.maxSize, item.style.fontSize, ofFloatColor(item.style.color)});
			}

			pen += item.advance;
		}

		top += line.height;
	}

	flush();
	ofRectangle box(minX, y, maxX - minX, top);
	if(debugDraw || debug) {
		// These rectangles show layout advances and line boxes, not glyph ink bounds.
		float lineTop = 0;
		ofFill();
		for(const auto &line : lines) {
			float offset = 0;
			if(std::isfinite(width)) {
				if(align == OF_ALIGN_HORZ_CENTER)
					offset = (width - line.width) * 0.5f;
				else if(align == OF_ALIGN_HORZ_RIGHT)
					offset = width - line.width;
			}

			float pen = x + offset, wordStart = pen;
			bool inWord = false;
			for(const auto &item : line.items) {
				if(item.text == " " || item.text == "\t") {
					if(inWord) {
						ofSetColor(255, 80, 80, 45);
						ofDrawRectangle(wordStart, y + lineTop, pen - wordStart, line.height);
						inWord = false;
					}

					ofSetColor(0, 220, 255, 45);
					ofDrawRectangle(pen, y + lineTop, item.advance, line.height);
				} else if(!inWord) {
					wordStart = pen;
					inWord = true;
				}

				pen += item.advance;
			}

			if(inWord) {
				ofSetColor(255, 80, 80, 45);
				ofDrawRectangle(wordStart, y + lineTop, pen - wordStart, line.height);
			}

			ofNoFill();
			ofSetColor(255, 255, 255, 95);
			ofDrawRectangle(x + offset, y + lineTop, line.width, line.height);
			ofSetColor(255, 180, 0, 180);
			ofDrawLine(x + offset, y + lineTop + line.maxSize, x + offset + line.width, y + lineTop + line.maxSize);
			ofFill();
			lineTop += line.height;
		}

		ofNoFill();
		ofSetColor(255, 0, 255, 220);
		ofDrawRectangle(box);
	}

	ofPopStyle();
	return box;
}
void ofxSlug::assignWordIndices(std::vector<Item> &items) const {
	size_t nextWord = 0;
	size_t currentWord = noWord;
	for(auto &item : items) {
		if(item.text == " " || item.text == "\t" || item.text == "\r" || item.text == "\n") {
			currentWord = noWord;
			item.wordIndex = noWord;
		} else {
			if(currentWord == noWord)
				currentWord = nextWord++;
			item.wordIndex = currentWord;
		}
	}
}

std::vector<ofxSlug::Item> ofxSlug::parseStyledText(const std::string &text) const {
	auto items = formatted(text);
	assignWordIndices(items);
	return items;
}

std::vector<ofxSlug::Item> ofxSlug::parseText(const std::string &text, const Style &style) const {
	auto items = plain(text, style);
	assignWordIndices(items);
	return items;
}

ofxSlug::Layout ofxSlug::layoutLines(const std::vector<Item> &items, float width, ofAlignHorz align, size_t maxLines) const {
	Layout result;
	result.width = width;
	result.alignment = align;
	if(width <= 0)
		return result;
	std::vector<Item> measured;
	for(const auto &item : items) {
		for(const auto &character : codepoints(item.text)) {
			float advance = character == "\n" ? 0 : measure(character, item.style);
			measured.push_back({character, item.style, advance});
		}
	}
	assignWordIndices(measured);
	result.lines = layout(measured, width, tabWidth);
	if(maxLines > 0 && result.lines.size() > maxLines) {
		result.truncated = true;
		result.lines.resize(maxLines);
	}
	return result;
}

ofRectangle ofxSlug::drawLines(const Layout &layout, float x, float y, bool debug) const {
	return renderLines(layout.lines, x, y, layout.width, layout.alignment, debug, true);
}

ofxSlug::ofxSlug() {
}

ofxSlug::~ofxSlug() = default;
void ofxSlug::setup(bool debug) {
	this->debug = debug;
}

void ofxSlug::clear() {
	fonts.clear();
	styles.clear();
	defaultStyle.clear();
	globalFallbackFont.clear();
}

bool ofxSlug::addFont(const std::string &id, const std::string &file, const std::string &glyphs) {
	auto f = std::make_unique<ofxSlugFont>();
	if(!f->load(file, baseSize, glyphs))
		return false;
	fonts[id] = std::move(f);
	return true;
}

bool ofxSlug::isFontLoaded(const std::string &id) const {
	return fonts.count(id) > 0;
}

std::vector<std::string> ofxSlug::getFontIDs() const {
	std::vector<std::string> ids;
	for(const auto &e : fonts)
		ids.push_back(e.first);
	return ids;
}

bool ofxSlug::addStyle(const std::string &id, const Style &style) {
	if(!isFontLoaded(style.fontID) || style.fontSize <= 0) {
		ofLogError("ofxSlug") << "Invalid style or unloaded font: " << id;
		return false;
	}

	styles[id] = style;
	if(defaultStyle.empty())
		defaultStyle = id;
	return true;
}

bool ofxSlug::removeStyle(const std::string &id) {
	return styles.erase(id) != 0;
}

bool ofxSlug::styleExists(const std::string &id) const {
	return styles.count(id) > 0;
}

ofxSlug::Style ofxSlug::getStyle(const std::string &id, bool *exists) const {
	auto it = styles.find(id);
	if(exists)
		*exists = it != styles.end();
	return it == styles.end() ? Style{} : it->second;
}

std::unordered_map<std::string, ofxSlug::Style> &ofxSlug::getStyles() {
	return styles;
}

void ofxSlug::setDefaultStyle(const std::string &id) {
	if(!styleExists(id)) {
		ofLogWarning("ofxSlug") << "Unknown default style: " << id;
		return;
	}

	defaultStyle = id;
}

void ofxSlug::setLineHeightMult(float m) {
	if(m > 0)
		lineHeight = m;
}

void ofxSlug::setGlobalFallbackFont(const std::string &fontID) {
	if(!fontID.empty() && !isFontLoaded(fontID)) {
		ofLogWarning("ofxSlug") << "Unknown fallback font: " << fontID;
		return;
	}
	globalFallbackFont = fontID;
}

std::string ofxSlug::getGlobalFallbackFont() const {
	return globalFallbackFont;
}

float ofxSlug::draw(const std::string &text, const Style &s, float x, float y) {
	auto items = plain(text, s);
	auto box = render(items, x, y - s.fontSize, std::numeric_limits<float>::infinity(), OF_ALIGN_HORZ_LEFT, false, tabWidth);
	return box.width;
}

float ofxSlug::drawFormatted(const std::string &text, float x, float y) {
	auto items = formatted(text);
	auto box = render(items, x, y - defaultValue().fontSize, std::numeric_limits<float>::infinity(), OF_ALIGN_HORZ_LEFT, false, tabWidth);
	return box.width;
}

ofRectangle ofxSlug::drawColumn(const std::string &text, const Style &s, float x, float y, float width, ofAlignHorz align, bool debug) {
	if(width <= 0)
		return {x, y, 0, 0};
	return render(plain(text, s), x, y, width, align, debug, tabWidth, true);
}

ofRectangle ofxSlug::drawFormattedColumn(const std::string &text, float x, float y, float width, ofAlignHorz align, bool debug) {
	if(width <= 0)
		return {x, y, 0, 0};
	return render(formatted(text), x, y, width, align, debug, tabWidth, true);
}

ofRectangle ofxSlug::getTextBounds(const std::string &text, const Style &style, float x, float y) const {
	if(text.empty())
		return {x, y, 0, 0};
	auto result = inkBounds(plain(text, style), x, y - style.fontSize, tabWidth);
	if(result.width == 0 && result.height == 0)
		return {x, y, 0, 0};
	return result;
}
