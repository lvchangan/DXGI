#pragma once

#include "SDL2/SDL.h"
#include "windows.h"

//Refresh Event
#define REFRESH_EVENT  (SDL_USEREVENT + 1)

#define BREAK_EVENT  (SDL_USEREVENT + 2)

class SDLPlayer {
private:
	//窗口大小
	int Window_W = 0;
	int Window_H = 0;

	//显示的图片宽高
	int picture_w = 0;
	int picture_h = 0;

	//画面显示区域，窗口左上角坐标是(0, 0)
	SDL_Rect DisplayRect = { 0 };

	//要播放的图片buffer
	uint8_t* SDLBuffer = NULL;

	//播放图片的格式
	Uint32 pixformat = 0;

	HANDLE SDLDisplayTDHandle;
	int SDLDisplay();
	static DWORD WINAPI SDLDisplayThead(void* p);
public:
	SDLPlayer(int w, int h);
	~SDLPlayer();
	void SDLPlayerFrame(const uint8_t* pixels);
};

