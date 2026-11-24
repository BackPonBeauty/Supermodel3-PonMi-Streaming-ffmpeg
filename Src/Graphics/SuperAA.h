#pragma once

#include "Supermodel.h"
#include "FBO.h"
#include "New3D/GLSLShader.h"
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>
#include <atomic>

#include "IEncoder.h"
#include "NvencEncoder.h"
#include "FfmpegEncoder.h"   // 11/16: AMD AMF removed (owner: no streaming with AMF; FFMPEG works on any GPU)

// This class just implements super sampling. Super sampling looks fantastic but is quite expensive.
// 8x and beyond values can start to eat ridiculous amounts of memory / gpu time, for less and less noticable returns
// 4x works and looks great
// values such as 3 are also possible, that works out 9 samples per pixel
// The algorithm is super simple, just add up all samples and divide by the number

class SuperAA
{
public:
	SuperAA(int aaValue, CRTcolor CRTcolors, bool scanLine, int scanlineStrength, int totalXRes, int totalYRes, int barrelStrength, const char *gameTitle, bool wideScreen, bool overlay, const char *configFilePath);
	~SuperAA();

	void Init(int width, int height, int port, bool streamingEnabled = false, const std::string &codec = "H265", const std::string &encoderType = "AUTO");
	void Draw();					  // this is a no-op if AA is 1 and CRTcolors 0, since we'll be drawing straight on the back buffer anyway

	GLuint GetTargetID();
	GLint m_locScanlineEnable = -1;
	GLint m_locScanlineStrength = -1;
	GLint m_locBarrelEffectEnable = -1; 
	GLint m_locBarrelStrength = -1;		
	GLint m_locMixStrength = -1;
	GLint m_locTex1 = -1;				
	GLint m_locUAspect = -1;
	IEncoder& GetEncoder() { return *m_encoder; }

	// ===== Toggle Methods (on/off) =====
	void ToggleScanline();
	void ToggleBarrelEffect();
	void ToggleMixEffect();
	void ToggleOverlay();

	// ===== Strength Adjustment Methods =====
	void IncreaseScanlineStrength();
	void DecreaseScanlineStrength();
	void IncreaseBarrelStrength();
	void DecreaseBarrelStrength();
	void IncreaseMixStrength();
	void DecreaseMixStrength();


	// ===== Getter Methods =====
	float GetScanlineStrength() const;
	float GetBarrelStrength() const;

	// ===== Legacy Methods =====
	void SetScanlineEnable(bool enable);
	bool IsScanlineEnabled() const;
	float BarrelStrength();

	void LoadOverlayByTitle(const std::string &gameTitle);
	
	void InitFrameRingBuffer(int width, int height);
	void UpdateFrameRingBuffer();
	bool IsStreamingEnabled() const { return m_streamingEnabled; }
	// 11/24 (owner) REMOTE slot: encode only while a stream client is in (a netlink client or nobody: no encoding)
	void SetEncodeOn(bool on) { m_encodeOn.store(on); }
	bool IsEncodeOn() const { return m_encodeOn.load(); }

	// Pre-encode callback (renders ImGui overlay into fbo2 before encoding)
	using PreEncodeCallback = std::function<void(GLuint fboID, int width, int height)>;
	void SetPreEncodeCallback(PreEncodeCallback cb) { m_preEncodeCallback = cb; }
	// 11/23 netlink: drawn on the window itself (framebuffer 0, after the frame is shown there) - not into the stream
	void SetLocalOverlayCallback(PreEncodeCallback cb) { m_localOverlayCallback = cb; }
private:
	FBO m_fbo;
	FBO m_fbo2;
	GLSLShader m_shader;
	GLSLShader m_overlayShader;
	const int m_aa;
	const CRTcolor m_crtcolors;
	bool m_scanlineEnable;
	float m_scanlineStrength;
	bool m_barrelEffectEnable;
	float m_barrelStrength;
	float m_MixStrength;
	int m_totalXRes;
	int m_totalYRes;
	GLuint m_vao;
	int m_width;
	int m_height;
	GLuint m_overlayTex = 0;
	bool m_wideScreen;
	bool m_overlay;
	std::string m_configFilePath;
	void SaveToINI();
	// Frame delay ring buffer
	static const int RING_BUFFER_SIZE = 10;
	
	GLuint m_frameRingBuffer[RING_BUFFER_SIZE]; // 2 textures
	int m_ringBufferIndex;						// Current write index

	int m_frameCounter;		// Frame counter
	bool m_mixEnabled;		// Mix enable flag
	GLint m_locOldFrameTex1; // Location of uOldFrameTex1
    GLint m_locOldFrameTex2; // Location of uOldFrameTex2
    //GLint m_locOldFrameTex3;
	GLint m_locMixEnabled;	// Location of uMixEnabled
    IEncoder* m_encoder = nullptr;
    bool m_streamingEnabled = false;
    std::atomic<bool> m_encodeOn{true};
    PreEncodeCallback m_preEncodeCallback;
    PreEncodeCallback m_localOverlayCallback;
};
