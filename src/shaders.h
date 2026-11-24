// Slug shader code Copyright 2017 by Eric Lengyel.
// Adapted from slughorn example, MIT license; see SLUGHORN-LICENSE.
#pragma once
static const char *k_VertSrc = R"(
#version 330 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec4 a_color;
layout(location = 2) in vec4 a_emCoord;
layout(location = 3) in vec4 a_bandTransform; // bandScaleX/Y, bandOffsetX/Y
layout(location = 4) in vec4 a_shapeData; // bandTexX/Y, bandMaxX/Y
layout(location = 5) in float a_effectId; // unused in this demo, carried through
layout(location = 6) in float a_strokeRadius; // > 0: a centerline-stroke shape

uniform mat4 u_mvp;
uniform float u_time;

out vec2 v_emCoord;
out vec2 v_uv;
out vec4 v_color;

flat out vec4 v_bandTransform;
flat out vec4 v_shapeData;
flat out float v_strokeRadius;

void main() {
	v_emCoord = a_emCoord.xy;
	v_uv = a_emCoord.zw;
	v_color = a_color;
	v_bandTransform = a_bandTransform;
	v_shapeData = a_shapeData;
	v_strokeRadius = a_strokeRadius;

	gl_Position = u_mvp * vec4(a_position, 1.0);
}
)";

// Fragment shader: Slug core only. No debug modes, no effect branches -- just
// the coverage solve and a single standard fill. Add effects / debug modes
// from osgSlug's fragment shader when needed.
static const char *k_FragSrc = R"(
#version 330 core

in vec2 v_emCoord;
in vec2 v_uv;
in vec4 v_color;

flat in vec4 v_bandTransform; // bandScaleX/Y, bandOffsetX/Y
flat in vec4 v_shapeData; // bandTexX/Y, bandMaxX/Y
flat in float v_strokeRadius;

uniform sampler2D u_curveTexture;
uniform usampler2D u_bandTexture;
uniform bool u_aaAxis;

out vec4 fragColor;

// Hardcoded to log2(512) = 9, matching the default Atlas() constructor.
// If using Atlas(uint32_t texWidth), replace with: int(log2(atlas.getTextureWidth()))
// and pass as a uniform rather than a define.
#define TEX_WIDTH 9

// Must match slughorn::Atlas::INDIRECTION_SIZE
#define SLUG_INDIRECTION_SIZE 32

// ---------------------------------------------------------------------------
// Slug core (Lengyel 2017); identical to osgSlug's implementation.
// ---------------------------------------------------------------------------

uint slug_CalcRootCode(float y1, float y2, float y3) {
	uint i1 = floatBitsToUint(y1) >> 31u;
	uint i2 = floatBitsToUint(y2) >> 30u;
	uint i3 = floatBitsToUint(y3) >> 29u;

	uint shift = (i2 & 2u) | (i1 & ~2u);
	shift = (i3 & 4u) | (shift & ~4u);

	return ((0x2E74u >> shift) & 0x0101u);
}

// Cancellation-free roots of a*t^2 - 2*b*t + c - see slughorn::render::Sampler::_solveRoots().
vec2 slug_SolveRoots(float a, float b, float c) {
	float disc = b * b - a * c;
	float d = sqrt(max(disc, 0.0));
	float q = b >= 0.0 ? b + d : b - d;
	float tFar = q / a;
	float tNear = disc > 0.0 ? c / q : tFar;
	return b >= 0.0 ? vec2(tNear, tFar) : vec2(tFar, tNear);
}

