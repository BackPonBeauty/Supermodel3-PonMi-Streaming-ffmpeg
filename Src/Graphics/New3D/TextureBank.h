#pragma once

#ifndef _TEXTUREBANK_H_
#define _TEXTUREBANK_H_

#include "Types.h"
#include <GL/glew.h>

// texture banks are a fixed size
// 2048x1024 pixels, each pixel is 16bits in size

namespace New3D {

	class TextureBank
	{
	public:
		TextureBank();
		~TextureBank();

		void AttachMemory(const UINT16* textureRam);
		// 11/23 Spikeout ghost pages: levels 1.. come from a sheet of their own (the ghost page's own mipmap quadrant
		// holds costumes at level 0, so its reduced levels cannot share the sheet). nullptr = the same sheet.
		void AttachMipMemory(const UINT16* mipRam) { m_mipRam = mipRam; }
		bool HasMipMemory() const { return m_mipRam != nullptr; }
		// 11/23: levels 1.. made from level 0 (the texel at (x << L, y << L)) - for a whole-sheet upload of a ghost
		// page, where the stored reduced levels of different costumes overlap
		void UploadMipsFromLevel0();
		void Bind();
		void UploadTextures(int level, int x, int y, int width, int height);
		int GetNumberOfLevels() const;

	private:
		const UINT16* m_textureRam = nullptr;
		const UINT16* m_mipRam = nullptr;
		GLuint m_texID = 0;
		int m_numLevels = 0;
	};

}

#endif
