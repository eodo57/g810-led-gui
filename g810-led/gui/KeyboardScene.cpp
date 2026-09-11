/*
  This file is part of g810-led.

  g810-led is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, version 3 of the License.

  g810-led is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with g810-led.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "KeyboardScene.h"

#include "Styling.h"

#include <epoxy/gl.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <iostream>

namespace {

// Everything is in millimetres, so the model has the dimensions of the
// keyboard on the desk.
const float unitMm = 19.05f;          // one key pitch
const float quarterMm = unitMm / 4.0f;
const float gapMm = 3.6f;             // the G512 floats its caps
const float capHeightMm = 9.5f;
const float capTaperMm = 1.1f;        // how much the top is inset
const float capRadiusMm = 1.6f;
const float dishMm = 0.6f;
// A lamp is a lens set into the plate rather than a cap standing on it.
// It is drawn at something over the size of the real thing: at the size
// of the real thing it is three pixels deep on a board seen whole, and a
// mark that cannot be seen cannot be painted. It stays well inside its
// cell, and much wider than this and the plate you click to reach it —
// which is the cell, not the lens — would be gone.
const float lampHeightMm = 1.2f;
const float lampWidthMm = 6.0f;
const float lampDepthMm = 4.6f;
const float plateBezelMm = 7.0f;
const float plateThickMm = 9.0f;
const float haloMm = 3.2f;            // how far the light spills
const int cornerSegments = 4;
const int maxCaps = 192;

// Row profile: the function row sits lower and the home row higher, as
// on a real board, so the silhouette is right from the side.
float rowHeight(int row, int rows) {
	if (rows <= 3)
		return capHeightMm;
	const float profile[6] = {0.0f, 0.0f, 0.6f, 1.1f, 0.7f, 0.0f};
	const int index = std::max(0, std::min(5, row - (rows - 6)));
	return capHeightMm + profile[index];
}

// The tallest a cap ever stands: the home row, the highest step of the
// profile above.
const float tallestCapMm = capHeightMm + 1.1f;

// The default view: turned a little and seen from well above. The angle
// is what makes it read as an object rather than a diagram, but every
// degree of it is paid for in the top faces — the part that carries the
// legend, the colour and the marks — so it is only as low as it has to
// be to see the sides of the caps and the thickness of the plate.
const float viewYaw = -0.24f;
const float viewPitch = 1.15f;
const float fovDegrees = 26.0f;
// Clear space left around the board, as a fraction of the viewport.
const float viewMargin = 0.012f;

void identity(float *m) {
	std::memset(m, 0, sizeof(float) * 16);
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

// out = a * b, column major.
void multiply(const float *a, const float *b, float *out) {
	float result[16];
	for (int column = 0; column < 4; ++column) {
		for (int row = 0; row < 4; ++row) {
			float sum = 0;
			for (int k = 0; k < 4; ++k)
				sum += a[k * 4 + row] * b[column * 4 + k];
			result[column * 4 + row] = sum;
		}
	}
	std::memcpy(out, result, sizeof(result));
}

void perspective(float fovyDegrees, float aspect, float nearPlane,
                 float farPlane, float *m) {
	const float f = 1.0f / std::tan(fovyDegrees * 3.14159265f / 360.0f);
	std::memset(m, 0, sizeof(float) * 16);
	m[0] = f / aspect;
	m[5] = f;
	m[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
	m[11] = -1.0f;
	m[14] = (2.0f * farPlane * nearPlane) / (nearPlane - farPlane);
}

void normalise(float *v) {
	const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	if (length > 1e-6f) {
		v[0] /= length;
		v[1] /= length;
		v[2] /= length;
	}
}

void cross(const float *a, const float *b, float *out) {
	out[0] = a[1] * b[2] - a[2] * b[1];
	out[1] = a[2] * b[0] - a[0] * b[2];
	out[2] = a[0] * b[1] - a[1] * b[0];
}

void lookAt(const float *eye, const float *centre, const float *up, float *m) {
	float forward[3] = {centre[0] - eye[0], centre[1] - eye[1],
	                    centre[2] - eye[2]};
	normalise(forward);
	float side[3];
	cross(forward, up, side);
	normalise(side);
	float trueUp[3];
	cross(side, forward, trueUp);
	identity(m);
	m[0] = side[0];    m[4] = side[1];    m[8]  = side[2];
	m[1] = trueUp[0];  m[5] = trueUp[1];  m[9]  = trueUp[2];
	m[2] = -forward[0]; m[6] = -forward[1]; m[10] = -forward[2];
	m[12] = -(side[0] * eye[0] + side[1] * eye[1] + side[2] * eye[2]);
	m[13] = -(trueUp[0] * eye[0] + trueUp[1] * eye[1] + trueUp[2] * eye[2]);
	m[14] = forward[0] * eye[0] + forward[1] * eye[1] + forward[2] * eye[2];
}

// Where a cap actually stands on the plate.
//
// A key fills its cell. An indicator does not: it is a lamp set into the
// plate — a short slot a few millimetres long, like the two beside the
// badge on a G512 — so it is sized in millimetres rather than in cells,
// the way the real thing is, and only clamped if the cell is too small
// to hold it.
void capFootprint(const keylayout::Cap &spec, float &x0, float &x1,
                  float &z0, float &z1) {
	x0 = spec.column * quarterMm + gapMm * 0.5f;
	x1 = (spec.column + spec.width) * quarterMm - gapMm * 0.5f;
	z0 = spec.row * unitMm + gapMm * 0.5f;
	z1 = (spec.row + spec.height) * unitMm - gapMm * 0.5f;
	if (!spec.indicator)
		return;
	const float midX = (x0 + x1) * 0.5f, midZ = (z0 + z1) * 0.5f;
	const float halfWidth = std::min(lampWidthMm, x1 - x0) * 0.5f;
	const float halfDepth = std::min(lampDepthMm, z1 - z0) * 0.5f;
	x0 = midX - halfWidth; x1 = midX + halfWidth;
	z0 = midZ - halfDepth; z1 = midZ + halfDepth;
}

// The strip of bare plate a lamp's name is printed on: as wide as the
// lamp's cell, and as deep as the clear plate between the top of that
// cell and the lens itself. The geometry that draws it and the atlas
// that letters it both need it and must agree on it, or the word comes
// out stretched.
void lampLabelStrip(const keylayout::Cap &spec, float &x0, float &x1,
                    float &z0, float &z1) {
	x0 = spec.column * quarterMm;
	x1 = (spec.column + spec.width) * quarterMm;
	float lensX0, lensX1, lensZ0, lensZ1;
	capFootprint(spec, lensX0, lensX1, lensZ0, lensZ1);
	z0 = spec.row * unitMm + 0.9f;
	z1 = std::max(z0 + 1.0f, lensZ0 - 0.6f);
}

// The face of a cap that carries the colour, the legend and the marks:
// the footprint drawn in by the taper, at the height its row stands at.
// The geometry and the framing both need it and must agree on it.
void capTop(const keylayout::Cap &spec, int rows, float &x0, float &x1,
            float &z0, float &z1, float &top) {
	capFootprint(spec, x0, x1, z0, z1);
	const float taper = spec.indicator ? 0.35f : capTaperMm;
	x0 += taper;
	x1 -= taper;
	z0 += taper;
	z1 -= taper;
	top = spec.indicator ? lampHeightMm : rowHeight(spec.row, rows);
}

// The same legend broken over two lines, or nothing if the word has no
// break in it. A keyboard prints Prt Sc and Pg Up on two lines because
// they are two words; it does not print Pau se, and neither does this.
std::string brokenLabel(const std::string &label) {
	for (size_t i = 1; i < label.size(); ++i) {
		if (label[i] == ' ')
			return label.substr(0, i) + "\n" + label.substr(i + 1);
		if (std::isupper((unsigned char)label[i]) &&
		    std::islower((unsigned char)label[i - 1]))
			return label.substr(0, i) + "\n" + label.substr(i);
	}
	return std::string();
}

// A ring of points around a rounded rectangle, counter-clockwise seen
// from above. Both rings of a cap use the same count so the skirt
// between them connects one to one.
void roundedRing(float x0, float z0, float x1, float z1, float radius,
                 std::vector<float> &outX, std::vector<float> &outZ) {
	radius = std::min(radius, std::min(x1 - x0, z1 - z0) * 0.45f);
	const float centres[4][2] = {
		{x1 - radius, z1 - radius},   // near right
		{x0 + radius, z1 - radius},   // near left
		{x0 + radius, z0 + radius},   // far left
		{x1 - radius, z0 + radius},   // far right
	};
	const float startAngle[4] = {0.0f, 1.57079633f, 3.14159265f, 4.71238898f};
	outX.clear();
	outZ.clear();
	for (int corner = 0; corner < 4; ++corner) {
		for (int step = 0; step <= cornerSegments; ++step) {
			const float angle = startAngle[corner] +
				1.57079633f * (float)step / (float)cornerSegments;
			outX.push_back(centres[corner][0] + radius * std::cos(angle));
			outZ.push_back(centres[corner][1] + radius * std::sin(angle));
		}
	}
}

const char *capVertexSource =
"#version 330 core\n"
"layout(location = 0) in vec3 aPos;\n"
"layout(location = 1) in vec3 aNormal;\n"
"layout(location = 2) in vec2 aUV;\n"
"layout(location = 3) in float aCap;\n"
"uniform mat4 uViewProj;\n"
"out vec3 vNormal;\n"
"out vec3 vWorld;\n"
"out vec2 vUV;\n"
"flat out int vCap;\n"
"void main() {\n"
"    vNormal = aNormal;\n"
"    vWorld = aPos;\n"
"    vUV = aUV;\n"
"    vCap = int(aCap + 0.5);\n"
"    gl_Position = uViewProj * vec4(aPos, 1.0);\n"
"}\n";

const char *capFragmentSource =
"#version 330 core\n"
"in vec3 vNormal;\n"
"in vec3 vWorld;\n"
"in vec2 vUV;\n"
"flat in int vCap;\n"
"uniform vec4 uCapColor[192];\n"   // the LED colour; a: the model has this key
"uniform vec4 uCapFlags[192];\n"   // selected, pending, hovered, legend ink
// Where in the atlas this cap's lettering is and how much of the cap it
// covers: span u, span v, cell x, cell y. A lamp has no legend on its
// lens and a name on the plate instead, so it carries the depth of that
// strip in the first slot and, in the second, minus the width the strip
// has to reach before the name is worth printing — negative, so that the
// legend on the lens stays switched off.
"uniform vec4 uCapLegend[192];\n"
"uniform sampler2D uAtlas;\n"
"uniform float uAtlasColumns;\n"
"uniform float uAtlasRows;\n"
"uniform vec3 uLightDir;\n"
"uniform vec3 uEye;\n"
"uniform int uPicking;\n"
"uniform int uPass;\n"              // 0 caps, 1 spill on the plate, 2 silkscreen
"out vec4 fragColor;\n"
// An sRGB channel and the linear light behind it, and back again — the
// same pair styling::decode and styling::encode are, to the same
// coefficients. A mark judged here and a legend judged there have to
// agree about what can be read against what, and "close enough" is not
// agreement: this used to raise the channel to the 2.2 and the two
// halves of the same rule landed on different greys for the same key.
"vec3 decode(vec3 c) {\n"
"    c = clamp(c, 0.0, 1.0);\n"
"    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)),\n"
"        step(vec3(0.03928), c));\n"
"}\n"
"vec3 encode(vec3 c) {\n"
"    c = clamp(c, 0.0, 1.0);\n"
"    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055,\n"
"        step(vec3(0.0031308), c));\n"
"}\n"
// Relative luminance, in the terms the contrast rule in Styling.h is
// written in.
"float luminance(vec3 c) {\n"
"    return dot(decode(c), vec3(0.2126, 0.7152, 0.0722));\n"
"}\n"
// What colour to draw a mark in on a cap of this colour.
//
// This is styling::markColor, in GLSL, statement for statement. It is
// the one rule both views of the board mark state by: the flat board
// calls the C++ one from KeyCap::on_draw, the model runs this per
// fragment, and the two have to give the same answer or the same board
// says two different things depending on which view you are in. Change
// one and change the other.
//
// The mark keeps the hue the rest of the window uses for that state and
// gives up value instead, and only as much of it as it takes to be seen.
// How much that is depends on the key underneath: two hues as far apart
// as gold and red are told apart by the hue alone and are left alone,
// while a hue laid on a key painted that same hue says nothing at all
// and has to clear three to one on brightness by itself. Paint the whole
// board the exact amber the selection is drawn in and the chosen keys
// still carry a ring — a deep gold one, on a bright gold key.
//
// What is asked for is a contrast, not a colour, because the road from
// white to a dark grey runs straight through the value of the cap it has
// to be seen against: a mark moved half way would be less visible than
// one not moved at all. The move is made in linear light, so the ratio
// it lands on is the ratio that was asked for.
"vec3 markColor(vec3 hue, vec3 cap) {\n"
"    float capLum = luminance(cap), hueLum = luminance(hue);\n"
"    float have = (max(capLum, hueLum) + 0.05) / (min(capLum, hueLum) + 0.05);\n"
"    vec3 hueTint = hue / max(max(hue.r, max(hue.g, hue.b)), 0.004);\n"
"    vec3 capTint = cap / max(max(cap.r, max(cap.g, cap.b)), 0.004);\n"
"    float want = mix(3.0, 1.5, clamp(distance(hueTint, capTint) / 0.65,\n"
"        0.0, 1.0));\n"
"    if (have >= want) return hue;\n"
"    float lower = (capLum + 0.05) / want - 0.05;\n"
"    if (lower > 0.015)\n"
"        return encode(decode(hue) * min(1.0, lower / max(hueLum, 1e-4)));\n"
"    float lift = clamp(((capLum + 0.05) * want - 0.05 - hueLum) /\n"
"        max(1.0 - hueLum, 1e-3), 0.0, 1.0);\n"
"    return encode(mix(decode(hue), vec3(1.0), lift));\n"
"}\n"
"void main() {\n"
"    if (uPicking == 1) {\n"
"        int id = vCap + 1;\n"
"        fragColor = vec4(float(id & 255) / 255.0,\n"
"                         float((id >> 8) & 255) / 255.0,\n"
"                         float((id >> 16) & 255) / 255.0, 1.0);\n"
"        return;\n"
"    }\n"
"    vec3 led = uCapColor[vCap].rgb;\n"
"    float live = uCapColor[vCap].a;\n"
"    vec3 lit = led * live;\n"
// The two state colours are @kb_amber and @kb_accent from the style
// sheet: one act, one colour, whichever view of the board you are in.
"    vec3 amber = vec3(0.961, 0.760, 0.067);\n"
"    vec3 accent = vec3(0.400, 0.800, 1.000);\n"
"    if (uPass == 2) {\n"
"        // The lamps' names, silkscreened on the plate above each lens.\n"
"        // A lamp has no top to print on, and it is the one thing on the\n"
"        // board that is not a key you could name from its shape, so it\n"
"        // is named where the real board names it. Below about six\n"
"        // pixels a letter the word is dropped rather than smeared: a\n"
"        // grey blur over the plate would say less than bare plate, and\n"
"        // the pointer names the lamp at any size.\n"
"        vec2 uv = (uCapLegend[vCap].zw +\n"
"            vec2(vUV.x, vUV.y * uCapLegend[vCap].x)) /\n"
"            vec2(uAtlasColumns, uAtlasRows);\n"
"        float ink = clamp((texture(uAtlas, uv).r - 0.10) * 2.0, 0.0, 1.0);\n"
"        float acrossPx = 1.0 / max(length(vec2(dFdx(vUV.x), dFdy(vUV.x))),\n"
"            1e-5);\n"
"        float deepPx = 1.0 / max(length(vec2(dFdx(vUV.y), dFdy(vUV.y))),\n"
"            1e-5);\n"
"        ink *= smoothstep(-uCapLegend[vCap].y, -uCapLegend[vCap].y * 1.25,\n"
"            acrossPx) * smoothstep(3.0, 4.5, deepPx);\n"
"        fragColor = vec4(vec3(0.80), ink);\n"
"        return;\n"
"    }\n"
"    if (uPass == 1) {\n"
"        // The spill onto the plate. The quad stands a few millimetres\n"
"        // proud of the cap on every side, so the only part of it anyone\n"
"        // ever sees is that outer strip: the falloff is measured across\n"
"        // the strip, not across the quad, or all of it lands under the\n"
"        // keycap that is sitting on top of it.\n"
"        vec2 d = abs(vUV - 0.5) * 2.0;\n"
"        float t = clamp((1.0 - max(d.x, d.y)) / 0.34, 0.0, 1.0);\n"
"        vec3 spill = lit * 0.55;\n"
"        // A chosen key is marked on the plate around it as well as on\n"
"        // its top. A ring on the cap alone is two pixels wide on a board\n"
"        // seen whole, and being seen whole is the case that matters.\n"
"        // The state's spill is drawn at full value against the plain\n"
"        // one's 0.55, so it still stands out on a board painted the\n"
"        // state's own colour.\n"
"        if (uCapFlags[vCap].x > 0.5) spill = amber;\n"
"        else if (uCapFlags[vCap].y > 0.5) spill = accent * 0.85;\n"
"        fragColor = vec4(spill * t * t, 1.0);\n"
"        return;\n"
"    }\n"
"    vec3 N = normalize(vNormal);\n"
"    vec3 L = normalize(uLightDir);\n"
"    vec3 V = normalize(uEye - vWorld);\n"
"    vec3 H = normalize(L + V);\n"
"    float diffuse = max(dot(N, L), 0.0);\n"
"    float specular = pow(max(dot(N, H), 0.0), 34.0) * 0.30;\n"
"    // Only the top face carries a legend or a mark, and it is the only\n"
"    // part of the cap given UVs inside the unit square.\n"
"    float onTop = step(0.0, vUV.x);\n"
"    vec3 plastic = vec3(0.085, 0.085, 0.095) * (0.34 + 0.66 * diffuse);\n"
"    // The top of a lit cap is the colour that was chosen, at the value\n"
"    // it was chosen at, to the number the flat board would show. This is\n"
"    // the one thing on screen that has to be trusted, so nothing is\n"
"    // allowed to darken it and nothing is allowed to brighten it either:\n"
"    // a model that can show a value above the one that will be sent is\n"
"    // telling the same class of untruth as one that shows less. The form\n"
"    // comes from the sides, from the shadow gaps between the caps, and\n"
"    // from the light on the plastic of the keys that are not lit.\n"
"    float level = max(lit.r, max(lit.g, lit.b));\n"
"    float faceLit = onTop * smoothstep(0.03, 0.13, level);\n"
"    vec3 color = mix(plastic, lit, faceLit);\n"
"    color += vec3(specular) * (1.0 - faceLit);\n"
"    // A rim of the key's colour where the cap meets the plate, which is\n"
"    // where the light actually escapes on this keyboard.\n"
"    float low = clamp(1.0 - vWorld.y / 4.5, 0.0, 1.0);\n"
"    color += lit * (1.0 - onTop) * low * 0.38;\n"
"    // What the cap reads as before any mark is put on it, which is what\n"
"    // a mark has to be told apart from.\n"
"    vec3 face = color;\n"
"    // Chosen, held and pointed-at, as a band around the top face, worked\n"
"    // out before the legend because what the marks leave of the cap is\n"
"    // what there is to print a legend in.\n"
"    //\n"
"    // Two states at once share the one band, half each, rather than\n"
"    // taking a band apiece: what the marks eat is the colour and the\n"
"    // letter the key is there for. Being pointed at is the one that\n"
"    // gives way when both of the others are on: the pointer is already\n"
"    // there, saying so.\n"
"    float slots = 0.0, atSelected = 0.0, atPending = 0.0;\n"
"    if (uCapFlags[vCap].x > 0.5) { slots += 1.0; atSelected = slots; }\n"
"    if (uCapFlags[vCap].y > 0.5) { slots += 1.0; atPending = slots; }\n"
"    if (uCapFlags[vCap].z > 1.5) slots += 1.0;\n"
"    else if (uCapFlags[vCap].z > 0.5 && slots < 2.0) slots += 1.0;\n"
"    // How many pixels of this cap there are to spend, along and across.\n"
"    float capPxU = 1.0 / max(fwidth(vUV.x), 1e-5);\n"
"    float capPxV = 1.0 / max(fwidth(vUV.y), 1e-5);\n"
"    // The band is set in pixels rather than as a share of the cap: a\n"
"    // share of a cap is a hairline on a board seen whole, and that is\n"
"    // the size the marks have to be read at. It grows a little with the\n"
"    // cap so that it is not a hairline zoomed right in either, and it is\n"
"    // held to a sixth of the cap besides. In the narrow window a key is\n"
"    // a dozen pixels across and ten deep; a band that took as much of\n"
"    // that as it liked would leave neither the colour nor the letter,\n"
"    // which are the two things the key is drawn for.\n"
"    float bandPx = clamp(0.07 * min(capPxU, capPxV) + 0.7, 1.3, 7.0);\n"
"    // Two states get half again the width of one, split between them.\n"
"    if (slots > 1.5) bandPx *= 0.7;\n"
"    float ringU = min(bandPx / capPxU, 0.17);\n"
"    float ringV = min(bandPx / capPxV, 0.17);\n"
"    float edge = min(min(vUV.x, 1.0 - vUV.x) / ringU,\n"
"                     min(vUV.y, 1.0 - vUV.y) / ringV);\n"
"    // The legend, in whichever of black or white can be read against the\n"
"    // colour underneath it — the rule the flat board and every swatch in\n"
"    // the window already use. A legend in the key's own hue cannot be\n"
"    // read at any size, and is unreadable outright on a pale key.\n"
"    vec2 patch = (vUV - 0.5) / max(uCapLegend[vCap].xy, vec2(0.001)) + 0.5;\n"
"    vec2 uv = (uCapLegend[vCap].zw + clamp(patch, 0.0, 1.0)) /\n"
"              vec2(uAtlasColumns, uAtlasRows);\n"
"    float mask = texture(uAtlas, uv).r;\n"
"    // A legend a few pixels tall is fetched from far down the mip chain,\n"
"    // where each stroke has been averaged with the space around it and\n"
"    // prints as a smear. Widening what is left of it is what a screen\n"
"    // does to small type, for the same reason.\n"
"    mask = clamp((mask - 0.12) * 1.9, 0.0, 1.0);\n"
"    vec2 in01 = step(vec2(0.0), patch) * step(patch, vec2(1.0));\n"
"    mask *= in01.x * in01.y * onTop;\n"
"    // How much of the cap the marks have left, in pixels, which is the\n"
"    // only unit in which the question means anything. Under about three\n"
"    // of them the legend is let go: what survives there is not a letter,\n"
"    // it is a smudge over the colour, and a clean banded key says more\n"
"    // than a dirty one.\n"
"    float roomPixels = min((1.0 - 2.0 * slots * ringU) * capPxU,\n"
"                           (1.0 - 2.0 * slots * ringV) * capPxV);\n"
"    mask *= smoothstep(2.0, 4.0, roomPixels);\n"
"    color = mix(color, vec3(uCapFlags[vCap].w), mask);\n"
"    if (onTop > 0.5 && edge < slots) {\n"
"        float which = floor(edge) + 1.0;\n"
"        vec3 band = markColor(vec3(1.0), face);\n"
"        if (which == atSelected) band = markColor(amber, face);\n"
"        else if (which == atPending) band = markColor(accent, face);\n"
"        // Only the inner edge of the band needs softening: the outer one\n"
"        // is the edge of the cap.\n"
"        color = mix(color, band, clamp((slots - edge) * max(bandPx, 1.0),\n"
"            0.0, 1.0));\n"
"    }\n"
"    // A chosen key is picked out on its sides too, so a row of them\n"
"    // reads as chosen from across the room.\n"
"    if (uCapFlags[vCap].x > 0.5)\n"
"        color = mix(color, amber * 0.75, (1.0 - onTop) * 0.72);\n"
"    if (live < 0.5)\n"
"        color *= 0.45;\n"   // a key this model cannot address
"    fragColor = vec4(color, 1.0);\n"
"}\n";

const char *plateVertexSource =
"#version 330 core\n"
"layout(location = 0) in vec3 aPos;\n"
"layout(location = 1) in vec3 aNormal;\n"
"uniform mat4 uViewProj;\n"
"out vec3 vNormal;\n"
"out vec3 vWorld;\n"
"void main() {\n"
"    vNormal = aNormal;\n"
"    vWorld = aPos;\n"
"    gl_Position = uViewProj * vec4(aPos, 1.0);\n"
"}\n";

// Brushed aluminium, generated rather than sampled: the streaks run
// along the board the way the real top plate is finished, and it stays
// sharp however far you zoom in.
const char *plateFragmentSource =
"#version 330 core\n"
"in vec3 vNormal;\n"
"in vec3 vWorld;\n"
"uniform vec3 uLightDir;\n"
"uniform vec3 uEye;\n"
"out vec4 fragColor;\n"
"float hash(vec2 p) {\n"
"    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);\n"
"}\n"
"void main() {\n"
"    vec3 N = normalize(vNormal);\n"
"    vec3 L = normalize(uLightDir);\n"
"    vec3 V = normalize(uEye - vWorld);\n"
"    vec3 H = normalize(L + V);\n"
"    float grain = 0.0;\n"
"    if (N.y > 0.5) {\n"
"        float line = floor(vWorld.z * 14.0);\n"
"        grain = hash(vec2(line, floor(vWorld.x * 0.35))) * 0.030 +\n"
"                hash(vec2(line, floor(vWorld.x * 1.7))) * 0.015;\n"
"    }\n"
// Light enough to be seen. A plate the colour of the panel behind it is
// not a keyboard, it is a set of keycaps floating in the dark, and the
// object the whole window is about has to have an edge.
"    vec3 albedo = vec3(0.400, 0.415, 0.450) + grain;\n"
"    float diffuse = max(dot(N, L), 0.0);\n"
"    float specular = pow(max(dot(N, H), 0.0), 18.0) * (0.18 + grain);\n"
"    fragColor = vec4(albedo * (0.30 + 0.70 * diffuse) + vec3(specular), 1.0);\n"
"}\n";

unsigned compile(unsigned type, const char *source, const char *what) {
	const unsigned shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);
	int ok = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[1024] = {0};
		glGetShaderInfoLog(shader, sizeof(log) - 1, NULL, log);
		std::cerr << "g810-led-gui: " << what << " shader: " << log << std::endl;
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

unsigned link(const char *vertexSource, const char *fragmentSource,
              const char *what) {
	const unsigned vertex = compile(GL_VERTEX_SHADER, vertexSource, what);
	const unsigned fragment = compile(GL_FRAGMENT_SHADER, fragmentSource, what);
	if (!vertex || !fragment)
		return 0;
	const unsigned program = glCreateProgram();
	glAttachShader(program, vertex);
	glAttachShader(program, fragment);
	glLinkProgram(program);
	glDeleteShader(vertex);
	glDeleteShader(fragment);
	int ok = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[1024] = {0};
		glGetProgramInfoLog(program, sizeof(log) - 1, NULL, log);
		std::cerr << "g810-led-gui: " << what << " link: " << log << std::endl;
		glDeleteProgram(program);
		return 0;
	}
	return program;
}

}  // namespace

KeyboardScene::KeyboardScene() {
	set_required_version(3, 3);
	set_has_depth_buffer(true);
	set_hexpand(true);
	set_vexpand(true);
	set_can_focus(true);
	resetView();
}

KeyboardScene::~KeyboardScene() {
}

bool KeyboardScene::usable() const {
	return m_usable;
}

KeyboardScene::type_signal_void KeyboardScene::signal_ready() {
	return m_signal_ready;
}

void KeyboardScene::setCaps(const std::vector<keylayout::Cap> &caps) {
	m_caps = caps;
	m_entries.clear();
	m_index.clear();
	m_boardWidth = 0;
	m_boardDepth = 0;
	int rows = 0;
	for (size_t i = 0; i < m_caps.size() && m_entries.size() < maxCaps; ++i) {
		CapEntry entry;
		entry.spec = m_caps[i];
		rows = std::max(rows, entry.spec.row + entry.spec.height);
		m_boardWidth = std::max(m_boardWidth,
			(entry.spec.column + entry.spec.width) * quarterMm);
		m_index[entry.spec.key] = (int)m_entries.size();
		m_entries.push_back(entry);
	}
	m_boardDepth = rows * unitMm;
	buildHull();
	m_geometryDirty = true;
	resetView();
	queue_render();
}

// The outline, as the set of points that can lie on it, in millimetres
// about the point the camera aims at. It is pure arithmetic on the
// layout, so it is ready before there is a context to draw with.
void KeyboardScene::buildHull() {
	m_hull.clear();
	const float centreX = m_boardWidth * 0.5f;
	const float centreZ = m_boardDepth * 0.5f;
	const int rows = (int)(m_boardDepth / unitMm + 0.5f);
	const float halfWidth = centreX + plateBezelMm;
	const float halfDepth = centreZ + plateBezelMm;
	// The plate, top and bottom: the four corners of each, which is what
	// the rounding is cut from.
	const float heights[2] = {0.0f, -plateThickMm};
	for (int level = 0; level < 2; ++level) {
		for (int corner = 0; corner < 4; ++corner) {
			m_hull.push_back((corner & 1) ? halfWidth : -halfWidth);
			m_hull.push_back(heights[level]);
			m_hull.push_back((corner & 2) ? halfDepth : -halfDepth);
		}
	}
	for (size_t i = 0; i < m_entries.size(); ++i) {
		float x0, x1, z0, z1, top;
		capTop(m_entries[i].spec, rows, x0, x1, z0, z1, top);
		for (int corner = 0; corner < 4; ++corner) {
			m_hull.push_back(((corner & 1) ? x1 : x0) - centreX);
			m_hull.push_back(top);
			m_hull.push_back(((corner & 2) ? z1 : z0) - centreZ);
		}
	}
	// The framing has to be worked out again, for a different board.
	m_fitWidth = m_fitHeight = m_homeWidth = m_homeHeight = 0;
}

void KeyboardScene::setKeyColor(LedKeyboard::Key key, const Gdk::RGBA &color) {
	std::map<LedKeyboard::Key, int>::const_iterator found = m_index.find(key);
	if (found == m_index.end())
		return;
	CapEntry &entry = m_entries[found->second];
	entry.color[0] = (float)color.get_red();
	entry.color[1] = (float)color.get_green();
	entry.color[2] = (float)color.get_blue();
	// The legend is decided here rather than in the shader so that the
	// model and the flat grid make the one decision, in the one place,
	// from the one rule. It costs a quarter of a microsecond a key, which
	// a live effect repainting the whole board sixty times a second can
	// afford several hundred times over.
	entry.ink = styling::contrastingText(color) == "#ffffff" ? 1.0f : 0.0f;
	queue_render();
}

void KeyboardScene::setKeyFlags(LedKeyboard::Key key, bool selected,
                                bool pending) {
	std::map<LedKeyboard::Key, int>::const_iterator found = m_index.find(key);
	if (found == m_index.end())
		return;
	m_entries[found->second].selected = selected;
	m_entries[found->second].pending = pending;
	queue_render();
}

void KeyboardScene::setHovered(LedKeyboard::Key key, bool hovered) {
	std::map<LedKeyboard::Key, int>::const_iterator found = m_index.find(key);
	if (found == m_index.end())
		return;
	if (m_entries[found->second].hovered == hovered)
		return;
	m_entries[found->second].hovered = hovered;
	queue_render();
}

void KeyboardScene::clearHover() {
	bool changed = false;
	for (size_t i = 0; i < m_entries.size(); ++i) {
		if (m_entries[i].hovered) {
			m_entries[i].hovered = false;
			changed = true;
		}
	}
	if (changed)
		queue_render();
}

void KeyboardScene::setCursor(LedKeyboard::Key key, bool on) {
	std::map<LedKeyboard::Key, int>::const_iterator found = m_index.find(key);
	if (found == m_index.end())
		return;
	if (m_entries[found->second].cursor == on)
		return;
	m_entries[found->second].cursor = on;
	queue_render();
}

void KeyboardScene::clearCursor() {
	bool changed = false;
	for (size_t i = 0; i < m_entries.size(); ++i) {
		if (m_entries[i].cursor) {
			m_entries[i].cursor = false;
			changed = true;
		}
	}
	if (changed)
		queue_render();
}

void KeyboardScene::resetView() {
	m_yaw = viewYaw;
	m_pitch = viewPitch;
	m_panX = 0;
	m_panY = 0;
	m_zoom = 1.0f;
	queue_render();
}

void KeyboardScene::orbit(double deltaX, double deltaY) {
	m_yaw += (float)deltaX * 0.007f;
	m_pitch += (float)deltaY * 0.007f;
	const float limit = 1.5533f;   // just short of straight down
	m_pitch = std::max(0.12f, std::min(limit, m_pitch));
	queue_render();
}

void KeyboardScene::pan(double deltaX, double deltaY) {
	const float scale = cameraDistance(get_allocated_width(),
		get_allocated_height()) * 0.0016f;
	m_panX -= (float)deltaX * scale * std::cos(m_yaw);
	m_panY -= (float)deltaX * scale * -std::sin(m_yaw);
	m_panY += (float)deltaY * scale;
	queue_render();
}

void KeyboardScene::zoomBy(double steps) {
	// The zoom is a factor on the fitted distance rather than a length in
	// millimetres, so its limits mean the same thing on every board and
	// in every window, and so a view the user has zoomed keeps the same
	// share of the board when the window changes shape.
	m_zoom *= std::pow(0.88f, (float)steps);
	m_zoom = std::max(0.16f, std::min(2.5f, m_zoom));
	queue_render();
}

// How far back a camera at this angle has to stand for the whole board,
// plate and all, to be inside a frustum with these half-tangents.
//
// The camera is aimed at the middle of the board, so how far a corner
// lies across the screen and up it does not change with distance: only
// its depth does. Containment is then one inequality per corner per axis,
// |across| <= tanX * (distance - along), each of which gives a distance
// outright, and the answer is the largest of them.
float KeyboardScene::distanceFor(float yaw, float pitch, float tanX,
                                 float tanY) const {
	const float sinYaw = std::sin(yaw), cosYaw = std::cos(yaw);
	const float sinPitch = std::sin(pitch), cosPitch = std::cos(pitch);
	// The plate, which stands proud of the caps on every side and is what
	// the eye reads as the edge of the object.
	const float halfWidth = m_boardWidth * 0.5f + plateBezelMm;
	const float halfDepth = m_boardDepth * 0.5f + plateBezelMm;
	float distance = 0.0f;
	for (int corner = 0; corner < 8; ++corner) {
		const float x = (corner & 1) ? halfWidth : -halfWidth;
		const float y = (corner & 2) ? tallestCapMm : -plateThickMm;
		const float z = (corner & 4) ? halfDepth : -halfDepth;
		// The corner resolved along the view, across the screen, and up it.
		const float along = x * cosPitch * sinYaw + y * sinPitch +
			z * cosPitch * cosYaw;
		const float across = x * cosYaw - z * sinYaw;
		const float upward = -x * sinPitch * sinYaw + y * cosPitch -
			z * sinPitch * cosYaw;
		distance = std::max(distance, along + std::fabs(across) / tanX);
		distance = std::max(distance, along + std::fabs(upward) / tanY);
	}
	return distance;
}

// How far back a camera turned this way has to stand for the outline —
// not the box around it — to be inside a frustum with these
// half-tangents.
//
// Fitting the box leaves the stage short. At the default angle the box's
// top far corner is a keycap's height over the bare bezel, where no
// keycap stands, and that phantom corner is what the frustum ends up
// touching: the camera backs off for something that is not there, and
// the board is left small and sitting low in the frame.
//
// The answer is found by halving, because the outline covers less of the
// frame the further back the camera goes. The box fit is a distance that
// certainly works, since the box contains the outline; the nearest point
// of the board is one that certainly does not; the answer is between
// them. The resolved outline is left in the scratch buffers, because
// whoever asked will want to know where the middle of it landed.
float KeyboardScene::fitOutline(float yaw, float pitch, float tanX,
                                float tanY) const {
	const float box = distanceFor(yaw, pitch, tanX, tanY);
	const size_t count = m_hull.size() / 3;
	m_fitAlong.resize(count);
	m_fitAcross.resize(count);
	m_fitUpward.resize(count);
	const float sinYaw = std::sin(yaw), cosYaw = std::cos(yaw);
	const float sinPitch = std::sin(pitch), cosPitch = std::cos(pitch);
	float nearest = -1e9f;
	for (size_t i = 0; i < count; ++i) {
		const float x = m_hull[i * 3], y = m_hull[i * 3 + 1],
			z = m_hull[i * 3 + 2];
		m_fitAlong[i] = x * cosPitch * sinYaw + y * sinPitch +
			z * cosPitch * cosYaw;
		m_fitAcross[i] = x * cosYaw - z * sinYaw;
		m_fitUpward[i] = -x * sinPitch * sinYaw + y * cosPitch -
			z * sinPitch * cosYaw;
		nearest = std::max(nearest, m_fitAlong[i]);
	}
	float low = nearest + 1.0f, high = std::max(box, nearest + 2.0f);
	// Fourteen halvings of a range a few hundred millimetres wide settle
	// the distance to well under a tenth of one, which is a fraction of a
	// pixel at any size the stage takes.
	for (int step = 0; step < 14; ++step) {
		const float middle = (low + high) * 0.5f;
		float lowAcross = 1e9f, highAcross = -1e9f;
		float lowUp = 1e9f, highUp = -1e9f;
		for (size_t i = 0; i < count; ++i) {
			const float depth = middle - m_fitAlong[i];
			const float u = m_fitAcross[i] / depth, v = m_fitUpward[i] / depth;
			lowAcross = std::min(lowAcross, u);
			highAcross = std::max(highAcross, u);
			lowUp = std::min(lowUp, v);
			highUp = std::max(highUp, v);
		}
		if ((highAcross - lowAcross) * 0.5f <= tanX &&
		    (highUp - lowUp) * 0.5f <= tanY)
			high = middle;
		else
			low = middle;
	}
	return std::max(10.0f, high);
}

// The framing: how far back to stand, and where to point the lens.
//
// The outline is not centred in the frame even when the box around it
// is, so what the fit leaves over is a shift of the lens rather than a
// move of the camera. It has to be the lens: a camera aimed off the
// board would turn about a point that is not the board, and moving the
// camera is the pan, which belongs to the user.
float KeyboardScene::frameFor(int width, int height, float &shiftX,
                              float &shiftY) const {
	const float viewWidth = (float)std::max(1, width);
	const float viewHeight = (float)std::max(1, height);
	const float halfFov = std::tan(fovDegrees * 3.14159265f / 360.0f);
	// The frustum the board has to fit, narrowed by the clear space left
	// around it on all four sides.
	const float target = 1.0f - 2.0f * viewMargin;
	const float tanX = halfFov * viewWidth / viewHeight * target;
	const float tanY = halfFov * target;
	if (m_fitWidth == width && m_fitHeight == height && m_fitYaw == m_yaw &&
	    m_fitPitch == m_pitch) {
		shiftX = m_fitShiftX;
		shiftY = m_fitShiftY;
		return m_fitDistance;
	}
	shiftX = 0.0f;
	shiftY = 0.0f;
	if (m_hull.empty())
		return std::max(10.0f, std::max(distanceFor(m_yaw, m_pitch, tanX, tanY),
			distanceFor(viewYaw, viewPitch, tanX, tanY)));

	// What the default view needs, which depends on the stage and not on
	// how the board is turned, so it is worked out when the stage changes
	// shape and not on every frame of a drag.
	if (m_homeWidth != width || m_homeHeight != height) {
		m_homeDistance = fitOutline(viewYaw, viewPitch, tanX, tanY);
		m_homeWidth = width;
		m_homeHeight = height;
	}
	// Whichever is further: what the board needs as it is turned now, or
	// what the default view needs. Turning it end on can stand the camera
	// back far enough to keep the whole of it in frame, but never brings
	// it closer than Home does, so a drag turns the board rather than
	// magnifying it.
	const float distance = std::max(fitOutline(m_yaw, m_pitch, tanX, tanY),
		m_homeDistance);
	// Where the middle of the outline landed, in the frame's own terms.
	// The call above left it resolved for the angles it is being seen at.
	float minAcross = 1e9f, maxAcross = -1e9f, minUp = 1e9f, maxUp = -1e9f;
	for (size_t i = 0; i < m_fitAlong.size(); ++i) {
		const float depth = distance - m_fitAlong[i];
		const float u = m_fitAcross[i] / depth, v = m_fitUpward[i] / depth;
		minAcross = std::min(minAcross, u);
		maxAcross = std::max(maxAcross, u);
		minUp = std::min(minUp, v);
		maxUp = std::max(maxUp, v);
	}
	// The shift is in the frame's own coordinates, so it is measured
	// against the whole of it and not against the part left inside the
	// clear space.
	shiftX = (minAcross + maxAcross) * 0.5f / (tanX / target);
	shiftY = (minUp + maxUp) * 0.5f / (tanY / target);
	m_fitWidth = width;
	m_fitHeight = height;
	m_fitYaw = m_yaw;
	m_fitPitch = m_pitch;
	m_fitDistance = distance;
	m_fitShiftX = shiftX;
	m_fitShiftY = shiftY;
	return distance;
}

float KeyboardScene::fitDistance(int width, int height) const {
	float shiftX = 0, shiftY = 0;
	return frameFor(width, height, shiftX, shiftY);
}

float KeyboardScene::cameraDistance(int width, int height) const {
	return fitDistance(width, height) * m_zoom;
}

float KeyboardScene::camera(int width, int height, float *eye,
                            float *centre) const {
	const float distance = cameraDistance(width, height);
	centre[0] = m_boardWidth * 0.5f + m_panX;
	centre[1] = 0.0f;
	centre[2] = m_boardDepth * 0.5f + m_panY;
	eye[0] = centre[0] + distance * std::cos(m_pitch) * std::sin(m_yaw);
	eye[1] = centre[1] + distance * std::sin(m_pitch);
	eye[2] = centre[2] + distance * std::cos(m_pitch) * std::cos(m_yaw);
	return distance;
}

void KeyboardScene::viewProjection(int width, int height, float *matrix) const {
	const float aspect = height > 0 ? (float)width / (float)height : 1.0f;
	float eye[3], centre[3];
	const float distance = camera(width, height, eye, centre);
	// The planes follow the camera, so the depth buffer keeps its
	// resolution wherever the fit or the zoom puts it.
	const float reach = std::sqrt(m_boardWidth * m_boardWidth +
		m_boardDepth * m_boardDepth) + plateBezelMm * 2.0f;
	float projection[16];
	perspective(fovDegrees, aspect, std::max(1.0f, distance * 0.02f),
		distance + reach * 2.0f, projection);
	// The lens, shifted so the board sits in the middle of the stage. It
	// has to be the lens and not the camera: a camera aimed off the board
	// would turn about a point that is not the board, and the drag that
	// turns it has to keep it in the frame.
	float shiftX = 0, shiftY = 0;
	frameFor(width, height, shiftX, shiftY);
	projection[8] = shiftX;
	projection[9] = shiftY;
	const float up[3] = {0.0f, 1.0f, 0.0f};
	float view[16];
	lookAt(eye, centre, up, view);
	multiply(projection, view, matrix);
}

void KeyboardScene::buildPlate() {
	std::vector<float> ringX, ringZ;
	roundedRing(-plateBezelMm, -plateBezelMm, m_boardWidth + plateBezelMm,
		m_boardDepth + plateBezelMm, 6.0f, ringX, ringZ);
	std::vector<float> data;
	const size_t count = ringX.size();
	const float centreX = m_boardWidth * 0.5f;
	const float centreZ = m_boardDepth * 0.5f;
	// Top face as a fan, then a skirt down to the base.
	for (size_t i = 0; i < count; ++i) {
		const size_t next = (i + 1) % count;
		const float triangle[3][3] = {
			{centreX, 0.0f, centreZ},
			{ringX[i], 0.0f, ringZ[i]},
			{ringX[next], 0.0f, ringZ[next]},
		};
		for (int v = 0; v < 3; ++v) {
			data.push_back(triangle[v][0]);
			data.push_back(triangle[v][1]);
			data.push_back(triangle[v][2]);
			data.push_back(0.0f); data.push_back(1.0f); data.push_back(0.0f);
		}
	}
	for (size_t i = 0; i < count; ++i) {
		const size_t next = (i + 1) % count;
		float normal[3] = {ringX[i] - centreX, 0.0f, ringZ[i] - centreZ};
		normalise(normal);
		const float quad[4][3] = {
			{ringX[i], 0.0f, ringZ[i]},
			{ringX[i], -plateThickMm, ringZ[i]},
			{ringX[next], -plateThickMm, ringZ[next]},
			{ringX[next], 0.0f, ringZ[next]},
		};
		const int order[6] = {0, 1, 2, 0, 2, 3};
		for (int v = 0; v < 6; ++v) {
			data.push_back(quad[order[v]][0]);
			data.push_back(quad[order[v]][1]);
			data.push_back(quad[order[v]][2]);
			data.push_back(normal[0]);
			data.push_back(normal[1]);
			data.push_back(normal[2]);
		}
	}
	m_plateVertexCount = (int)(data.size() / 6);
	if (!m_plateVao)
		glGenVertexArrays(1, &m_plateVao);
	if (!m_plateVbo)
		glGenBuffers(1, &m_plateVbo);
	glBindVertexArray(m_plateVao);
	glBindBuffer(GL_ARRAY_BUFFER, m_plateVbo);
	glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float),
		data.empty() ? NULL : &data[0], GL_STATIC_DRAW);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
		(void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(1);
	glBindVertexArray(0);
}

void KeyboardScene::buildGeometry() {
	buildPlate();

	std::vector<Vertex> vertices;
	int rows = 0;
	for (size_t i = 0; i < m_entries.size(); ++i)
		rows = std::max(rows, m_entries[i].spec.row + m_entries[i].spec.height);

	for (size_t i = 0; i < m_entries.size(); ++i) {
		CapEntry &entry = m_entries[i];
		const keylayout::Cap &spec = entry.spec;
		float x0, x1, z0, z1;
		capFootprint(spec, x0, x1, z0, z1);
		float height = 0;
		capTop(spec, rows, entry.minX, entry.maxX, entry.minZ, entry.maxZ,
			height);
		// Wide keys are barely dished on a real board.
		const float dish = spec.indicator ? 0.0f :
			(x1 - x0 > unitMm * 1.6f ? dishMm * 0.25f : dishMm);
		entry.top = height;

		const float radius = spec.indicator ?
			std::min(x1 - x0, z1 - z0) * 0.45f : capRadiusMm;
		std::vector<float> lowX, lowZ, topX, topZ;
		roundedRing(x0, z0, x1, z1, radius, lowX, lowZ);
		roundedRing(entry.minX, entry.minZ, entry.maxX, entry.maxZ,
			radius * 0.8f, topX, topZ);
		const size_t count = lowX.size();
		const float capIndex = (float)i;

		// The dish, applied across the cap.
		auto topY = [&](float x) {
			const float u = (entry.maxX - entry.minX) > 1e-3f ?
				(x - entry.minX) / (entry.maxX - entry.minX) : 0.5f;
			const float d = (2.0f * u - 1.0f);
			return height - dish * d * d;
		};
		auto uvOf = [&](float x, float z) {
			return std::make_pair(
				(entry.maxX - entry.minX) > 1e-3f ?
					(x - entry.minX) / (entry.maxX - entry.minX) : 0.5f,
				(entry.maxZ - entry.minZ) > 1e-3f ?
					(z - entry.minZ) / (entry.maxZ - entry.minZ) : 0.5f);
		};

		// Skirt: from the footprint up and inward to the top ring.
		for (size_t p = 0; p < count; ++p) {
			const size_t next = (p + 1) % count;
			float normal[3] = {lowX[p] - (x0 + x1) * 0.5f, 0.75f,
			                   lowZ[p] - (z0 + z1) * 0.5f};
			normalise(normal);
			const float corners[4][3] = {
				{lowX[p], 0.0f, lowZ[p]},
				{topX[p], topY(topX[p]), topZ[p]},
				{topX[next], topY(topX[next]), topZ[next]},
				{lowX[next], 0.0f, lowZ[next]},
			};
			const int order[6] = {0, 1, 2, 0, 2, 3};
			for (int v = 0; v < 6; ++v) {
				Vertex vertex;
				vertex.x = corners[order[v]][0];
				vertex.y = corners[order[v]][1];
				vertex.z = corners[order[v]][2];
				vertex.nx = normal[0];
				vertex.ny = normal[1];
				vertex.nz = normal[2];
				// Outside 0..1 so the legend and the border band, which
				// belong to the top face, never appear on the sides.
				vertex.u = -1.0f;
				vertex.v = -1.0f;
				vertex.cap = capIndex;
				vertices.push_back(vertex);
			}
		}
		// Top face as a fan from the middle, following the dish.
		const float midX = (entry.minX + entry.maxX) * 0.5f;
		const float midZ = (entry.minZ + entry.maxZ) * 0.5f;
		for (size_t p = 0; p < count; ++p) {
			const size_t next = (p + 1) % count;
			const float triangle[3][3] = {
				{midX, topY(midX), midZ},
				{topX[p], topY(topX[p]), topZ[p]},
				{topX[next], topY(topX[next]), topZ[next]},
			};
			for (int v = 0; v < 3; ++v) {
				Vertex vertex;
				vertex.x = triangle[v][0];
				vertex.y = triangle[v][1];
				vertex.z = triangle[v][2];
				// Tilt the normal with the dish so it catches the light.
				const float slope = (entry.maxX - entry.minX) > 1e-3f ?
					-2.0f * dish * (2.0f * (triangle[v][0] - entry.minX) /
						(entry.maxX - entry.minX) - 1.0f) : 0.0f;
				float normal[3] = {-slope * 0.12f, 1.0f, 0.0f};
				normalise(normal);
				vertex.nx = normal[0];
				vertex.ny = normal[1];
				vertex.nz = normal[2];
				const std::pair<float, float> uv =
					uvOf(triangle[v][0], triangle[v][2]);
				vertex.u = uv.first;
				vertex.v = uv.second;
				vertex.cap = capIndex;
				vertices.push_back(vertex);
			}
		}
	}
	m_capVertexCount = (int)vertices.size();

	// A lamp is only a few millimetres across, which is a hard thing to
	// aim at. These quads fill the rest of its cell and are drawn in the
	// picking pass only, so the lamp stays the size it is on the
	// keyboard while the target you click stays the size of a key. They
	// take the cell whole, gap and all: the gap exists because keycaps
	// are separate mouldings, and nothing else claims that strip of
	// plate. Cells touch, so two lamps meet along an edge rather than
	// overlap.
	for (size_t i = 0; i < m_entries.size(); ++i) {
		const keylayout::Cap &spec = m_entries[i].spec;
		if (!spec.indicator)
			continue;
		const float x0 = spec.column * quarterMm;
		const float x1 = (spec.column + spec.width) * quarterMm;
		const float z0 = spec.row * unitMm;
		const float z1 = (spec.row + spec.height) * unitMm;
		const float y = 0.06f;
		const float quad[4][3] = {
			{x0, y, z0}, {x1, y, z0}, {x1, y, z1}, {x0, y, z1},
		};
		const int order[6] = {0, 1, 2, 0, 2, 3};
		for (int v = 0; v < 6; ++v) {
			Vertex vertex;
			vertex.x = quad[order[v]][0];
			vertex.y = quad[order[v]][1];
			vertex.z = quad[order[v]][2];
			vertex.nx = 0.0f; vertex.ny = 1.0f; vertex.nz = 0.0f;
			vertex.u = -1.0f; vertex.v = -1.0f;
			vertex.cap = (float)i;
			vertices.push_back(vertex);
		}
	}
	m_pickVertexCount = (int)vertices.size() - m_capVertexCount;

	// The light that escapes around each cap, as a quad lying on the
	// plate. Drawn after the caps, added to what is already there.
	for (size_t i = 0; i < m_entries.size(); ++i) {
		const keylayout::Cap &spec = m_entries[i].spec;
		float capX0, capX1, capZ0, capZ1;
		capFootprint(spec, capX0, capX1, capZ0, capZ1);
		const float x0 = capX0 - haloMm;
		const float x1 = capX1 + haloMm;
		const float z0 = capZ0 - haloMm;
		const float z1 = capZ1 + haloMm;
		const float y = 0.12f;
		const float quad[4][5] = {
			{x0, y, z0, 0.0f, 0.0f},
			{x1, y, z0, 1.0f, 0.0f},
			{x1, y, z1, 1.0f, 1.0f},
			{x0, y, z1, 0.0f, 1.0f},
		};
		const int order[6] = {0, 1, 2, 0, 2, 3};
		for (int v = 0; v < 6; ++v) {
			Vertex vertex;
			vertex.x = quad[order[v]][0];
			vertex.y = quad[order[v]][1];
			vertex.z = quad[order[v]][2];
			vertex.nx = 0.0f; vertex.ny = 1.0f; vertex.nz = 0.0f;
			vertex.u = quad[order[v]][3];
			vertex.v = quad[order[v]][4];
			vertex.cap = (float)i;
			vertices.push_back(vertex);
		}
	}

	// The lamps' names, on the bare plate in front of each lens. They lie
	// on the plate rather than on the lamp because the lamp is four
	// millimetres deep and a word is not, and because that is where the
	// keyboard prints them.
	m_silkVertexCount = 0;
	for (size_t i = 0; i < m_entries.size(); ++i) {
		const keylayout::Cap &spec = m_entries[i].spec;
		if (!spec.indicator || spec.label.empty())
			continue;
		float x0, x1, z0, z1;
		lampLabelStrip(spec, x0, x1, z0, z1);
		const float y = 0.16f;
		const float quad[4][5] = {
			{x0, y, z0, 0.0f, 0.0f},
			{x1, y, z0, 1.0f, 0.0f},
			{x1, y, z1, 1.0f, 1.0f},
			{x0, y, z1, 0.0f, 1.0f},
		};
		const int order[6] = {0, 1, 2, 0, 2, 3};
		for (int v = 0; v < 6; ++v) {
			Vertex vertex;
			vertex.x = quad[order[v]][0];
			vertex.y = quad[order[v]][1];
			vertex.z = quad[order[v]][2];
			vertex.nx = 0.0f; vertex.ny = 1.0f; vertex.nz = 0.0f;
			vertex.u = quad[order[v]][3];
			vertex.v = quad[order[v]][4];
			vertex.cap = (float)i;
			vertices.push_back(vertex);
		}
		m_silkVertexCount += 6;
	}

	if (!m_capVao)
		glGenVertexArrays(1, &m_capVao);
	if (!m_capVbo)
		glGenBuffers(1, &m_capVbo);
	glBindVertexArray(m_capVao);
	glBindBuffer(GL_ARRAY_BUFFER, m_capVbo);
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex),
		vertices.empty() ? NULL : &vertices[0], GL_STATIC_DRAW);
	const int stride = sizeof(Vertex);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
		(void*)(3 * sizeof(float)));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
		(void*)(6 * sizeof(float)));
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride,
		(void*)(8 * sizeof(float)));
	glEnableVertexAttribArray(3);
	glBindVertexArray(0);
	m_geometryDirty = false;
}

// One texture holding every legend, drawn with the toolkit's own text
// stack so the model's lettering matches the rest of the program. It is
// a mask, not a picture: the colour comes from the key at draw time,
// which is why one atlas serves every colour a key can take.
void KeyboardScene::buildAtlas() {
	if (m_entries.empty())
		return;
	// A legend is only a few pixels tall on a board seen whole, which is
	// the size the mip chain is sampled at; the cell is generous so that
	// what those few pixels are averaged from is a clean glyph.
	const int cell = 128;
	m_atlasColumns = 16;
	const int rows = (int)((m_entries.size() + m_atlasColumns - 1) /
		m_atlasColumns);
	const int width = m_atlasColumns * cell;
	const int height = std::max(1, rows) * cell;
	Cairo::RefPtr<Cairo::ImageSurface> surface =
		Cairo::ImageSurface::create(Cairo::FORMAT_A8, width, height);
	Cairo::RefPtr<Cairo::Context> context = Cairo::Context::create(surface);
	context->set_source_rgba(0, 0, 0, 0);
	context->set_operator(Cairo::OPERATOR_SOURCE);
	context->paint();
	context->set_operator(Cairo::OPERATOR_OVER);
	context->set_source_rgba(1, 1, 1, 1);

	Glib::RefPtr<Pango::Layout> layout = Pango::Layout::create(context);
	layout->set_alignment(Pango::ALIGN_CENTER);
	// Lays a label out at a size and reports what the glyphs themselves
	// cover. The line box Pango reports is half again as tall as the
	// letters standing in it, and sizing to that throws the difference
	// away on every key.
	auto measure = [&](const std::string &text, double em) {
		Pango::FontDescription font("Sans Bold");
		font.set_absolute_size(em * PANGO_SCALE);
		layout->set_font_description(font);
		layout->set_text(text);
		return layout->get_pixel_ink_extents();
	};
	// The largest size at which this text's letters fit the room given.
	// Ink scales with the size, so the answer is one measurement and a
	// division; the passes after it only settle what hinting moved, and
	// never let the result grow past the room.
	auto largest = [&](const std::string &text, double squeeze, double roomW,
	                   double roomH) {
		Pango::Rectangle ink = measure(text, cell * 0.5);
		if (ink.get_width() <= 0 || ink.get_height() <= 0)
			return 0.0;
		double em = cell * 0.5 * std::min(roomW / (ink.get_width() * squeeze),
			roomH / (double)ink.get_height());
		for (int pass = 0; pass < 3; ++pass) {
			ink = measure(text, em);
			const double over = std::max(ink.get_width() * squeeze / roomW,
				(double)ink.get_height() / roomH);
			if (over <= 1.0)
				break;
			em /= over * 1.01;
		}
		return em;
	};
	// The lamps are named or not named together: a row where two of the
	// five carry a word and three carry nothing would read as three
	// nameless lamps, so they all answer to the longest of them.
	size_t longestLamp = 0;
	for (size_t i = 0; i < m_entries.size(); ++i)
		if (m_entries[i].spec.indicator)
			longestLamp = std::max(longestLamp, m_entries[i].spec.label.size());
	for (size_t i = 0; i < m_entries.size(); ++i) {
		CapEntry &entry = m_entries[i];
		const int column = (int)i % m_atlasColumns;
		const int row = (int)i / m_atlasColumns;
		entry.legend[0] = 0.0f;
		entry.legend[1] = 0.0f;
		entry.legend[2] = (float)column;
		entry.legend[3] = (float)row;
		if (entry.spec.label.empty())
			continue;
		// A lamp carries no legend on the lens — there is none on the
		// keyboard, and at that size it would be noise — but it is the one
		// thing on the board you cannot name from its shape, so its name
		// is silkscreened on the bare plate behind it, where the real
		// board carries it and where nothing stands in front of it. The
		// word is printed in the top strip of the cell, as deep a share of
		// the cell as the strip is of its own width, so that the square of
		// the cell lands square on the plate.
		if (entry.spec.indicator) {
			float sx0, sx1, sz0, sz1;
			lampLabelStrip(entry.spec, sx0, sx1, sz0, sz1);
			const double flat = (sx1 - sx0) > 1e-3f ?
				(sz1 - sz0) / (sx1 - sx0) : 1.0;
			const double em = largest(entry.spec.label, 1.0, cell * 0.94,
				cell * flat * 0.78);
			const Pango::Rectangle ink = measure(entry.spec.label, em);
			context->save();
			context->translate(column * cell, row * cell);
			context->move_to((cell - ink.get_width()) / 2.0 - ink.get_x(),
				(cell * flat - ink.get_height()) / 2.0 - ink.get_y());
			// Printed heavier than type on a screen, because that is what
			// silkscreen is: a stroke one pixel wide averages away to
			// nothing on the way down the mip chain, and what is left of
			// it on a plate is a grey haze rather than a word.
			layout->add_to_cairo_context(context);
			context->fill_preserve();
			context->set_line_width(em * 0.07);
			context->stroke();
			context->restore();
			entry.legend[0] = (float)flat;
			// How wide the strip has to be drawn, in pixels, before the
			// word in it is a word rather than a grey bar.
			entry.legend[1] = -5.6f * (float)longestLamp;
			continue;
		}
		// The patch the legend is printed in is as deep as the cap's
		// shorter side, so a letter is the same size on every key however
		// wide the key is. It is allowed to be wider than that, up to
		// twice as wide and never past the cap: "Backspace" then prints
		// at something like the size "B" does instead of being shrunk to
		// fit a square in the middle of a key with room to spare.
		const float capWidth = entry.maxX - entry.minX;
		const float capDepth = entry.maxZ - entry.minZ;
		const float side = std::min(capWidth, capDepth);
		const float wide = std::min(capWidth, side * 2.0f);
		// The atlas cell is square. Laying it on a patch that is not
		// stretches it, so the text is drawn narrowed by that same factor
		// and comes out with its own proportions.
		const double stretch = side > 1e-3f ? wide / side : 1.0;
		// As large as will fit. A legend printed at the proportions of a
		// real keycap is four pixels tall on a board seen whole, which is
		// not a legend; this is a picture you have to be able to name a
		// key from, so the lettering is generous.
		//
		// A word too long for the key it is on has two ways out before it
		// is shrunk to a smear. It can go down the key on two lines, the
		// way a keyboard prints Prt Sc and Pg Up — but only at a break the
		// word already has, because a word broken anywhere else stops
		// being the word, and only while that leaves the letters larger
		// than one line would. Failing that it can be condensed, which is
		// what narrow type is for: three quarters of the width buys a
		// third again of letter height, and it is worth having only if it
		// buys most of that.
		const double roomW = cell * stretch * 0.84;
		const double plain = largest(entry.spec.label, 1.0, roomW, cell * 0.62);
		double em = plain;
		double squeeze = 1.0;
		std::string text = entry.spec.label;
		const std::string broken = brokenLabel(entry.spec.label);
		const double stacked = broken.empty() ? 0.0 :
			largest(broken, 1.0, roomW, cell * 0.88);
		if (stacked > plain) {
			em = stacked;
			text = broken;
		} else {
			const double condensed = largest(text, 0.76, roomW, cell * 0.62);
			if (condensed > plain * 1.12) {
				em = condensed;
				squeeze = 0.76;
			}
		}
		const Pango::Rectangle ink = measure(text, em);
		context->save();
		context->translate(column * cell, row * cell);
		context->scale(1.0 / stretch, 1.0);
		context->scale(squeeze, 1.0);
		context->move_to((cell * stretch / squeeze - ink.get_width()) / 2.0 -
			ink.get_x(), (cell - ink.get_height()) / 2.0 - ink.get_y());
		layout->show_in_cairo_context(context);
		context->restore();
		entry.legend[0] = capWidth > 1e-3f ? wide / capWidth : 1.0f;
		entry.legend[1] = capDepth > 1e-3f ? side / capDepth : 1.0f;
	}
	surface->flush();

	if (!m_atlas)
		glGenTextures(1, &m_atlas);
	glBindTexture(GL_TEXTURE_2D, m_atlas);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, surface->get_stride());
	glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED,
		GL_UNSIGNED_BYTE, surface->get_data());
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	// A cap top is seen at a slant, so its legend is squashed down the
	// board and not across it. A filter that has one setting for both axes
	// has to take the coarser, which fetches the lettering from further
	// down the mip chain than its width ever needed and prints a smear
	// where a letter should be. Asking per axis is the difference between
	// a readable A and a grey lozenge. Every driver has had it for twenty
	// years, but a driver that has not simply keeps what it had.
	if (epoxy_has_gl_extension("GL_EXT_texture_filter_anisotropic")) {
		float most = 1.0f;
		glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &most);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
			std::min(8.0f, std::max(1.0f, most)));
	}
	glGenerateMipmap(GL_TEXTURE_2D);
}

void KeyboardScene::uploadCapState() {
	std::vector<float> colors(maxCaps * 4, 0.0f);
	std::vector<float> flags(maxCaps * 4, 0.0f);
	std::vector<float> legends(maxCaps * 4, 0.0f);
	for (size_t i = 0; i < m_entries.size() && (int)i < maxCaps; ++i) {
		const CapEntry &entry = m_entries[i];
		colors[i * 4 + 0] = entry.color[0];
		colors[i * 4 + 1] = entry.color[1];
		colors[i * 4 + 2] = entry.color[2];
		colors[i * 4 + 3] = entry.spec.controllable ? 1.0f : 0.0f;
		flags[i * 4 + 0] = entry.selected ? 1.0f : 0.0f;
		flags[i * 4 + 1] = entry.pending ? 1.0f : 0.0f;
		// One slot, two ranks: 1 is the pointer, 2 is the arrow keys'
		// cursor, which never stands aside.
		flags[i * 4 + 2] = entry.cursor ? 2.0f : (entry.hovered ? 1.0f : 0.0f);
		// Whether this model has the key rides in the colour's alpha, so
		// this slot carries the one thing that is per-cap and cannot be
		// worked out from the colour in the shader.
		flags[i * 4 + 3] = entry.ink;
		legends[i * 4 + 0] = entry.legend[0];
		legends[i * 4 + 1] = entry.legend[1];
		legends[i * 4 + 2] = entry.legend[2];
		legends[i * 4 + 3] = entry.legend[3];
	}
	glUniform4fv(glGetUniformLocation(m_capProgram, "uCapColor"), maxCaps,
		&colors[0]);
	glUniform4fv(glGetUniformLocation(m_capProgram, "uCapFlags"), maxCaps,
		&flags[0]);
	glUniform4fv(glGetUniformLocation(m_capProgram, "uCapLegend"), maxCaps,
		&legends[0]);
	glUniform1f(glGetUniformLocation(m_capProgram, "uAtlasColumns"),
		(float)std::max(1, m_atlasColumns));
	glUniform1f(glGetUniformLocation(m_capProgram, "uAtlasRows"),
		(float)std::max(1, (int)((m_entries.size() + m_atlasColumns - 1) /
			std::max(1, m_atlasColumns))));
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, m_atlas);
	glUniform1i(glGetUniformLocation(m_capProgram, "uAtlas"), 0);
}

bool KeyboardScene::ensureProgram() {
	if (m_capProgram && m_plateProgram)
		return true;
	m_capProgram = link(capVertexSource, capFragmentSource, "keycap");
	m_plateProgram = link(plateVertexSource, plateFragmentSource, "plate");
	return m_capProgram && m_plateProgram;
}

void KeyboardScene::on_realize() {
	Gtk::GLArea::on_realize();
	make_current();
	try {
		throw_if_error();
	} catch (const Glib::Error &error) {
		std::cerr << "g810-led-gui: no GL context: " << error.what() << std::endl;
		m_usable = false;
		m_signal_ready.emit();
		return;
	}
	m_usable = ensureProgram();
	if (m_usable) {
		buildGeometry();
		buildAtlas();
		m_geometryDirty = false;
	}
	m_signal_ready.emit();
}

void KeyboardScene::on_unrealize() {
	make_current();
	if (m_capVbo) glDeleteBuffers(1, &m_capVbo);
	if (m_plateVbo) glDeleteBuffers(1, &m_plateVbo);
	if (m_capVao) glDeleteVertexArrays(1, &m_capVao);
	if (m_plateVao) glDeleteVertexArrays(1, &m_plateVao);
	if (m_capProgram) glDeleteProgram(m_capProgram);
	if (m_plateProgram) glDeleteProgram(m_plateProgram);
	if (m_atlas) glDeleteTextures(1, &m_atlas);
	if (m_pickFbo) glDeleteFramebuffers(1, &m_pickFbo);
	if (m_pickColor) glDeleteTextures(1, &m_pickColor);
	if (m_pickDepth) glDeleteRenderbuffers(1, &m_pickDepth);
	m_capVbo = m_plateVbo = m_capVao = m_plateVao = 0;
	m_capProgram = m_plateProgram = 0;
	m_pickFbo = m_pickColor = m_pickDepth = m_atlas = 0;
	Gtk::GLArea::on_unrealize();
}

void KeyboardScene::drawScene(int width, int height, bool picking) {
	if (m_geometryDirty) {
		buildGeometry();
		buildAtlas();
	}
	glViewport(0, 0, width, height);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	if (picking)
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	else
		// @kb_sunken, the colour the board sits on in the flat view: the
		// stage is one surface, whichever view is shown on it.
		glClearColor(0.0627f, 0.0706f, 0.0863f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	float viewProj[16];
	viewProjection(width, height, viewProj);
	float eye[3], centre[3];
	camera(width, height, eye, centre);
	float light[3] = {-0.35f, 0.86f, 0.36f};
	normalise(light);

	if (!picking && m_plateProgram && m_plateVertexCount) {
		glUseProgram(m_plateProgram);
		glUniformMatrix4fv(glGetUniformLocation(m_plateProgram, "uViewProj"),
			1, GL_FALSE, viewProj);
		glUniform3fv(glGetUniformLocation(m_plateProgram, "uLightDir"), 1, light);
		glUniform3fv(glGetUniformLocation(m_plateProgram, "uEye"), 1, eye);
		glBindVertexArray(m_plateVao);
		glDrawArrays(GL_TRIANGLES, 0, m_plateVertexCount);
	}

	if (!m_capProgram || !m_capVertexCount)
		return;
	glUseProgram(m_capProgram);
	glUniformMatrix4fv(glGetUniformLocation(m_capProgram, "uViewProj"),
		1, GL_FALSE, viewProj);
	glUniform3fv(glGetUniformLocation(m_capProgram, "uLightDir"), 1, light);
	glUniform3fv(glGetUniformLocation(m_capProgram, "uEye"), 1, eye);
	glUniform1i(glGetUniformLocation(m_capProgram, "uPicking"), picking ? 1 : 0);
	glUniform1i(glGetUniformLocation(m_capProgram, "uPass"), 0);
	uploadCapState();
	glBindVertexArray(m_capVao);
	// The lamps' extra targets sit straight after the caps, so picking is
	// still one draw.
	glDrawArrays(GL_TRIANGLES, 0,
		picking ? m_capVertexCount + m_pickVertexCount : m_capVertexCount);

	if (picking)
		return;
	// The spill on the plate, added on top of it and never occluding a
	// cap that stands in front.
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	glDepthMask(GL_FALSE);
	glUniform1i(glGetUniformLocation(m_capProgram, "uPass"), 1);
	const int halos = (int)m_entries.size() * 6;
	const int spill = m_capVertexCount + m_pickVertexCount;
	glDrawArrays(GL_TRIANGLES, spill, halos);
	if (m_silkVertexCount) {
		glUniform1i(glGetUniformLocation(m_capProgram, "uPass"), 2);
		glDrawArrays(GL_TRIANGLES, spill + halos, m_silkVertexCount);
	}
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glBindVertexArray(0);
}

bool KeyboardScene::on_render(const Glib::RefPtr<Gdk::GLContext> &) {
	if (!m_usable)
		return false;
	const int scale = get_scale_factor();
	drawScene(get_allocated_width() * scale, get_allocated_height() * scale,
		false);
	return true;
}

bool KeyboardScene::renderPick(int x, int y, int width, int height,
                               int &capIndex) {
	if (!m_usable || width <= 0 || height <= 0)
		return false;
	if (!m_pickFbo) {
		glGenFramebuffers(1, &m_pickFbo);
		glGenTextures(1, &m_pickColor);
		glGenRenderbuffers(1, &m_pickDepth);
	}
	if (width != m_pickWidth || height != m_pickHeight) {
		glBindTexture(GL_TEXTURE_2D, m_pickColor);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
			GL_UNSIGNED_BYTE, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glBindRenderbuffer(GL_RENDERBUFFER, m_pickDepth);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width,
			height);
		m_pickWidth = width;
		m_pickHeight = height;
	}
	int previous = 0;
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous);
	glBindFramebuffer(GL_FRAMEBUFFER, m_pickFbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
		GL_TEXTURE_2D, m_pickColor, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
		GL_RENDERBUFFER, m_pickDepth);
	const bool complete =
		glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
	if (complete) {
		drawScene(width, height, true);
		unsigned char pixel[4] = {0, 0, 0, 0};
		// GL counts rows from the bottom; widget coordinates do not.
		glReadPixels(x, height - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
		const int id = pixel[0] | (pixel[1] << 8) | (pixel[2] << 16);
		capIndex = id - 1;
	}
	glBindFramebuffer(GL_FRAMEBUFFER, previous);
	return complete;
}

bool KeyboardScene::keyAt(double x, double y, LedKeyboard::Key &key) {
	if (!m_usable || !get_realized())
		return false;
	make_current();
	const int scale = get_scale_factor();
	const int width = get_allocated_width() * scale;
	const int height = get_allocated_height() * scale;
	int index = -1;
	if (!renderPick((int)(x * scale), (int)(y * scale), width, height, index))
		return false;
	if (index < 0 || index >= (int)m_entries.size())
		return false;
	key = m_entries[index].spec.key;
	return m_entries[index].spec.controllable;
}

bool KeyboardScene::projectCap(const CapEntry &cap, int width, int height,
                               Gdk::Rectangle &rect) const {
	float viewProj[16];
	viewProjection(width, height, viewProj);
	const float corners[4][3] = {
		{cap.minX, cap.top, cap.minZ},
		{cap.maxX, cap.top, cap.minZ},
		{cap.maxX, cap.top, cap.maxZ},
		{cap.minX, cap.top, cap.maxZ},
	};
	float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
	for (int i = 0; i < 4; ++i) {
		const float *p = corners[i];
		const float clip[4] = {
			viewProj[0] * p[0] + viewProj[4] * p[1] + viewProj[8] * p[2] + viewProj[12],
			viewProj[1] * p[0] + viewProj[5] * p[1] + viewProj[9] * p[2] + viewProj[13],
			viewProj[2] * p[0] + viewProj[6] * p[1] + viewProj[10] * p[2] + viewProj[14],
			viewProj[3] * p[0] + viewProj[7] * p[1] + viewProj[11] * p[2] + viewProj[15],
		};
		if (clip[3] <= 1e-5f)
			return false;
		const float sx = (clip[0] / clip[3] * 0.5f + 0.5f) * width;
		const float sy = (1.0f - (clip[1] / clip[3] * 0.5f + 0.5f)) * height;
		minX = std::min(minX, sx);
		maxX = std::max(maxX, sx);
		minY = std::min(minY, sy);
		maxY = std::max(maxY, sy);
	}
	rect = Gdk::Rectangle((int)minX, (int)minY,
		(int)std::ceil(maxX - minX), (int)std::ceil(maxY - minY));
	return true;
}

bool KeyboardScene::keyRect(LedKeyboard::Key key, Gdk::Rectangle &rect) {
	std::map<LedKeyboard::Key, int>::const_iterator found = m_index.find(key);
	if (found == m_index.end())
		return false;
	return projectCap(m_entries[found->second], get_allocated_width(),
		get_allocated_height(), rect);
}

std::vector<LedKeyboard::Key> KeyboardScene::keysIn(const Gdk::Rectangle &rect) {
	std::vector<LedKeyboard::Key> keys;
	const int width = get_allocated_width();
	const int height = get_allocated_height();
	for (size_t i = 0; i < m_entries.size(); ++i) {
		if (!m_entries[i].spec.controllable)
			continue;
		Gdk::Rectangle capRect;
		if (!projectCap(m_entries[i], width, height, capRect))
			continue;
		Gdk::Rectangle overlap = capRect;
		overlap.intersect(rect);
		if (overlap.get_width() > 0 && overlap.get_height() > 0)
			keys.push_back(m_entries[i].spec.key);
	}
	return keys;
}

Glib::RefPtr<Gdk::Pixbuf> KeyboardScene::snapshot(int width, int height) {
	if (!m_usable || !get_realized())
		return Glib::RefPtr<Gdk::Pixbuf>();
	make_current();
	unsigned fbo = 0, color = 0, depth = 0;
	glGenFramebuffers(1, &fbo);
	glGenTextures(1, &color);
	glGenRenderbuffers(1, &depth);
	glBindTexture(GL_TEXTURE_2D, color);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
		GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glBindRenderbuffer(GL_RENDERBUFFER, depth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
	int previous = 0;
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
		color, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
		GL_RENDERBUFFER, depth);
	Glib::RefPtr<Gdk::Pixbuf> pixbuf;
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
		drawScene(width, height, false);
		std::vector<unsigned char> pixels((size_t)width * height * 4);
		glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, &pixels[0]);
		pixbuf = Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB, true, 8, width, height);
		// GL hands back the bottom row first.
		for (int y = 0; y < height; ++y)
			std::memcpy(pixbuf->get_pixels() + y * pixbuf->get_rowstride(),
				&pixels[(size_t)(height - 1 - y) * width * 4], width * 4);
	}
	glBindFramebuffer(GL_FRAMEBUFFER, previous);
	glDeleteFramebuffers(1, &fbo);
	glDeleteTextures(1, &color);
	glDeleteRenderbuffers(1, &depth);
	return pixbuf;
}
