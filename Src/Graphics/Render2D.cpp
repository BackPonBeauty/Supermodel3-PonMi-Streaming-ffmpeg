/**
 ** Supermodel
 ** A Sega Model 3 Arcade Emulator.
 ** Copyright 2011-2012 Bart Trzynadlowski, Nik Henson
 **
 ** This file is part of Supermodel.
 **
 ** Supermodel is free software: you can redistribute it and/or modify it under
 ** the terms of the GNU General Public License as published by the Free
 ** Software Foundation, either version 3 of the License, or (at your option)
 ** any later version.
 **
 ** Supermodel is distributed in the hope that it will be useful, but WITHOUT
 ** ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 ** FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 ** more details.
 **
 ** You should have received a copy of the GNU General Public License along
 ** with Supermodel.  If not, see <http://www.gnu.org/licenses/>.
 **/

 /*
  * Render2D.cpp
  *
  * Implementation of the CRender2D class: OpenGL tile generator graphics.
  *
  * To-Do List
  * ----------
  * - Is there a universal solution to the 'ROLLING START' scrolling bug (Scud
  *   Race) and the scrolling text during Magical Truck Adventure's attract
  *   mode? To fix Scud Race, either the stencil mask or the h-scroll value must
  *   be shifted by 16 pixels. Magical Truck Adventure is similar but opposite.
  *   Perhaps this is a function of timing registers accessed via JTAG?
  * - Is there a better way to handle the overscan regions in wide screen mode?
  *   Is clearing two thin viewports better than one big clear?
  * - Are v-scroll values 9 or 10 bits? (Does it matter?) Lost World seems to
  *   have some scrolling issues.
  * - A proper shut-down function is needed! OpenGL might not be available when
  *   the destructor for this class is called.
  *
  * Tile Generator Hardware Overview
  * --------------------------------
  *
  * Model 3's medium resolution tile generator hardware appears to be derived
  * from the Model 2 and System 24 chipset, but is much simpler. It consists of
  * four 64x64 tile layers, comprised of 8x8 pixel tiles, with configurable
  * priorities. There may be additional features but so far, no known Model 3
  * games use them.
  *
  * VRAM is comprised of 1 MB for tile data and an additional 128 KB for the
  * palette (each color occupies 32 bits). The four tilemap layers are referred
  * to as: A (0), A' (1), B (2), and B' (3). Palette RAM may be located on a
  * separate RAM IC.
  *
  * Registers
  * ---------

	 0xF1180020:         -------- -------- -------- -------- ?
						 -------- -------- x------- -------- Layer 3 bitdepth (0 = 8-bit, 1 = 4-bit)
						 -------- -------- -x------ -------- Layer 2 bitdepth (0 = 8-bit, 1 = 4-bit)
						 -------- -------- --x----- -------- Layer 1 bitdepth (0 = 8-bit, 1 = 4-bit)
						 -------- -------- ---x---- -------- Layer 0 bitdepth (0 = 8-bit, 1 = 4-bit)
						 -------- -------- ----x--- -------- Layer 3 priority (0 = below 3D, 1 = above 3D)
						 -------- -------- -----x-- -------- Layer 2 priority (0 = below 3D, 1 = above 3D)
						 -------- -------- ------x- -------- Layer 1 priority (0 = below 3D, 1 = above 3D)
						 -------- -------- -------x -------- Layer 0 priority (0 = below 3D, 1 = above 3D)

	 0xF1180040:                                             Layer 0 & 1 modulation
						 -------- xxxxxxxx -------- -------- Red component
						 -------- -------- xxxxxxxx -------- Green component
						 -------- -------- -------- xxxxxxxx Blue component

	 0xF1180044:                                             Layer 2 & 3 modulation
						 -------- xxxxxxxx -------- -------- Red component
						 -------- -------- xxxxxxxx -------- Green component
						 -------- -------- -------- xxxxxxxx Blue component

	 0xF1180060:         x------- -------- -------- -------- Layer 0 enable
						 -------x xxxxxxxx -------- -------- Layer 0 Y scroll position
						 -------- -------- x------- -------- Layer 0 X line scroll enable
						 -------- -------- -------x xxxxxxxx Layer 0 X scroll position

	 0xF1180064:         x------- -------- -------- -------- Layer 1 enable
						 -------x xxxxxxxx -------- -------- Layer 1 Y scroll position
						 -------- -------- x------- -------- Layer 1 X line scroll enable
						 -------- -------- -------x xxxxxxxx Layer 1 X scroll position

	 0xF1180068:         x------- -------- -------- -------- Layer 2 enable
						 -------x xxxxxxxx -------- -------- Layer 2 Y scroll position
						 -------- -------- x------- -------- Layer 2 X line scroll enable
						 -------- -------- -------x xxxxxxxx Layer 2 X scroll position

	 0xF118006C:         x------- -------- -------- -------- Layer 3 enable
						 -------x xxxxxxxx -------- -------- Layer 3 Y scroll position
						 -------- -------- x------- -------- Layer 3 X line scroll enable
						 -------- -------- -------x xxxxxxxx Layer 3 X scroll position

  *
  * VRAM Memory Map
  * ---------------
  *
  * The lower 1 MB of VRAM is used for storing tiles, per-line horizontal scroll
  * values, and the stencil mask, which determines which of each pair of layers
  * is displayed on a given line and column.
  *
  *    00000-F5FFF   Tile pattern data
  *    F6000-F63FF   Layer A horizontal scroll table (512 lines)
  *    F6400-F67FF   Layer A' horizontal scroll table
  *    F6800-F6BFF   Layer B horizontal scroll table
  *    F6C00-F6FFF   Layer B' horizontal scroll table
  *    F7000-F77FF   Mask table (assuming 4 bytes per line, 512 lines)
  *    F7800-F7FFF   ?
  *    F8000-F9FFF   Layer A name table
  *    FA000-FBFFF   Layer A' name table
  *    FC000-FDFFF   Layer B name table
  *    FE000-FFFFF   Layer B' name table
  *
  * Tiles may actually address the entire 1 MB space, although in practice,
  * that would conflict with the other fixed memory regions.
  *
  * Palette
  * -------
  *
  * The palette stores 32768 colors. Each entry is a little endian 32-bit word.
  * The upper 16 bits are unused and the lower 16 bits contain the color:
  *
  *    15                 0
  *     tbbb bbgg gggr rrrr
  *
  * The 't' bit is for transparency. When set, pixels of that color are
  * transparent, unless they are the bottom-most layer.
  *
  * Tile Name Table and Pattern Layout
  * ----------------------------------
  *
  * The name table is a 64x64 array of 16-bit words serving as indices for tile
  * pattern data and the palette. The first 64 words correspond to the first
  * row of tiles, the next 64 to the second row, etc. Although 64x64 entries
  * describes a 512x512 pixel screen, only the upper-left 62x48 tiles are
  * visible when the vertical and horizontal scroll values are 0. Scrolling
  * moves the 496x384 pixel 'window' around, with individual wrapping of the
  * two axes.
  *
  * The data is actually arranged in 32-bit chunks in little endian format, so
  * that tiles 0, 1, 2, and 3 will be stored as 1, 0, 3, 2. Fetching two name
  * table entries as a single 32-bit word places the left tile in the high 16
  * bits and the right tile in the low 16 bits.
  *
  * The format of a name table entry in 4-bit color mode is:
  *
  *    15                 0
  *     jkpp pppp pppp iiii
  *
  * The pattern index is '0ppp pppp pppi iiij'. Multiplying by 32 yields the
  * offset in VRAM at which the tile pattern data is stored. Note that the MSB
  * of the name table entry becomes the LSB of the pattern index. This allows
  * for 32768 4-bit tile patterns, each occupying 32 bytes, which means the
  * whole 1 MB VRAM space can be addressed.
  *
  * The 4-bit pattern data is stored as 8 32-bit words. Each word stores a row
  * of 8 pixels:
  *
  *    31                                     0
  *     aaaa bbbb cccc dddd eeee ffff gggg hhhh
  *
  * 'a' is the left-most pixel data. These 4-bit values are combined with bits
  * from the name table to form a palette index, which determines the final
  * color. For example, for pixel 'a', the 15-bit color index is:
  *
  *      14                0
  *     kpp pppp pppp aaaa
  *
  * Note that index bits are re-used to form the palette index, meaning that
  * the pattern address partly determines the color.
  *
  * In 8-bit color mode, the name table entry looks like:
  *
  *    15                 0
  *     ?ppp pppp iiii iiii
  *
  * The low 15 'p' and 'i' bits together form the pattern index, which must be
  * multiplied by 64 to get the offset. The pattern data now consists of 16 32-
  * bit words, each containing four 8-bit pixels:
  *
  *    31                                     0
  *     aaaa aaaa bbbb bbbb cccc cccc dddd dddd
  *
  * 'a' is the left-most pixel. Each line is therefore comprised of two 32-bit
  * words. The palette index for pixel 'a' is now formed from:
  *
  *    14                0
  *     ppp pppp aaaa aaaa
  *
  * Stencil Mask
  * ------------
  *
  * For any pixel position, there are in fact only two visible layers, despite
  * there being four defined layers. The layers are grouped in pairs: A (the
  * 'primary' layer) and A' (the 'alternate') form one pair, and B and B' form
  * the other. Only one of the primary or alternate layers from each group may
  * be visible at a given position. The 'stencil mask' controls this.
  *
  * The mask table is a bit field organized into 512 (or 384?) lines with each
  * bit controlling four columns (32 pixels). The mask does not appear to be
  * affected by scrolling -- that is, it does not scroll with the underlying
  * tiles, which do so independently. The mask remains fixed.
  *
  * Each mask entry is a little endian 32-bit word. The high 16 bits control
  * A/A' and the low 16 bits control B/B'. Each word controls an entire line
  * (32 pixels per bit, 512 pixels per 16-bit line mask, where the first 16
  * pixels are allocated to the overscan region.) If a bit is set to 1, the
  * pixel from the primary layer is used, otherwise the alternate layer is
  * used when the mask is 0. It is important to remember that the layers may
  * have been scrolled independently. The mask operates on the final resultant
  * two pixels that are determined for each location.
  *
  * Example of a line mask:
  *
  *    31                  15                 0
  *     0111 0000 0000 1111 0000 0000 1111 1111
  *
  * These settings would display layer A' for the first 32 pixels of the line,
  * followed by layer A for the next 96 pixels, A' for the subsequent 256
  * pixels, and A for the final 128 pixels. The first 256 pixels of the line
  * would display layer B' and the second 256 pixels would be from layer B.
  *
  * The stencil mask does not affect layer priorities, which are managed
  * separately regardless of mask settings.
  *
  * Scrolling
  * ---------
  *
  * Each of the four layers can be scrolled independently. Vertical scroll
  * values are stored in the appropriate scroll register and horizontal scroll
  * values can be sourced either from the register (in which case the entire
  * layer will be scrolled uniformly) or from a table in VRAM (which contains
  * independent values for each line).
  *
  * The scroll registers are laid out as:
  *
  *    31                                     0
  *     e??? ???y yyyy yyyy h??? ??xx xxxx xxxx
  *
  * The 'e' bit enables the layer when set. The 'y' bits comprise a vertical
  * scroll value in pixels. The 'x' bits form a horizontal scroll value. If 'h'
  * is set, then the VRAM table (line-by-line scrolling) is used, otherwise the
  * 'x' values are applied to every line. It is also possible that the scroll
  * values use more or less bits, but probably no more than 1.
  *
  * Each line must be wrapped back to the beginning of the same line. Likewise,
  * vertical scrolling wraps around back to the top of the tilemap.
  *
  * The horizontal scroll table is a series of 16-bit little endian words, one
  * for each line beginning at 0. It appears all the values can be used for
  * scrolling (no control bits have been observed). The number of bits actually
  * used by the hardware is irrelevant -- wrapping has the effect of making
  * higher order bits unimportant.
  *
  * Layer Priorities
  * ----------------
  *
  * The layer control register (0x20) contains 4 bits that appear to control
  * layer priorities. It is assumed that the 3D graphics, output by the Real3D
  * pixel processors independently of the tile generator, constitute their own
  * 'layer' and that the 2D tilemaps appear in front or behind. There may be a
  * specific function for each priority bit or the field may be interpreted as a
  * single 4-bit value denoting preset layer orders.
  *
  * Color Offsets
  * -------------
  *
  * Color offsets can be applied to the final RGB color value of every pixel.
  * This is used for effects such as fading to a certain color, lightning (Lost
  * World), etc. The current best guess is that the two registers control each
  * pair (A/A' and B/B') of layers. The format appears to be:
  *
  *    31                                     0
  *     ???? ???? rrrr rrrr gggg gggg bbbb bbbb
  *
  * Where 'r', 'g', and 'b' appear to be signed 8-bit color offsets. Because
  * they exceed the color resolution of the palette, they must be scaled
  * appropriately.
  *
  * Color offset registers are handled in TileGen.cpp. Two palettes are computed
  * -- one for A/A' and another for B/B'. These are passed to the renderer.
  */

