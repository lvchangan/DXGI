
#include "DuplicationManager.h"

//下面列出了在模式更改、PnpStop、PnpStart等转换事件发生时，Dxgi API调用可能出现的错误
//桌面交换机、TDR或会话断开/重新连接。在所有这些情况下，我们希望应用程序清理处理的线程
//桌面将更新并尝试重新创建它们。
//如果我们得到一个不在适当列表中的错误，那么我们将退出应用程序
//这些是由于转换导致的常规Dxgi API的错误
//https://github.com/balapradeepswork/DXGICaptureApp.git
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

HRESULT SystemTransitionsExpectedErrors[] = {
	DXGI_ERROR_DEVICE_REMOVED,
	DXGI_ERROR_ACCESS_LOST,
	static_cast<HRESULT>(WAIT_ABANDONED),
	S_OK                                    // Terminate list with zero valued HRESULT//使用零值HRESULT终止列表
};


// 这些是由于转换导致的IDXGIOutput1:：DuplicateOutput的错误
HRESULT CreateDuplicationExpectedErrors[] = {
	DXGI_ERROR_DEVICE_REMOVED,
	static_cast<HRESULT>(E_ACCESSDENIED),
	DXGI_ERROR_UNSUPPORTED,
	DXGI_ERROR_SESSION_DISCONNECTED,
	S_OK                                    // Terminate list with zero valued HRESULT
};

// These are the errors we expect from IDXGIOutputDuplication methods due to a transition
HRESULT FrameInfoExpectedErrors[] = {
	DXGI_ERROR_DEVICE_REMOVED,
	DXGI_ERROR_ACCESS_LOST,
	S_OK                                    // Terminate list with zero valued HRESULT
};

// These are the errors we expect from IDXGIAdapter::EnumOutputs methods due to outputs becoming stale during a transition
HRESULT EnumOutputsExpectedErrors[] = {
	DXGI_ERROR_NOT_FOUND,
	S_OK                                    // Terminate list with zero valued HRESULT
};


//
// Constructor sets up references / variables
//
DUPLICATIONMANAGER::DUPLICATIONMANAGER() : m_DeskDupl(nullptr),
                                           m_AcquiredDesktopImage(nullptr),
										   m_DestImage(nullptr),
                                           m_OutputNumber(0),
										   m_ImagePitch(0),
										   m_DxRes(nullptr)
{
    RtlZeroMemory(&m_OutputDesc, sizeof(m_OutputDesc));
}

//
// Destructor simply calls CleanRefs to destroy everything
//
DUPLICATIONMANAGER::~DUPLICATIONMANAGER()
{
    if (m_DeskDupl)
    {
        m_DeskDupl->Release();
        m_DeskDupl = nullptr;
    }
    if (m_AcquiredDesktopImage)
    {
        m_AcquiredDesktopImage->Release();
        m_AcquiredDesktopImage = nullptr;
    }
	if (m_DestImage)
	{
		m_DestImage->Release();
		m_DestImage = nullptr;
	}
    if (m_DxRes->Device)
    {
		m_DxRes->Device->Release();
		m_DxRes->Device = nullptr;
    }
	if (m_DxRes->Device)
	{
		m_DxRes->Device->Release();
		m_DxRes->Device = nullptr;
	}
	if (m_DxRes->Context)
	{
		m_DxRes->Context->Release();
		m_DxRes->Context = nullptr;
	}
}