// tangents: the curve's direction at each root, a * t - b (xy for the first root, zw for the
// second), unnormalized; only the edge ramp reads them.
vec2 slug_SolveHorizPoly(vec4 p12, vec2 p3, out vec4 tangents) {
	vec2 a = p12.xy - p12.zw * 2.0 + p3;
	vec2 b = p12.xy - p12.zw;
	float rb = 0.5 / b.y;
	vec2 t = slug_SolveRoots(a.y, b.y, p12.y);
	float t1 = t.x;
	float t2 = t.y;
	if(abs(a.y) < 1.0 / 65536.0) { t1 = p12.y * rb; t2 = t1; }
	tangents = vec4(a * t1 - b, a * t2 - b);
	return vec2(
		(a.x * t1 - b.x * 2.0) * t1 + p12.x,
		(a.x * t2 - b.x * 2.0) * t2 + p12.x
	);
}

vec2 slug_SolveVertPoly(vec4 p12, vec2 p3, out vec4 tangents) {
	vec2 a = p12.xy - p12.zw * 2.0 + p3;
	vec2 b = p12.xy - p12.zw;
	float rb = 0.5 / b.x;
	vec2 t = slug_SolveRoots(a.x, b.x, p12.x);
	float t1 = t.x;
	float t2 = t.y;
	if(abs(a.x) < 1.0 / 65536.0) { t1 = p12.x * rb; t2 = t1; }
	tangents = vec4(a * t1 - b, a * t2 - b);
	return vec2(
		(a.y * t1 - b.y * 2.0) * t1 + p12.y,
		(a.y * t2 - b.y * 2.0) * t2 + p12.y
	);
}

ivec2 slug_CalcBandLoc(ivec2 glyphLoc, uint offset) {
	ivec2 bandLoc = ivec2(glyphLoc.x + int(offset), glyphLoc.y);
	bandLoc.y += bandLoc.x >> TEX_WIDTH;
	bandLoc.x &= (1 << TEX_WIDTH) - 1;
	return bandLoc;
}

// Re-derive a curve's second texel location the same way slug_CalcBandLoc()
// re-derives a band fetch's row, instead of flat-adding to curveLoc.x. Packing today always
// aligns a curve's 2 texels to start at an even X so they never straddle a row, making this a
// no-op -- but endpoint-shared curve packing (planned) removes that alignment, so a curve's
// second texel CAN legitimately land at the start of the next row.
ivec2 slug_CalcCurveLoc(ivec2 curveLoc, int offset) {
	ivec2 loc = ivec2(curveLoc.x + offset, curveLoc.y);
	loc.y += loc.x >> TEX_WIDTH;
	loc.x &= (1 << TEX_WIDTH) - 1;
	return loc;
}

float slug_CalcCoverage(float xcov, float ycov, float xwgt, float ywgt) {
	float coverage = max(
		abs(xcov * xwgt + ycov * ywgt) / max(xwgt + ywgt, 1.0 / 65536.0),
		min(abs(xcov), abs(ycov))
	);
	return clamp(coverage, 0.0, 1.0);
}

// Edge ramp (u_aaAxis false; slughorn::render::AARamp::Edge): each crossing's distance is
// converted to its distance across the edge in screen pixels, divided by the edge's ramp width
// |cos| + |sin|, so edges ramp over |cos| + |sin| pixels at any angle, rotation or perspective.
// The axis ramp (Lengyel's original) ramps one pixel along each ray instead, sharper on angled
// edges. The scale is floored so a crossing contributes at most 2 pixels behind the sample, which
// the early exit below relies on.
const float SLUG_AA_SCALE_FLOOR = 0.25;

// tangent: em-space curve direction at the crossing; n = (tangent.y, -tangent.x) is its normal.
// The edge's offset along the ray is n.axis * r em, and one screen pixel step changes it by
// n . emJacobian[0] and n . emJacobian[1]. across holds the loop-invariant part of that (see
// slug_RampAcross()), already multiplied by the ray's pixels per em.
float slug_RampScale(vec2 tangent, vec4 across, bool horizontal) {
	float sum = abs(dot(tangent, across.xy)) + abs(dot(tangent, across.zw));
	return sum > 0.0 ? max(SLUG_AA_SCALE_FLOOR, abs(horizontal ? tangent.y : tangent.x) / sum) : 1.0;
}

