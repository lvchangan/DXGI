
#include "DuplicationManager.h"
#include <vector>
//下面列出了在模式更改、PnpStop、PnpStart等转换事件发生时，Dxgi API调用可能出现的错误
//桌面交换机、TDR或会话断开/重新连接。在所有这些情况下，我们希望应用程序清理处理的线程
//桌面将更新并尝试重新创建它们。
//如果我们得到一个不在适当列表中的错误，那么我们将退出应用程序
//这些是由于转换导致的常规Dxgi API的错误
//https://github.com/balapradeepswork/DXGICaptureApp.git
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

#define MORE_Adapter 1    //扩展屏功能

#if MORE_Adapter

struct DxgiData
{
    IDXGIAdapter* pAdapter;
    IDXGIOutput* pOutput;
};
std::vector<DxgiData> m_vOutputs;

int DUPLICATIONMANAGER::Enum()
{
    IDXGIFactory1* pFactory1;

    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)(&pFactory1));

    if (FAILED(hr))
    {
        return 0;
    }

    for (UINT i = 0;; i++)
    {
        IDXGIAdapter1* pAdapter1 = nullptr;

        hr = pFactory1->EnumAdapters1(i, &pAdapter1);       //枚举适配器

        if (hr == DXGI_ERROR_NOT_FOUND)
        {
            // no more adapters
            break;
        }

        if (FAILED(hr))
        {
            return 0;
        }

        DXGI_ADAPTER_DESC1 desc;

        hr = pAdapter1->GetDesc1(&desc);

        if (FAILED(hr))
        {
            return 0;
        }

        desc.Description;

        for (UINT j = 0;; j++)
        {
            IDXGIOutput* pOutput = nullptr;

            HRESULT hr = pAdapter1->EnumOutputs(j, &pOutput);       //枚举输出

            if (hr == DXGI_ERROR_NOT_FOUND)
            {
                // no more outputs
                break;
            }

            if (FAILED(hr))
            {
                return 0;
            }

            DXGI_OUTPUT_DESC desc;

            hr = pOutput->GetDesc(&desc);
            if (FAILED(hr))
            {
                return 0;
            }

            desc.DeviceName;
            desc.DesktopCoordinates.left;
            desc.DesktopCoordinates.top;
            int width = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
            int height = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;
            printf("width x height(%dx%d)\n", width, height);
            DxgiData data;
            data.pAdapter = pAdapter1;
            data.pOutput = pOutput;
            m_vOutputs.push_back(data);
        }
    }
    return m_vOutputs.size();
}
#endif

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

#if MORE_Adapter
    // QI for Output 1
    IDXGIOutput1* DxgiOutput1 = nullptr;
    hr = hDxgiOutput->QueryInterface(__uuidof(DxgiOutput1), reinterpret_cast<void**>(&DxgiOutput1));
    hDxgiOutput->Release();
    hDxgiOutput = nullptr;
    if (FAILED(hr))
    {
        return ProcessFailure(m_DxRes->Device, L"Failed to get specified output in DUPLICATIONMANAGER", hr, EnumOutputsExpectedErrors);
    }
#else
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

