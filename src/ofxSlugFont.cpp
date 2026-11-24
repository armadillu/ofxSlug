#include "ofxSlugFont.h"
#include "slughorn/freetype.hpp"
#include "shaders.h"
#include <algorithm>
#include <cstddef>
#include <unordered_map>
#include <limits>
#include <vector>

namespace {
std::vector<uint32_t> decode(const std::string &s) {
	std::vector<uint32_t> out;
	for(size_t i = 0; i < s.size();) {
		unsigned char c = s[i++];
		uint32_t v = 0;
		int n = 0;
		if(c < 0x80)
			v = c;
		else if(c >= 0xc2 && c <= 0xdf) {
			v = c & 31;
			n = 1;
		} else if(c >= 0xe0 && c <= 0xef) {
			v = c & 15;
			n = 2;
		} else if(c >= 0xf0 && c <= 0xf4) {
			v = c & 7;
			n = 3;
		} else {
			out.push_back(0xfffd);
			continue;
		}

		size_t start = i;
		bool ok = i + n <= s.size();
		for(int j = 0; ok && j < n; ++j) {
			unsigned char d = s[i++];
			if((d & 0xc0) != 0x80)
				ok = false;
			else
				v = (v << 6) | (d & 63);
		}

		if(!ok || (n == 2 && v < 0x800) || (n == 3 && v < 0x10000) || v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff)) {
			i = start;
			v = 0xfffd;
		}

		out.push_back(v);
	}
	return out;
}

// Bounds the actual quadratic curves, including interior extrema, in OF's y-down coordinates.
ofRectangle outlineBounds(const slughorn::Atlas::Shape &shape) {
	double minX = std::numeric_limits<double>::infinity();
	double minY = minX, maxX = -minX, maxY = -minX;
	auto axis = [](double a, double b, double c, double &lo, double &hi) {
		lo = std::min(lo, std::min(a, c));
		hi = std::max(hi, std::max(a, c));
		double denominator = a - 2 * b + c;
		if(denominator != 0) {
			double t = (a - b) / denominator;
			if(t > 0 && t < 1) {
				double u = 1 - t;
				double value = u * u * a + 2 * u * t * b + t * t * c;
				lo = std::min(lo, value);
				hi = std::max(hi, value);
			}
		}
	};
	for(const auto &curve : shape.curves) {
		axis(double(curve.x1), double(curve.x2), double(curve.x3), minX, maxX);
		axis(double(curve.y1), double(curve.y2), double(curve.y3), minY, maxY);
	}

	if(shape.curves.empty())
		return {};
	return ofRectangle(float(minX), float(-maxY), float(maxX - minX), float(maxY - minY));
}

struct Vertex {
	float pos[3], color[4], em[4], band[4], data[4], effect, stroke;
};
GLuint compile(GLenum type, const char *source) {
	GLuint s = glCreateShader(type);
	glShaderSource(s, 1, &source, nullptr);
	glCompileShader(s);
	GLint ok = 0;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if(!ok) {
		char log[4096] = {};
		glGetShaderInfoLog(s, sizeof(log), nullptr, log);
		ofLogError("ofxSlug") << (type == GL_VERTEX_SHADER ? "Vertex" : "Fragment") << " shader compilation failed: " << log;
		glDeleteShader(s);
		return 0;
	}
	return s;
}

GLuint texture(const slughorn::Atlas::TextureData &d, bool band) {
	GLuint t;
	glGenTextures(1, &t);
	glBindTexture(GL_TEXTURE_2D, t);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, band ? GL_RG16UI : GL_RGBA32F, d.width, d.height, 0, band ? GL_RG_INTEGER : GL_RGBA,
				 band ? GL_UNSIGNED_SHORT : GL_FLOAT, d.bytes.data());
	return t;
}

} // namespace

ofxSlugFont::ofxSlugFont() = default;

ofxSlugFont::~ofxSlugFont() {
	clear();
}

void ofxSlugFont::clear() {
	if(loaded)
		ofLogNotice("ofxSlug") << "Releasing font and GPU resources";
	if(vbo)
		glDeleteBuffers(1, &vbo);
	if(vao)
		glDeleteVertexArrays(1, &vao);
	if(curve)
		glDeleteTextures(1, &curve);
	if(band)
		glDeleteTextures(1, &band);
	if(program)
		glDeleteProgram(program);
	atlas = slughorn::Atlas{};
	inkBounds.clear();
	size = 0;
	program = curve = band = vao = vbo = 0;
	loaded = false;
}

bool ofxSlugFont::isLoaded() const {
	return loaded;
}

