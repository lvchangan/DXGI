#include <stdio.h>
#include "SDLPlayer.h"
#include <thread>

#pragma comment(lib, "../SDL2/lib/SDL2.lib")
//#pragma comment(lib, "Imm32.lib")

SDLPlayer::SDLPlayer(int w,int h)
{
	//pixformat = SDL_PIXELFORMAT_IYUV;
	pixformat = SDL_PIXELFORMAT_ARGB32;
	picture_w = w;
	picture_h = h;
	this->Window_W = w;
	this->Window_H = h;
	
	SDLDisplayTDHandle = CreateThread(NULL, 0, SDLDisplayThead, this, 0, NULL);
}

SDLPlayer::~SDLPlayer()
{
	SDL_Event event;
	event.type = BREAK_EVENT;
	do
	{
		SDL_PushEvent(&event);
	} while (WaitForSingleObject(SDLDisplayTDHandle, 500) == WAIT_TIMEOUT); //等待线程结束再释放资源
}

void SDLPlayer::SDLPlayerFrame(const uint8_t* pixels)
{
	SDLBuffer = (uint8_t*)pixels;
	SDL_Event event;
	event.type = REFRESH_EVENT;
	SDL_PushEvent(&event);
}


DWORD WINAPI SDLPlayer::SDLDisplayThead(void* p)
{
	SDLPlayer* This = (SDLPlayer*)p;
	This->SDLDisplay();

	return 0;
}

int SDLPlayer::SDLDisplay()
{
	if (SDL_Init(SDL_INIT_VIDEO)) {
		printf("Could not initialize SDL - %s\n", SDL_GetError());
		return -1;
	}

	SDL_Window* Window;
	//SDL 2.0 Support for multiple windows
	Window = SDL_CreateWindow("DXGI", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		Window_W, Window_H, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
	if (!Window) {
		printf("SDL: could not create window - exiting:%s\n", SDL_GetError());
		return -1;
	}
	SDL_Renderer* sdlRenderer = SDL_CreateRenderer(Window, -1, 0);

	SDL_Texture* sdlTexture = SDL_CreateTexture(sdlRenderer, pixformat, SDL_TEXTUREACCESS_STREAMING, picture_w, picture_h);

	this->DisplayRect.x = 0;
	this->DisplayRect.y = 0;
	this->DisplayRect.w = this->Window_W;
	this->DisplayRect.h = this->Window_H;

	SDL_Event event;
	bool SDL_running = true;

#define REFRESH()                                                                 \
	{                                                                             \
		if (this->SDLBuffer)                                                      \
		{                                                                         \
			SDL_UpdateTexture(sdlTexture, NULL, this->SDLBuffer, this->picture_w); \
			SDL_RenderClear(sdlRenderer);                                         \
			SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, &this->DisplayRect);    \
			SDL_RenderPresent(sdlRenderer);                                       \
		}                                                                         \
	}

	while (SDL_running)
	{
		//至少50ms刷新一次
		event.type = REFRESH_EVENT;
		SDL_WaitEventTimeout(&event, 50);
		//DPA("event.type = 0x%x \n", event.type);
		switch (event.type)
		{
			case REFRESH_EVENT:
			{
				REFRESH();
				break;
			}
			case SDL_WINDOWEVENT:
			{
				SDL_GetWindowSize(Window, &this->Window_W, &this->Window_H);

				if (this->DisplayRect.w != this->Window_W || this->DisplayRect.h != this->Window_H)
				{//FIX: If window is resize
					this->DisplayRect.x = 0;
					this->DisplayRect.y = 0;
					this->DisplayRect.w = this->Window_W;
					this->DisplayRect.h = this->Window_H;
					REFRESH();
				}
				break;
			}
			case SDL_QUIT:
			{
				break;
			}
			case BREAK_EVENT:
			{
				SDL_running = false;
				break;
			}
			default:
				break;
		}
	}

	SDL_DestroyTexture(sdlTexture);
	SDL_DestroyRenderer(sdlRenderer);
	SDL_DestroyWindow(Window);
	SDL_Quit();
	return 0;
}