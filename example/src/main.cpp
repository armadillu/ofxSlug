#include "ofMain.h"
#include "ofxSlug.h"
#include "ofxTimeMeasurements.h"
#include <algorithm>

class Example : public ofBaseApp {
	ofxSlug fonts;
	bool debug = false;
	float columnVariationSpeed = 1.f;
	ofAlignHorz alignment() const {
		int index = std::min(int(ofMap(ofGetMouseX(), 0, ofGetWidth(), 0, 3, true)), 2);
		return index == 0 ? OF_ALIGN_HORZ_LEFT : index == 1 ? OF_ALIGN_HORZ_CENTER : OF_ALIGN_HORZ_RIGHT;
	}
	float width(float speed, float phase = 0) const {
		return 300 + (0.5f + 0.5f * std::sin(columnVariationSpeed * ofGetElapsedTimef() * speed + phase)) * 200;
	}
	void insertion(float x, float y, float w = 100) {
		ofSetColor((ofGetFrameNum() * 20) % 255, 200);
		ofDrawCircle(x, y, 1.5);
		ofSetColor(255, 64);
		ofDrawLine(x - 10, y, x + w, y);
		ofSetColor(255);
	}
	void id(int n, float x, float y) {
		ofDrawBitmapStringHighlight(ofToString(n), x - 15, y, ofColor::red * 0.5, ofColor::white);
	}
	void guides(float x, float y, float w, const ofRectangle &r, int alpha = 16) {
		ofPushStyle();
		ofSetColor(255, 33);
		ofDrawLine(x, y - 15, x, y + r.height);
		ofDrawLine(x + w, y - 15, x + w, y + r.height);
		ofFill();
		ofSetColor(255, alpha);
		ofDrawRectangle(r);
		ofPopStyle();
	}