#include "Render2D.h"
#include <algorithm>

#include "Supermodel.h"
#include "Shader.h"
#include "Shaders2D.h" // fragment and vertex shaders
#include "Stream/Satellite.h"   // 11/20 SatHud (satellite overlay)

/******************************************************************************
 Frame Display Functions
******************************************************************************/

// Set up viewport and OpenGL state for 2D rendering (sets up blending function but disables blending)
void CRender2D::Setup2D(bool isBottom)
{
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);  // alpha of 1.0 is opaque, 0 is transparent

	// Disable Z-buffering
	glDisable(GL_DEPTH_TEST);

	// Clear everything if requested or just overscan areas for wide screen mode
	if (isBottom)
	{
		if (m_aaTarget) {
			glBindFramebuffer(GL_FRAMEBUFFER, m_aaTarget);	// set target if needed
		}

		glClearColor(0.0, 0.0, 0.0, 0.0);
		glViewport	(0, 0, m_totalXPixels, m_totalYPixels);
		glDisable	(GL_SCISSOR_TEST);							// scissor is enabled to fix the 2d/3d miss match problem
		glClear		(GL_COLOR_BUFFER_BIT);						// we want to clear outside the scissored areas so must disable it
		glEnable	(GL_SCISSOR_TEST);

		if (m_aaTarget) {
			glBindFramebuffer(GL_FRAMEBUFFER, 0);			// restore target if needed
		}
	}

	// Set up the viewport and orthogonal projection
	bool stretchBottom = m_config["WideBackground"].ValueAs<bool>() && isBottom;
	if (!stretchBottom)
	{
		glViewport(m_xOffset - m_correction, m_yOffset + m_correction, m_xPixels, m_yPixels); //Preserve aspect ratio of tile layer by constraining and centering viewport
	}
}