//
// 初始化复制接口
//
DUPL_RETURN DUPLICATIONMANAGER::InitDupl(_In_ FILE *log_file, UINT Output)
{
	m_log_file = log_file;
	m_DxRes = new (std::nothrow) DX_RESOURCES;
	RtlZeroMemory(m_DxRes, sizeof(DX_RESOURCES));
	DUPL_RETURN Ret = InitializeDx(); 
	if (Ret != DUPL_RETURN_SUCCESS)
	{
		//fprintf_s(log_file, "DX_RESOURCES couldn't be initialized.");
		printf_s("DX_RESOURCES couldn't be initialized.");
		return Ret;
	}
    m_OutputNumber = Output;
	//https://blog.csdn.net/weixin_33984032/article/details/86358890
	//获取到IDXGIOutputDuplication接口
	//IDXGIDevice --> IDXGIAdapter --> IDXGIOutput --> IDXGIOutput1 --> IDXGIOutputDuplication
    //获取DXGI设备
    IDXGIDevice* DxgiDevice = nullptr;
    HRESULT hr = m_DxRes->Device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&DxgiDevice));
    if (FAILED(hr))
    {
        return ProcessFailure(nullptr, L"Failed to QI for DXGI Device", hr);
    }

    //获取DXGI适配器
    IDXGIAdapter* DxgiAdapter = nullptr;
    hr = DxgiDevice->GetParent(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&DxgiAdapter));
    DxgiDevice->Release();
    DxgiDevice = nullptr;
    if (FAILED(hr))
    {
        return ProcessFailure(m_DxRes->Device, L"Failed to get parent DXGI Adapter", hr, SystemTransitionsExpectedErrors);
    }

    // Get output
    IDXGIOutput* DxgiOutput = nullptr;
    hr = DxgiAdapter->EnumOutputs(Output, &DxgiOutput);
    DxgiAdapter->Release();
    DxgiAdapter = nullptr;
    if (FAILED(hr))
    {
        return ProcessFailure(m_DxRes->Device, L"Failed to get specified output in DUPLICATIONMANAGER", hr, EnumOutputsExpectedErrors);
    }

    DxgiOutput->GetDesc(&m_OutputDesc);

    // QI for Output 1
    IDXGIOutput1* DxgiOutput1 = nullptr;
    hr = DxgiOutput->QueryInterface(__uuidof(DxgiOutput1), reinterpret_cast<void**>(&DxgiOutput1));
    DxgiOutput->Release();
    DxgiOutput = nullptr;
    if (FAILED(hr))
    {
        return ProcessFailure(nullptr, L"Failed to QI for DxgiOutput1 in DUPLICATIONMANAGER", hr);
    }

    // Create desktop duplication 创建桌面复制
    hr = DxgiOutput1->DuplicateOutput(m_DxRes->Device, &m_DeskDupl);
    DxgiOutput1->Release();
    DxgiOutput1 = nullptr;
    if (FAILED(hr))
    {
        if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE)
        {
            MessageBoxW(nullptr, L"There is already the maximum number of applications using the Desktop Duplication API running, please close one of those applications and then try again.", L"Error", MB_OK);
            return DUPL_RETURN_ERROR_UNEXPECTED;
        }
        return ProcessFailure(m_DxRes->Device, L"Failed to get duplicate output in DUPLICATIONMANAGER", hr, CreateDuplicationExpectedErrors);
    }

	D3D11_TEXTURE2D_DESC desc; 
	DXGI_OUTDUPL_DESC lOutputDuplDesc;
	m_DeskDupl->GetDesc(&lOutputDuplDesc);
	desc.Width = lOutputDuplDesc.ModeDesc.Width;
	desc.Height = lOutputDuplDesc.ModeDesc.Height;
	desc.Format = lOutputDuplDesc.ModeDesc.Format;
	desc.ArraySize = 1;
	desc.BindFlags = 0;
	desc.MiscFlags = 0;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.MipLevels = 1;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	desc.Usage = D3D11_USAGE_STAGING;

	hr = m_DxRes->Device->CreateTexture2D(&desc, NULL, &m_DestImage);

	if (FAILED(hr))
	{
		ProcessFailure(nullptr, L"Creating cpu accessable texture failed.", hr);
		return DUPL_RETURN_ERROR_UNEXPECTED;
	}

	if (m_DestImage == nullptr)
	{
		ProcessFailure(nullptr, L"Creating cpu accessable texture failed.", hr);
		return DUPL_RETURN_ERROR_UNEXPECTED;
	}

    int width = GetImageWidth();
    int height = GetImageHeight();
    if (width&(32 - 1)) {
        mExtendWidth = TRUE;
    }
    return DUPL_RETURN_SUCCESS;
}


