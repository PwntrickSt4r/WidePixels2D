// WidePixels2D Engine - v1.0.0 (Release)
#include <iostream>
#include <fstream>   
#include <string>    
#include <vector>
#include <chrono>    
#include <ctime>     
#include <climits>   
#include <algorithm>
#include <memory>
#include <windows.h> 
#include <SDL.h>

std::ofstream logFile;

#define RETRO_DEVICE_ID_JOYPAD_B      0
#define RETRO_DEVICE_ID_JOYPAD_Y      1
#define RETRO_DEVICE_ID_JOYPAD_SELECT 2
#define RETRO_DEVICE_ID_JOYPAD_START  3
#define RETRO_DEVICE_ID_JOYPAD_UP     4
#define RETRO_DEVICE_ID_JOYPAD_DOWN   5
#define RETRO_DEVICE_ID_JOYPAD_LEFT   6
#define RETRO_DEVICE_ID_JOYPAD_RIGHT  7
#define RETRO_DEVICE_ID_JOYPAD_A      8

enum retro_pixel_format {
	RETRO_PIXEL_FORMAT_0RGB1555 = 0,
	RETRO_PIXEL_FORMAT_XRGB8888 = 1,
	RETRO_PIXEL_FORMAT_RGB565 = 2,
	RETRO_PIXEL_FORMAT_UNKNOWN = INT_MAX
};

struct retro_game_info {
	const char *path;
	const void *data;
	size_t      size;
	const char *meta;
};

#define CORE_CALL __cdecl

typedef void (CORE_CALL *retro_init_t)(void);
typedef void (CORE_CALL *retro_deinit_t)(void);
typedef void (CORE_CALL *retro_set_environment_t)(bool (CORE_CALL *cb)(unsigned cmd, void *data));
typedef void (CORE_CALL *retro_set_video_refresh_t)(void (CORE_CALL *cb)(const void *data, unsigned width, unsigned height, size_t pitch));
typedef void (CORE_CALL *retro_set_audio_sample_batch_t)(size_t(CORE_CALL *cb)(const int16_t *data, size_t frames));
typedef void (CORE_CALL *retro_set_input_poll_t)(void (CORE_CALL *cb)(void));
typedef void (CORE_CALL *retro_set_input_state_t)(int16_t(CORE_CALL *cb)(unsigned port, unsigned device, unsigned index, unsigned id));
typedef bool (CORE_CALL *retro_load_game_t)(const void* game);
typedef void (CORE_CALL *retro_run_t)(void);

HMODULE coreDescriptor = NULL;
retro_init_t r_init = nullptr;
retro_deinit_t r_deinit = nullptr;
retro_set_environment_t r_set_environment = nullptr;
retro_set_video_refresh_t r_set_video_refresh = nullptr;
retro_set_audio_sample_batch_t r_set_audio_sample_batch = nullptr;
retro_set_input_poll_t r_set_input_poll = nullptr;
retro_set_input_state_t r_set_input_state = nullptr;
retro_load_game_t r_load_game = nullptr;
retro_run_t r_run = nullptr;

SDL_Window* window = nullptr;
SDL_Renderer* renderer = nullptr;
SDL_Texture* texture = nullptr;
SDL_Texture* scanlineOverlay = nullptr;
SDL_AudioDeviceID audioDevice = 0;

unsigned int currentVideoWidth = 320;
unsigned int currentVideoHeight = 224;

bool cfg_AudioEnabled = true;
bool cfg_HideDevConsole = true;

bool cfg_FastForwardAllowed = true;
unsigned int cfg_FastForwardSpeed = 3;

bool cfg_Scanlines = false;
unsigned int cfg_ScanlineIntensity = 60;

bool inputState_Up = false;
bool inputState_Down = false;
bool inputState_Left = false;
bool inputState_Right = false;
bool inputState_ButtonS = false;
bool inputState_Space = false;
bool inputState_Enter = false;
bool inputState_Backspace = false;

const double TARGET_FPS = 60.0;
const double FRAME_TARGET_MS = 1000.0 / TARGET_FPS;