void CRender2D::DrawSurface(GLuint textureID)
{
	if (m_aaTarget) {
		glBindFramebuffer(GL_FRAMEBUFFER, m_aaTarget);	// set target if needed
	}

	m_drawShader.EnableShader();

	glEnable			(GL_BLEND);
	glBindVertexArray	(m_vao);
	glActiveTexture		(GL_TEXTURE0); // texture unit 0
	glBindTexture		(GL_TEXTURE_2D, textureID);
	glDrawArrays		(GL_TRIANGLE_STRIP, 0, 4);
	glBindVertexArray	(0);
	glDisable			(GL_BLEND);

	m_drawShader.DisableShader();

	if (m_aaTarget) {
		glBindFramebuffer(GL_FRAMEBUFFER, 0);			// restore target if needed
	}
}

void CRender2D::BeginFrame(void)
{
}

void CRender2D::PreRenderFrame(void)
{
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 512);	// skip the non viewable data

	for (int i = 0; i < 2; i++) {
		if (!m_drawBuffers[i]) continue;		// we don't have a draw buffer yet
		glBindTexture(GL_TEXTURE_2D, m_textureIDs[i]);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 496, 384, GL_RGBA, GL_UNSIGNED_BYTE, m_drawBuffers[i]->data);
	}

	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