//
// Get next frame and write it into Data
// 将当前屏幕数据保存到 BYTE* ImageData 中
// 注意：屏幕数据格式为 ARGB32，参数 ImageData 必须有足够的缓冲区大小，函数中不会判断
//
//
_Success_(*Timeout == false && return == DUPL_RETURN_SUCCESS)
DUPL_RETURN DUPLICATIONMANAGER::GetFrame(_Inout_ BYTE* ImageData, _Out_ bool &timeOut)
{
    IDXGIResource* DesktopResource = nullptr;
    DXGI_OUTDUPL_FRAME_INFO FrameInfo;

    // Get new frame
    HRESULT hr = m_DeskDupl->AcquireNextFrame(20, &FrameInfo, &DesktopResource);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT)
    {
		//在一些win10的系统上,如果桌面没有变化的情况下;
	    //这里会发生超时现象，但是这并不是发生了错误，而是系统优化了刷新动作导致的。;
	    //所以，这里没必要返回FALSE，返回不带任何数据的TRUE即可;	
		timeOut = true;
        return DUPL_RETURN_SUCCESS;
    }
    timeOut = false;
    if (FAILED(hr))
    {
        return ProcessFailure(m_DxRes->Device, L"Failed to acquire next frame in DUPLICATIONMANAGER", hr, FrameInfoExpectedErrors);
    }

    // If still holding old frame, destroy it
    if (m_AcquiredDesktopImage)
    {
        m_AcquiredDesktopImage->Release();
        m_AcquiredDesktopImage = nullptr;
    }

    // QI for IDXGIResource
    hr = DesktopResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&m_AcquiredDesktopImage));
    DesktopResource->Release();
    DesktopResource = nullptr;
    if (FAILED(hr))
    {
        return ProcessFailure(nullptr, L"Failed to QI for ID3D11Texture2D from acquired IDXGIResource in DUPLICATIONMANAGER", hr);
    }
    m_FrameInfo = FrameInfo;

    GetMouse(); //获取鼠标

	CopyImage(ImageData);

    DoneWithFrame();

    return DUPL_RETURN_SUCCESS;
}

void  DUPLICATIONMANAGER::CopyImage(BYTE* ImageData)
{
	m_DxRes->Context->CopyResource(m_DestImage, m_AcquiredDesktopImage);
	D3D11_MAPPED_SUBRESOURCE resource;
	UINT subresource = D3D11CalcSubresource(0, 0, 0);
	//https://blog.csdn.net/pizi0475/article/details/7786362
	m_DxRes->Context->Map(m_DestImage, subresource, D3D11_MAP_READ, 0, &resource);  //CPU 读取 GPU计算的结果并转换成CPU理解的类型

	BYTE* sptr = reinterpret_cast<BYTE*>(resource.pData);

	//Store Image Pitch
	m_ImagePitch = resource.RowPitch;

	if (mExtendWidth) {
		int rowBytes = mWidth * 4;
		for (int i = 0; i < mHeight; i++) {
			memcpy(ImageData, sptr, rowBytes);
			ImageData += rowBytes;
			sptr += resource.RowPitch;
		}
	}
	else {
		memcpy_s(ImageData, resource.RowPitch * mHeight, sptr, resource.RowPitch * mHeight);
	}

#if 0
	//Store Image Pitch
	m_ImagePitch = resource.RowPitch;
	int height = GetImageHeight();
	memcpy_s(ImageData, resource.RowPitch*height, sptr, resource.RowPitch*height);
#endif

#if 1
    if (!mExtendWidth)
    {
        //将鼠标指针绘制
        if (m_PtrInfo.PtrShapeBuffer && m_PtrInfo.BufferSize > 0 && m_PtrInfo.Position.y >= 0 && m_PtrInfo.Position.x >= 0) {
            BYTE* pDst = ImageData + resource.RowPitch * m_PtrInfo.Position.y + 4 * m_PtrInfo.Position.x;
            //绘制一个矩形测试
            int drawHeight = min(m_PtrInfo.ShapeInfo.Height, mHeight - m_PtrInfo.Position.y);
            int drawWidth = min(m_PtrInfo.ShapeInfo.Width, mWidth - m_PtrInfo.Position.x);

            if (m_PtrInfo.ShapeInfo.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME) {
                drawHeight = min(m_PtrInfo.ShapeInfo.Height / 2, mHeight - m_PtrInfo.Position.y);
                /*
                https://docs.microsoft.com/zh-cn/windows/win32/api/dxgi1_2/ne-dxgi1_2-dxgi_outdupl_pointer_shape_type
                The pointer type is a monochrome mouse pointer, which is a monochrome bitmap.
                The bitmap's size is specified by width and height in a 1 bits per pixel (bpp) device independent bitmap (DIB) format AND mask that is followed by another 1 bpp DIB format XOR mask of the same size.
                */
                //请注意下面的 m_PtrInfo.ShapeInfo.Height/2，直接定位到 mask
                BYTE* pSrc = m_PtrInfo.PtrShapeBuffer + m_PtrInfo.ShapeInfo.Height / 2 * m_PtrInfo.ShapeInfo.Pitch;
                for (int h = drawHeight - 1; h >= 0; h--) {
                    BYTE* p = pDst;
                    BYTE* src = pSrc;
                    int data = src[3] | src[2] << 8 | src[1] << 16 | src[0] << 24;
                    for (int w = 0; w < drawWidth; w++) {
                        //按照位来请求数据
                        bool set = data & (1 << (31 - w));
                        if (set) {
                            //应该根据CURSOR中的来设置，但这里简单直接设置反色的
                            p[0] = 0xff;
                            p[1] = 255 - p[1];
                            p[2] = 255 - p[2];
                            p[3] = 255 - p[3];
                        }
                        p += 4;
                    }
                    pDst += resource.RowPitch;
                    pSrc += m_PtrInfo.ShapeInfo.Pitch;
                }
            }
            else if (m_PtrInfo.ShapeInfo.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR) {
                BYTE* pSrc = m_PtrInfo.PtrShapeBuffer;
                for (int h = 0; h < drawHeight; h++) {
                    BYTE* p = pDst;
                    BYTE* src = pSrc;
                    for (int w = 0; w < drawWidth; w++) {
                        if (*src == 0x00) {
                            //alpha = 0
                            p += 4;
                            src += 4;
                        }
                        else {
                            *p++ = *src++;
                            *p++ = *src++;
                            *p++ = *src++;
                            *p++ = *src++;
                        }
                    }
                    pSrc += m_PtrInfo.ShapeInfo.Pitch;
                    pDst += resource.RowPitch;
                }
            }
        }
    }
#endif

	m_DxRes->Context->Unmap(m_DestImage, subresource);		//Unmap：无效指针指向的资源，并重新启用GPU的访问该资源。
}