std::string GetCurrentTimestamp() {
	auto now = std::chrono::system_clock::now();
	auto in_time_t = std::chrono::system_clock::to_time_t(now);
	struct tm buf;
	if (localtime_s(&buf, &in_time_t) != 0) return "[00:00:00]";
	char timeStr[32];
	sprintf_s(timeStr, "[%02d:%02d:%02d]", buf.tm_hour, buf.tm_min, buf.tm_sec);
	return std::string(timeStr);
}

void WriteLog(const std::string& type, const std::string& message) {
	std::string fullLine = GetCurrentTimestamp() + " [" + type + "] " + message;
	std::cout << fullLine << std::endl;
	if (logFile.is_open()) {
		logFile << fullLine << std::endl;
		logFile.flush();
	}
}

std::string GetExecutionDirectory() {
	char exePath[MAX_PATH];
	if (GetModuleFileNameA(NULL, exePath, MAX_PATH) == 0) return "";
	std::string currentDir(exePath);
	size_t lastSlash = currentDir.find_last_of("\\/");
	if (lastSlash != std::string::npos) {
		return currentDir.substr(0, lastSlash);
	}
	return "";
}

bool ParseBoolString(const char* section, const char* key, const char* defaultVal, const std::string& iniPath) {
	char buf[8];
	GetPrivateProfileStringA(section, key, defaultVal, buf, sizeof(buf), iniPath.c_str());
	std::string val(buf);
	std::transform(val.begin(), val.end(), val.begin(), ::tolower);
	return (val == "y");
}

void LoadOrCreateConfiguration() {
	std::string currentDir = GetExecutionDirectory();
	if (currentDir.empty()) return;

	std::string iniPath = currentDir + "\\settings.ini";

	DWORD fileAttr = GetFileAttributesA(iniPath.c_str());
	if (fileAttr == INVALID_FILE_ATTRIBUTES) {
		std::ofstream createIni(iniPath, std::ios::out | std::ios::trunc);
		if (createIni.is_open()) {
			createIni << "[General]\n";
			createIni << "AudioEnabled=y\n\n";
			createIni << "[Additional]\n";
			createIni << "HideDevConsole=y\n";
			createIni << "FastForward=y\n";
			createIni << "FastForwardSpeed=3\n\n";
			createIni << "[Video Filters]\n";
			createIni << "Scanlines=n\n";
			createIni << "ScanlineIntensity=60\n";
			createIni.close();
			WriteLog("INTEGRITY", "Default settings.ini generated successfully.");
		}
	}

	cfg_AudioEnabled = ParseBoolString("General", "AudioEnabled", "y", iniPath);
	cfg_HideDevConsole = ParseBoolString("Additional", "HideDevConsole", "y", iniPath);
	cfg_FastForwardAllowed = ParseBoolString("Additional", "FastForward", "y", iniPath);

	cfg_FastForwardSpeed = GetPrivateProfileIntA("Additional", "FastForwardSpeed", 3, iniPath.c_str());
	if (cfg_FastForwardSpeed < 1) cfg_FastForwardSpeed = 1;

	cfg_Scanlines = ParseBoolString("Video Filters", "Scanlines", "n", iniPath);
	cfg_ScanlineIntensity = GetPrivateProfileIntA("Video Filters", "ScanlineIntensity", 60, iniPath.c_str());

	if (cfg_ScanlineIntensity > 255) cfg_ScanlineIntensity = 255;
}

void CreateScanlineOverlay() {
	if (!cfg_Scanlines || !renderer) return;

	scanlineOverlay = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, currentVideoWidth, currentVideoHeight);
	if (!scanlineOverlay) return;

	SDL_SetTextureBlendMode(scanlineOverlay, SDL_BLENDMODE_BLEND);

	Uint32* pixels = nullptr;
	int pitch = 0;

	if (SDL_LockTexture(scanlineOverlay, NULL, (void**)&pixels, &pitch) == 0) {
		int widthInPixels = pitch / 4;
		for (unsigned int y = 0; y < currentVideoHeight; y++) {
			bool isScanline = (y % 2 == 0);
			for (unsigned int x = 0; x < currentVideoWidth; x++) {
				if (isScanline) {
					pixels[y * widthInPixels + x] = (0 << 24) | (0 << 16) | (0 << 8) | cfg_ScanlineIntensity;
				}
				else {
					pixels[y * widthInPixels + x] = 0x00000000;
				}
			}
		}
		SDL_UnlockTexture(scanlineOverlay);
	}
}

