#pragma once
#include <stdio.h>
#include <windows.h>
#define CONSOLE_Debug 1

#if CONSOLE_Debug
#pragma warning(disable:4996)
void InitConsoleWindow()
{
	AllocConsole();
	freopen("CONOUT$", "w", stdout);
	ShowWindow(GetConsoleWindow(), SW_MINIMIZE);
}
void DeInitConsoleWindow()
{
	fclose(stdout);
	FreeConsole();
}
#endif