int DUPLICATIONMANAGER::GetImageHeight()
{
    if (mHeight < 0) {
        DXGI_OUTDUPL_DESC lOutputDuplDesc;
        m_DeskDupl->GetDesc(&lOutputDuplDesc);
        mHeight = m_OutputDesc.DesktopCoordinates.bottom - m_OutputDesc.DesktopCoordinates.top;
    }
    return mHeight;
}


int DUPLICATIONMANAGER::GetImageWidth()
{
    if (mWidth < 0) {
        DXGI_OUTDUPL_DESC lOutputDuplDesc;
        m_DeskDupl->GetDesc(&lOutputDuplDesc);
        mWidth = m_OutputDesc.DesktopCoordinates.right - m_OutputDesc.DesktopCoordinates.left;
    }
    return mWidth;
}


int DUPLICATIONMANAGER::GetImagePitch()
{
	return m_ImagePitch;
}

//
// Release frame
//
DUPL_RETURN DUPLICATIONMANAGER::DoneWithFrame()
{
    HRESULT hr = m_DeskDupl->ReleaseFrame();
    if (FAILED(hr))
    {
        return ProcessFailure(m_DxRes->Device, L"Failed to release frame in DUPLICATIONMANAGER", hr, FrameInfoExpectedErrors);
    }

    if (m_AcquiredDesktopImage)
    {
        m_AcquiredDesktopImage->Release();
        m_AcquiredDesktopImage = nullptr;
    }

    return DUPL_RETURN_SUCCESS;
}

//
// Gets output desc into DescPtr
//
void DUPLICATIONMANAGER::GetOutputDesc(_Out_ DXGI_OUTPUT_DESC* DescPtr)
{
    *DescPtr = m_OutputDesc;
}