void CRender2D::RenderFrameBottom(void)
{
	Setup2D(true);
	DrawSurface(m_textureIDs[0]);
}

void CRender2D::RenderFrameTop(void)
{
	Setup2D(false);
	DrawSurface(m_textureIDs[1]);
}

void CRender2D::RenderSatelliteOverlay(const uint32_t *vramP, const uint32_t *pal, bool inGame, const std::string &title, const std::string &ticker, const SatHud *hud)
{
	const int W = 496, H = 384, S = 512;
	if (!vramP || !pal)
		return;
	if (!m_satTex)
	{
		glGenTextures(1, &m_satTex);
		glBindTexture(GL_TEXTURE_2D, m_satTex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, m_upscaleMode == UpscaleMode::Nearest ? GL_NEAREST : GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, m_upscaleMode == UpscaleMode::Nearest ? GL_NEAREST : GL_LINEAR);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	}
	m_satBuf.assign(W * H, 0);
	// 11/21 (owner: LOADING... never showed): our text uses the game's own font tiles and palette in VRAM, which are
	// replaced while an area / stage loads - so keep a copy taken in plain play and draw from that.
	{
		static uint32_t fontCopy[0x800], palCopy[256]; static bool haveFont = false;
		if (!haveFont && inGame && hud && hud->valid && !hud->loading && (vramP['A' * 16] | vramP['A' * 16 + 4] | vramP['A' * 16 + 8]))
		{
			memcpy(fontCopy, vramP, sizeof(fontCopy));
			memcpy(palCopy, pal, sizeof(palCopy));
			haveFont = true;
		}
		if (haveFont) { vramP = fontCopy; pal = palCopy; }
	}
	auto put = [&](int x, int y, uint32_t c) { if (x >= 0 && x < W && y >= 0 && y < H) m_satBuf[y * W + x] = c; };
	auto glyph = [&](int ch, int x0, int y0)
	{
		if (ch >= 'a' && ch <= 'z')
			ch -= 32;
		if (ch <= 0x20 || ch >= 0x7F)
			return;
		for (int y = 0; y < 8; y++)
			for (int x = 0; x < 8; x++)
			{
				uint32_t w = vramP[ch * 16 + y * 2 + x / 4];
				uint32_t c = pal[(w >> ((3 - (x % 4)) * 8)) & 0xFF];
				if (c >= 0x1000000)
				{
					put(x0 + x, y0 + y, c);
					if (y == 0) put(x0 + x, y0 - 1, 0xFF000000u);   // 11/24: the black top edge the tiles lack
				}
			}
	};
	auto text = [&](const std::string &t, int x, int y) { for (char ch : t) { glyph((unsigned char) ch, x, y); x += 8; } };
	// 11/21: text at sc x size; col != 0 paints the font's bright pixels in that colour (its dark edge stays dark)
	auto textC = [&](const std::string &t, int x0, int y0, int sc, uint32_t col)
	{
		for (size_t i = 0; i < t.size(); i++)
		{
			int ch = (unsigned char) t[i];
			if (ch >= 'a' && ch <= 'z') ch -= 32;
			if (ch <= 0x20 || ch >= 0x7F) continue;
			for (int y = 0; y < 8; y++)
				for (int x = 0; x < 8; x++)
				{
					uint32_t w = vramP[ch * 16 + y * 2 + x / 4];
					uint32_t c = pal[(w >> ((3 - (x % 4)) * 8)) & 0xFF];
					if (c < 0x1000000) continue;
					if (col)
					{
						int lum = (int) (c & 0xFF) + (int) ((c >> 8) & 0xFF) + (int) ((c >> 16) & 0xFF);
						c = lum < 200 ? 0xFF000000u : col;
					}
					for (int dy = 0; dy < sc; dy++)
						for (int dx = 0; dx < sc; dx++)
						{
							put(x0 + ((int) i * 8 + x) * sc + dx, y0 + y * sc + dy, c);
							if (y == 0) put(x0 + ((int) i * 8 + x) * sc + dx, y0 - sc + dy, 0xFF000000u);   // 11/24: top edge
						}
				}
		}
	};
	// 11/21 (owner): each player's name colour, learnt from the HUD while we follow them (slot 0-7; 0 = not seen yet)
	static uint32_t s_slotCol[8] = {};

	// title, top centre
	// 11/21 (owner): the title 3x tall, as wide as fits (up to 3x)
	{
		const int sy = 3, n = (int) title.size();
		const float sx = n > 0 ? std::min(3.0f, (float) (W - 8) / (float) (n * 8)) : 3.0f;
		const int tx0 = (W - (int) (n * 8 * sx)) / 2;
		for (int i = 0; i < n; i++)
		{
			int ch = (unsigned char) title[i];
			if (ch >= 'a' && ch <= 'z') ch -= 32;
			if (ch <= 0x20 || ch >= 0x7F) continue;
			for (int y = 0; y < 8; y++)
				for (int x = 0; x < 8; x++)
				{
					uint32_t w = vramP[ch * 16 + y * 2 + x / 4];
					uint32_t c = pal[(w >> ((3 - (x % 4)) * 8)) & 0xFF];
					if (c < 0x1000000) continue;
					const int px0 = tx0 + (int) ((i * 8 + x) * sx), px1 = tx0 + (int) ((i * 8 + x + 1) * sx);
					for (int dy = 0; dy < sy; dy++)
						for (int px = px0; px < px1; px++)
						{
							put(px, 4 + y * sy + dy, c);
							if (y == 0) put(px, 4 - sy + dy, 0xFF000000u);   // 11/24: top edge
						}
				}
		}
	}

	// the followed player's name, centred below the title. 11/21 (owner): drawn as text from the name table (SatHud::name)
	// at 3x, in the player's colour: the most common bright colour of the FIRST letter of the game's own HUD name (x 40-127,
	// y 8-23 of the top layer). Cut out of the HUD as a picture, an old longer name's tail stayed (LINDA + TENSHIN's "IN").
	const int nameY = 32, nameSc = 3;   // 11/21: below the 3x title (4-27)
	if (inGame && hud && hud->valid && hud->name[0])
	{
		uint32_t col = 0xFFFFFFFFu;
		// 11/22 (owner: the name was pale): the colour of the game's name image for that player (as the ally list) first;
		// the HUD sample below only when it is not known yet (the HUD shows the clone's own player, not the shown one).
		if (hud->selfCol) col = hud->selfCol | 0xFF000000u;   // (not kept as the player's fallback: it is the boss's while a boss is shown)
		else if (m_drawBuffers[1])
		{
			const uint32_t *src = m_drawBuffers[1]->data;
			int fx = -1;
			for (int x = 40; x < 128 && fx < 0; x++)
				for (int y = 8; y < 24; y++)
					if (src[y * S + x] >= 0x1000000) { fx = x; break; }
			if (fx >= 0)
			{
				uint32_t cs[8] = {}; int cn[8] = {}, nc = 0;
				for (int y = 8; y < 24; y++)
					for (int x = fx; x < fx + 7; x++)
					{
						uint32_t c = src[y * S + x];
						if (c < 0x1000000) continue;
						int lum = (int) (c & 0xFF) + (int) ((c >> 8) & 0xFF) + (int) ((c >> 16) & 0xFF);
						if (lum < 200) continue;                           // the dark outline
						int k = 0;
						while (k < nc && cs[k] != c) k++;
						if (k == nc) { if (nc == 8) continue; cs[nc] = c; cn[nc++] = 0; }
						cn[k]++;
					}
				int best = -1;
				for (int k = 0; k < nc; k++) if (best < 0 || cn[k] > cn[best]) best = k;
				if (best >= 0) col = cs[best] | 0xFF000000u;
				if (best >= 0 && hud->self >= 1 && hud->self <= 8) s_slotCol[hud->self - 1] = col;
			}
		}
		const std::string nm = std::string(hud->name) + (hud->selfBot ? "_BOT" : "");   // 11/21: "_BOT" for a bot cabinet
		const int x0 = (W - (int) nm.size() * 8 * nameSc) / 2;
		for (size_t i = 0; i < nm.size(); i++)
		{
			int ch = (unsigned char) nm[i];
			if (ch >= 'a' && ch <= 'z') ch -= 32;
			if (ch <= 0x20 || ch >= 0x7F) continue;
			for (int y = 0; y < 8; y++)
				for (int x = 0; x < 8; x++)
				{
					uint32_t w = vramP[ch * 16 + y * 2 + x / 4];
					uint32_t c = pal[(w >> ((3 - (x % 4)) * 8)) & 0xFF];
					if (c < 0x1000000) continue;
					int lum = (int) (c & 0xFF) + (int) ((c >> 8) & 0xFF) + (int) ((c >> 16) & 0xFF);
					uint32_t o = lum < 200 ? 0xFF000000u : col;        // the font's dark edge stays dark, the rest in the colour
					for (int dy = 0; dy < nameSc; dy++)
						for (int dx = 0; dx < nameSc; dx++)
						{
							put(x0 + ((int) i * 8 + x) * nameSc + dx, nameY + y * nameSc + dy, o);
							if (y == 0) put(x0 + ((int) i * 8 + x) * nameSc + dx, nameY - nameSc + dy, 0xFF000000u);   // 11/24: top edge
						}
				}
		}
	}

	// 11/20 (owner): STAGE - AREA top left, the followed player's life under the name, the bosses above the ticker.
	// Colours are 0xAABBGGRR (the buffer goes up as RGBA bytes).
	auto bar = [&](int x0, int y0, int w, int h, int v, int vMax, uint32_t fill)
	{
		if (vMax <= 0)
			return;
		int f = (int) ((long long) (v < 0 ? 0 : v > vMax ? vMax : v) * (w - 2) / vMax);
		for (int y = y0; y < y0 + h; y++)
			for (int x = x0; x < x0 + w; x++)
			{
				bool edge = y == y0 || y == y0 + h - 1 || x == x0 || x == x0 + w - 1;
				put(x, y, edge ? 0xFFD8D8D8u : (x - x0 - 1 < f ? fill : 0xC0000000u));
			}
	};
	if (inGame && hud && hud->valid)
	{
		std::string sa = "STAGE " + std::to_string(hud->stage) + " - AREA " + std::to_string(hud->area);
		text(sa, 4, 66);   // 11/21: under the name / life bar row (a 3x "TENSHIN_BOT" reaches the left side)

		const int bw = 128;
		int r = hud->hpMax > 0 ? hud->hp * 100 / hud->hpMax : 0;
		uint32_t col = r > 50 ? 0xFF40E040u : r > 25 ? 0xFF20D0F0u : 0xFF3030F0u;   // green / yellow / red
		bar((W - bw) / 2, nameY + 8 * nameSc + 4, bw, 6, hud->hp, hud->hpMax, col);   // 11/21: under the 3x name

		// 11/21 (owner): the other players on the left - name, then a life bar (same colours as the followed one)
		for (int i = 0; i < hud->nAlly && i < 8; i++)   // 11/22: 8 (all players while a boss is shown)
		{
			const SatHudBoss &a = hud->ally[i];
			const int ay = 80 + i * 12;   // 11/21: below STAGE - AREA
			const int as = hud->allySlot[i];
			const uint32_t ac0 = hud->allyCol[i] ? hud->allyCol[i] : ((as >= 0 && as < 8) ? s_slotCol[as] : 0u);
			textC(std::string(a.name) + (hud->allyBot[i] ? "_BOT" : ""), 4, ay, 1, ac0);   // 11/21: their colour, "_BOT" for bots
			int ar = a.hpMax > 0 ? a.hp * 100 / a.hpMax : 0;
			uint32_t ac = ar > 50 ? 0xFF40E040u : ar > 25 ? 0xFF20D0F0u : 0xFF3030F0u;
			bar(4 + 12 * 8, ay + 1, 64, 6, a.hp, a.hpMax, ac);   // 11/21: after up to 11 letters (TENSHIN_BOT)
			// 11/21 (owner): their special stock as lightning marks after the bar - drawn here (the game's font has no such
			// mark): 8x8, yellow with a dark edge, one per stock (5 at most).
			{
				static const char *bolt[8] = {
					"...##...",
					"..##....",
					".##.....",
					"#####...",
					"...##...",
					"..##....",
					".##.....",
					"##......" };
				const int sp = hud->allySp[i] < 0 ? 0 : hud->allySp[i] > 5 ? 5 : hud->allySp[i];
				for (int k = 0; k < sp; k++)
				{
					const int bx = 4 + 12 * 8 + 64 + 4 + k * 8, by = ay;
					for (int y = 0; y < 8; y++)
						for (int x = 0; x < 8; x++)
						{
							if (bolt[y][x] != '#') continue;
							for (int oy = -1; oy <= 1; oy++)   // dark edge round the mark
								for (int ox = -1; ox <= 1; ox++)
								{
									int nx = x + ox, ny = y + oy;
									bool inside = nx >= 0 && nx < 8 && ny >= 0 && ny < 8 && bolt[ny][nx] == '#';
									if (!inside && bx + nx >= 0 && bx + nx < W && by + ny >= 0 && by + ny < H && m_satBuf[(by + ny) * W + bx + nx] == 0)
										put(bx + nx, by + ny, 0xFF000000u);
								}
							put(bx + x, by + y, 0xFF20E0FFu);   // yellow (0xAABBGGRR)
						}
				}
			}
		}

		const int bbw = 144, rowH = 22, top = H - 20 - 2 - 4;   // above the 2x ticker band; 11/22: two lines a boss (was 12)
		for (int i = 0; i < hud->nBoss && i < 4; i++)
		{
			const SatHudBoss &b = hud->boss[i];
			std::string nm = b.name;
			int tw = (int) nm.size() * 8, gap = nm.empty() ? 0 : 6;
			int x = (W - (tw + gap + bbw)) / 2, y = top - rowH * (hud->nBoss - i);
			text(nm, x, y);
			bar(x + tw + gap, y + 1, bbw, 6, b.hp, b.hpMax, 0xFF2058F0u);   // orange-red
			// 11/22 (owner): its move under the bar - the action number, then its steps (^ v < > for the lever, then
			// +B +C +S +J; "-" = let go), centred. The step it is on in yellow; the rest white, red while its attack is out.
			const int n = hud->bossMvN[i] < 0 ? 0 : hud->bossMvN[i] > 16 ? 16 : hud->bossMvN[i];
			if (n > 0)
			{
				char an[8]; snprintf(an, sizeof(an), "%02X ", (unsigned) hud->bossMvAct[i] & 0xFF);
				std::vector<std::string> st;
				for (int k = 0; k < n; k++)
				{
					const uint8_t v = hud->bossMvSeq[i][k];
					std::string s;
					if (v & 0x20) s += '^';
					if (v & 0x10) s += 'v';
					if (v & 0x80) s += '<';
					if (v & 0x40) s += '>';
					const char *bn[4] = { "B", "C", "S", "J" };
					for (int q = 0; q < 4; q++) if (v & (1 << q)) { if (!s.empty()) s += '+'; s += bn[q]; }
					st.push_back(s.empty() ? "-" : s);
				}
				int len = (int) strlen(an);
				for (int k = 0; k < n; k++) len += (int) st[k].size() + (k ? 1 : 0);
				int mx = (W - len * 8) / 2;
				const int my = y + 10;
				const uint32_t base = hud->bossMvAtk[i] ? 0xFF3030F0u : 0xFFFFFFFFu;   // red while the attack is out
				textC(an, mx, my, 1, 0xFFC0C0C0u); mx += (int) strlen(an) * 8;
				for (int k = 0; k < n; k++)
				{
					if (k) { textC(",", mx, my, 1, 0xFFC0C0C0u); mx += 8; }
					textC(st[k], mx, my, 1, k == hud->bossMvStep[i] ? 0xFF20E0FFu : base);   // yellow = the step now
					mx += (int) st[k].size() * 8;
				}
			}
		}
	}

	// a line scrolling right to left at the bottom, on a dark band
	if (!ticker.empty())
	{
		// 11/21 (owner): 2x
		// 11/24: the text can change (stage / area / players): it is taken at the start of each pass and kept to its end
		static std::string pass; static unsigned passStart = 0;
		if (pass.empty() || m_satFrame - passStart >= (unsigned) (W + (int) pass.size() * 16)) { pass = ticker; passStart = m_satFrame; }
		int by = H - 20;
		for (int y = by - 2; y < by + 18; y++)
			for (int x = 0; x < W; x++)
				put(x, y, 0x90000000u);
		int x = W - (int) (m_satFrame - passStart);
		textC(pass, x, by, 2, 0);
	}
	// 11/21 (owner): LOADING... in the middle, white, 4x (a join / an area or stage change)
	if (inGame && hud && hud->valid && hud->loading)
	{
		const std::string ld = "LOADING...";
		textC(ld, (W - (int) ld.size() * 32) / 2, (H - 32) / 2, 4, 0xFFFFFFFFu);
	}
	m_satFrame++;

	glBindTexture(GL_TEXTURE_2D, m_satTex);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, m_satBuf.data());
	Setup2D(false);
	DrawSurface(m_satTex);
}

