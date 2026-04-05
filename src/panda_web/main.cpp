// WASM / Emscripten web frontend for Panda3DS
// Compiled via build_wasm.sh; produces panda3ds.js + panda3ds.wasm
// that are loaded by the React app in web/.

#include <emscripten.h>
#include <emscripten/html5.h>
#include <SDL.h>
#include <glad/gl.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "emulator.hpp"
#include "input_mappings.hpp"
#include "screen_layout.hpp"
#include "services/hid.hpp"

// ─── global state ─────────────────────────────────────────────────────────────

static Emulator* g_emu = nullptr;
static SDL_Window* g_window = nullptr;
static SDL_GLContext g_glContext = nullptr;
static InputMappings g_keyboardMappings;
static ScreenLayout::WindowCoordinates g_screenCoords;
static bool g_romLoaded = false;
static int g_windowWidth = 400;
static int g_windowHeight = 480;

// Track which circle-pad directions are currently pressed by keyboard so we
// can zero out the axis when all keys for that axis are released.
static bool g_cpRight = false, g_cpLeft = false;
static bool g_cpUp = false, g_cpDown = false;

// ─── helpers ──────────────────────────────────────────────────────────────────

static void handleTouchDown(HIDService& hid, int mouseX, int mouseY) {
	const auto& c = g_screenCoords;
	const int bx = int(c.bottomScreenX);
	const int by = int(c.bottomScreenY);
	const int bw = int(c.bottomScreenWidth);
	const int bh = int(c.bottomScreenHeight);

	if (mouseX >= bx && mouseX < bx + bw && mouseY >= by && mouseY < by + bh) {
		float relX = float(mouseX - bx) / float(bw);
		float relY = float(mouseY - by) / float(bh);
		u16 tx = u16(std::clamp(relX * ScreenLayout::BOTTOM_SCREEN_WIDTH, 0.f, float(ScreenLayout::BOTTOM_SCREEN_WIDTH - 1)));
		u16 ty = u16(std::clamp(relY * ScreenLayout::BOTTOM_SCREEN_HEIGHT, 0.f, float(ScreenLayout::BOTTOM_SCREEN_HEIGHT - 1)));
		hid.setTouchScreenPress(tx, ty);
	} else {
		hid.releaseTouchScreen();
	}
}

static void recalcCoords() {
	ScreenLayout::calculateCoordinates(
		g_screenCoords, u32(g_windowWidth), u32(g_windowHeight),
		g_emu->getConfig().topScreenSize,
		g_emu->getConfig().screenLayout
	);
}

// ─── main loop ────────────────────────────────────────────────────────────────

static void mainLoop() {
	// Poll events even when no ROM is loaded so the browser tab stays responsive
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		namespace Keys = HID::Keys;

		if (!g_emu) continue;
		HIDService& hid = g_emu->getServiceManager().getHID();

		switch (event.type) {
			case SDL_QUIT:
				emscripten_cancel_main_loop();
				return;

			case SDL_KEYDOWN: {
				if (!g_romLoaded) break;
				u32 key = g_keyboardMappings.getMapping(event.key.keysym.sym);
				if (key == Keys::Null) break;

				switch (key) {
					case Keys::CirclePadRight: hid.setCirclepadX(0x9C);  g_cpRight = true; break;
					case Keys::CirclePadLeft:  hid.setCirclepadX(-0x9C); g_cpLeft  = true; break;
					case Keys::CirclePadUp:    hid.setCirclepadY(0x9C);  g_cpUp    = true; break;
					case Keys::CirclePadDown:  hid.setCirclepadY(-0x9C); g_cpDown  = true; break;
					default: hid.pressKey(key); break;
				}
				break;
			}

			case SDL_KEYUP: {
				if (!g_romLoaded) break;
				u32 key = g_keyboardMappings.getMapping(event.key.keysym.sym);
				if (key == Keys::Null) break;

				switch (key) {
					case Keys::CirclePadRight: g_cpRight = false; if (!g_cpLeft)  hid.setCirclepadX(0); break;
					case Keys::CirclePadLeft:  g_cpLeft  = false; if (!g_cpRight) hid.setCirclepadX(0); break;
					case Keys::CirclePadUp:    g_cpUp    = false; if (!g_cpDown)  hid.setCirclepadY(0); break;
					case Keys::CirclePadDown:  g_cpDown  = false; if (!g_cpUp)    hid.setCirclepadY(0); break;
					default: hid.releaseKey(key); break;
				}
				break;
			}

			case SDL_MOUSEBUTTONDOWN:
				if (!g_romLoaded) break;
				if (event.button.button == SDL_BUTTON_LEFT) {
					handleTouchDown(hid, event.button.x, event.button.y);
				}
				break;

			case SDL_MOUSEBUTTONUP:
				if (!g_romLoaded) break;
				if (event.button.button == SDL_BUTTON_LEFT) {
					hid.releaseTouchScreen();
				}
				break;

			case SDL_MOUSEMOTION:
				if (!g_romLoaded) break;
				if (event.motion.state & SDL_BUTTON_LMASK) {
					handleTouchDown(hid, event.motion.x, event.motion.y);
				}
				break;

			case SDL_WINDOWEVENT:
				if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
					g_windowWidth  = event.window.data1;
					g_windowHeight = event.window.data2;
					g_emu->setOutputSize(u32(g_windowWidth), u32(g_windowHeight));
					recalcCoords();
				}
				break;

			default: break;
		}
	}

	if (g_emu && g_romLoaded) {
		g_emu->runFrame();
		SDL_GL_SwapWindow(g_window);
	}
}