_Post_satisfies_(return != DUPL_RETURN_SUCCESS)
DUPL_RETURN DUPLICATIONMANAGER::ProcessFailure(_In_opt_ ID3D11Device* Device, _In_ LPCWSTR Str, HRESULT hr, _In_opt_z_ HRESULT* ExpectedErrors)
{
	HRESULT TranslatedHr;

	// On an error check if the DX device is lost
	if (Device)
	{
		HRESULT DeviceRemovedReason = Device->GetDeviceRemovedReason();

		switch (DeviceRemovedReason)
		{
		case DXGI_ERROR_DEVICE_REMOVED:
		case DXGI_ERROR_DEVICE_RESET:
		case static_cast<HRESULT>(E_OUTOFMEMORY) :
		{
			// Our device has been stopped due to an external event on the GPU so map them all to
			// device removed and continue processing the condition
			TranslatedHr = DXGI_ERROR_DEVICE_REMOVED;
			break;
		}

		case S_OK:
		{
			// Device is not removed so use original error
			TranslatedHr = hr;
			break;
		}

		default:
		{
			// Device is removed but not a error we want to remap
			TranslatedHr = DeviceRemovedReason;
		}
		}
	}
	else
	{
		TranslatedHr = hr;
	}

	// Check if this error was expected or not
	if (ExpectedErrors)
	{
		HRESULT* CurrentResult = ExpectedErrors;

		while (*CurrentResult != S_OK)
		{
			if (*(CurrentResult++) == TranslatedHr)
			{
				return DUPL_RETURN_ERROR_EXPECTED;
			}
		}
	}

	// Error was not expected so display the message box
	DisplayMsg(Str, TranslatedHr);

	return DUPL_RETURN_ERROR_UNEXPECTED;
}

//
// Displays a message
//
void DUPLICATIONMANAGER::DisplayMsg(_In_ LPCWSTR Str, HRESULT hr)
{
	if (SUCCEEDED(hr))
	{
        if (m_log_file != nullptr) {
            fprintf_s(m_log_file, "%ls\n", Str);
        }
		return;
	}

	const UINT StringLen = (UINT)(wcslen(Str) + sizeof(" with HRESULT 0x########."));
	wchar_t* OutStr = new wchar_t[StringLen];
	if (!OutStr)
	{
		return;
	}

	INT LenWritten = swprintf_s(OutStr, StringLen, L"%s with 0x%X.", Str, hr);
	if (LenWritten != -1)
	{
        if (m_log_file != nullptr) {
            fprintf_s(m_log_file, "%ls\n", OutStr);
        }
	}

	delete[] OutStr;
}

