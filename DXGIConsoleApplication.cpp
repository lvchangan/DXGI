// DXGIConsoleApplication.cpp：定义控制台应用程序的入口点。
//

#include "DuplicationManager.h"
#include <time.h>
#pragma warning(disable:4996)
clock_t start = 0, stop = 0, duration = 0;
int count = 0;
FILE *log_file;
char file_name[MAX_PATH];
#define align_32(n)  ((((n)+ 31) >> 5) << 5)   //对齐32

#define BMP_OUT 1
#define YUVFILE_OUT 0


#if BMP_OUT
void save_as_bitmap(unsigned char *bitmap_data, int bmp_width, int bmp_height, char *filename)
{
	//创建了一个文件，我们将在这里保存屏幕截图。

	FILE *f = fopen(filename, "wb");
	if (f == NULL)
	{
		printf_s("fopen f error\n");
		return;
	}

	BITMAPFILEHEADER   bmfHeader;
	BITMAPINFOHEADER   bi;

	bi.biSize = sizeof(BITMAPINFOHEADER);
	bi.biWidth = bmp_width;
	//如果图像上下颠倒，请将大小设置为负数
	bi.biHeight = -bmp_height;
	//RGB颜色空间中只有一个平面，而YUV中只有三个平面。
	bi.biPlanes = 1;
	//在windows RGB中，R、G、B和alpha各有8位深度
	bi.biBitCount = 32;
	//我们不是在压缩图像。
	bi.biCompression = BI_RGB;
	// 图像的大小（以字节为单位）。对于BI_RGB位图，可以将其设置为零.
	bi.biSizeImage = 0;
	bi.biXPelsPerMeter = 0;
	bi.biYPelsPerMeter = 0;
	bi.biClrUsed = 0;
	bi.biClrImportant = 0;

	// rowPitch = the size of the row in bytes.
	DWORD dwSizeofImage = bmp_width * bmp_height * 4;

	// Add the size of the headers to the size of the bitmap to get the total file size
	DWORD dwSizeofDIB = dwSizeofImage + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

	//Offset to where the actual bitmap bits start.
	bmfHeader.bfOffBits = (DWORD)sizeof(BITMAPFILEHEADER) + (DWORD)sizeof(BITMAPINFOHEADER);

	//Size of the file
	bmfHeader.bfSize = dwSizeofDIB;

	//bfType must always be BM for Bitmaps
	bmfHeader.bfType = 0x4D42; //BM   

							   // TODO: Handle getting current directory
	DWORD dwBytesWritten = 0;
	dwBytesWritten += fwrite(&bmfHeader, sizeof(BITMAPFILEHEADER), 1, f);
	dwBytesWritten += fwrite(&bi, sizeof(BITMAPINFOHEADER), 1, f);
	dwBytesWritten += fwrite(bitmap_data, 1, dwSizeofImage, f);

	fclose(f);
}

unsigned char clip_value(unsigned char x, unsigned char min_val, unsigned char  max_val) {
	if (x > max_val) {
		return max_val;
	}
	else if (x < min_val) {
		return min_val;
	}
	else {
		return x;
	}
}

//RGB32 to YUV420
//RGB 32位真彩色  1个像素点占4个字节
//YUV420 1个像素点占1.5个字节 YYYYYYYY UU VV=>YUV420P
bool RGB32_TO_YUV420(unsigned char* RgbBuf, int w, int h, unsigned char* yuvBuf)
{
	unsigned char* ptrY, * ptrU, * ptrV, * ptrRGB;
	memset(yuvBuf, 0, w * h * 3 / 2);
	ptrY = yuvBuf;
	ptrU = yuvBuf + w * h;
	ptrV = ptrU + (w * h * 1 / 4);
	unsigned char y, u, v, r, g, b;
	for (int j = 0; j < h; j++) {
		ptrRGB = RgbBuf + w * j * 4; //跳转至第j行开头
		for (int i = 0; i < w; i++) {
			int pos = w * i + j;
			r = *(ptrRGB++);      //依次取出一行中每个像素点的RGB
			g = *(ptrRGB++);
			b = *(ptrRGB++);
			ptrRGB++;             //a
			y = (unsigned char)((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;       //根据公式转化
			u = (unsigned char)((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
			v = (unsigned char)((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
			// 取全部的Y
			*(ptrY++) = clip_value(y, 0, 255);
			if (j % 2 == 0 && i % 2 == 0) {
				// u取的是偶数行的偶数列，相当于宽、高都除2, 也就是w * h/4;
				*(ptrU++) = clip_value(u, 0, 255);
			}
			else {
				if (i % 2 == 0) {
					// v取的是奇数行的偶数列，相当于宽、高都除2, 也就是w * h/4;
					*(ptrV++) = clip_value(v, 0, 255);
				}
			}
		}
	}
	return true;
}
#endif

int main()
{
	fopen_s(&log_file, "logY.txt", "w");
	if (log_file == NULL)
		return -1;
	//设置缓存区大小并校验
	int mScreenWidth = GetSystemMetrics(SM_CXSCREEN);
	int mScreenHeight = GetSystemMetrics(SM_CYSCREEN);		//获取PC桌面的宽和高
	printf("mScreenWidth = %d,mScreenHeight = %d\n", mScreenWidth, mScreenHeight);

	int BufferSize = align_32(mScreenWidth) * mScreenHeight << 2;	//1366 -> 1376  RGB32位,左移2位*4

	DUPLICATIONMANAGER DuplMgr;
	DUPL_RETURN Ret;

	

	UINT Output = 0;
	
	// Make duplication manager
	Ret = DuplMgr.InitDupl(log_file, Output);
	if (Ret != DUPL_RETURN_SUCCESS)
	{
		printf_s("Duplication Manager couldn't be initialized.");
		return 0;
	}

	//注意：DXGI使用32字节宽度对齐，在1366分辨率下实际占用1376的宽度
	BYTE* pBuf = (BYTE*)malloc(BufferSize);
	if (pBuf == NULL)
	{
		printf("malloc error\n");
		return -1;
	}
	
	// Main duplication loop
	bool timeOut = false;
#if YUVFILE_OUT
	FILE *fp = fopen("./outfileyuv.yuv", "wb");
	if (fp == NULL)
	{
		printf_s("open fp error\n");
		return -1;
	}
	unsigned char* pic_yuv420 = (unsigned char*)malloc(mScreenWidth * mScreenHeight * 3 / 2);
	if (pic_yuv420 == NULL)
		return -1;
#endif
	for (int i = 0; i < 10; i++)
	{
		// Get new frame from desktop duplication
		Ret = DuplMgr.GetFrame(pBuf, timeOut);   //pBuf永远是RGB32位
		if (Ret != DUPL_RETURN_SUCCESS)
		{
			printf_s( "Could not get the frame.");
		}
#if BMP_OUT
		sprintf_s(file_name, "./BMP/%d.bmp", i);
		/*bool RtoY = RGB32_TO_YUV420(pBuf, DuplMgr.GetImagePitch()/4, DuplMgr.GetImageHeight(), pic_yuv420);
		if(RtoY == true)
			fwrite(pic_yuv420, 1, mScreenWidth * mScreenHeight * 3 / 2, fp);*/

		save_as_bitmap(pBuf, mScreenWidth, mScreenHeight, file_name);
#endif
	}
	//fclose(fp);
	delete pBuf;

	fclose(log_file);
    return 0;
}
