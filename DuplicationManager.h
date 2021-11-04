// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#ifndef _DUPLICATIONMANAGER_H_
#define _DUPLICATIONMANAGER_H_

#include <d3d11.h>
#include <dxgi1_2.h>
#include <sal.h>
#include <new>
#include <stdio.h>

extern HRESULT SystemTransitionsExpectedErrors[];
extern HRESULT CreateDuplicationExpectedErrors[];
extern HRESULT FrameInfoExpectedErrors[];
extern HRESULT AcquireFrameExpectedError[];
extern HRESULT EnumOutputsExpectedErrors[];

//https://www.cnblogs.com/lanuage/p/7775005.html
typedef _Return_type_success_(return == DUPL_RETURN_SUCCESS) enum
{
    DUPL_RETURN_SUCCESS = 0,
    DUPL_RETURN_ERROR_EXPECTED = 1,
    DUPL_RETURN_ERROR_UNEXPECTED = 2
}DUPL_RETURN;


//
// Structure that holds D3D resources not directly tied to any one thread
//
typedef struct _DX_RESOURCES
{
	ID3D11Device* Device;
	ID3D11DeviceContext* Context;
	ID3D11VertexShader* VertexShader;
	ID3D11PixelShader* PixelShader;
	ID3D11InputLayout* InputLayout;
	ID3D11SamplerState* SamplerLinear;
} DX_RESOURCES;

//
// Holds info about the pointer/cursor
//
typedef struct _PTR_INFO
{
    _Field_size_bytes_(BufferSize) BYTE* PtrShapeBuffer;
    DXGI_OUTDUPL_POINTER_SHAPE_INFO ShapeInfo;
    POINT Position;
    bool Visible;
    UINT BufferSize;
    UINT WhoUpdatedPositionLast;
    LARGE_INTEGER LastTimeStamp;
} PTR_INFO;

//
// Handles the task of duplicating an output.
//
class DUPLICATIONMANAGER
{
    public:
		
	//methods
        DUPLICATIONMANAGER();
        ~DUPLICATIONMANAGER();
        _Success_(*Timeout == false && return == DUPL_RETURN_SUCCESS) 
		DUPL_RETURN GetFrame(_Inout_ BYTE* ImageData, _Out_ bool &timeOut);
        DUPL_RETURN InitDupl(_In_ FILE *log_file, UINT Output);
		int GetImageHeight();
		int GetImageWidth();
		int GetImagePitch();
        bool isScreenRotate();
        bool CheckBufferSize(int BufferSize);
	//vars

    private:

    // vars
        IDXGIOutputDuplication* m_DeskDupl;
        ID3D11Texture2D* m_AcquiredDesktopImage;
		ID3D11Texture2D* m_DestImage;
        UINT m_OutputNumber;
        DXGI_OUTPUT_DESC m_OutputDesc;
		DX_RESOURCES *m_DxRes;
		FILE *m_log_file;
		int m_ImagePitch;
        //pointer/cursor info
        PTR_INFO m_PtrInfo;
        INT m_OffsetX;
        INT m_OffsetY;
        DXGI_OUTDUPL_FRAME_INFO m_FrameInfo;
        //获取到的屏幕分辨率大小
        int mWidth = -1;
        int mHeight = -1;
        //是否扩充了。如果是则需要每行逐一复制
        //例如：当前分辨率为1366，DXGI会扩充到1376宽
        //此时应该每行逐一复制到DEST中
        BOOL mExtendWidth;
	//methods
		DUPL_RETURN InitializeDx();
		_Post_satisfies_(return != DUPL_RETURN_SUCCESS)
		DUPL_RETURN ProcessFailure(_In_opt_ ID3D11Device* Device, _In_ LPCWSTR Str, HRESULT hr, _In_opt_z_ HRESULT* ExpectedErrors = nullptr);
		void DisplayMsg(_In_ LPCWSTR Str, HRESULT hr);
		void CopyImage(BYTE* ImageData);
		void GetOutputDesc(_Out_ DXGI_OUTPUT_DESC* DescPtr);
		DUPL_RETURN DoneWithFrame();
        DUPL_RETURN GetMouse();

};

#endif