bool ofxSlugFont::load(const std::string &path, float size, const std::string &glyphs) {
	clear();
	if(size <= 0 || !ofIsGLProgrammableRenderer()) {
		ofLogError("ofxSlug") << "Requires a positive size and desktop OpenGL 3.3";
		return false;
	}

	std::vector<uint32_t> chars = decode(glyphs);
	if(chars.empty())
		for(uint32_t c = 32; c <= 255; ++c)
			chars.push_back(c);
	std::sort(chars.begin(), chars.end());
	chars.erase(std::unique(chars.begin(), chars.end()), chars.end());
	const auto resolvedPath = ofToDataPath(path, true);
	ofLogNotice("ofxSlug") << "Loading font " << resolvedPath << " at size " << size << ", requesting " << chars.size() << " glyphs";
	size_t loadedGlyphs = 0;
	try {
		loadedGlyphs = slughorn::freetype::loadFontGlyphs(resolvedPath, chars, atlas);
		if(!loadedGlyphs) {
			ofLogError("ofxSlug") << "Unable to load font or any requested glyphs: " << resolvedPath;
			return false;
		}

	} catch(const std::exception &error) {
		ofLogError("ofxSlug") << "Unable to load font: " << error.what();
		return false;
	}

	ofLogNotice("ofxSlug") << "Loaded " << loadedGlyphs << " of " << chars.size() << " requested glyphs";
	if(loadedGlyphs < chars.size())
		ofLogWarning("ofxSlug") << (chars.size() - loadedGlyphs) << " requested glyphs were not loaded";
	atlas.build();
	if(atlas.getCurveTextureData().bytes.empty() || atlas.getBandTextureData().bytes.empty()) {
		ofLogError("ofxSlug") << "No drawable outlines in requested glyphs";
		return false;
	}

	for(auto c : chars) {
		auto shape = atlas.getShape(c);
		if(shape && !shape->curves.empty())
			inkBounds.emplace(c, outlineBounds(*shape));
	}

	const auto &curves = atlas.getCurveTextureData();
	const auto &bands = atlas.getBandTextureData();
	ofLogNotice("ofxSlug") << "Built atlas: curve texture " << curves.width << "x" << curves.height << " (" << curves.bytes.size()
						   << " bytes), band texture " << bands.width << "x" << bands.height << " (" << bands.bytes.size() << " bytes)";
	GLuint vs = compile(GL_VERTEX_SHADER, k_VertSrc), fs = compile(GL_FRAGMENT_SHADER, k_FragSrc);
	if(!vs || !fs) {
		if(vs)
			glDeleteShader(vs);
		if(fs)
			glDeleteShader(fs);
		return false;
	}

	program = glCreateProgram();
	glAttachShader(program, vs);
	glAttachShader(program, fs);
	glLinkProgram(program);
	glDeleteShader(vs);
	glDeleteShader(fs);
	GLint ok = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &ok);
	if(!ok) {
		char log[4096] = {};
		glGetProgramInfoLog(program, sizeof(log), nullptr, log);
		ofLogError("ofxSlug") << "Shader program link failed: " << log;
		clear();
		return false;
	}

	GLint oldActive, oldTex, oldVao, oldBuffer;
	glGetIntegerv(GL_ACTIVE_TEXTURE, &oldActive);
	glActiveTexture(GL_TEXTURE0);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTex);
	curve = texture(atlas.getCurveTextureData(), false);
	band = texture(atlas.getBandTextureData(), true);
	glBindTexture(GL_TEXTURE_2D, oldTex);
	glActiveTexture(oldActive);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &oldBuffer);
	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	const int counts[] = {3, 4, 4, 4, 4, 1, 1};
	const size_t offsets[] = {offsetof(Vertex, pos),  offsetof(Vertex, color),	offsetof(Vertex, em),	 offsetof(Vertex, band),
							  offsetof(Vertex, data), offsetof(Vertex, effect), offsetof(Vertex, stroke)};
	for(int i = 0; i < 7; ++i) {
		glEnableVertexAttribArray(i);
		glVertexAttribPointer(i, counts[i], GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(offsets[i]));
	}

	glBindVertexArray(oldVao);
	glBindBuffer(GL_ARRAY_BUFFER, oldBuffer);
	this->size = size;
	loaded = true;
	ofLogNotice("ofxSlug") << "GPU resources initialized; font ready";
	return true;
}

float ofxSlugFont::stringWidth(const std::string &text) const {
	if(!isLoaded())
		return 0;
	float x = 0, width = 0;
	for(auto c : decode(text)) {
		if(c == '\n') {
			width = std::max(width, x);
			x = 0;
			continue;
		}
		auto s = atlas.getShape(c);
		x += (s ? float(s->advance) : 0.5f) * size;
	}
	return std::max(width, x);
}