// ─── exported C API (called from JavaScript) ─────────────────────────────────

extern "C" {

/// Receive ROM bytes from JS, write to MEMFS, then boot it.
EMSCRIPTEN_KEEPALIVE
void loadROMData(const uint8_t* data, int size) {
	if (!g_emu) {
		printf("[panda3ds] Emulator not ready\n");
		return;
	}

	// Write ROM into the in-memory filesystem
	const char* romPath = "/tmp/game.3ds";
	FILE* f = fopen(romPath, "wb");
	if (!f) {
		printf("[panda3ds] Cannot open %s for writing\n", romPath);
		return;
	}
	fwrite(data, 1, size, f);
	fclose(f);

	if (g_emu->loadROM(std::filesystem::path(romPath))) {
		g_romLoaded = true;
		printf("[panda3ds] ROM loaded successfully (%d bytes)\n", size);
	} else {
		printf("[panda3ds] Failed to load ROM\n");
	}
}

/// Returns 1 once a ROM has been loaded successfully.
EMSCRIPTEN_KEEPALIVE
int isROMLoaded() {
	return g_romLoaded ? 1 : 0;
}

} // extern "C"

// ─── entry point ─────────────────────────────────────────────────────────────

int main() {
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO) < 0) {
		printf("[panda3ds] SDL_Init failed: %s\n", SDL_GetError());
		return 1;
	}

	// Route SDL keyboard events to our canvas element
	SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "#canvas");

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	g_window = SDL_CreateWindow(
		"Panda3DS",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		g_windowWidth, g_windowHeight,
		SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
	);
	if (!g_window) {
		printf("[panda3ds] Window creation failed: %s\n", SDL_GetError());
		return 1;
	}

	g_glContext = SDL_GL_CreateContext(g_window);
	if (!g_glContext) {
		printf("[panda3ds] GL context creation failed: %s\n", SDL_GetError());
		return 1;
	}

	// Load OpenGL ES function pointers via GLAD
	if (!gladLoadGLES2Loader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
		printf("[panda3ds] GLAD init failed\n");
		return 1;
	}

	SDL_GL_SetSwapInterval(1);

	g_emu = new Emulator();
	g_emu->initGraphicsContext(g_window);
	g_emu->setOutputSize(u32(g_windowWidth), u32(g_windowHeight));

	g_keyboardMappings = InputMappings::defaultKeyboardMappings();
	recalcCoords();

	printf("[panda3ds] Emulator initialized. Drop a ROM file to start.\n");

	// Hand control to the browser's requestAnimationFrame loop.
	// Passing 0 as FPS lets the browser cap it (usually 60 fps via rAF).
	emscripten_set_main_loop(mainLoop, 0, 1);

	return 0;
}