// n . v = tangent.y * v.x - tangent.x * v.y, for both Jacobian columns.
vec4 slug_RampAcross(mat2 emJacobian, float pixelsPerEm) {
	return vec4(-emJacobian[0].y, emJacobian[0].x, -emJacobian[1].y, emJacobian[1].x) * pixelsPerEm;
}

// emJacobian: the em-space change for a one pixel step along screen x (column 0) and screen y
// (column 1), mat2(dFdx(em), dFdy(em)). The axis ramp reads only 1 / (|dx| + |dy|), which is
// 1 / fwidth(em).
float slug_Render(vec2 renderCoord, mat2 emJacobian, vec4 bandTransform, ivec2 glyphLoc, ivec2 bandMax) {
	vec2 pixelsPerEm = 1.0 / (abs(emJacobian[0]) + abs(emJacobian[1]));
	float reach = u_aaAxis ? 0.5 : 0.5 / SLUG_AA_SCALE_FLOOR;
	vec4 hAcross = slug_RampAcross(emJacobian, pixelsPerEm.x);
	vec4 vAcross = slug_RampAcross(emJacobian, pixelsPerEm.y);

	// O(1) band index via indirection tables (2 fetches per axis).
	int qY = clamp(int(renderCoord.y * bandTransform.y + bandTransform.w), 0, SLUG_INDIRECTION_SIZE - 1);
	int qX = clamp(int(renderCoord.x * bandTransform.x + bandTransform.z), 0, SLUG_INDIRECTION_SIZE - 1);
	int bandY = int(texelFetch(u_bandTexture, ivec2(glyphLoc.x + qY, glyphLoc.y), 0).r);
	int bandX = int(texelFetch(u_bandTexture, ivec2(glyphLoc.x + SLUG_INDIRECTION_SIZE + qX, glyphLoc.y), 0).r);

	// Horizontal bands -- headers at glyphLoc + 2*IS + bandY
	float xcov = 0.0, xwgt = 0.0;
	uvec2 hbandData = texelFetch(u_bandTexture, ivec2(glyphLoc.x + 2 * SLUG_INDIRECTION_SIZE + bandY, glyphLoc.y), 0).xy;

	for(int ci = 0; ci < int(hbandData.x); ci++) {
		// Re-derive this fetch's row every iteration (instead of computing hbandLoc once
		// and flat-adding ci to its X) so a band's curve-index list can span more than one
		// texture row -- removes the old "count > texWidth" hard limit on shape complexity.
		// Costs up to ~10us on complex scenes vs. the single-fetch version; worth it.
		ivec2 hbandLoc = slug_CalcBandLoc(glyphLoc, hbandData.y + uint(ci));
		ivec2 curveLoc = ivec2(texelFetch(u_bandTexture, hbandLoc, 0).xy);
		vec4 p12 = texelFetch(u_curveTexture, curveLoc, 0) - vec4(renderCoord, renderCoord);

		// Same re-derive-per-fetch pattern as slug_CalcBandLoc above, now applied to
		// a curve's own second texel -- needed once packTextures() (slughorn.cpp) started
		// sharing texels between connected curves (Lengyel's Slug convention: a chain of N
		// connected curves costs N+1 texels instead of 2N), which packs densely with no
		// per-curve row alignment. Adds a handful of ALU ops per curve iteration; negligible
		// next to the texture fetch and sqrt-based polynomial solve already in this loop.
		// Shipped as the default (no opt-in flag) -- lossless, and verified bit-identical
		// against the pre-sharing renderer at real CJK-atlas scale.
		vec2 p3 = texelFetch(u_curveTexture, slug_CalcCurveLoc(curveLoc, 1), 0).xy - renderCoord;

		if(max(max(p12.x, p12.z), p3.x) * pixelsPerEm.x < -reach) break;

		uint code = slug_CalcRootCode(p12.y, p12.w, p3.y);
		if(code != 0u) {
			vec4 tangents;
			vec2 r = slug_SolveHorizPoly(p12, p3, tangents) * pixelsPerEm.x;
			// The weights keep the unscaled distance, so a ray nearly parallel to an edge stays down-weighted.
			vec2 rc = u_aaAxis ? r : r * vec2(slug_RampScale(tangents.xy, hAcross, true), slug_RampScale(tangents.zw, hAcross, true));
			if((code & 1u) != 0u) { xcov += clamp(rc.x + 0.5, 0.0, 1.0); xwgt = max(xwgt, clamp(1.0 - abs(r.x) * 2.0, 0.0, 1.0)); }
			if(code > 1u) { xcov -= clamp(rc.y + 0.5, 0.0, 1.0); xwgt = max(xwgt, clamp(1.0 - abs(r.y) * 2.0, 0.0, 1.0)); }
		}
	}

	// Vertical bands -- headers at glyphLoc + 2*IS + numHBands + bandX
	float ycov = 0.0, ywgt = 0.0;
	uvec2 vbandData = texelFetch(u_bandTexture, ivec2(glyphLoc.x + 2 * SLUG_INDIRECTION_SIZE + bandMax.y + 1 + bandX, glyphLoc.y), 0).xy;

	for(int ci = 0; ci < int(vbandData.x); ci++) {
		// See the matching comment in the horizontal-band loop above.
		ivec2 vbandLoc = slug_CalcBandLoc(glyphLoc, vbandData.y + uint(ci));
		ivec2 curveLoc = ivec2(texelFetch(u_bandTexture, vbandLoc, 0).xy);
		vec4 p12 = texelFetch(u_curveTexture, curveLoc, 0) - vec4(renderCoord, renderCoord);

		// See the matching endpoint-sharing tradeoff comment in the horizontal-band loop above.
		vec2 p3 = texelFetch(u_curveTexture, slug_CalcCurveLoc(curveLoc, 1), 0).xy - renderCoord;

		if(max(max(p12.y, p12.w), p3.y) * pixelsPerEm.y < -reach) break;

		uint code = slug_CalcRootCode(p12.x, p12.z, p3.x);
		if(code != 0u) {
			vec4 tangents;
			vec2 r = slug_SolveVertPoly(p12, p3, tangents) * pixelsPerEm.y;
			vec2 rc = u_aaAxis ? r : r * vec2(slug_RampScale(tangents.xy, vAcross, false), slug_RampScale(tangents.zw, vAcross, false));
			if((code & 1u) != 0u) { ycov -= clamp(rc.x + 0.5, 0.0, 1.0); ywgt = max(ywgt, clamp(1.0 - abs(r.x) * 2.0, 0.0, 1.0)); }
			if(code > 1u) { ycov += clamp(rc.y + 0.5, 0.0, 1.0); ywgt = max(ywgt, clamp(1.0 - abs(r.y) * 2.0, 0.0, 1.0)); }
		}
	}

	return slug_CalcCoverage(xcov, ycov, xwgt, ywgt);
}