ofRectangle ofxSlugFont::getStringBoundingBox(const std::string &text, float x, float y) const {
	if(!isLoaded())
		return {x, y, 0, 0};
	float pen = 0, line = 0, left = 0, top = 0, right = 0, bottom = 0;
	bool hasInk = false;
	for(auto c : decode(text)) {
		if(c == '\n') {
			pen = 0;
			line += size * 1.2f;
			continue;
		}

		auto shape = atlas.getShape(c);
		if(!shape) {
			pen += size * 0.5f;
			continue;
		}

		auto it = inkBounds.find(c);
		if(it != inkBounds.end()) {
			const auto &b = it->second;
			float l = x + pen + b.x * size, t = y + line + b.y * size;
			float r = l + b.width * size, bot = t + b.height * size;
			if(!hasInk) {
				left = l;
				top = t;
				right = r;
				bottom = bot;
				hasInk = true;
			}

			else {
				left = std::min(left, l);
				top = std::min(top, t);
				right = std::max(right, r);
				bottom = std::max(bottom, bot);
			}
		}

		pen += float(shape->advance) * size;
	}

	return hasInk ? ofRectangle(left, top, right - left, bottom - top) : ofRectangle(x, y, 0, 0);
}

void ofxSlugFont::drawGlyphs(const std::vector<PositionedGlyph> &glyphs) const {
	if(!isLoaded() || glyphs.empty())
		return;
	std::vector<Vertex> vertices;
	vertices.reserve(glyphs.size() * 6);
	const float e = 0.01f;
	for(const auto &glyph : glyphs) {
		auto chars = decode(glyph.text);
		if(chars.empty())
			continue;
		auto s = atlas.getShape(chars[0]);
		if(!s || s->width <= 0 || s->height <= 0)
			continue;
		float l = float(s->bearing.x) - e, r = float(s->bearing.x + s->width) + e, b = float(s->bearing.y - s->height) - e,
			  t = float(s->bearing.y) + e;
		const float px[] = {l, r, r, l}, py[] = {b, b, t, t};
		const int order[] = {0, 1, 2, 0, 2, 3};
		for(int i : order) {
			Vertex v = {};
			v.pos[0] = glyph.x + px[i] * glyph.size;
			v.pos[1] = glyph.baselineY - py[i] * glyph.size;
			v.color[0] = glyph.color.r;
			v.color[1] = glyph.color.g;
			v.color[2] = glyph.color.b;
			v.color[3] = glyph.color.a;
			v.em[0] = px[i];
			v.em[1] = py[i];
			v.band[0] = float(s->bandScaleX);
			v.band[1] = float(s->bandScaleY);
			v.band[2] = float(s->bandOffsetX);
			v.band[3] = float(s->bandOffsetY);
			v.data[0] = float(s->bandTexX);
			v.data[1] = float(s->bandTexY);
			v.data[2] = float(s->bandMaxX);
			v.data[3] = float(s->bandMaxY);
			vertices.push_back(v);
		}
	}

	if(vertices.empty())
		return;
	GLint oldProgram, oldVao, oldBuffer, oldActive, tex0, tex1;
	glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &oldBuffer);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &oldActive);
	glActiveTexture(GL_TEXTURE0);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex0);
	glBindTexture(GL_TEXTURE_2D, curve);
	glActiveTexture(GL_TEXTURE1);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex1);
	glBindTexture(GL_TEXTURE_2D, band);
	glUseProgram(program);
	glUniform1i(glGetUniformLocation(program, "u_curveTexture"), 0);
	glUniform1i(glGetUniformLocation(program, "u_bandTexture"), 1);
	glUniform1i(glGetUniformLocation(program, "u_aaAxis"), 1);
	auto renderer = ofGetCurrentRenderer();
	glm::mat4 mvp = renderer->getCurrentMatrix(OF_MATRIX_PROJECTION) * renderer->getCurrentMatrix(OF_MATRIX_MODELVIEW);
	glUniformMatrix4fv(glGetUniformLocation(program, "u_mvp"), 1, GL_FALSE, &mvp[0][0]);
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STREAM_DRAW);
	glDrawArrays(GL_TRIANGLES, 0, vertices.size());
	glBindVertexArray(oldVao);
	glBindBuffer(GL_ARRAY_BUFFER, oldBuffer);
	glUseProgram(oldProgram);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, tex1);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, tex0);
	glActiveTexture(oldActive);
}