#endif

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
    printf("desc.Width = %d ,desc.Height = %d\n", desc.Width, desc.Height);
	desc.Format = lOutputDuplDesc.ModeDesc.Format;
	desc.ArraySize = 1;
	desc.BindFlags = 0;
	desc.MiscFlags = 0;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.MipLevels = 1;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	desc.Usage = D3D11_USAGE_STAGING;

    mScreenWidth = desc.Width;
    mScreenHeight = desc.Height;
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
    //1366*768分辨率  mWidth = 1366 resource.RowPitch = 1376 * 4
	m_ImagePitch = resource.RowPitch;
	if (mExtendWidth) {
		int rowBytes = mWidth * 4;
        //int rowBytes = m_ImagePitch;
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
    //将鼠标指针绘制
    if (m_PtrInfo.PtrShapeBuffer && m_PtrInfo.BufferSize > 0 && m_PtrInfo.Position.y >= 0 && m_PtrInfo.Position.x >= 0) {
        //BYTE* pDst = ImageData + resource.RowPitch * m_PtrInfo.Position.y + 4 * m_PtrInfo.Position.x;
        BYTE* pDst = ImageData + mWidth * 4 * m_PtrInfo.Position.y + 4 * m_PtrInfo.Position.x;
        //绘制一个矩形测试   在鼠标的真实宽度和所留下的宽度取最小 
        int drawHeight = min(m_PtrInfo.ShapeInfo.Height, mHeight - m_PtrInfo.Position.y);
        int drawWidth = min(m_PtrInfo.ShapeInfo.Width, mWidth - m_PtrInfo.Position.x);
        
        if (m_PtrInfo.ShapeInfo.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME) {
            //鼠标高度取一半
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

	HRESULT hr = S_OK;                      //返回结果

#if MORE_Adapter
    int nCount = Enum();
    if (nCount <= 0)
        return DUPL_RETURN_ERROR_EXPECTED;
    DxgiData data;
    pAdapter = NULL;
    hDxgiOutput = NULL;
    int nSize = m_vOutputs.size();
    if (m_nDisPlay < 0 || m_nDisPlay >= nSize)
        m_nDisPlay = 0;
    data = m_vOutputs.at(m_nDisPlay);
    hDxgiOutput = data.pOutput;
    pAdapter = data.pAdapter;
    hDxgiOutput->GetDesc(&m_OutputDesc);
#endif
    //驱动类型数组
	D3D_DRIVER_TYPE DriverTypes[] =
	{
		D3D_DRIVER_TYPE_HARDWARE,
		D3D_DRIVER_TYPE_WARP,
		D3D_DRIVER_TYPE_REFERENCE,
	};
	UINT NumDriverTypes = ARRAYSIZE(DriverTypes);

    //特征级别数组
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
#if MORE_Adapter
        hr = D3D11CreateDevice(pAdapter, D3D_DRIVER_TYPE_UNKNOWN , nullptr, 0, 0, 0,
            D3D11_SDK_VERSION, &m_DxRes->Device, &FeatureLevel, &m_DxRes->Context);
#else
		hr = D3D11CreateDevice(nullptr, DriverTypes[DriverTypeIndex], nullptr, 0, FeatureLevels, NumFeatureLevels,
			D3D11_SDK_VERSION, &m_DxRes->Device, &FeatureLevel, &m_DxRes->Context);
#endif
		if (SUCCEEDED(hr))
		{
			// Device creation success, no need to loop anymore
			break;
		}
	}
	if (FAILED(hr))
	{
        printf("CreateDevice not success\n");
		return ProcessFailure(nullptr, L"Failed to create device in InitializeDx", hr);
	}

	return DUPL_RETURN_SUCCESS;
}
DUPL_RETURN DUPLICATIONMANAGER::GetMouse()
{
    //判断鼠标形状位置是否变化
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

    // Update position   鼠标位置
    if (UpdatePosition)
    {
        m_PtrInfo.Position.x = m_FrameInfo.PointerPosition.Position.x + m_OutputDesc.DesktopCoordinates.left - m_OffsetX;
        m_PtrInfo.Position.y = m_FrameInfo.PointerPosition.Position.y + m_OutputDesc.DesktopCoordinates.top - m_OffsetY;
        m_PtrInfo.WhoUpdatedPositionLast = m_OutputNumber;
        m_PtrInfo.LastTimeStamp = m_FrameInfo.LastMouseUpdateTime;
        m_PtrInfo.Visible = m_FrameInfo.PointerPosition.Visible != 0;
    }
    /*printf("Position.x=%d,Position.y=%d|left=%d,top=%d|m_OffsetX=%d,m_OffsetY=%d|mouse_x =%d,mouse_y= %d\n", 
        m_FrameInfo.PointerPosition.Position.x,m_FrameInfo.PointerPosition.Position.y,
        m_OutputDesc.DesktopCoordinates.left, m_OutputDesc.DesktopCoordinates.top,
        m_OffsetX, m_OffsetY,
        m_PtrInfo.Position.x, m_PtrInfo.Position.x);*/
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