std::string AutoFindValidROM() {
	std::string currentDir = GetExecutionDirectory();
	if (currentDir.empty()) return "";

	std::string targetFolder = currentDir + "\\content\\";

	DWORD folderAttr = GetFileAttributesA(targetFolder.c_str());
	if (folderAttr == INVALID_FILE_ATTRIBUTES) {
		if (CreateDirectoryA(targetFolder.c_str(), NULL)) {
			WriteLog("INTEGRITY", "Folder \\content\\ was missing and was automatically created.");
		}
		else {
			WriteLog("ERROR", "Failed to create \\content\\ directory.");
			return "";
		}
	}

	std::string searchFilter = targetFolder + "*.bin";
	WIN32_FIND_DATAA findData;
	HANDLE hFind = FindFirstFileA(searchFilter.c_str(), &findData);
	if (hFind == INVALID_HANDLE_VALUE) return "";

	std::string targetROMPath = "";

	do {
		if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
		std::string fullFilePath = targetFolder + findData.cFileName;

		std::ifstream file(fullFilePath, std::ios::binary | std::ios::ate);
		if (!file.is_open()) continue;

		file.close();
		targetROMPath = fullFilePath;
		break;
	} while (FindNextFileA(hFind, &findData));

	FindClose(hFind);
	return targetROMPath;
}

bool CORE_CALL core_environment(unsigned cmd, void *data) {
	if (cmd == 10 && data) {
		auto* fmt = static_cast<enum retro_pixel_format*>(data);
		*fmt = RETRO_PIXEL_FORMAT_RGB565;
		return true;
	}
	if (cmd == 3) {
		if (data) *(bool*)data = true;
		return true;
	}
	return false;
}

void CORE_CALL core_video_refresh(const void *data, unsigned width, unsigned height, size_t pitch) {
	if (!data) return;

	if (width != currentVideoWidth || height != currentVideoHeight) {
		currentVideoWidth = width;
		currentVideoHeight = height;

		if (texture != nullptr) { SDL_DestroyTexture(texture); texture = nullptr; }
		if (scanlineOverlay != nullptr) { SDL_DestroyTexture(scanlineOverlay); scanlineOverlay = nullptr; }

		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

		texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, currentVideoWidth, currentVideoHeight);

		CreateScanlineOverlay();
		WriteLog("VIDEO", "Resolution shifted: " + std::to_string(width) + "x" + std::to_string(height));
	}

	if (texture) {
		SDL_UpdateTexture(texture, NULL, data, (int)pitch);
	}
}

size_t CORE_CALL core_audio_sample_batch(const int16_t *data, size_t frames) {
	if (cfg_AudioEnabled && audioDevice != 0 && data && frames > 0) {
		if (cfg_FastForwardAllowed && inputState_Backspace) {
			return frames;
		}
		SDL_QueueAudio(audioDevice, data, static_cast<Uint32>(frames * 4));
	}
	return frames;
}

void CORE_CALL core_input_poll(void) {
	const Uint8* state = SDL_GetKeyboardState(NULL);
	if (state) {
		inputState_Up = state[SDL_SCANCODE_UP] != 0;
		inputState_Down = state[SDL_SCANCODE_DOWN] != 0;
		inputState_Left = state[SDL_SCANCODE_LEFT] != 0;
		inputState_Right = state[SDL_SCANCODE_RIGHT] != 0;
		inputState_ButtonS = state[SDL_SCANCODE_S] != 0;
		inputState_Space = state[SDL_SCANCODE_SPACE] != 0;
		inputState_Enter = state[SDL_SCANCODE_RETURN] != 0;
		inputState_Backspace = state[SDL_SCANCODE_BACKSPACE] != 0;
	}
}