  public:
	void setup() {
		ofBackground(22);
		ofEnableAlphaBlending();
		TIME_SAMPLE_ENABLE();
		TIME_SAMPLE_SET_AVERAGE_RATE(0.1);
		TIME_SAMPLE_SET_DRAW_LOCATION(TIME_SAMPLE_DRAW_LOC_BOTTOM_RIGHT);
		TIME_SAMPLE_SET_PRECISION(4);
		fonts.setup();
		std::string glyphs;
		for(unsigned c = 32; c <= 255; ++c) {
			if(c < 128)
				glyphs += char(c);
			else {
				glyphs += char(0xc0 | (c >> 6));
				glyphs += char(0x80 | (c & 63));
			}
		}
		glyphs += "バナナのようなサル😀💩🐙🐵";
		fonts.addFont("Arial", "font-unicode.ttf", glyphs);
		fonts.addFont("veraMono", "font-mono.ttf", glyphs);
		fonts.addFont("emoji", "NotoEmoji-Regular.ttf", "😀💩🐙🐵");
		fonts.setGlobalFallbackFont("emoji");
		fonts.addStyle("style1", ofxSlug::Style("Arial", 33.f, ofColor::yellow));
		fonts.addStyle("style2", ofxSlug::Style("Arial", 33.f, ofColor::turquoise));
		fonts.addStyle("style3", ofxSlug::Style("veraMono", 55.f, ofColor::lightCyan));
		fonts.setDefaultStyle("style1");
	}
	void draw() {
		if(!fonts.isFontLoaded("Arial") || !fonts.isFontLoaded("veraMono")) {
			ofSetColor(255);
			ofDrawBitmapString("Place font-unicode.ttf and font-mono.ttf in bin/data.", 30, 50);
			return;
		}
		ofDisableAntiAliasing();
		ofPushMatrix();
		auto window = dynamic_cast<ofAppGLFWWindow *>(ofGetWindowPtr());
		float scale = window ? window->getPixelScreenCoordScale() : 1.f;
		ofScale(scale);
		TSGL_START("d");
		float x = 40, y = 40;
		ofRectangle box;
		float col = 0, w = 0, xx = 0;
		std::string text, styled;
		ofxSlug::Style s("Arial", 22.f, ofColor::white);
		TS_START("0 testDraw()");
			id(0, x, y);
			insertion(x, y);
			fonts.draw("testDraw(): method with one-line string. バナナのようなサル", s, x, y);
			insertion(x, y + 30);
			fonts.draw("testDraw(): methods with\na multiline string (not supported).", s, x, y + 30);
		TS_STOP("0 testDraw()");
		y += 100;

		TS_START("1 testDrawBatch()");
			id(1, x, y);
			xx = x;
			for(int i = 0; i < 10; ++i) {
				std::string text = "t" + ofToString(i) + ",";
				xx += fonts.draw(text, s, xx, y);
			}
			insertion(x, y);
		TS_STOP("1 testDrawBatch()");
		y += 45;

		box.set(0, 0, 0, 0);
		TS_START("2 testDrawColumn()");
			id(2, x, y);
			s.fontSize = 45.f;
			s.spacing = -0.35f;
			text = "testDrawColumn(): methods with a long string and possibly line breaks with '\\n'.\n\nAlso, adding some fancy unicode "
				   "バナナのようなサル. And back to normal...";
			col = width(0.39f, 4);
			insertion(x, y);
			box = fonts.drawColumn(text, s, x, y, col, alignment(), debug);
			guides(x, box.y, col, box, 13);
		TS_STOP("2 testDrawColumn()");
		y = box.getBottom() + 20;

		box.set(0, 0, 0, 0);
		TS_START("3 testDrawColumnSmall()");
			id(3, x, y);
			s = ofxSlug::Style("veraMono", 8.5f, ofColor::white);
			text = "testDrawColumnSmall(): El català és una llengua de transició entre les llengües iberoromàniques i les llengües "
				   "gal·loromàniques, encara que antigament fos molt pròxima a l'occità, amb qui comparteix origen i grup: l'occitanoromànic.";
			col = width(0.24f, -1);
			insertion(x, y);
			box = fonts.drawColumn(text, s, x, y, col, alignment(), debug);
			guides(x, box.y, col, box, 22);
		TS_STOP("3 testDrawColumnSmall()");
		y = box.getBottom() + 20;

		TS_START("4 testDrawTabs()");
			id(4, x, y);
			text = "//testDrawTabs():\nclass ofApp : public ofBaseApp{\n\tpublic:\n\t\tvoid setup(); //some emoji too 😀💩🐙🐵\n\t\tvoid "
				   "update();\n}";
			s = ofxSlug::Style("veraMono", 16.f, ofColor::white);
			insertion(x, y);
			box = fonts.drawColumn(text, s, x, y, ofGetWidth(), OF_ALIGN_HORZ_LEFT, debug);
			guides(x, box.y, ofGetWidth(), box, 20);
		TS_STOP("4 testDrawTabs()");

		x = 600;
		y = 40;
		TS_START("5 testDrawFormatted()");
			id(5, x, y);
			styled = "<style id='style1'>testDrawFormatted(): style1.</style><style id='style2'>And style2.</style><style1>And back to "
					 "style1</style1>";
			w = fonts.drawFormatted(styled, x, y);
			insertion(x, y, w);
		TS_STOP("5 testDrawFormatted()");
		y += 60;

		box.set(0, 0, 0, 0);
		TS_START("6 testDrawFormattedColumn()");
			id(6, x, y);
			styled = "<style id='style1'>testDrawFormattedColumn(): This is style1 style.</style><style id='style2'>And this is Style2, "
					 "adjusted to the column width and with a line break \"[br/]\" <br/>in the middle of the line.</style>";
			styled += " <style id='style3'>And this is stlye3, which has a bigger font size.</style>";
			col = width(0.33f);
			insertion(x, y);
			box = fonts.drawFormattedColumn(styled, x, y, col, alignment(), debug);
			guides(x, box.y, col, box);
		TS_STOP("6 testDrawFormattedColumn()");
		y = box.getBottom() + 60;

		box.set(0, 0, 0, 0);
		TS_START("7 testDiyPlainLayout()");
			id(7, x, y);
			text = "testDiyPlainLayout(): L'italiano è una tra le ventiquattro lingue ufficiali dell'Unione europea ed è lingua ufficiale "
				   "dell'Italia, di San Marino, della Svizzera, della Città del Vaticano e del Sovrano Militare Ordine di Malta.";
			col = width(0.27f);
			insertion(x, y);
			auto plainItems = fonts.parseText(text, fonts.getStyle("style1"));
			auto plainLayout = fonts.layoutLines(plainItems, col, alignment(), 3);
			// The lines and their word indices can be inspected or edited here.
			box = fonts.drawLines(plainLayout, x, y, debug);
			guides(x, box.y, col, box, 32);
		TS_STOP("7 testDiyPlainLayout()");
		y = box.getBottom() + 60;

		TS_START("8 testDiyFormattedLayout()");
			id(8, x, y);
			styled =
				"<style id='style1'>testDiyFormattedLayout(): hello</style><style id='style2'>banana</style><style id='style3'>monkey</style>";
			col = width(0.45f, 2.3f);
			insertion(x, y);
			auto formattedItems = fonts.parseStyledText(styled);
			auto formattedLayout = fonts.layoutLines(formattedItems, col, alignment(), 2);
			// formattedLayout.truncated reports whether the two-line limit omitted text.
			box = fonts.drawLines(formattedLayout, x, y, debug);
			guides(x, box.y, col, box);
		TS_STOP("8 testDiyFormattedLayout()");
		TSGL_STOP("d");
		ofPopMatrix();
		ofSetColor(220);
		ofDrawBitmapString("Mouse: alignment | D: guides | T: timing menu | M: timings on/off", 35, ofGetHeight() - 15);
	}
	void keyPressed(int key) {
		if(key == 'd' || key == 'D')
			debug = !debug;
		if(key == 'm' || key == 'M')
			TIME_SAMPLE_SET_ENABLED(!TIME_SAMPLE_GET_ENABLED());
	}
	void exit() {
		fonts.clear();
	}
};
int main() {
	ofGLFWWindowSettings settings;
	settings.setGLVersion(3, 3);
	settings.setSize(1200, 1100);
	ofCreateWindow(settings);
	ofRunApp(std::make_shared<Example>());
	ofRunMainLoop();
}