// ---------------------------------------------------------------------------
// Centerline strokes; mirrors slughorn::render::distanceToCurve() and Sampler::_renderStroke().
// ---------------------------------------------------------------------------

// Distance from the origin to B(t) = p1 + 2t * a + t^2 * c.
float slug_DistanceAt(vec2 p1, vec2 a, vec2 c, float t) {
	return length(p1 + t * (2.0 * a + t * c));
}

// Distance from the origin to the quadratic p1, p2, p3 (points already relative to the pixel).
// The nearest point solves B(t) . B'(t) = 0:
//
//     (c . c) t^3 + 3 (a . c) t^2 + (2 (a . a) + p1 . c) t + p1 . a = 0
//
// with a = p2 - p1 and c = p1 - 2 * p2 + p3. Its real roots clamped to [0, 1], plus both
// endpoints (round caps), are the candidates.
float slug_DistanceToCurve(vec2 p1, vec2 p2, vec2 p3) {
	vec2 a = p2 - p1;
	vec2 c = p1 - 2.0 * p2 + p3;
	float cc = dot(c, c);
	float aa = dot(a, a);

	// Degenerate: a segment p1 -> p1 + 2a (or a single point when a is also zero).
	if(cc <= 1e-12 * aa || cc == 0.0) {
		vec2 d = 2.0 * a;
		float len2 = dot(d, d);
		float t = len2 > 0.0 ? clamp(-dot(p1, d) / len2, 0.0, 1.0) : 0.0;

		return length(p1 + t * d);
	}

	// Monic cubic t^3 + ma t^2 + mb t + mk, then depressed with t = u - ma / 3: u^3 + p u + q = 0.
	float ma = 3.0 * dot(a, c) / cc;
	float mb = (2.0 * aa + dot(p1, c)) / cc;
	float mk = dot(p1, a) / cc;
	float p = mb - ma * ma / 3.0;
	float q = 2.0 * ma * ma * ma / 27.0 - ma * mb / 3.0 + mk;
	float shift = -ma / 3.0;
	float disc = q * q / 4.0 + p * p * p / 27.0;
	float best = min(slug_DistanceAt(p1, a, c, 0.0), slug_DistanceAt(p1, a, c, 1.0));

	if(disc > 0.0) {
		// One real root. The cube root takes the sign that adds like signs (no cancellation); the
		// other term is -p / (3 * w).
		float s = sqrt(disc);
		float h = q >= 0.0 ? -q / 2.0 - s : -q / 2.0 + s;
		float w = sign(h) * pow(abs(h), 1.0 / 3.0);
		float u = w != 0.0 ? w - p / (3.0 * w) : 0.0;

		best = min(best, slug_DistanceAt(p1, a, c, clamp(u + shift, 0.0, 1.0)));
	}

	else {
		// Three real roots (p <= 0): the trigonometric form.
		float m = 2.0 * sqrt(max(-p / 3.0, 0.0));
		float arg = m > 0.0 ? clamp(3.0 * q / (p * m), -1.0, 1.0) : 0.0;
		float theta = acos(arg) / 3.0;
		const float third = 2.0943951; // 2 * pi / 3

		for(int i = 0; i < 3; i++) {
			float u = m * cos(theta - float(i) * third);

			best = min(best, slug_DistanceAt(p1, a, c, clamp(u + shift, 0.0, 1.0)));
		}
	}

	return best;
}