//
// Get DX_RESOURCES
//
DUPL_RETURN DUPLICATIONMANAGER::InitializeDx()
{

	HRESULT hr = S_OK;

	// Driver types supported
	D3D_DRIVER_TYPE DriverTypes[] =
	{
		D3D_DRIVER_TYPE_HARDWARE,
		D3D_DRIVER_TYPE_WARP,
		D3D_DRIVER_TYPE_REFERENCE,
	};
	UINT NumDriverTypes = ARRAYSIZE(DriverTypes);

	// Feature levels supported
	D3D_FEATURE_LEVEL FeatureLevels[] =
	{
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
		D3D_FEATURE_LEVEL_9_1
	};
	UINT NumFeatureLevels = ARRAYSIZE(FeatureLevels);

	D3D_FEATURE_LEVEL FeatureLevel;

	// Create device
	for (UINT DriverTypeIndex = 0; DriverTypeIndex < NumDriverTypes; ++DriverTypeIndex)
	{
		hr = D3D11CreateDevice(nullptr, DriverTypes[DriverTypeIndex], nullptr, 0, FeatureLevels, NumFeatureLevels,
			D3D11_SDK_VERSION, &m_DxRes->Device, &FeatureLevel, &m_DxRes->Context);
		if (SUCCEEDED(hr))
		{
			// Device creation success, no need to loop anymore
			break;
		}
	}
	if (FAILED(hr))
	{

		return ProcessFailure(nullptr, L"Failed to create device in InitializeDx", hr);
	}

	return DUPL_RETURN_SUCCESS;
}
DUPL_RETURN DUPLICATIONMANAGER::GetMouse()
{
    // A non-zero mouse update timestamp indicates that there is a mouse position update and optionally a shape change
    if (m_FrameInfo.LastMouseUpdateTime.QuadPart == 0)
    {
        return DUPL_RETURN_SUCCESS;
    }

    bool UpdatePosition = true;

    // Make sure we don't update pointer position wrongly
    // If pointer is invisible, make sure we did not get an update from another output that the last time that said pointer
    // was visible, if so, don't set it to invisible or update.
    if (!m_FrameInfo.PointerPosition.Visible && (m_PtrInfo.WhoUpdatedPositionLast != m_OutputNumber))
    {
        UpdatePosition = false;
    }

    // If two outputs both say they have a visible, only update if new update has newer timestamp
    if (m_FrameInfo.PointerPosition.Visible && m_PtrInfo.Visible && (m_PtrInfo.WhoUpdatedPositionLast != m_OutputNumber) && (m_PtrInfo.LastTimeStamp.QuadPart > m_FrameInfo.LastMouseUpdateTime.QuadPart))
    {
        UpdatePosition = false;
    }

    // Update position
    if (UpdatePosition)
    {
        m_PtrInfo.Position.x = m_FrameInfo.PointerPosition.Position.x + m_OutputDesc.DesktopCoordinates.left - m_OffsetX;
        m_PtrInfo.Position.y = m_FrameInfo.PointerPosition.Position.y + m_OutputDesc.DesktopCoordinates.top - m_OffsetY;
        m_PtrInfo.WhoUpdatedPositionLast = m_OutputNumber;
        m_PtrInfo.LastTimeStamp = m_FrameInfo.LastMouseUpdateTime;
        m_PtrInfo.Visible = m_FrameInfo.PointerPosition.Visible != 0;
    }

    // No new shape
    if (m_FrameInfo.PointerShapeBufferSize == 0)
    {
        return DUPL_RETURN_SUCCESS;
    }

    // Old buffer too small
    if (m_FrameInfo.PointerShapeBufferSize > m_PtrInfo.BufferSize)
    {
        if (m_PtrInfo.PtrShapeBuffer)
        {
            delete[] m_PtrInfo.PtrShapeBuffer;
            m_PtrInfo.PtrShapeBuffer = nullptr;
        }
        m_PtrInfo.PtrShapeBuffer = new (std::nothrow) BYTE[m_FrameInfo.PointerShapeBufferSize];
        if (!m_PtrInfo.PtrShapeBuffer)
        {
            m_PtrInfo.BufferSize = 0;
            return ProcessFailure(nullptr, L"Failed to allocate memory for pointer shape in DUPLICATIONMANAGER", E_OUTOFMEMORY);
        }

        // Update buffer size
        m_PtrInfo.BufferSize = m_FrameInfo.PointerShapeBufferSize;
    }

    // Get shape
    UINT BufferSizeRequired;
    HRESULT hr = m_DeskDupl->GetFramePointerShape(m_FrameInfo.PointerShapeBufferSize, reinterpret_cast<VOID*>(m_PtrInfo.PtrShapeBuffer), &BufferSizeRequired, &(m_PtrInfo.ShapeInfo));
    if (FAILED(hr))
    {
        delete[] m_PtrInfo.PtrShapeBuffer;
        m_PtrInfo.PtrShapeBuffer = nullptr;
        m_PtrInfo.BufferSize = 0;
        return ProcessFailure(m_DxRes->Device, L"Failed to get frame pointer shape in DUPLICATIONMANAGER", hr, FrameInfoExpectedErrors);
    }

    return DUPL_RETURN_SUCCESS;
}






bool DUPLICATIONMANAGER::isScreenRotate()
{
    DXGI_OUTDUPL_DESC lOutputDuplDesc;

    if (!m_DeskDupl)
    {
        return false;
    }

    m_DeskDupl->GetDesc(&lOutputDuplDesc);

    if (lOutputDuplDesc.Rotation == DXGI_MODE_ROTATION_IDENTITY)
    {
        return false;
    }
    else
    {
        return true;
    }
}

bool DUPLICATIONMANAGER::CheckBufferSize(int BufferSize)
{
    m_DxRes->Context->CopyResource(m_DestImage, m_AcquiredDesktopImage);
    D3D11_MAPPED_SUBRESOURCE resource;
    UINT subresource = D3D11CalcSubresource(0, 0, 0);
    m_DxRes->Context->Map(m_DestImage, subresource, D3D11_MAP_READ, 0, &resource);

    return !(resource.RowPitch * mHeight > BufferSize);
}