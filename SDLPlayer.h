#pragma once

#include "SDL2/SDL.h"
#include "windows.h"

//Refresh Event
#define REFRESH_EVENT  (SDL_USEREVENT + 1)

#define BREAK_EVENT  (SDL_USEREVENT + 2)

class SDLPlayer {
private:
	//���ڴ�С
	int Window_W = 0;
	int Window_H = 0;

	//��ʾ��ͼƬ���
	int picture_w = 0;
	int picture_h = 0;

	//������ʾ���򣬴������Ͻ�������(0, 0)
	SDL_Rect DisplayRect = { 0 };

	//Ҫ���ŵ�ͼƬbuffer
	uint8_t* SDLBuffer = NULL;

	//����ͼƬ�ĸ�ʽ
	Uint32 pixformat = 0;

	HANDLE SDLDisplayTDHandle;
	int SDLDisplay();
	static DWORD WINAPI SDLDisplayThead(void* p);
public:
	SDLPlayer(int w, int h);
	~SDLPlayer();
	void SDLPlayerFrame(const uint8_t* pixels);
};