void CRender2D::EndFrame(void)
{
}


/******************************************************************************
 Emulation Callbacks
******************************************************************************/

// Deprecated
void CRender2D::WriteVRAM(unsigned addr, uint32_t data)
{
}


/******************************************************************************
 Configuration, Initialization, and Shutdown
******************************************************************************/

void CRender2D::AttachRegisters(const uint32_t* regPtr)
{
}

void CRender2D::AttachPalette(const uint32_t* palPtr[2])
{
}

void CRender2D::AttachVRAM(const uint8_t* vramPtr)
{
}

void CRender2D::AttachDrawBuffers(std::shared_ptr<TileGenBuffer> bottom, std::shared_ptr<TileGenBuffer> top)
{
	m_drawBuffers[0] = bottom;
	m_drawBuffers[1] = top;
}

Result CRender2D::Init(unsigned xOffset, unsigned yOffset, unsigned xRes, unsigned yRes, unsigned totalXRes, unsigned totalYRes, unsigned aaTarget, UpscaleMode upscaleMode)
{
	// Resolution
	m_xPixels		= xRes;
	m_yPixels		= yRes;
	m_xOffset		= xOffset;
	m_yOffset		= yOffset;
	m_totalXPixels	= totalXRes;
	m_totalYPixels	= totalYRes;
	m_correction	= (UINT32)(((yRes / 384.) * 2.) + 0.5);		// for some reason the 2d layer is 2 pixels off the 3D
	m_aaTarget		= aaTarget;
	m_upscaleMode	= (m_xPixels == 496 && m_yPixels == 384) ? UpscaleMode::Nearest : upscaleMode;

	std::string um = "#define UPSCALEMODE " + std::to_string((int)m_upscaleMode) + '\n';

	m_drawShader.LoadShaders(s_vertexShader, (std::string(s_fragmentShaderHeader) + um + s_fragmentShader).c_str());
	m_drawShader.GetUniformLocationMap("tex1");
	// init uniform memory
	m_drawShader.EnableShader();
	glUniform1i(m_drawShader.uniformLocMap["tex1"], 0);	// texture bound to texture unit 0
	m_drawShader.DisableShader();

	// allocate storage
	for (auto& t : m_textureIDs) {
		glBindTexture(GL_TEXTURE_2D, t);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, m_upscaleMode == UpscaleMode::Nearest ? GL_NEAREST : GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, m_upscaleMode == UpscaleMode::Nearest ? GL_NEAREST : GL_LINEAR);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 496, 384, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	}

	return Result::OKAY;
}

CRender2D::CRender2D(const Util::Config::Node& config)
	: m_config(config),
	m_vao(0)
{
	glGenVertexArrays(1, &m_vao);
	glBindVertexArray(m_vao);
	// no states needed since we do it in the shader
	glBindVertexArray(0);

	// create textures
	glGenTextures(2, m_textureIDs);
}

CRender2D::~CRender2D(void)
{
	if (m_vao) {
		glDeleteVertexArrays(1, &m_vao);
		m_vao = 0;
	}

	for (auto& t : m_textureIDs) {
		glDeleteTextures(1, &t);
		t = 0;
	}
	if (m_satTex) {
		glDeleteTextures(1, &m_satTex);
		m_satTex = 0;
	}
}