// A stroke shape's curves are a centerline; a point is covered within radius em of the nearest
// one. Coverage ramps linearly over one pixel centered on that boundary, the pixel's em size taken
// as the mean of its two axes.
//
// The builder files each curve in every band within radius of it, so the pixel's h-band and its
// v-band each hold every curve that can reach it, and the shorter list is walked. Lists are sorted
// by descending max x (h-bands) / max y (v-bands), so once a curve's max is farther behind the
// pixel than the radius plus a pixel, no later curve can reach it either.
//
// The ramp reaches half a pixel past radius but the band padding is only radius, so every band of
// the walked axis within half a pixel of the point is walked; a curve seen twice cannot change
// the minimum.
float slug_RenderStroke(
	vec2 renderCoord,
	mat2 emJacobian,
	vec4 bandTransform,
	ivec2 glyphLoc,
	ivec2 bandMax,
	float radius
) {
	vec2 emsPerPixel = abs(emJacobian[0]) + abs(emJacobian[1]);
	float emPerPixel = max(0.5 * (emsPerPixel.x + emsPerPixel.y), 1e-12);

	int qY = clamp(int(renderCoord.y * bandTransform.y + bandTransform.w), 0, SLUG_INDIRECTION_SIZE - 1);
	int qX = clamp(int(renderCoord.x * bandTransform.x + bandTransform.z), 0, SLUG_INDIRECTION_SIZE - 1);
	int bandY = int(texelFetch(u_bandTexture, ivec2(glyphLoc.x + qY, glyphLoc.y), 0).r);
	int bandX = int(texelFetch(u_bandTexture, ivec2(glyphLoc.x + SLUG_INDIRECTION_SIZE + qX, glyphLoc.y), 0).r);
	int hHeaders = glyphLoc.x + 2 * SLUG_INDIRECTION_SIZE;
	int vHeaders = hHeaders + bandMax.y + 1;
	uint hcount = texelFetch(u_bandTexture, ivec2(hHeaders + bandY, glyphLoc.y), 0).x;
	uint vcount = texelFetch(u_bandTexture, ivec2(vHeaders + bandX, glyphLoc.y), 0).x;
	bool horizontal = hcount <= vcount;

	// The band axis is perpendicular to the sort axis: h-bands slice y, v-bands slice x.
	float coord = horizontal ? renderCoord.y : renderCoord.x;
	float bandScale = horizontal ? bandTransform.y : bandTransform.x;
	float bandOffset = horizontal ? bandTransform.w : bandTransform.z;
	int indir = glyphLoc.x + (horizontal ? 0 : SLUG_INDIRECTION_SIZE);
	int headers = horizontal ? hHeaders : vHeaders;
	float fringe = 0.5 * emPerPixel;
	int qFirst = clamp(int((coord - fringe) * bandScale + bandOffset), 0, SLUG_INDIRECTION_SIZE - 1);
	int qLast = clamp(int((coord + fringe) * bandScale + bandOffset), 0, SLUG_INDIRECTION_SIZE - 1);
	int first = int(texelFetch(u_bandTexture, ivec2(indir + qFirst, glyphLoc.y), 0).r);
	int last = int(texelFetch(u_bandTexture, ivec2(indir + qLast, glyphLoc.y), 0).r);

	// Curve points are taken relative to the pixel, so the cutoff is relative too.
	float cutoff = -radius - emPerPixel;
	float d = 1e30;

	for(int band = first; band <= last; band++) {
		uvec2 bandData = texelFetch(u_bandTexture, ivec2(headers + band, glyphLoc.y), 0).xy;

		for(int ci = 0; ci < int(bandData.x); ci++) {
			ivec2 bandLoc = slug_CalcBandLoc(glyphLoc, bandData.y + uint(ci));
			ivec2 curveLoc = ivec2(texelFetch(u_bandTexture, bandLoc, 0).xy);
			vec4 p12 = texelFetch(u_curveTexture, curveLoc, 0) - vec4(renderCoord, renderCoord);
			vec2 p3 = texelFetch(u_curveTexture, slug_CalcCurveLoc(curveLoc, 1), 0).xy - renderCoord;
			float maxCoord = horizontal ? max(max(p12.x, p12.z), p3.x) : max(max(p12.y, p12.w), p3.y);

			if(maxCoord < cutoff) break;

			d = min(d, slug_DistanceToCurve(p12.xy, p12.zw, p3));
		}
	}

	return clamp((radius - d) / emPerPixel + 0.5, 0.0, 1.0);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
void main() {
	ivec2 glyphLoc = ivec2(v_shapeData.xy);
	ivec2 bandMax = ivec2(v_shapeData.zw);

	// Taken here, before any branch or discard: derivatives need all four pixels of the 2x2 quad.
	mat2 emJacobian = mat2(dFdx(v_emCoord), dFdy(v_emCoord));

	// The shape's kind picks the method: a stroke radius means its curves are a centerline.
	float fill = v_strokeRadius > 0.0
		? slug_RenderStroke(v_emCoord, emJacobian, v_bandTransform, glyphLoc, bandMax, v_strokeRadius)
		: slug_Render(v_emCoord, emJacobian, v_bandTransform, glyphLoc, bandMax)
	;

	if(fill < 0.001) discard;

	fragColor = vec4(v_color.rgb, fill * v_color.a);
}
)";