int16_t CORE_CALL core_input_state(unsigned port, unsigned device, unsigned index, unsigned id) {
	if (port == 0) {
		switch (id) {
		case RETRO_DEVICE_ID_JOYPAD_UP:     return inputState_Up ? 1 : 0;
		case RETRO_DEVICE_ID_JOYPAD_DOWN:   return inputState_Down ? 1 : 0;
		case RETRO_DEVICE_ID_JOYPAD_LEFT:   return inputState_Left ? 1 : 0;
		case RETRO_DEVICE_ID_JOYPAD_RIGHT:  return inputState_Right ? 1 : 0;
		case RETRO_DEVICE_ID_JOYPAD_A:      return inputState_ButtonS ? 1 : 0;
		case RETRO_DEVICE_ID_JOYPAD_B:      return inputState_ButtonS ? 1 : 0;
		case RETRO_DEVICE_ID_JOYPAD_START:  return inputState_Space ? 1 : 0;
		case RETRO_DEVICE_ID_JOYPAD_SELECT: return inputState_Enter ? 1 : 0;
		}
	}
	return 0;
}

void CleanUpResources() {
	if (audioDevice != 0) {
		SDL_ClearQueuedAudio(audioDevice);
		SDL_CloseAudioDevice(audioDevice);
		audioDevice = 0;
	}
	if (r_deinit) {
		r_deinit();
		r_deinit = nullptr;
	}
	if (coreDescriptor) {
		FreeLibrary(coreDescriptor);
		coreDescriptor = NULL;
	}
	if (texture) { SDL_DestroyTexture(texture); texture = nullptr; }
	if (scanlineOverlay) { SDL_DestroyTexture(scanlineOverlay); scanlineOverlay = nullptr; }
	if (renderer) { SDL_DestroyRenderer(renderer); renderer = nullptr; }
	if (window) { SDL_DestroyWindow(window); window = nullptr; }
	SDL_Quit();
	if (logFile.is_open()) logFile.close();
}

int main(int argc, char* argv[]) {
	logFile.open("emulator_log.txt", std::ios::out | std::ios::trunc);
	WriteLog("INFO", "=== Engine Bootstrap Started ===");

	LoadOrCreateConfiguration();

	if (cfg_HideDevConsole) {
		HWND hConsole = GetConsoleWindow();
		if (hConsole != NULL) ShowWindow(hConsole, SW_HIDE);
	}

	std::string romPath = AutoFindValidROM();
	if (romPath.empty()) {
		MessageBoxA(NULL, "Please put your .bin ROM file into the 'content' folder and restart.", "Engine Notification", MB_ICONINFORMATION | MB_OK);
		if (logFile.is_open()) logFile.close();
		return -1;
	}

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
		if (logFile.is_open()) logFile.close();
		return -1;
	}

	window = SDL_CreateWindow("Genesis Plus GX Wide",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		0, 0,
		SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN_DESKTOP);
	if (!window) { CleanUpResources(); return -1; }

	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!renderer) { CleanUpResources(); return -1; }

	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, currentVideoWidth, currentVideoHeight);
	if (!texture) { CleanUpResources(); return -1; }

	CreateScanlineOverlay();

	if (cfg_AudioEnabled) {
		SDL_AudioSpec wantedSpec;
		SDL_zero(wantedSpec);
		wantedSpec.freq = 44100;
		wantedSpec.format = AUDIO_S16SYS;
		wantedSpec.channels = 2;
		wantedSpec.samples = 512;
		wantedSpec.callback = NULL;

		audioDevice = SDL_OpenAudioDevice(NULL, 0, &wantedSpec, NULL, 0);
		if (audioDevice != 0) SDL_PauseAudioDevice(audioDevice, 0);
	}

	coreDescriptor = LoadLibraryA("genesis_plus_gx_wide_libretro.dll");
	if (!coreDescriptor) {
		MessageBoxA(NULL, "Critical file missing: genesis_plus_gx_wide_libretro.dll must be in the engine directory.", "Engine Error", MB_ICONERROR | MB_OK);
		CleanUpResources();
		return -1;
	}

	r_init = (retro_init_t)GetProcAddress(coreDescriptor, "retro_init");
	r_deinit = (retro_deinit_t)GetProcAddress(coreDescriptor, "retro_deinit");
	r_set_environment = (retro_set_environment_t)GetProcAddress(coreDescriptor, "retro_set_environment");
	r_set_video_refresh = (retro_set_video_refresh_t)GetProcAddress(coreDescriptor, "retro_set_video_refresh");
	r_set_audio_sample_batch = (retro_set_audio_sample_batch_t)GetProcAddress(coreDescriptor, "retro_set_audio_sample_batch");
	r_set_input_poll = (retro_set_input_poll_t)GetProcAddress(coreDescriptor, "retro_set_input_poll");
	r_set_input_state = (retro_set_input_state_t)GetProcAddress(coreDescriptor, "retro_set_input_state");
	r_load_game = (retro_load_game_t)GetProcAddress(coreDescriptor, "retro_load_game");
	r_run = (retro_run_t)GetProcAddress(coreDescriptor, "retro_run");

	if (!r_init || !r_deinit || !r_set_environment || !r_set_video_refresh ||
		!r_set_audio_sample_batch || !r_set_input_poll || !r_set_input_state ||
		!r_load_game || !r_run) {
		CleanUpResources();
		return -1;
	}

	r_set_environment(core_environment);
	r_set_video_refresh(core_video_refresh);
	r_set_audio_sample_batch(core_audio_sample_batch);
	r_set_input_poll(core_input_poll);
	r_set_input_state(core_input_state);

	r_init();

	std::ifstream romFile(romPath, std::ios::binary | std::ios::ate);
	if (!romFile.is_open()) { CleanUpResources(); return -1; }

	size_t romSize = (size_t)romFile.tellg();
	std::vector<char> romBuffer(romSize);
	romFile.seekg(0, std::ios::beg);
	romFile.read(romBuffer.data(), romSize);
	romFile.close();

	retro_game_info gameInfo = { 0 };
	gameInfo.path = romPath.c_str();
	gameInfo.data = romBuffer.data();
	gameInfo.size = romSize;

	if (!r_load_game(&gameInfo)) {
		CleanUpResources();
		return -1;
	}

	unsigned int frameCount = 0;
	auto lastFPSUpdate = std::chrono::high_resolution_clock::now();
	bool isRunning = true;
	SDL_Event event;

	Uint64 frameStartTicks = SDL_GetTicks64();

	while (isRunning) {
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT) isRunning = false;
			if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) isRunning = false;
		}

		bool fastForwardActive = (cfg_FastForwardAllowed && inputState_Backspace);
		unsigned int executionLoops = fastForwardActive ? cfg_FastForwardSpeed : 1;

		if (fastForwardActive && audioDevice != 0) {
			SDL_ClearQueuedAudio(audioDevice);
		}

		for (unsigned int i = 0; i < executionLoops; i++) {
			if (r_run) r_run();
		}

		if (!fastForwardActive) {
			if (cfg_AudioEnabled && audioDevice != 0) {
				Uint32 queuedAudioSize = SDL_GetQueuedAudioSize(audioDevice);
				while (queuedAudioSize > 4096) {
					SDL_Delay(1);
					queuedAudioSize = SDL_GetQueuedAudioSize(audioDevice);
				}
			}
			else {
				Uint64 currentTicks = SDL_GetTicks64();
				double elapsedMs = static_cast<double>(currentTicks - frameStartTicks);
				if (elapsedMs < FRAME_TARGET_MS) {
					SDL_Delay(static_cast<Uint32>(FRAME_TARGET_MS - elapsedMs));
				}
				frameStartTicks = SDL_GetTicks64();
			}
		}

		SDL_RenderClear(renderer);
		if (texture) SDL_RenderCopy(renderer, texture, NULL, NULL);
		if (cfg_Scanlines && scanlineOverlay) {
			SDL_RenderCopy(renderer, scanlineOverlay, NULL, NULL);
		}
		SDL_RenderPresent(renderer);

		frameCount++;
		auto currentTime = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double, std::milli> elapsed = currentTime - lastFPSUpdate;
		if (elapsed.count() >= 1000.0) {
			std::string windowTitle = "WidePixels2D Engine Runtime v1.0.0";
			SDL_SetWindowTitle(window, windowTitle.c_str());
			frameCount = 0;
			lastFPSUpdate = currentTime;
		}
	}

	CleanUpResources();
	return 0;
}