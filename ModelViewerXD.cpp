// ModelViewerXD.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "ModelViewerXD.h"

#include <ShObjIdl.h>
#include <string>
#include <vector>
#include <map>

#define _CRTDBG_MAP_ALLOC  
#include <stdlib.h>  
#include <crtdbg.h>  

#include <wrl.h>
#include <chrono>

// DXm
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D2d1.lib")
#pragma comment(lib, "Dwrite.lib")
#include <dxgi1_6.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <d2d1_3.h>
#include <dwrite_3.h>
using namespace DirectX;
using namespace Microsoft::WRL;

#include "WICTextureLoader.h"

#pragma region Defines

// Defines
#define MAX_LOADSTRING 100
#define SAFE_RELEASE(x) if(x) { x->Release(); x = nullptr; }

#define MAX_POINT_LIGHTS 16
#define MAX_SPOT_LIGHTS 16

enum EMenuObjectSelection
{
	eTriangleDebugSelection,
	eTriangleSelection,
	eQuadSelection,
	ePlaneSelection,
	eCubeSelection,
	eSphereSelection,
	eTotalSelection
};

#pragma endregion

#pragma region Structs

struct SVertexPC
{
	XMFLOAT3 position;
	XMFLOAT3 color;

};

struct SVertexPT
{
	XMFLOAT3 position;
	XMFLOAT2 textureUV;
};

struct SVertexPNT
{
	XMFLOAT3 position;
	XMFLOAT3 normal;
	XMFLOAT2 textureUV;
};

struct SVertexPNTT
{
	XMFLOAT3 position;
	XMFLOAT3 normal;
	XMFLOAT4 tangent;
	XMFLOAT2 textureUV;
};

struct SMesh
{
	ComPtr<ID3D11Buffer> _pVertBuf = nullptr;
	ComPtr<ID3D11Buffer> _pIdxBuf = nullptr;
	std::string _material = "";
	int vertexCnt = 0;
	int indexCnt = 0;
	int stride = 0;
};

struct SModel
{
	XMFLOAT3 _pos = XMFLOAT3(0, 0, 0);
	XMFLOAT3 _rot = XMFLOAT3(0, 0, 0);
	float _scale = 1.f;
	std::string _meshId = "";
};

struct SMaterial
{
	XMFLOAT3 _color;			// Diffuse color
	XMFLOAT3 _specularColor;
	float _specular = 0;
	float _specularIntensity = 0;
	std::string _shaderID = "";
	std::string _diffuseTextureID = "";
	std::string _normalMapTextureID = "";
	std::string _displacementMapTextureID = "";

	std::string _ID = "";
};

typedef struct _vertexCBufferStruct {
	XMFLOAT4X4 world;
	XMFLOAT4X4 view;
	XMFLOAT4X4 projection;
} VertexCBufferStruct;

typedef struct _vertexCBufferStruct2 {
	XMFLOAT4X4 world;
	XMFLOAT4X4 view;
	XMFLOAT4X4 projection;
	XMFLOAT4X4 invTransWorld;
} VertexCBufferStruct2;

typedef struct _pixelCBufferStructLightData {
	struct DirectionalLightData
	{
		XMFLOAT4 dirLight;		
		XMFLOAT4 dirLightCol;	// W Component determines if enabled
	};

	struct PointLightData
	{
		XMFLOAT4 pos;		// Attenuation in W - component
		XMFLOAT4 color;		// Intensity in W - component
	};

	struct SpotLightData
	{
		XMFLOAT4 pos;			// Attenuation in w
		XMFLOAT4 color;			// Intensity in w
		XMFLOAT4 dir;			// Cone angle in w
		float innerConeAngle;	// Maximum intensity inside of inner cone
		XMFLOAT3 pad;
	};

	DirectionalLightData	dirLightData;
	PointLightData			pointLightData[MAX_POINT_LIGHTS];
	SpotLightData			spotLightData[MAX_POINT_LIGHTS];
	XMFLOAT4				eyePos;
	XMFLOAT4				specularColor;
	UINT					activePointLights = 0;
	UINT					activeSpotLights = 0;
	float					specular = 16.f;
	int						pad;
} PixelCBufferStructLightData;

#pragma endregion

#pragma region Classes

// DirectX device/context handler
class CDirectX
{
public:
	CDirectX() = default;
	~CDirectX() 
	{
		_winWidth = 0;
		_winHeight = 0;
	}

	bool Initialize(HWND hWnd, UINT winWidth, UINT winHeight)
	{
		_winWidth = winWidth;
		_winHeight = winHeight;

		if (!InitWindowIndependant())
			return false;

		if (!InitWindowDependant(hWnd))
			return false;

		return true;
	}

	bool ReInitWindowDependant(HWND HWND, UINT winWidth, UINT winHeight)
	{
		_winWidth = winWidth;
		_winHeight = winHeight;

		// Remove old window dependant resources and re-create
		SAFE_RELEASE(_pDepthStencilBuf);
		SAFE_RELEASE(_pDepthStencil);
		SAFE_RELEASE(_pTarget);
		SAFE_RELEASE(_pSwapChain);

		if (!InitWindowDependant(HWND))
			return false;

		return true;
	}

	void Begin(XMFLOAT4 clearCol) 
	{
		float col[] = { clearCol.x, clearCol.y, clearCol.z, clearCol.w };
		_pContext->ClearRenderTargetView(_pTarget.Get(), col);
		_pContext->ClearDepthStencilView(_pDepthStencil.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.f, 0);
	}

	void End()
	{
		_pSwapChain->Present(0, 0);

		if (_pSwapChain1)
		{
			// Present() unbinds the render target for SwapChain1 (DX 11.1) when using flip mode.
			_pContext->OMSetRenderTargets(1,
				_pTarget.GetAddressOf(), _pDepthStencil.Get());
		}
	}

	void SetWireFrame(bool bEnable)
	{
		ID3D11RasterizerState* rasterizeState;

		if (bEnable)
		{
			_rasterState.FillMode = D3D11_FILL_WIREFRAME;
			HRESULT result = _pDevice->CreateRasterizerState(&_rasterState, &rasterizeState);
			if (SUCCEEDED(result))
				_pContext->RSSetState(rasterizeState);
		}
		else
		{
			_rasterState.FillMode = D3D11_FILL_SOLID;
			HRESULT result = _pDevice->CreateRasterizerState(&_rasterState, &rasterizeState);
			if (SUCCEEDED(result))
				_pContext->RSSetState(rasterizeState);
		}
		
		
		

		SAFE_RELEASE(rasterizeState);
	}

	ComPtr<ID3D11Device> GetDevice() { return _pDevice; }
	ComPtr<ID3D11Device1> GetDevice1() { return _pDevice1; }
	ComPtr<ID3D11DeviceContext> GetContext() { return _pContext; }
	ComPtr<ID3D11DeviceContext1> GetContext1() { return _pContext1; }
	ComPtr<IDXGISwapChain> GetSwapChain() { return _pSwapChain; }
	ComPtr<IDXGISwapChain1> GetSwapChain1() { return _pSwapChain1; }

	ComPtr<ID3D11RenderTargetView> GetRenderTargetView() { return _pTarget; }
	ComPtr<ID3D11DepthStencilView> GetDepthStencilView() { return _pDepthStencil; }

	D3D_FEATURE_LEVEL GetFeatureLevel() const { return _featureLevel; }

	UINT GetWindowWidth() { return _winWidth; }
	UINT GetWindowHeight() { return _winHeight; }

private:
	bool InitWindowIndependant()
	{
		// Device flags
		UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
		flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

		D3D_FEATURE_LEVEL levels[] =
		{
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
			D3D_FEATURE_LEVEL_10_1,
			D3D_FEATURE_LEVEL_10_0,
		};
		UINT numFeatureLevels = ARRAYSIZE(levels);

		// Create device and context
		HRESULT result = D3D11CreateDevice(
			NULL,
			D3D_DRIVER_TYPE_HARDWARE,
			NULL,
			flags,
			levels,
			numFeatureLevels,
			D3D11_SDK_VERSION,
			_pDevice.ReleaseAndGetAddressOf(),
			&_featureLevel,
			_pContext.ReleaseAndGetAddressOf()
		);
		if (FAILED(result))
			return false;

		// Using ComPtr we can use the below to check if DirectX 11.1 is supported
		if (SUCCEEDED(_pDevice.As(&_pDevice1)))
		{
			_pContext.As(&_pContext1);
		}

		// Setup raster state
		_rasterState.FillMode = D3D11_FILL_SOLID;
		_rasterState.CullMode = D3D11_CULL_BACK;
		_rasterState.FrontCounterClockwise = 0;
		_rasterState.DepthBias = FALSE;
		_rasterState.DepthBiasClamp = 0.f;
		_rasterState.SlopeScaledDepthBias = 0.f;
		_rasterState.DepthClipEnable = TRUE;
		_rasterState.ScissorEnable = FALSE;
		_rasterState.MultisampleEnable = FALSE;
		_rasterState.AntialiasedLineEnable = FALSE;

		ID3D11RasterizerState* rasterizeState;
		result = _pDevice->CreateRasterizerState(&_rasterState, &rasterizeState);
		if (FAILED(result))
			return false;
		_pContext->RSSetState(rasterizeState);
		SAFE_RELEASE(rasterizeState);

		// Setup texture sampler state
		_samplerState.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		_samplerState.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		_samplerState.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		_samplerState.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		_samplerState.MipLODBias = 0.0f;
		_samplerState.MaxAnisotropy = 1;
		_samplerState.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
		_samplerState.BorderColor[0] = 0;
		_samplerState.BorderColor[1] = 0;
		_samplerState.BorderColor[2] = 0;
		_samplerState.BorderColor[3] = 0;
		_samplerState.MinLOD = 0;
		_samplerState.MaxLOD = D3D11_FLOAT32_MAX;

		ID3D11SamplerState* samplerState;
		result = _pDevice->CreateSamplerState(&_samplerState, &samplerState);
		if (FAILED(result))
			return false;
		_pContext->PSSetSamplers(0, 1, &samplerState);
		SAFE_RELEASE(samplerState);

		return true;
	}

	bool InitWindowDependant(HWND hWnd)
	{
		HRESULT result;
		ComPtr<IDXGIFactory1> dxgiFactory;
		{
			ComPtr<IDXGIDevice> dxgiDev;
			result = _pDevice.As(&dxgiDev);
			if (FAILED(result))
				return false;

			ComPtr<IDXGIAdapter> adapter;
			result = dxgiDev->GetAdapter(&adapter);
			if (FAILED(result))
				return false;

			result = adapter->GetParent(IID_PPV_ARGS(&dxgiFactory));
			if (FAILED(result))
				return false;
		}

		// Get factory to create swap chain
		ComPtr<IDXGIFactory2> dxgiFactory2;
		result = dxgiFactory.As(&dxgiFactory2);
		// If factory retrieval failed
		if (FAILED(result))
			return false;

		if (dxgiFactory2)
		{
			// DirectX 11.1 or later
			// Swap chain desc
			DXGI_SWAP_CHAIN_DESC1 sd;
			ZeroMemory(&sd, sizeof(sd));
			sd.Width = _winWidth;
			sd.Height = _winHeight;
			sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			sd.BufferCount = 2;
			sd.SampleDesc.Count = 1;
			sd.SampleDesc.Quality = 0;
			sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

			result = dxgiFactory2->CreateSwapChainForHwnd(_pDevice.Get(), hWnd, &sd, NULL, NULL, _pSwapChain1.ReleaseAndGetAddressOf());
			if (FAILED(result))
				return false;

			result = _pSwapChain1.As(&_pSwapChain);
			if (FAILED(result))
				return false;
		}
		else
		{
			// DirectX 11.0 systems
			// Swap chain desc
			DXGI_SWAP_CHAIN_DESC sd;
			ZeroMemory(&sd, sizeof(sd));
			sd.BufferDesc.Width = _winWidth;
			sd.BufferDesc.Height = _winHeight;
			sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			sd.BufferDesc.RefreshRate.Numerator = 60;
			sd.BufferDesc.RefreshRate.Denominator = 1;
			sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
			sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
			sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			sd.OutputWindow = hWnd;
			sd.BufferCount = 2;
			sd.SampleDesc.Count = 1;
			sd.SampleDesc.Quality = 0;
			sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;	// No multi-sampling
			sd.Windowed = TRUE;

			result = dxgiFactory->CreateSwapChain(_pDevice.Get(), &sd, &_pSwapChain);
		}
		// Create render target
		ComPtr<ID3D11Texture2D> backBuff;
		result = _pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(backBuff.ReleaseAndGetAddressOf()));
		if (FAILED(result))
			return false;

		result = _pDevice->CreateRenderTargetView(backBuff.Get(), nullptr, _pTarget.ReleaseAndGetAddressOf());
		if (FAILED(result))
			return false;

		// Create depth stencil stuff
		D3D11_TEXTURE2D_DESC backBuffer;
		ZeroMemory(&backBuffer, sizeof(backBuffer));
		backBuffer.Width = _winWidth;
		backBuffer.Height = _winHeight;
		backBuffer.MipLevels = 1;
		backBuffer.ArraySize = 1;
		backBuffer.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		backBuffer.Usage = D3D11_USAGE_DEFAULT;
		backBuffer.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		backBuffer.CPUAccessFlags = 0;
		backBuffer.MiscFlags = 0;
		backBuffer.SampleDesc.Count = 1;
		backBuffer.SampleDesc.Quality = 0;

		result = _pDevice->CreateTexture2D(&backBuffer, nullptr, _pDepthStencilBuf.ReleaseAndGetAddressOf());
		if (FAILED(result))
			return false;

		D3D11_DEPTH_STENCIL_DESC depthStencilDesc;
		ZeroMemory(&depthStencilDesc, sizeof(depthStencilDesc));

		// Set up the description of the stencil state.
		depthStencilDesc.DepthEnable = true;
		depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS;

		depthStencilDesc.StencilEnable = true;
		depthStencilDesc.StencilReadMask = 0xFF;
		depthStencilDesc.StencilWriteMask = 0xFF;

		// Stencil operations if pixel is front-facing.
		depthStencilDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		depthStencilDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR;
		depthStencilDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
		depthStencilDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

		// Stencil operations if pixel is back-facing.
		depthStencilDesc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		depthStencilDesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_DECR;
		depthStencilDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
		depthStencilDesc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

		result = _pDevice->CreateDepthStencilState(&depthStencilDesc, _depthStencilOn.ReleaseAndGetAddressOf());

		// Set depth stencil state
		_pContext->OMSetDepthStencilState(_depthStencilOn.Get(), 1);

		// Create 2nd depth stencil state with depth off
		depthStencilDesc.DepthEnable = false;
		result = _pDevice->CreateDepthStencilState(&depthStencilDesc, _depthStencilOff.ReleaseAndGetAddressOf());
		if (FAILED(result))
			return false;

		D3D11_DEPTH_STENCIL_VIEW_DESC descDSV;
		ZeroMemory(&descDSV, sizeof(D3D11_DEPTH_STENCIL_VIEW_DESC));
		descDSV.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		descDSV.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		descDSV.Texture2D.MipSlice = 0;

		result = _pDevice->CreateDepthStencilView(_pDepthStencilBuf.Get(), &descDSV, _pDepthStencil.ReleaseAndGetAddressOf());
		if (FAILED(result))
			return false;

		// Bind merger output
		_pContext->OMSetRenderTargets(1,
			_pTarget.GetAddressOf(), _pDepthStencil.Get());

		// Just going to always use the full client area
		D3D11_VIEWPORT vp;
		vp.TopLeftX = 0.0f;
		vp.TopLeftY = 0.0f;
		vp.Width = static_cast<float>(_winWidth);
		vp.Height = static_cast<float>(_winHeight);
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;

		_pContext->RSSetViewports(1,
			&vp);

		return true;
	}

private:
	ComPtr<ID3D11Device>				_pDevice = nullptr;
	ComPtr<ID3D11DeviceContext>			_pContext = nullptr;
	ComPtr<ID3D11Device1>				_pDevice1 = nullptr;
	ComPtr<ID3D11DeviceContext1>		_pContext1 = nullptr;

	ComPtr<IDXGISwapChain>				_pSwapChain = nullptr;
	ComPtr<IDXGISwapChain1>				_pSwapChain1 = nullptr;
	ComPtr<ID3D11RenderTargetView>		_pTarget = nullptr;
	ComPtr<ID3D11DepthStencilView>		_pDepthStencil = nullptr;
	ComPtr<ID3D11Texture2D>				_pDepthStencilBuf = nullptr;
	D3D11_RASTERIZER_DESC				_rasterState;
	D3D11_SAMPLER_DESC					_samplerState;

	ComPtr<ID3D11DepthStencilState>		_depthStencilOn = nullptr;
	ComPtr<ID3D11DepthStencilState>		_depthStencilOff = nullptr;

	D3D_FEATURE_LEVEL					_featureLevel;

	UINT _winWidth = 0;
	UINT _winHeight = 0;
};

// Handles 2D stuff, requires CDirectX
class CDirect2D
{
public:
	CDirect2D() = default;
	~CDirect2D() {}

	bool Initialise(CDirectX* pDX)
	{
		bool result = false;
		if (pDX == nullptr)
			return result;

		_directX = pDX;

		if (CreateDevice())
		{
			if (CreateBitmapRenderTarget())
			{
				if (InitialiseTextFormats())
				{
					result = true;
					_bInit = true;
				}
			}
		}

		return result;
	}

	void SetRenderText(const std::string text)
	{
		if (!_bInit)
			return;

		HRESULT result = S_OK;
		if (text != _textToDraw)
		{
			// Create text
			std::wstring drawText = std::wstring(text.begin(), text.end());
			HRESULT result = _writeFactory2->CreateTextLayout(drawText.c_str(), static_cast<UINT32>(drawText.size()), _textFormat.Get(),
				static_cast<float>(_directX->GetWindowWidth()), static_cast<float>(_directX->GetWindowHeight()),
				_textLayout.ReleaseAndGetAddressOf());

			if (SUCCEEDED(result))
			{
				_textLayout->GetMetrics(&_textMetrics);
				_textToDraw = text;
			}
		}
	}

	float GetRenderTextWidth()	{ return _textMetrics.width; }

	float GetRenderTextHeight() { return _textMetrics.height; }

	// Called within Begin/End draw calls (CDirectX)
	void RenderText(UINT x, UINT y, const std::string& text)
	{
		if (!_bInit)
			return;

		HRESULT result = S_OK;
		if (text != _textToDraw)
		{
			// Create text
			std::wstring drawText = std::wstring(text.begin(), text.end());
			HRESULT result = _writeFactory2->CreateTextLayout(drawText.c_str(), static_cast<UINT32>(drawText.size()), _textFormat.Get(),
				static_cast<float>(_directX->GetWindowWidth()), static_cast<float>(_directX->GetWindowHeight()),
				_textLayout.ReleaseAndGetAddressOf());

			if (SUCCEEDED(result))
			{
				_textLayout->GetMetrics(&_textMetrics);
				_textToDraw = text;
			}
		}

		if (SUCCEEDED(result))
		{
			// Draw the text
			_context1->BeginDraw();
			_context1->DrawTextLayout(D2D1::Point2F(static_cast<float>(x), static_cast<float>(y)), _textLayout.Get(), _colorBrush.Get());
			_context1->EndDraw();
		}
	}
	
	void RenderText(UINT x, UINT y)
	{
		if (!_bInit)
			return;

		if (_textLayout.Get())
		{
			// Draw the text
			_context1->BeginDraw();
			_context1->DrawTextLayout(D2D1::Point2F(static_cast<float>(x), static_cast<float>(y)), _textLayout.Get(), _colorBrush.Get());
			_context1->EndDraw();
		}
	}

private:
	bool CreateDevice()
	{
		// DWrite factory
		HRESULT result = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
			reinterpret_cast<IUnknown**>(_writeFactory2.ReleaseAndGetAddressOf()));
		if (FAILED(result))
			return false;

		// Get the DXGI device
		ComPtr<IDXGIDevice> dxgiDevice = nullptr;
		result = _directX->GetDevice()->QueryInterface(__uuidof(IDXGIDevice),
			reinterpret_cast<void**>(dxgiDevice.ReleaseAndGetAddressOf()));
		if (FAILED(result))
			return false;

		// Create the Direct2D factory
		D2D1_FACTORY_OPTIONS options;
#if defined (DEBUG) | defined(_DEBUG)
		options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#else
		options.debugLevel = D2D1_DEBUG_LEVEL_NONE;
#endif
		ComPtr<ID2D1Factory2> factory = nullptr;
		result = D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory2),
			&options, reinterpret_cast<void**>(factory.ReleaseAndGetAddressOf()));
		if (FAILED(result))
			return false;

		// Create the Direct2D device
		result = factory->CreateDevice(dxgiDevice.Get(), _device1.ReleaseAndGetAddressOf());
		if (FAILED(result))
			return false;

		// Create the context
		result = _device1->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_ENABLE_MULTITHREADED_OPTIMIZATIONS,
			&_context1);
		if (FAILED(result))
			return false;

		return true;
	}

	bool CreateBitmapRenderTarget()
	{
		ID2D1Image* pImage = nullptr;
		_context1->GetTarget(&pImage);

		if (pImage != nullptr)
		{
			// Release! We're deferencing the counter and not deleting the it.
			// Else check against m_targetBitmap and not bother with the GetTarget().
			pImage->Release();
			return false;
		}

		// Bitmap properties
		D2D1_BITMAP_PROPERTIES1 bProps;
		bProps.pixelFormat.format = DXGI_FORMAT_R8G8B8A8_UNORM;
		bProps.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
		bProps.dpiX = 96.0f;
		bProps.dpiY = 96.0f;
		bProps.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
		bProps.colorContext = nullptr;

		// Get DXGI version of the backbuffer for Direct2D
		ComPtr<IDXGISurface> dxgiBuffer = nullptr;
		IDXGISwapChain* swapChain = _directX->GetSwapChain().Get();
		if (swapChain == nullptr)
			return false;

		HRESULT result = swapChain->GetBuffer(0, __uuidof(IDXGISurface), reinterpret_cast<void**>(dxgiBuffer.ReleaseAndGetAddressOf()));

		if (SUCCEEDED(result))
		{
			// Create the bitmap
			result = _context1->CreateBitmapFromDxgiSurface(dxgiBuffer.Get(), &bProps, _targetBitmap1.ReleaseAndGetAddressOf());
			if (SUCCEEDED(result))
			{
				// Set newly created bitmap as render target
				_context1->SetTarget(_targetBitmap1.Get());
				return true;
			}
		}

		return false;
	}

	bool InitialiseTextFormats()
	{
		HRESULT result;
		result = _context1->CreateSolidColorBrush(D2D1::ColorF(0, 255, 0), _colorBrush.ReleaseAndGetAddressOf());
		if (FAILED(result))
			return false;

		result = _writeFactory2->CreateTextFormat(L"Lucudia Console", nullptr, DWRITE_FONT_WEIGHT_LIGHT,
			DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"en-GB", _textFormat.ReleaseAndGetAddressOf());
		if (FAILED(result))
			return false;

		result = _textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
		if (FAILED(result))
			return false;

		result = _textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
		if (FAILED(result))
			return false;

		return true;
	}

private:
	CDirectX* _directX;

	ComPtr<IDWriteFactory2>			_writeFactory2 = nullptr;
	ComPtr<ID2D1Device1>			_device1 = nullptr;
	ComPtr<ID2D1DeviceContext1>		_context1 = nullptr;

	ComPtr<ID2D1SolidColorBrush>	_colorBrush = nullptr;

	ComPtr<IDWriteTextFormat>		_textFormat = nullptr;
	ComPtr<IDWriteTextLayout>		_textLayout = nullptr;

	ComPtr<ID2D1Bitmap1>			_targetBitmap1 = nullptr;

	DWRITE_TEXT_METRICS				_textMetrics;
	std::string						_textToDraw = "";

	bool							_bInit = false;
};

// Shader management
class CShaderMGR
{
public:
	struct SShader
	{
		ComPtr<ID3D11VertexShader> _vertexShader = nullptr;
		ComPtr<ID3D11PixelShader> _pixelShader = nullptr;
		ComPtr<ID3D11InputLayout> _inputLayout = nullptr;
		std::string _id = "";
	};

	struct SCBuffer
	{
		ComPtr<ID3D11Buffer> _cBuffer = nullptr;
		std::string _id = "";
	};

public:
	CShaderMGR() = default;
	~CShaderMGR() {}

	bool Initialise(CDirectX* pDx)
	{
		_pDirectX = pDx;
		return true;
	}

	bool LoadShader(const std::string& vertexShaderFile, const std::string& pixelShaderFile, D3D11_INPUT_ELEMENT_DESC* desc, int inputElements, const std::string& assignID)
	{
		// If not initialised
		if (_pDirectX == nullptr)
			return false;

		_shaders.push_back(SShader());
		SShader* pShader = &_shaders.back();
		pShader->_id = assignID;
		if (LoadVertexShader(pShader, vertexShaderFile, desc, inputElements))
		{
			if (LoadPixelShader(pShader, pixelShaderFile))
				return true;
		}
		
		// Failed if we reach here
		_shaders.pop_back();
		return false;
	}

	bool BindConstant(const std::string& id, CD3D11_BUFFER_DESC* desc)
	{
		_cBuffers.push_back(SCBuffer());
		SCBuffer* pBuffer = &_cBuffers.back();
		pBuffer->_id = id;
		HRESULT result = _pDirectX->GetDevice()->CreateBuffer(desc, nullptr, pBuffer->_cBuffer.ReleaseAndGetAddressOf());
		if (FAILED(result))
		{
			_cBuffers.pop_back();
			return false;
		}
		
		return true;
	}

	SShader* GetShader(UINT handle)
	{
		if (handle < static_cast<int>(_shaders.size()))
			return &_shaders[handle];

		return nullptr;
	}
	SShader* GetShader(const std::string& id)
	{
		for (int i = 0; i < static_cast<int>(_shaders.size()); i++)
		{
			if (_shaders[i]._id == id)
				return GetShader(i);
		}

		return nullptr;
	}

	ComPtr<ID3D11Buffer> GetCBuffer(const std::string& id)
	{
		for (auto it = _cBuffers.begin(); it != _cBuffers.end(); it++)
		{
			if (it->_id == id)
				return it->_cBuffer.Get();
		}
		return nullptr;
	}

	ComPtr<ID3D11Buffer> GetCBuffer(UINT handle)
	{
		if (handle < static_cast<int>(_cBuffers.size()))
			return _cBuffers[handle]._cBuffer.Get();

		return nullptr;
	}

private:
	bool LoadVertexShader(SShader* pShader, const std::string& vertexShaderFile, D3D11_INPUT_ELEMENT_DESC* desc, int inputElements)
	{
		UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(DEBUG) || defined(_DEBUG)
		flags |= D3DCOMPILE_DEBUG;
#endif

		ID3DBlob* shaderBlob = nullptr;
		ID3DBlob* errorBlob = nullptr;
		std::wstring ws(vertexShaderFile.begin(), vertexShaderFile.end());
		HRESULT result = D3DCompileFromFile(ws.c_str(),
			NULL, D3D_COMPILE_STANDARD_FILE_INCLUDE, "Main", "vs_5_0", flags,
			0, &shaderBlob, &errorBlob);

		if (FAILED(result))
		{
			if (errorBlob)
			{
				OutputDebugStringA((char*)errorBlob->GetBufferPointer());
				errorBlob->Release();
			}

			if (shaderBlob)
				shaderBlob->Release();

			return false;
		}

		// Create vertex shader
		result = _pDirectX->GetDevice()->CreateVertexShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, pShader->_vertexShader.ReleaseAndGetAddressOf());
		if (FAILED(result))
		{
			shaderBlob->Release();
			return false;
		}

		result = _pDirectX->GetDevice()->CreateInputLayout(desc, inputElements, shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), pShader->_inputLayout.ReleaseAndGetAddressOf());
		if (FAILED(result))
		{
			shaderBlob->Release();
			return false;
		}
		shaderBlob->Release();

		return true;
	}

	bool LoadPixelShader(SShader* pShader, const std::string& pixelShaderFile)
	{
		UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(DEBUG) || defined(_DEBUG)
		flags |= D3DCOMPILE_DEBUG;
#endif

		ID3DBlob* shaderBlob = nullptr;
		ID3DBlob* errorBlob = nullptr;
		std::wstring ws(pixelShaderFile.begin(), pixelShaderFile.end());
		HRESULT result = D3DCompileFromFile(ws.c_str(),
			NULL, D3D_COMPILE_STANDARD_FILE_INCLUDE, "Main", "ps_5_0", flags,
			0, &shaderBlob, &errorBlob);

		if (FAILED(result))
		{
			if (errorBlob)
			{
				OutputDebugStringA((char*)errorBlob->GetBufferPointer());
				errorBlob->Release();
			}

			if (shaderBlob)
				shaderBlob->Release();

			return false;
		}

		result = _pDirectX->GetDevice()->CreatePixelShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, pShader->_pixelShader.ReleaseAndGetAddressOf());
		if (FAILED(result))
		{
			shaderBlob->Release();
			return false;
		}

		shaderBlob->Release();
		return true;
	}

private:
	std::vector<SShader> _shaders;
	std::vector<SCBuffer> _cBuffers;
	CDirectX* _pDirectX = nullptr;
};

class CMaterialMGR
{
public:
	CMaterialMGR() = default;
	~CMaterialMGR() {}

	bool AddMaterial(const std::string assignMaterialID, const SMaterial& mat)
	{
		for (auto it = _materials.begin(); it != _materials.end(); it++)
		{
			// If already a material with existing ID.
			if (it->_ID == assignMaterialID)
				return false;
		}

		_materials.push_back(SMaterial());
		SMaterial* pMat = &_materials.back();

		*pMat = mat;
		pMat->_ID = assignMaterialID;
		return true;
	}

	SMaterial* GetMaterial(const std::string& materialID)
	{
		for (int i = 0; i < static_cast<int>(_materials.size()); i++)
		{
			if (materialID == _materials[i]._ID)
				return &_materials[i];
		}

		return nullptr;
	}

private:
	std::vector<SMaterial> _materials;
};

class CMeshMGR
{
public:
	CMeshMGR() = default;
	~CMeshMGR() {}

	bool Init(CDirectX* dxHdl) { _dxHandle = dxHdl; return true; }

	// Meshes with vertices which include Position, Color
	bool CreateTriangleDebugPC(const std::string& id, float size, const std::string& materialID)
	{
		if (_dxHandle == nullptr)
			return false;

		auto it = _mapIdx.find(id);
		if (it != _mapIdx.end())
			return false;

		// The center of the triangle is based around the origin (0,0,0).
		// Triangle is facing directly down the Z.
		float halfX = size / 2;
		float halfY = size / 2;

		SVertexPC pVerts[] = {
			XMFLOAT3(-halfX, -halfY, 0),
			XMFLOAT3(1, 0, 0),
			XMFLOAT3(0, halfY, 0),
			XMFLOAT3(0, 1, 0),
			XMFLOAT3(halfX, -halfY, 0),
			XMFLOAT3(0, 0, 1)
		};

		_meshes.push_back(SMesh());
		auto pMesh = &_meshes.back();

		// Vert buffer desc
		D3D11_BUFFER_DESC vertDesc;
		vertDesc.Usage = D3D11_USAGE_DEFAULT;
		vertDesc.ByteWidth = sizeof(SVertexPC) * 3;
		vertDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vertDesc.CPUAccessFlags = 0;
		vertDesc.MiscFlags = 0;

		// Vert buffer subresource data
		D3D11_SUBRESOURCE_DATA vertData;
		vertData.pSysMem = pVerts;
		vertData.SysMemPitch = 0;
		vertData.SysMemSlicePitch = 0;

		// Create vertex buffer
		HRESULT result = _dxHandle->GetDevice()->CreateBuffer(&vertDesc, &vertData, pMesh->_pVertBuf.ReleaseAndGetAddressOf());
		if (FAILED(result))
		{
			_meshes.pop_back();
			return false;
		}

		// Index data
		UINT triangleIdx[3] = {
			0, 1, 2
		};

		// Index buffer desc
		D3D11_BUFFER_DESC indexDesc;
		indexDesc.Usage = D3D11_USAGE_DEFAULT;
		indexDesc.ByteWidth = sizeof(UINT) * 3;
		indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		indexDesc.CPUAccessFlags = 0;
		indexDesc.MiscFlags = 0;

		// Index buffer subresource data
		D3D11_SUBRESOURCE_DATA indexData;
		indexData.pSysMem = triangleIdx;
		indexData.SysMemPitch = 0;
		indexData.SysMemSlicePitch = 0;

		// Create index buffer
		result = _dxHandle->GetDevice()->CreateBuffer(&indexDesc, &indexData, pMesh->_pIdxBuf.ReleaseAndGetAddressOf());
		if (FAILED(result))
		{
			_meshes.pop_back();
			return false;
		}

		pMesh->indexCnt = 3;
		pMesh->vertexCnt = 3;
		pMesh->stride = sizeof(SVertexPC);
		pMesh->_material = materialID;

		// Add to map
		_mapIdx.insert(std::pair<std::string, int>(id, static_cast<int>(_meshes.size()) - 1));
		return true;
	}
	bool CreateTrianglePC(const std::string& id, float size, const std::string& materialID, const XMFLOAT3& color)
	{
		if (_dxHandle == nullptr)
			return false;

		auto it = _mapIdx.find(id);
		if (it != _mapIdx.end())
			return false;

		// The center of the triangle is based around the origin (0,0,0).
		// Triangle is facing directly down the Z.
		float halfX = size / 2;
		float halfY = size / 2;

		SVertexPC pVerts[] = {
			XMFLOAT3(-halfX, -halfY, 0),
			XMFLOAT3(color),
			XMFLOAT3(0, halfY, 0),
			XMFLOAT3(color),
			XMFLOAT3(halfX, -halfY, 0),
			XMFLOAT3(color)
		};

		_meshes.push_back(SMesh());
		auto pMesh = &_meshes.back();

		// Vert buffer desc
		D3D11_BUFFER_DESC vertDesc;
		vertDesc.Usage = D3D11_USAGE_DEFAULT;
		vertDesc.ByteWidth = sizeof(SVertexPC) * 3;
		vertDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vertDesc.CPUAccessFlags = 0;
		vertDesc.MiscFlags = 0;

		// Vert buffer subresource data
		D3D11_SUBRESOURCE_DATA vertData;
		vertData.pSysMem = pVerts;
		vertData.SysMemPitch = 0;
		vertData.SysMemSlicePitch = 0;

		// Create vertex buffer
		HRESULT result = _dxHandle->GetDevice()->CreateBuffer(&vertDesc, &vertData, pMesh->_pVertBuf.ReleaseAndGetAddressOf());
		if (FAILED(result))
		{
			_meshes.pop_back();
			return false;
		}

		// Index data
		UINT triangleIdx[3] = {
			0, 1, 2
		};

		// Index buffer desc
		D3D11_BUFFER_DESC indexDesc;
		indexDesc.Usage = D3D11_USAGE_DEFAULT;
		indexDesc.ByteWidth = sizeof(UINT) * 3;
		indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		indexDesc.CPUAccessFlags = 0;
		indexDesc.MiscFlags = 0;

		// Index buffer subresource data
		D3D11_SUBRESOURCE_DATA indexData;
		indexData.pSysMem = triangleIdx;
		indexData.SysMemPitch = 0;
		indexData.SysMemSlicePitch = 0;

		// Create index buffer
		result = _dxHandle->GetDevice()->CreateBuffer(&indexDesc, &indexData, pMesh->_pIdxBuf.ReleaseAndGetAddressOf());
		if (FAILED(result))
		{
			_meshes.pop_back();
			return false;
		}

		pMesh->indexCnt = 3;
		pMesh->vertexCnt = 3;
		pMesh->stride = sizeof(SVertexPC);
		pMesh->_material = materialID;

		// Add to map
		_mapIdx.insert(std::pair<std::string, int>(id, static_cast<int>(_meshes.size()) - 1));

		return true;
	}
	bool CreateQuadPC(const std::string& id, float size, const std::string& materialID, const XMFLOAT3& color)
	{
		if (_dxHandle == nullptr)
			return false;

		auto find = _mapIdx.find(id);
		if (find != _mapIdx.end())
			return false;

		float halfX = size / 2.f;
		float halfY = size / 2.f;

		SVertexPC quad[] = {
			XMFLOAT3(-halfX, halfY, 0),
			color,
			XMFLOAT3(halfX, halfY, 0),
			color,
			XMFLOAT3(-halfX, -halfY, 0),
			color,
			XMFLOAT3(halfX, -halfY, 0),
			color,
		};

		_meshes.push_back(SMesh());
		auto pMesh = &_meshes.back();

		// Vert buffer desc
		D3D11_BUFFER_DESC vertDesc;
		vertDesc.Usage = D3D11_USAGE_DEFAULT;
		vertDesc.ByteWidth = sizeof(SVertexPC) * 4;
		vertDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vertDesc.CPUAccessFlags = 0;
		vertDesc.MiscFlags = 0;

		// Vert buffer subresource data
		D3D11_SUBRESOURCE_DATA vertData;
		vertData.pSysMem = quad;
		vertData.SysMemPitch = 0;
		vertData.SysMemSlicePitch = 0;

		// Create vertex buffer
		HRESULT result = _dxHandle->GetDevice()->CreateBuffer(&vertDesc, &vertData, &pMesh->_pVertBuf);
		if (FAILED(result))
		{
			_meshes.pop_back();
			return false;
		}

		// Index data
		UINT triangleIdx[6] = {
			0, 3, 2,
			0, 1, 3
		};

		// Index buffer desc
		D3D11_BUFFER_DESC indexDesc;
		indexDesc.Usage = D3D11_USAGE_DEFAULT;
		indexDesc.ByteWidth = sizeof(UINT) * 6;
		indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		indexDesc.CPUAccessFlags = 0;
		indexDesc.MiscFlags = 0;

		// Index buffer subresource data
		D3D11_SUBRESOURCE_DATA indexData;
		indexData.pSysMem = triangleIdx;
		indexData.SysMemPitch = 0;
		indexData.SysMemSlicePitch = 0;

		// Create index buffer
		result = _dxHandle->GetDevice()->CreateBuffer(&indexDesc, &indexData, &pMesh->_pIdxBuf);
		if (FAILED(result))
		{
			_meshes.pop_back();
			return false;
		}

		pMesh->indexCnt = 6;
		pMesh->vertexCnt = 4;
		pMesh->stride = sizeof(SVertexPC);
		pMesh->_material = materialID;

		// Add to map
		_mapIdx.insert(std::pair<std::string, int>(id, static_cast<int>(_meshes.size()) - 1));
		return true;
	}
	bool CreatePlanePC(const std::string& id, float size, const std::string& materialID, const XMFLOAT3& color, int subDiv = 1)
	{
		if (_dxHandle == nullptr)
			return false;

		// Existing check
		auto find = _mapIdx.find(id);
		if (find != _mapIdx.end())
			return false;

		// subDiv check
		if (subDiv < 1)
			subDiv = 1;

		float minX = -(size / 2);
		float minY = -(size / 2);

		float xInc = size / (float)(subDiv + 1);
		float yInc = size / (float)(subDiv + 1);

		int totalVerts = (subDiv + 1) * (subDiv + 1);
		// Generate the vertices 
		SVertexPC* pVerts = new SVertexPC[totalVerts];
		int counter = 0;
		for (int y = 0; y < subDiv + 1; y++)
		{
			for (int x = 0; x < subDiv + 1; x++)
			{
				pVerts[counter].position.x = minX + (xInc * x);
				pVerts[counter].position.y = 0;
				pVerts[counter].position.z = (-minY) - (yInc * y);
				pVerts[counter].color = color;
				counter++;
			}
		}

		// New mesh
		_meshes.push_back(SMesh());
		auto pMesh = &_meshes.back();

		// Vert buffer desc
		D3D11_BUFFER_DESC vertDesc;
		vertDesc.Usage = D3D11_USAGE_DEFAULT;
		vertDesc.ByteWidth = sizeof(SVertexPC) * totalVerts;
		vertDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vertDesc.CPUAccessFlags = 0;
		vertDesc.MiscFlags = 0;

		// Vert buffer subresource data
		D3D11_SUBRESOURCE_DATA vertData;
		vertData.pSysMem = pVerts;
		vertData.SysMemPitch = 0;
		vertData.SysMemSlicePitch = 0;

		// Create vertex buffer
		HRESULT result = _dxHandle->GetDevice()->CreateBuffer(&vertDesc, &vertData, &pMesh->_pVertBuf);
		if (FAILED(result))
		{
			_meshes.pop_back();
			delete[] pVerts;
			return false;
		}

		// Index data
		int totalIdx = subDiv * subDiv * 6;
		UINT* pIdx = new UINT[totalIdx];
		int cntIdx = 0;
		int vIdx = 0;
		for (int yFace = 0; yFace < subDiv; yFace++)
		{
			for (int xFace = 0; xFace < subDiv; xFace++)
			{
				pIdx[cntIdx++] = vIdx;
				pIdx[cntIdx++] = vIdx + (subDiv + 1) + 1;
				pIdx[cntIdx++] = vIdx + (subDiv + 1);
				pIdx[cntIdx++] = vIdx;
				pIdx[cntIdx++] = vIdx + 1;
				pIdx[cntIdx++] = vIdx + (subDiv + 1) + 1;
				vIdx++;
			}
			vIdx++;
		}

		// Index buffer desc
		D3D11_BUFFER_DESC indexDesc;
		indexDesc.Usage = D3D11_USAGE_DEFAULT;
		indexDesc.ByteWidth = sizeof(UINT) * totalIdx;
		indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		indexDesc.CPUAccessFlags = 0;
		indexDesc.MiscFlags = 0;

		// Index buffer subresource data
		D3D11_SUBRESOURCE_DATA indexData;
		indexData.pSysMem = pIdx;
		indexData.SysMemPitch = 0;
		indexData.SysMemSlicePitch = 0;

		// Create index buffer
		result = _dxHandle->GetDevice()->CreateBuffer(&indexDesc, &indexData, &pMesh->_pIdxBuf);
		if (FAILED(result))
		{
			_meshes.pop_back();
			delete[] pVerts;
			delete[] pIdx;
			return true;
		}

		pMesh->indexCnt = totalIdx;
		pMesh->vertexCnt = totalVerts;
		pMesh->stride = sizeof(SVertexPC);
		pMesh->_material = materialID;

		// Add to map
		_mapIdx.insert(std::pair<std::string, int>(id, static_cast<int>(_meshes.size()) - 1));

		delete[] pVerts;
		delete[] pIdx;
		return true;
	}
	bool CreateCubePC(const std::string& id, float size, const std::string& materialID, const XMFLOAT3& color, int subDiv = 1)
	{
		if (_dxHandle == nullptr)
			return false;

		// Existing check
		auto find = _mapIdx.find(id);
		if (find != _mapIdx.end())
			return false;
		
		// subDiv check
		if (subDiv < 1)
			subDiv = 1;

		float halfX = size / 2.f;
		float halfY = size / 2.f;
		float halfZ = size / 2.f;

		float xInc = size / (float)(subDiv);
		float yInc = size / (float)(subDiv);
		float zInc = size / (float)(subDiv);

		int cubeSides = 6;
		int totalVert = (subDiv + 1) * (subDiv + 1) * cubeSides;

		SVertexPC* pVerts = new SVertexPC[totalVert];
		int counter = 0;

		// Generating faces of cube.
		// Directions based on cube's orientation

		// Front
		for (int y = 0; y < subDiv + 1; y++)
		{
			for (int x = 0; x < subDiv + 1; x++)
			{
				pVerts[counter].position.x = (-halfX) + (xInc * x);
				pVerts[counter].position.y = halfY - (yInc * y);
				pVerts[counter].position.z = -halfZ;
				pVerts[counter].color = color;
				counter++;
			}
		}

		// Left
		for (int y = 0; y < subDiv + 1; y++)
		{
			for (int z = 0; z < subDiv + 1; z++)
			{
				pVerts[counter].position.x = halfX;
				pVerts[counter].position.y = halfY - (yInc * y);
				pVerts[counter].position.z = (-halfZ) + (zInc * z);
				pVerts[counter].color = color;
				counter++;
			}
		}

		// Back 
		for (int y = 0; y < subDiv + 1; y++)
		{
			for (int x = 0; x < subDiv + 1; x++)
			{
				pVerts[counter].position.x = halfX - (xInc * x);
				pVerts[counter].position.y = halfY - (yInc * y);
				pVerts[counter].position.z = halfZ;
				pVerts[counter].color = color;
				counter++;
			}
		}

		// Right
		for (int y = 0; y < subDiv + 1; y++)
		{
			for (int z = 0; z < subDiv + 1; z++)
			{
				pVerts[counter].position.x = -halfX;
				pVerts[counter].position.y = halfY - (yInc * y);
				pVerts[counter].position.z = halfZ - (zInc * z);
				pVerts[counter].color = color;
				counter++;
			}
		}

		// Bottom
		for (int z = 0; z < subDiv + 1; z++)
		{
			for (int x = 0; x < subDiv + 1; x++)
			{
				pVerts[counter].position.x = (-halfX) + (xInc * x);
				pVerts[counter].position.y = -halfY;
				pVerts[counter].position.z = (-halfZ) + (zInc * z);
				pVerts[counter].color = color;
				counter++;
			}
		}

		// Top
		for (int z = 0; z < subDiv + 1; z++)
		{
			for (int x = 0; x < subDiv + 1; x++)
			{
				pVerts[counter].position.x = (-halfX) + (xInc * x);
				pVerts[counter].position.y = halfY;
				pVerts[counter].position.z = halfZ - (zInc * z);
				pVerts[counter].color = color;
				counter++;
			}
		}

		_meshes.push_back(SMesh());
		auto pMesh = &_meshes.back();

		// Vert buffer desc
		D3D11_BUFFER_DESC vertDesc;
		vertDesc.Usage = D3D11_USAGE_DEFAULT;
		vertDesc.ByteWidth = sizeof(SVertexPC) * totalVert;
		vertDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vertDesc.CPUAccessFlags = 0;
		vertDesc.MiscFlags = 0;

		// Vert buffer subresource data
		D3D11_SUBRESOURCE_DATA vertData;
		vertData.pSysMem = pVerts;
		vertData.SysMemPitch = 0;
		vertData.SysMemSlicePitch = 0;

		// Create vertex buffer
		HRESULT result = _dxHandle->GetDevice()->CreateBuffer(&vertDesc, &vertData, &pMesh->_pVertBuf);
		if (FAILED(result))
		{
			_meshes.pop_back();
			return false;
		}

		// Index data
		int totalIdx = (subDiv * subDiv * 6) * cubeSides;
		UINT* pIdx = new UINT[totalIdx];
		int cntIdx = 0;
		int vIdx = 0;
		for (int sides = 0; sides < cubeSides; sides++)
		{
			for (int yFace = 0; yFace < subDiv; yFace++)
			{
				for (int xFace = 0; xFace < subDiv; xFace++)
				{
					pIdx[cntIdx++] = vIdx;
					pIdx[cntIdx++] = vIdx + (subDiv + 1) + 1;
					pIdx[cntIdx++] = vIdx + (subDiv + 1);
					pIdx[cntIdx++] = vIdx;
					pIdx[cntIdx++] = vIdx + 1;
					pIdx[cntIdx++] = vIdx + (subDiv + 1) + 1;
					vIdx++;
				}
				vIdx++;
			}

			vIdx += (subDiv + 1);
		}

		// Index buffer desc
		D3D11_BUFFER_DESC indexDesc;
		indexDesc.Usage = D3D11_USAGE_DEFAULT;
		indexDesc.ByteWidth = sizeof(UINT) * totalIdx;
		indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		indexDesc.CPUAccessFlags = 0;
		indexDesc.MiscFlags = 0;

		// Index buffer subresource data
		D3D11_SUBRESOURCE_DATA indexData;
		indexData.pSysMem = pIdx;
		indexData.SysMemPitch = 0;
		indexData.SysMemSlicePitch = 0;

		// Create index buffer
		result = _dxHandle->GetDevice()->CreateBuffer(&indexDesc, &indexData, &pMesh->_pIdxBuf);
		if (FAILED(result))
		{
			_meshes.pop_back();
			delete[] pVerts;
			delete[] pIdx;
			return false;
		}

		pMesh->indexCnt = totalIdx;
		pMesh->vertexCnt = totalVert;
		pMesh->stride = sizeof(SVertexPC);
		pMesh->_material = materialID;

		// Add to map
		_mapIdx.insert(std::pair<std::string, int>(id, static_cast<int>(_meshes.size()) - 1));

		delete[] pVerts;
		delete[] pIdx;
		return true;
	}
	bool CreateSpherePC(const std::string& id, float size, const std::string& materialID, const XMFLOAT3& color, int subDiv = 1)
	{
		if (_dxHandle == nullptr)
			return false;

		UINT subDivisions = subDiv;
		// Min subdivisions needs to be 3
		if (subDivisions < 3) subDivisions = 3;

		int vertSegments = subDivisions;
		int horizSegments = subDivisions;

		int totalVerts = (vertSegments - 1) * horizSegments + 2;	
		int totalIndices = (2 * horizSegments * 3) + (((vertSegments - 2) * horizSegments) * 6);

		SVertexPC* pVerts = new SVertexPC[totalVerts];	
		int counter = 0;
		float radius = size / 2;

		// First vertex
		pVerts[counter].position = XMFLOAT3(0, (radius * -1), 0);
		pVerts[counter].color = color;
		counter++;

		// Create rings of vertices at higher latitidues
		for (int i = 0; i < (vertSegments - 1); i++)
		{
			float latitude = ((i + 1) * XM_PI / vertSegments) - (XM_PI / 2);

			float dy = (float)sin(latitude);
			float dxz = (float)cos(latitude);

			// Create a single ring of vertices at this latitude
			for (int j = 0; j < horizSegments; j++)
			{
				float longitude = j * (2 * XM_PI) / horizSegments;

				float dx = cos(longitude) * dxz;
				float dz = sin(longitude) * dxz;

				pVerts[counter].position = XMFLOAT3((radius * dx), (radius * dy), (radius * dz));
				pVerts[counter].color = color;

				counter++;
			}
		}

		// Last vertex
		pVerts[counter].position = XMFLOAT3(0, radius, 0);
		pVerts[counter].color = color;

		_meshes.push_back(SMesh());
		auto pMesh = &_meshes.back();

		// Vert buffer desc
		D3D11_BUFFER_DESC vertDesc;
		vertDesc.Usage = D3D11_USAGE_DEFAULT;
		vertDesc.ByteWidth = sizeof(SVertexPC) * totalVerts;
		vertDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vertDesc.CPUAccessFlags = 0;
		vertDesc.MiscFlags = 0;

		// Vert buffer subresource data
		D3D11_SUBRESOURCE_DATA vertData;
		vertData.pSysMem = pVerts;
		vertData.SysMemPitch = 0;
		vertData.SysMemSlicePitch = 0;

		// Create vertex buffer
		HRESULT result = _dxHandle->GetDevice()->CreateBuffer(&vertDesc, &vertData, &pMesh->_pVertBuf);
		if (FAILED(result))
		{
			_meshes.pop_back();
			return false;
		}

		// Index data
		UINT* pIdx = new UINT[totalIndices];
		int indexCounter = 0;

		// Create a fan connecting the bottom vertex to the bottom of the latitude ring
		for (int i = 0; i < horizSegments; i++) {
			pIdx[indexCounter] = 0;
			pIdx[indexCounter + 2] = 1 + (i + 1) % horizSegments;
			pIdx[indexCounter + 1] = 1 + i;

			indexCounter += 3;
		}

		// Fill the sphere body with triangles joining each of latitude rings
		for (int i = 0; i < vertSegments - 2; i++) {
			for (int j = 0; j < horizSegments; j++) {
				int nextI = i + 1;
				int nextJ = (j + 1) % horizSegments;

				pIdx[indexCounter] = 1 + i * horizSegments + j;
				pIdx[indexCounter + 2] = 1 + i * horizSegments + nextJ;
				pIdx[indexCounter + 1] = 1 + nextI * horizSegments + j;

				pIdx[indexCounter + 3] = (1 + i * horizSegments + nextJ);
				pIdx[indexCounter + 5] = 1 + nextI * horizSegments + nextJ;
				pIdx[indexCounter + 4] = 1 + nextI * horizSegments + j;

				indexCounter += 6;
			}
		}

		// Create a fan connecting the top vertex to the top latitude ring.
		for (int i = 0; i < horizSegments; i++) {
			pIdx[indexCounter] = totalVerts - 1;
			pIdx[indexCounter + 2] = totalVerts - 2 - (i + 1) % horizSegments;
			pIdx[indexCounter + 1] = totalVerts - 2 - i;

			indexCounter += 3;
		}

		// Index buffer desc
		D3D11_BUFFER_DESC indexDesc;
		indexDesc.Usage = D3D11_USAGE_DEFAULT;
		indexDesc.ByteWidth = sizeof(UINT) * totalIndices;
		indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		indexDesc.CPUAccessFlags = 0;
		indexDesc.MiscFlags = 0;

		// Index buffer subresource data
		D3D11_SUBRESOURCE_DATA indexData;
		indexData.pSysMem = pIdx;
		indexData.SysMemPitch = 0;
		indexData.SysMemSlicePitch = 0;

		// Create index buffer
		result = _dxHandle->GetDevice()->CreateBuffer(&indexDesc, &indexData, &pMesh->_pIdxBuf);
		if (FAILED(result))
		{
			_meshes.pop_back();
			delete[] pVerts;
			delete[] pIdx;
			return false;
		}

		pMesh->indexCnt = totalIndices;
		pMesh->vertexCnt = totalVerts;
		pMesh->stride = sizeof(SVertexPC);
		pMesh->_material = materialID;

		// Add to map
		_mapIdx.insert(std::pair<std::string, int>(id, static_cast<int>(_meshes.size()) - 1));

		delete[] pVerts;
		delete[] pIdx;
		return true;
	}

	// Meshes with vertices which include Position, Texture UV
	bool CreateTrianglePT(const std::string& id, float size, const std::string& materialID)
	{ 
		if (_dxHandle == nullptr)
			return false;

		auto it = _mapIdx.find(id);
		if (it != _mapIdx.end())
			return false;

		// The center of the triangle is based around the origin (0,0,0).
		// Triangle is facing directly down the Z.
		float halfX = size / 2;
		float halfY = size / 2;

		SVertexPT pVerts[] = {
			XMFLOAT3(-halfX, -halfY, 0),
			XMFLOAT2(0, 1),
			XMFLOAT3(0, halfY, 0),
			XMFLOAT2(0.5, 0),
			XMFLOAT3(halfX, -halfY, 0),
			XMFLOAT2(1, 1)
		};

		_meshes.push_back(SMesh());
		auto pMesh = &_meshes.back();

		// Vert buffer desc
		D3D11_BUFFER_DESC vertDesc;
		vertDesc.Usage = D3D11_USAGE_DEFAULT;
		vertDesc.ByteWidth = sizeof(SVertexPT) * 3;
		vertDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vertDesc.CPUAccessFlags = 0;
		vertDesc.MiscFlags = 0;

		// Vert buffer subresource data
		D3D11_SUBRESOURCE_DATA vertData;
		vertData.pSysMem = pVerts;
		vertData.SysMemPitch = 0;
		vertData.SysMemSlicePitch = 0;

		// Create vertex buffer
		HRESULT result = _dxHandle->GetDevice()->CreateBuffer(&vertDesc, &vertData, pMesh->_pVertBuf.ReleaseAndGetAddressOf());
		if (FAILED(result))
		{
			_meshes.pop_back();
			return false;
		}

		// Index data
		UINT triangleIdx[3] = {
			0, 1, 2
		};

		// Index buffer desc
		D3D11_BUFFER_DESC indexDesc;
		indexDesc.Usage = D3D11_USAGE_DEFAULT;
		indexDesc.ByteWidth = sizeof(UINT) * 3;
		indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		indexDesc.CPUAccessFlags = 0;
		indexDesc.MiscFlags = 0;

		// Index buffer subresource data
		D3D11_SUBRESOURCE_DATA indexData;
		indexData.pSysMem = triangleIdx;
		indexData.SysMemPitch = 0;
		indexData.SysMemSlicePitch = 0;

		// Create index buffer
		result = _dxHandle->GetDevice()->CreateBuffer(&indexDesc, &indexData, pMesh->_pIdxBuf.ReleaseAndGetAddressOf());
		if (FAILED(result))
		{
			_meshes.pop_back();
			return false;
		}

		pMesh->indexCnt = 3;
		pMesh->vertexCnt = 3;
		pMesh->stride = sizeof(SVertexPT);
		pMesh->_material = materialID;

		// Add to map
		_mapIdx.insert(std::pair<std::string, int>(id, static_cast<int>(_meshes.size()) - 1));

		return true;
	}
	bool CreateQuadPT(const std::string& id, float size, const std::string& materialID)
	{
		if (_dxHandle == nullptr)
			return false;

		auto find = _mapIdx.find(id);
		if (find != _mapIdx.end())
			return false;

		float halfX = size / 2.f;
		float halfY = size / 2.f;

		SVertexPT quad[] = {
			XMFLOAT3(-halfX, halfY, 0),
			XMFLOAT2(0, 0),
			XMFLOAT3(halfX, halfY, 0),
			XMFLOAT2(1, 0),
			XMFLOAT3(-halfX, -halfY, 0),
			XMFLOAT2(0, 1),
			XMFLOAT3(halfX, -halfY, 0),
			XMFLOAT2(1, 1),
		};

		_meshes.push_back(SMesh());
		auto pMesh = &_meshes.back();

		// Vert buffer desc
		D3D11_BUFFER_DESC vertDesc;
		vertDesc.Usage = D3D11_USAGE_DEFAULT;
		vertDesc.ByteWidth = sizeof(SVertexPT) * 4;
		vertDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vertDesc.CPUAccessFlags = 0;
		vertDesc.MiscFlags = 0;

		// Vert buffer subresource data
		D3D11_SUBRESOURCE_DATA vertData;
		vertData.pSysMem = quad;
		vertData.SysMemPitch = 0;
		vertData.SysMemSlicePitch = 0;

		// Create vertex buffer
		HRESULT result = _dxHandle->GetDevice()->CreateBuffer(&vertDesc, &vertData, &pMesh->_pVertBuf);
		if (FAILED(result))
		{
			_meshes.pop_back();
			return false;
		}

		// Index data
		UINT triangleIdx[6] = {
			0, 3, 2,
			0, 1, 3
		};

		// Index buffer desc
		D3D11_BUFFER_DESC indexDesc;
		indexDesc.Usage = D3D11_USAGE_DEFAULT;
		indexDesc.ByteWidth = sizeof(UINT) * 6;
		indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		indexDesc.CPUAccessFlags = 0;
		indexDesc.MiscFlags = 0;

		// Index buffer subresource data
		D3D11_SUBRESOURCE_DATA indexData;
		indexData.pSysMem = triangleIdx;
		indexData.SysMemPitch = 0;
		indexData.SysMemSlicePitch = 0;

		// Create index buffer
		result = _dxHandle->GetDevice()->CreateBuffer(&indexDesc, &indexData, &pMesh->_pIdxBuf);
		if (FAILED(result))
		{
			_meshes.pop_back();
			return false;
		}

		pMesh->indexCnt = 6;
		pMesh->vertexCnt = 4;
		pMesh->stride = sizeof(SVertexPT);
		pMesh->_material = materialID;

		// Add to map
		_mapIdx.insert(std::pair<std::string, int>(id, static_cast<int>(_meshes.size()) - 1));
		return true;
	}
	bool CreatePlanePT(const std::string& id, float size, const std::string& materialID, int subDiv = 1)
	{ 
		if (_dxHandle == nullptr)
			return false;

		// Existing check
		auto find = _mapIdx.find(id);
		if (find != _mapIdx.end())
			return false;

		// subDiv check
		if (subDiv < 1)
			subDiv = 1;

		float minX = -(size / 2);
		float minY = -(size / 2);

		float xInc = size / (float)(subDiv + 1);
		float yInc = size / (float)(subDiv + 1);

		int totalVerts = (subDiv + 1) * (subDiv + 1);
		// Generate the vertices 
		SVertexPT* pVerts = new SVertexPT[totalVerts];
		int counter = 0;
		for (int y = 0; y < subDiv + 1; y++)
		{
			for (int x = 0; x < subDiv + 1; x++)
			{
				pVerts[counter].position.x = minX + (xInc * x);
				pVerts[counter].position.y = 0;
				pVerts[counter].position.z = (-minY) - (yInc * y);
				pVerts[counter].textureUV.x = static_cast<float>(x) / static_cast<float>(subDiv);
				pVerts[counter].textureUV.y = static_cast<float>(y) / static_cast<float>(subDiv);
				counter++;
			}
		}

		// New mesh
		_meshes.push_back(SMesh());
		auto pMesh = &_meshes.back();

		// Vert buffer desc
		D3D11_BUFFER_DESC vertDesc;
		vertDesc.Usage = D3D11_USAGE_DEFAULT;
		vertDesc.ByteWidth = sizeof(SVertexPT) * totalVerts;
		vertDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vertDesc.CPUAccessFlags = 0;
		vertDesc.MiscFlags = 0;

		// Vert buffer subresource data
		D3D11_SUBRESOURCE_DATA vertData;
		vertData.pSysMem = pVerts;
		vertData.SysMemPitch = 0;
		vertData.SysMemSlicePitch = 0;

		// Create vertex buffer
		HRESULT result = _dxHandle->GetDevice()->CreateBuffer(&vertDesc, &vertData, &pMesh->_pVertBuf);
		if (FAILED(result))
		{
			_meshes.pop_back();
			delete[] pVerts;
			return false;
		}

		// Index data
		int totalIdx = subDiv * subDiv * 6;
		UINT* pIdx = new UINT[totalIdx];
		int cntIdx = 0;
		int vIdx = 0;
		for (int yFace = 0; yFace < subDiv; yFace++)
		{
			for (int xFace = 0; xFace < subDiv; xFace++)
			{
				pIdx[cntIdx++] = vIdx;
				pIdx[cntIdx++] = vIdx + (subDiv + 1) + 1;
				pIdx[cntIdx++] = vIdx + (subDiv + 1);
				pIdx[cntIdx++] = vIdx;
				pIdx[cntIdx++] = vIdx + 1;
				pIdx[cntIdx++] = vIdx + (subDiv + 1) + 1;
				vIdx++;
			}
			vIdx++;
		}

		// Index buffer desc
		D3D11_BUFFER_DESC indexDesc;
		indexDesc.Usage = D3D11_USAGE_DEFAULT;
		indexDesc.ByteWidth = sizeof(UINT) * totalIdx;
		indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		indexDesc.CPUAccessFlags = 0;
		indexDesc.MiscFlags = 0;

		// Index buffer subresource data
		D3D11_SUBRESOURCE_DATA indexData;
		indexData.pSysMem = pIdx;
		indexData.SysMemPitch = 0;
		indexData.SysMemSlicePitch = 0;

		// Create index buffer
		result = _dxHandle->GetDevice()->CreateBuffer(&indexDesc, &indexData, &pMesh->_pIdxBuf);
		if (FAILED(result))
		{
			_meshes.pop_back();
			delete[] pVerts;
			delete[] pIdx;
			return true;
		}

		pMesh->indexCnt = totalIdx;
		pMesh->vertexCnt = totalVerts;
		pMesh->stride = sizeof(SVertexPT);
		pMesh->_material = materialID;

		// Add to map
		_mapIdx.insert(std::pair<std::string, int>(id, static_cast<int>(_meshes.size()) - 1));

		delete[] pVerts;
		delete[] pIdx;
		return true;
	}
	bool CreateCubePT(const std::string& id, float size, const std::string& materialID, int subDiv = 1)
	{
		if (_dxHandle == nullptr)
			return false;

		// Existing check
		auto find = _mapIdx.find(id);
		if (find != _mapIdx.end())
			return false;

		// subDiv check
		if (subDiv < 1)
			subDiv = 1;

		float halfX = size / 2.f;
		float halfY = size / 2.f;
		float halfZ = size / 2.f;

		float xInc = size / (float)(subDiv);
		float yInc = size / (float)(subDiv);
		float zInc = size / (float)(subDiv);

		int cubeSides = 6;
		int totalVert = (subDiv + 1) * (subDiv + 1) * cubeSides;

		SVertexPT* pVerts = new SVertexPT[totalVert];
		int counter = 0;

		// Generating faces of cube.
		// Directions based on cube's orientation

		// Front
		for (int y = 0; y < subDiv + 1; y++)
		{
			for (int x = 0; x < subDiv + 1; x++)
			{
				pVerts[counter].position.x = (-halfX) + (xInc * x);
				pVerts[counter].position.y = halfY - (yInc * y);
				pVerts[counter].position.z = -halfZ;
				pVerts[counter].textureUV.x = static_cast<float>(x) / static_cast<float>(subDiv);
				pVerts[counter].textureUV.y = static_cast<float>(y) / static_cast<float>(subDiv);
				counter++;
			}
		}

		// Left
		for (int y = 0; y < subDiv + 1; y++)
		{
			for (int z = 0; z < subDiv + 1; z++)
			{
				pVerts[counter].position.x = halfX;
				pVerts[counter].position.y = halfY - (yInc * y);
				pVerts[counter].position.z = (-halfZ) + (zInc * z);
				pVerts[counter].textureUV.x = static_cast<float>(z) / static_cast<float>(subDiv);
				pVerts[counter].textureUV.y = static_cast<float>(y) / static_cast<float>(subDiv);
				counter++;
			}
		}

		// Back 
		for (int y = 0; y < subDiv + 1; y++)
		{
			for (int x = 0; x < subDiv + 1; x++)
			{
				pVerts[counter].position.x = halfX - (xInc * x);
				pVerts[counter].position.y = halfY - (yInc * y);
				pVerts[counter].position.z = halfZ;
				pVerts[counter].textureUV.x = static_cast<float>(x) / static_cast<float>(subDiv);
				pVerts[counter].textureUV.y = static_cast<float>(y) / static_cast<float>(subDiv);
				counter++;
			}
		}

		// Right
		for (int y = 0; y < subDiv + 1; y++)
		{
			for (int z = 0; z < subDiv + 1; z++)
			{
				pVerts[counter].position.x = -halfX;
				pVerts[counter].position.y = halfY - (yInc * y);
				pVerts[counter].position.z = halfZ - (zInc * z);
				pVerts[counter].textureUV.x = static_cast<float>(z) / static_cast<float>(subDiv);
				pVerts[counter].textureUV.y = static_cast<float>(y) / static_cast<float>(subDiv);
				counter++;
			}
		}

		// Bottom
		for (int z = 0; z < subDiv + 1; z++)
		{
			for (int x = 0; x < subDiv + 1; x++)
			{
				pVerts[counter].position.x = (-halfX) + (xInc * x);
				pVerts[counter].position.y = -halfY;
				pVerts[counter].position.z = (-halfZ) + (zInc * z);
				pVerts[counter].textureUV.x = static_cast<float>(x) / static_cast<float>(subDiv);
				pVerts[counter].textureUV.y = static_cast<float>(z) / static_cast<float>(subDiv);
				counter++;
			}
		}

		// Top
		for (int z = 0; z < subDiv + 1; z++)
		{
			for (int x = 0; x < subDiv + 1; x++)
			{
				pVerts[counter].position.x = (-halfX) + (xInc * x);
				pVerts[counter].position.y = halfY;
				pVerts[counter].position.z = halfZ - (zInc * z);
				pVerts[counter].textureUV.x = static_cast<float>(x) / static_cast<float>(subDiv);
				pVerts[counter].textureUV.y = static_cast<float>(z) / static_cast<float>(subDiv);
				counter++;
			}
		}

		_meshes.push_back(SMesh());
		auto pMesh = &_meshes.back();

		// Vert buffer desc
		D3D11_BUFFER_DESC vertDesc;
		vertDesc.Usage = D3D11_USAGE_DEFAULT;
		vertDesc.ByteWidth = sizeof(SVertexPT) * totalVert;
		vertDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vertDesc.CPUAccessFlags = 0;
		vertDesc.MiscFlags = 0;

		// Vert buffer subresource data
		D3D11_SUBRESOURCE_DATA vertData;
		vertData.pSysMem = pVerts;
		vertData.SysMemPitch = 0;
		vertData.SysMemSlicePitch = 0;

		// Create vertex buffer
		HRESULT result = _dxHandle->GetDevice()->CreateBuffer(&vertDesc, &vertData, &pMesh->_pVertBuf);
		if (FAILED(result))
		{
			_meshes.pop_back();
			return false;
		}

		// Index data
		int totalIdx = (subDiv * subDiv * 6) * cubeSides;
		UINT* pIdx = new UINT[totalIdx];
		int cntIdx = 0;
		int vIdx = 0;
		for (int sides = 0; sides < cubeSides; sides++)
		{
			for (int yFace = 0; yFace < subDiv; yFace++)
			{
				for (int xFace = 0; xFace < subDiv; xFace++)
				{
					pIdx[cntIdx++] = vIdx;
					pIdx[cntIdx++] = vIdx + (subDiv + 1) + 1;
					pIdx[cntIdx++] = vIdx + (subDiv + 1);
					pIdx[cntIdx++] = vIdx;
					pIdx[cntIdx++] = vIdx + 1;
					pIdx[cntIdx++] = vIdx + (subDiv + 1) + 1;
					vIdx++;
				}
				vIdx++;
			}

			vIdx += (subDiv + 1);
		}

		// Index buffer desc
		D3D11_BUFFER_DESC indexDesc;
		indexDesc.Usage = D3D11_USAGE_DEFAULT;
		indexDesc.ByteWidth = sizeof(UINT) * totalIdx;
		indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		indexDesc.CPUAccessFlags = 0;
		indexDesc.MiscFlags = 0;

		// Index buffer subresource data
		D3D11_SUBRESOURCE_DATA indexData;
		indexData.pSysMem = pIdx;
		indexData.SysMemPitch = 0;
		indexData.SysMemSlicePitch = 0;

		// Create index buffer
		result = _dxHandle->GetDevice()->CreateBuffer(&indexDesc, &indexData, &pMesh->_pIdxBuf);
		if (FAILED(result))
		{
			_meshes.pop_back();
			delete[] pVerts;
			delete[] pIdx;
			return false;
		}

		pMesh->indexCnt = totalIdx;
		pMesh->vertexCnt = totalVert;
		pMesh->stride = sizeof(SVertexPT);
		pMesh->_material = materialID;

		// Add to map
		_mapIdx.insert(std::pair<std::string, int>(id, static_cast<int>(_meshes.size()) - 1));

		delete[] pVerts;
		delete[] pIdx;
		return true;
	}
	bool CreateSpherePT(const std::string& id, float size, const std::string& materialID, int subDiv = 1)
	{
		if (_dxHandle == nullptr)
			return false;

		UINT subDivisions = subDiv;
		// Min subdivisions needs to be 3
		if (subDivisions < 3) subDivisions = 3;

		int vertSegments = subDivisions;
		int horizSegments = subDivisions;

		int totalVerts = (vertSegments - 1) * horizSegments + 2;
		int totalIndices = (2 * horizSegments * 3) + (((vertSegments - 2) * horizSegments) * 6);

		SVertexPT* pVerts = new SVertexPT[totalVerts];
		int counter = 0;
		float radius = size / 2;

		// First vertex
		pVerts[counter].position = XMFLOAT3(0, (radius * -1), 0);
		pVerts[counter].textureUV = XMFLOAT2(1, 1);
		counter++;

		// Create rings of vertices at higher latitidues
		for (int i = 0; i < (vertSegments - 1); i++)
		{
			float latitude = ((i + 1) * XM_PI / vertSegments) - (XM_PI / 2);

			float dy = (float)sin(latitude);
			float dxz = (float)cos(latitude);

			// Create a single ring of vertices at this latitude
			for (int j = 0; j < horizSegments; j++)
			{
				float longitude = j * (2 * XM_PI) / horizSegments;

				float dx = cos(longitude) * dxz;
				float dz = sin(longitude) * dxz;

				float u = 1.f - (asinf(dx) / XM_PI + 0.5f);
				float v = 1.f - (asinf(dy) / XM_PI + 0.5f);

				pVerts[counter].position = XMFLOAT3((radius * dx), (radius * dy), (radius * dz));
				pVerts[counter].textureUV = XMFLOAT2(u, v);

				counter++;
			}
		}

		// Last vertex
		pVerts[counter].position = XMFLOAT3(0, radius, 0);
		pVerts[counter].textureUV = XMFLOAT2(0, 0);

		_meshes.push_back(SMesh());
		auto pMesh = &_meshes.back();

		// Vert buffer desc
		D3D11_BUFFER_DESC vertDesc;
		vertDesc.Usage = D3D11_USAGE_DEFAULT;
		vertDesc.ByteWidth = sizeof(SVertexPT) * totalVerts;
		vertDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vertDesc.CPUAccessFlags = 0;
		vertDesc.MiscFlags = 0;

		// Vert buffer subresource data
		D3D11_SUBRESOURCE_DATA vertData;
		vertData.pSysMem = pVerts;
		vertData.SysMemPitch = 0;
		vertData.SysMemSlicePitch = 0;

		// Create vertex buffer
		HRESULT result = _dxHandle->GetDevice()->CreateBuffer(&vertDesc, &vertData, &pMesh->_pVertBuf);
		if (FAILED(result))
		{
			_meshes.pop_back();
			return false;
		}

		// Index data
		UINT* pIdx = new UINT[totalIndices];
		int indexCounter = 0;

		// Create a fan connecting the bottom vertex to the bottom of the latitude ring
		for (int i = 0; i < horizSegments; i++) {
			pIdx[indexCounter] = 0;
			pIdx[indexCounter + 2] = 1 + (i + 1) % horizSegments;
			pIdx[indexCounter + 1] = 1 + i;

			indexCounter += 3;
		}

		// Fill the sphere body with triangles joining each of latitude rings
		for (int i = 0; i < vertSegments - 2; i++) {
			for (int j = 0; j < horizSegments; j++) {
				int nextI = i + 1;
				int nextJ = (j + 1) % horizSegments;

				pIdx[indexCounter] = 1 + i * horizSegments + j;
				pIdx[indexCounter + 2] = 1 + i * horizSegments + nextJ;
				pIdx[indexCounter + 1] = 1 + nextI * horizSegments + j;

				pIdx[indexCounter + 3] = (1 + i * horizSegments + nextJ);
				pIdx[indexCounter + 5] = 1 + nextI * horizSegments + nextJ;
				pIdx[indexCounter + 4] = 1 + nextI * horizSegments + j;

				indexCounter += 6;
			}
		}

		// Create a fan connecting the top vertex to the top latitude ring.
		for (int i = 0; i < horizSegments; i++) {
			pIdx[indexCounter] = totalVerts - 1;
			pIdx[indexCounter + 2] = totalVerts - 2 - (i + 1) % horizSegments;
			pIdx[indexCounter + 1] = totalVerts - 2 - i;

			indexCounter += 3;
		}

		// Index buffer desc
		D3D11_BUFFER_DESC indexDesc;
		indexDesc.Usage = D3D11_USAGE_DEFAULT;
		indexDesc.ByteWidth = sizeof(UINT) * totalIndices;
		indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		indexDesc.CPUAccessFlags = 0;
		indexDesc.MiscFlags = 0;

		// Index buffer subresource data
		D3D11_SUBRESOURCE_DATA indexData;
		indexData.pSysMem = pIdx;
		indexData.SysMemPitch = 0;
		indexData.SysMemSlicePitch = 0;

		// Create index buffer
		result = _dxHandle->GetDevice()->CreateBuffer(&indexDesc, &indexData, &pMesh->_pIdxBuf);
		if (FAILED(result))
		{
			_meshes.pop_back();
			delete[] pVerts;
			delete[] pIdx;
			return false;
		}

		pMesh->indexCnt = totalIndices;
		pMesh->vertexCnt = totalVerts;
		pMesh->stride = sizeof(SVertexPT);
		pMesh->_material = materialID;

		// Add to map
		_mapIdx.insert(std::pair<std::string, int>(id, static_cast<int>(_meshes.size()) - 1));

		delete[] pVerts;
		delete[] pIdx;
		return true;
	}

	// Meshes with vertices which include Position, Normal, Texture UV
	bool CreateTrianglePNT(const std::string& id, float size, const std::string& materialID)
	{
		if (_dxHandle == nullptr)
			return false;

		auto it = _mapIdx.find(id);
		if (it != _mapIdx.end())
			return false;

		// The center of the triangle is based around the origin (0,0,0).
		// Triangle is facing directly down the Z.
		float halfX = size / 2;
		float halfY = size / 2;

		SVertexPNT pVerts[] = {
			XMFLOAT3(-halfX, -halfY, 0),
			XMFLOAT3(0, 0, -1),
			XMFLOAT2(0, 1),
			XMFLOAT3(0, halfY, 0),
			XMFLOAT3(0, 0, -1),
			XMFLOAT2(0.5, 0),
			XMFLOAT3(halfX, -halfY, 0),
			XMFLOAT3(0, 0, -1),
			XMFLOAT2(1, 1)
		};

		_meshes.push_back(SMesh());
		auto pMesh = &_meshes.back();

		// Vert buffer desc
		D3D11_BUFFER_DESC vertDesc;
		vertDesc.Usage = D3D11_USAGE_DEFAULT;
		vertDesc.ByteWidth = sizeof(SVertexPNT) * 3;
		vertDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vertDesc.CPUAccessFlags = 0;
		vertDesc.MiscFlags = 0;

		// Vert buffer subresource data
		D3D11_SUBRESOURCE_DATA vertData;
		vertData.pSysMem = pVerts;
		vertData.SysMemPitch = 0;
		vertData.SysMemSlicePitch = 0;

		// Create vertex buffer
		HRESULT result = _dxHandle->GetDevice()->CreateBuffer(&vertDesc, &vertData, pMesh->_pVertBuf.ReleaseAndGetAddressOf());
		if (FAILED(result))
		{
			_meshes.pop_back();
			return false;
		}

		// Index data
		UINT triangleIdx[3] = {
			0, 1, 2
		};

		// Index buffer desc
		D3D11_BUFFER_DESC indexDesc;
		indexDesc.Usage = D3D11_USAGE_DEFAULT;
		indexDesc.ByteWidth = sizeof(UINT) * 3;
		indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		indexDesc.CPUAccessFlags = 0;
		indexDesc.MiscFlags = 0;

		// Index buffer subresource data
		D3D11_SUBRESOURCE_DATA indexData;
		indexData.pSysMem = triangleIdx;
		indexData.SysMemPitch = 0;
		indexData.SysMemSlicePitch = 0;

		// Create index buffer
		result = _dxHandle->GetDevice()->CreateBuffer(&indexDesc, &indexData, pMesh->_pIdxBuf.ReleaseAndGetAddressOf());
		if (FAILED(result))
		{
			_meshes.pop_back();
			return false;
		}

		pMesh->indexCnt = 3;
		pMesh->vertexCnt = 3;
		pMesh->stride = sizeof(SVertexPNT);
		pMesh->_material = materialID;

		// Add to map
		_mapIdx.insert(std::pair<std::string, int>(id, static_cast<int>(_meshes.size()) - 1));
		return true;
	}
	bool CreateQuadPNT(const std::string& id, float size, const std::string& materialID)
	{
		if (_dxHandle == nullptr)
			return false;

		auto find = _mapIdx.find(id);
		if (find != _mapIdx.end())
			return false;

		float halfX = size / 2.f;
		float halfY = size / 2.f;

		SVertexPNT quad[] = {
			XMFLOAT3(-halfX, halfY, 0),
			XMFLOAT3(0, 0, -1),
			XMFLOAT2(0, 0),
			XMFLOAT3(halfX, halfY, 0),
			XMFLOAT3(0, 0, -1),
			XMFLOAT2(1, 0),
			XMFLOAT3(-halfX, -halfY, 0),
			XMFLOAT3(0, 0, -1),
			XMFLOAT2(0, 1),
			XMFLOAT3(halfX, -halfY, 0),
			XMFLOAT3(0, 0, -1),
			XMFLOAT2(1, 1),
		};

		_meshes.push_back(SMesh());
		auto pMesh = &_meshes.back();

		// Vert buffer desc
		D3D11_BUFFER_DESC vertDesc;
		vertDesc.Usage = D3D11_USAGE_DEFAULT;
		vertDesc.ByteWidth = sizeof(SVertexPNT) * 4;
		vertDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vertDesc.CPUAccessFlags = 0;
		vertDesc.MiscFlags = 0;

		// Vert buffer subresource data
		D3D11_SUBRESOURCE_DATA vertData;
		vertData.pSysMem = quad;
		vertData.SysMemPitch = 0;
		vertData.SysMemSlicePitch = 0;

		// Create vertex buffer
		HRESULT result = _dxHandle->GetDevice()->CreateBuffer(&vertDesc, &vertData, &pMesh->_pVertBuf);
		if (FAILED(result))
		{
			_meshes.pop_back();
			return false;
		}

		// Index data
		UINT triangleIdx[6] = {
			0, 3, 2,
			0, 1, 3
		};

		// Index buffer desc
		D3D11_BUFFER_DESC indexDesc;
		indexDesc.Usage = D3D11_USAGE_DEFAULT;
		indexDesc.ByteWidth = sizeof(UINT) * 6;
		indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		indexDesc.CPUAccessFlags = 0;
		indexDesc.MiscFlags = 0;

		// Index buffer subresource data
		D3D11_SUBRESOURCE_DATA indexData;
		indexData.pSysMem = triangleIdx;
		indexData.SysMemPitch = 0;
		indexData.SysMemSlicePitch = 0;

		// Create index buffer
		result = _dxHandle->GetDevice()->CreateBuffer(&indexDesc, &indexData, &pMesh->_pIdxBuf);
		if (FAILED(result))
		{
			_meshes.pop_back();
			return false;
		}

		pMesh->indexCnt = 6;
		pMesh->vertexCnt = 4;
		pMesh->stride = sizeof(SVertexPNT);
		pMesh->_material = materialID;

		// Add to map
		_mapIdx.insert(std::pair<std::string, int>(id, static_cast<int>(_meshes.size()) - 1));
		return true;
	}
	bool CreatePlanePNT(const std::string& id, float size, const std::string& materialID, int subDiv = 1)
	{
		if (_dxHandle == nullptr)
			return false;

		// Existing check
		auto find = _mapIdx.find(id);
		if (find != _mapIdx.end())
			return false;

		// subDiv check
		if (subDiv < 1)
			subDiv = 1;

		float minX = -(size / 2);
		float minY = -(size / 2);

		float xInc = size / (float)(subDiv + 1);
		float yInc = size / (float)(subDiv + 1);

		int totalVerts = (subDiv + 1) * (subDiv + 1);
		// Generate the vertices 
		SVertexPNT* pVerts = new SVertexPNT[totalVerts];
		int counter = 0;
		for (int y = 0; y < subDiv + 1; y++)
		{
			for (int x = 0; x < subDiv + 1; x++)
			{
				pVerts[counter].position.x = minX + (xInc * x);
				pVerts[counter].position.y = 0;
				pVerts[counter].position.z = (-minY) - (yInc * y);
				pVerts[counter].normal = XMFLOAT3(0, 1, 0);
				pVerts[counter].textureUV.x = static_cast<float>(x) / static_cast<float>(subDiv);
				pVerts[counter].textureUV.y = static_cast<float>(y) / static_cast<float>(subDiv);
				counter++;
			}
		}

		// New mesh
		_meshes.push_back(SMesh());
		auto pMesh = &_meshes.back();

		// Vert buffer desc
		D3D11_BUFFER_DESC vertDesc;
		vertDesc.Usage = D3D11_USAGE_DEFAULT;
		vertDesc.ByteWidth = sizeof(SVertexPNT) * totalVerts;
		vertDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vertDesc.CPUAccessFlags = 0;
		vertDesc.MiscFlags = 0;

		// Vert buffer subresource data
		D3D11_SUBRESOURCE_DATA vertData;
		vertData.pSysMem = pVerts;
		vertData.SysMemPitch = 0;
		vertData.SysMemSlicePitch = 0;

		// Create vertex buffer
		HRESULT result = _dxHandle->GetDevice()->CreateBuffer(&vertDesc, &vertData, &pMesh->_pVertBuf);
		if (FAILED(result))
		{
			_meshes.pop_back();
			delete[] pVerts;
			return false;
		}

		// Index data
		int totalIdx = subDiv * subDiv * 6;
		UINT* pIdx = new UINT[totalIdx];
		int cntIdx = 0;
		int vIdx = 0;
		for (int yFace = 0; yFace < subDiv; yFace++)
		{
			for (int xFace = 0; xFace < subDiv; xFace++)
			{
				pIdx[cntIdx++] = vIdx;
				pIdx[cntIdx++] = vIdx + (subDiv + 1) + 1;
				pIdx[cntIdx++] = vIdx + (subDiv + 1);
				pIdx[cntIdx++] = vIdx;
				pIdx[cntIdx++] = vIdx + 1;
				pIdx[cntIdx++] = vIdx + (subDiv + 1) + 1;
				vIdx++;
			}
			vIdx++;
		}

		// Index buffer desc
		D3D11_BUFFER_DESC indexDesc;
		indexDesc.Usage = D3D11_USAGE_DEFAULT;
		indexDesc.ByteWidth = sizeof(UINT) * totalIdx;
		indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		indexDesc.CPUAccessFlags = 0;
		indexDesc.MiscFlags = 0;

		// Index buffer subresource data
		D3D11_SUBRESOURCE_DATA indexData;
		indexData.pSysMem = pIdx;
		indexData.SysMemPitch = 0;
		indexData.SysMemSlicePitch = 0;

		// Create index buffer
		result = _dxHandle->GetDevice()->CreateBuffer(&indexDesc, &indexData, &pMesh->_pIdxBuf);
		if (FAILED(result))
		{
			_meshes.pop_back();
			delete[] pVerts;
			delete[] pIdx;
			return true;
		}

		pMesh->indexCnt = totalIdx;
		pMesh->vertexCnt = totalVerts;
		pMesh->stride = sizeof(SVertexPNT);
		pMesh->_material = materialID;

		// Add to map
		_mapIdx.insert(std::pair<std::string, int>(id, static_cast<int>(_meshes.size()) - 1));

		delete[] pVerts;
		delete[] pIdx;
		return true;
	}
	bool CreateCubePNT(const std::string& id, float size, const std::string& materialID, int subDiv = 1)
	{
		if (_dxHandle == nullptr)
			return false;

		// Existing check
		auto find = _mapIdx.find(id);
		if (find != _mapIdx.end())
			return false;

		// subDiv check
		if (subDiv < 1)
			subDiv = 1;

		float halfX = size / 2.f;
		float halfY = size / 2.f;
		float halfZ = size / 2.f;

		float xInc = size / (float)(subDiv);
		float yInc = size / (float)(subDiv);
		float zInc = size / (float)(subDiv);

		int cubeSides = 6;
		int totalVert = (subDiv + 1) * (subDiv + 1) * cubeSides;

		SVertexPNT* pVerts = new SVertexPNT[totalVert];
		int counter = 0;

		// Generating faces of cube.
		// Directions based on cube's orientation

		// Front
		for (int y = 0; y < subDiv + 1; y++)
		{
			for (int x = 0; x < subDiv + 1; x++)
			{
				pVerts[counter].position.x = (-halfX) + (xInc * x);
				pVerts[counter].position.y = halfY - (yInc * y);
				pVerts[counter].position.z = -halfZ;
				pVerts[counter].normal = XMFLOAT3(0, 0, -1);
				pVerts[counter].textureUV.x = static_cast<float>(x) / static_cast<float>(subDiv);
				pVerts[counter].textureUV.y = static_cast<float>(y) / static_cast<float>(subDiv);
				counter++;
			}
		}

		// Left
		for (int y = 0; y < subDiv + 1; y++)
		{
			for (int z = 0; z < subDiv + 1; z++)
			{
				pVerts[counter].position.x = halfX;
				pVerts[counter].position.y = halfY - (yInc * y);
				pVerts[counter].position.z = (-halfZ) + (zInc * z);
				pVerts[counter].normal = XMFLOAT3(1, 0, 0);
				pVerts[counter].textureUV.x = static_cast<float>(z) / static_cast<float>(subDiv);
				pVerts[counter].textureUV.y = static_cast<float>(y) / static_cast<float>(subDiv);
				counter++;
			}
		}

		// Back 
		for (int y = 0; y < subDiv + 1; y++)
		{
			for (int x = 0; x < subDiv + 1; x++)
			{
				pVerts[counter].position.x = halfX - (xInc * x);
				pVerts[counter].position.y = halfY - (yInc * y);
				pVerts[counter].position.z = halfZ;
				pVerts[counter].normal = XMFLOAT3(0, 0, 1);
				pVerts[counter].textureUV.x = static_cast<float>(x) / static_cast<float>(subDiv);
				pVerts[counter].textureUV.y = static_cast<float>(y) / static_cast<float>(subDiv);
				counter++;
			}
		}

		// Right
		for (int y = 0; y < subDiv + 1; y++)
		{
			for (int z = 0; z < subDiv + 1; z++)
			{
				pVerts[counter].position.x = -halfX;
				pVerts[counter].position.y = halfY - (yInc * y);
				pVerts[counter].position.z = halfZ - (zInc * z);
				pVerts[counter].normal = XMFLOAT3(-1, 0, 0);
				pVerts[counter].textureUV.x = static_cast<float>(z) / static_cast<float>(subDiv);
				pVerts[counter].textureUV.y = static_cast<float>(y) / static_cast<float>(subDiv);
				counter++;
			}
		}

		// Bottom
		for (int z = 0; z < subDiv + 1; z++)
		{
			for (int x = 0; x < subDiv + 1; x++)
			{
				pVerts[counter].position.x = (-halfX) + (xInc * x);
				pVerts[counter].position.y = -halfY;
				pVerts[counter].position.z = (-halfZ) + (zInc * z);
				pVerts[counter].normal = XMFLOAT3(0, -1, 0);
				pVerts[counter].textureUV.x = static_cast<float>(x) / static_cast<float>(subDiv);
				pVerts[counter].textureUV.y = static_cast<float>(z) / static_cast<float>(subDiv);
				counter++;
			}
		}

		// Top
		for (int z = 0; z < subDiv + 1; z++)
		{
			for (int x = 0; x < subDiv + 1; x++)
			{
				pVerts[counter].position.x = (-halfX) + (xInc * x);
				pVerts[counter].position.y = halfY;
				pVerts[counter].position.z = halfZ - (zInc * z);
				pVerts[counter].normal = XMFLOAT3(0, 1, 0);
				pVerts[counter].textureUV.x = static_cast<float>(x) / static_cast<float>(subDiv);
				pVerts[counter].textureUV.y = static_cast<float>(z) / static_cast<float>(subDiv);
				counter++;
			}
		}

		_meshes.push_back(SMesh());
		auto pMesh = &_meshes.back();

		// Vert buffer desc
		D3D11_BUFFER_DESC vertDesc;
		vertDesc.Usage = D3D11_USAGE_DEFAULT;
		vertDesc.ByteWidth = sizeof(SVertexPNT) * totalVert;
		vertDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vertDesc.CPUAccessFlags = 0;
		vertDesc.MiscFlags = 0;

		// Vert buffer subresource data
		D3D11_SUBRESOURCE_DATA vertData;
		vertData.pSysMem = pVerts;
		vertData.SysMemPitch = 0;
		vertData.SysMemSlicePitch = 0;

		// Create vertex buffer
		HRESULT result = _dxHandle->GetDevice()->CreateBuffer(&vertDesc, &vertData, &pMesh->_pVertBuf);
		if (FAILED(result))
		{
			_meshes.pop_back();
			return false;
		}

		// Index data
		int totalIdx = (subDiv * subDiv * 6) * cubeSides;
		UINT* pIdx = new UINT[totalIdx];
		int cntIdx = 0;
		int vIdx = 0;
		for (int sides = 0; sides < cubeSides; sides++)
		{
			for (int yFace = 0; yFace < subDiv; yFace++)
			{
				for (int xFace = 0; xFace < subDiv; xFace++)
				{
					pIdx[cntIdx++] = vIdx;
					pIdx[cntIdx++] = vIdx + (subDiv + 1) + 1;
					pIdx[cntIdx++] = vIdx + (subDiv + 1);
					pIdx[cntIdx++] = vIdx;
					pIdx[cntIdx++] = vIdx + 1;
					pIdx[cntIdx++] = vIdx + (subDiv + 1) + 1;
					vIdx++;
				}
				vIdx++;
			}

			vIdx += (subDiv + 1);
		}

		// Index buffer desc
		D3D11_BUFFER_DESC indexDesc;
		indexDesc.Usage = D3D11_USAGE_DEFAULT;
		indexDesc.ByteWidth = sizeof(UINT) * totalIdx;
		indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		indexDesc.CPUAccessFlags = 0;
		indexDesc.MiscFlags = 0;

		// Index buffer subresource data
		D3D11_SUBRESOURCE_DATA indexData;
		indexData.pSysMem = pIdx;
		indexData.SysMemPitch = 0;
		indexData.SysMemSlicePitch = 0;

		// Create index buffer
		result = _dxHandle->GetDevice()->CreateBuffer(&indexDesc, &indexData, &pMesh->_pIdxBuf);
		if (FAILED(result))
		{
			_meshes.pop_back();
			delete[] pVerts;
			delete[] pIdx;
			return false;
		}

		pMesh->indexCnt = totalIdx;
		pMesh->vertexCnt = totalVert;
		pMesh->stride = sizeof(SVertexPNT);
		pMesh->_material = materialID;

		// Add to map
		_mapIdx.insert(std::pair<std::string, int>(id, static_cast<int>(_meshes.size()) - 1));

		delete[] pVerts;
		delete[] pIdx;
		return true;
	}
	bool CreateSpherePNT(const std::string& id, float size, const std::string& materialID, int subDiv = 1)
	{
		if (_dxHandle == nullptr)
			return false;

		UINT subDivisions = subDiv;
		// Min subdivisions needs to be 3
		if (subDivisions < 3) subDivisions = 3;

		int vertSegments = subDivisions;
		int horizSegments = subDivisions;

		int totalVerts = (vertSegments - 1) * horizSegments + 2;
		int totalIndices = (2 * horizSegments * 3) + (((vertSegments - 2) * horizSegments) * 6);

		SVertexPNT* pVerts = new SVertexPNT[totalVerts];
		int counter = 0;
		float radius = size / 2;

		// First vertex
		pVerts[counter].position = XMFLOAT3(0, (radius * -1), 0);
		pVerts[counter].normal = XMFLOAT3(0, -1, 0);
		pVerts[counter].textureUV = XMFLOAT2(1, 1);
		counter++;

		// Create rings of vertices at higher latitidues
		for (int i = 0; i < (vertSegments - 1); i++)
		{
			float latitude = ((i + 1) * XM_PI / vertSegments) - (XM_PI / 2);

			float dy = (float)sin(latitude);
			float dxz = (float)cos(latitude);

			// Create a single ring of vertices at this latitude
			for (int j = 0; j < horizSegments; j++)
			{
				float longitude = j * (2 * XM_PI) / horizSegments;

				float dx = cos(longitude) * dxz;
				float dz = sin(longitude) * dxz;

				float u = 1.f - (asinf(dx) / XM_PI + 0.5f);
				float v = 1.f - (asinf(dy) / XM_PI + 0.5f);

				pVerts[counter].position = XMFLOAT3((radius * dx), (radius * dy), (radius * dz));
				pVerts[counter].normal = XMFLOAT3(dx, dy, dz);
				pVerts[counter].textureUV = XMFLOAT2(u, v);

				counter++;
			}
		}

		// Last vertex
		pVerts[counter].position = XMFLOAT3(0, radius, 0);
		pVerts[counter].normal = XMFLOAT3(0, 1, 0);
		pVerts[counter].textureUV = XMFLOAT2(0, 0);

		_meshes.push_back(SMesh());
		auto pMesh = &_meshes.back();

		// Vert buffer desc
		D3D11_BUFFER_DESC vertDesc;
		vertDesc.Usage = D3D11_USAGE_DEFAULT;
		vertDesc.ByteWidth = sizeof(SVertexPNT) * totalVerts;
		vertDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vertDesc.CPUAccessFlags = 0;
		vertDesc.MiscFlags = 0;

		// Vert buffer subresource data
		D3D11_SUBRESOURCE_DATA vertData;
		vertData.pSysMem = pVerts;
		vertData.SysMemPitch = 0;
		vertData.SysMemSlicePitch = 0;

		// Create vertex buffer
		HRESULT result = _dxHandle->GetDevice()->CreateBuffer(&vertDesc, &vertData, &pMesh->_pVertBuf);
		if (FAILED(result))
		{
			_meshes.pop_back();
			return false;
		}

		// Index data
		UINT* pIdx = new UINT[totalIndices];
		int indexCounter = 0;

		// Create a fan connecting the bottom vertex to the bottom of the latitude ring
		for (int i = 0; i < horizSegments; i++) {
			pIdx[indexCounter] = 0;
			pIdx[indexCounter + 2] = 1 + (i + 1) % horizSegments;
			pIdx[indexCounter + 1] = 1 + i;

			indexCounter += 3;
		}

		// Fill the sphere body with triangles joining each of latitude rings
		for (int i = 0; i < vertSegments - 2; i++) {
			for (int j = 0; j < horizSegments; j++) {
				int nextI = i + 1;
				int nextJ = (j + 1) % horizSegments;

				pIdx[indexCounter] = 1 + i * horizSegments + j;
				pIdx[indexCounter + 2] = 1 + i * horizSegments + nextJ;
				pIdx[indexCounter + 1] = 1 + nextI * horizSegments + j;

				pIdx[indexCounter + 3] = (1 + i * horizSegments + nextJ);
				pIdx[indexCounter + 5] = 1 + nextI * horizSegments + nextJ;
				pIdx[indexCounter + 4] = 1 + nextI * horizSegments + j;

				indexCounter += 6;
			}
		}

		// Create a fan connecting the top vertex to the top latitude ring.
		for (int i = 0; i < horizSegments; i++) {
			pIdx[indexCounter] = totalVerts - 1;
			pIdx[indexCounter + 2] = totalVerts - 2 - (i + 1) % horizSegments;
			pIdx[indexCounter + 1] = totalVerts - 2 - i;

			indexCounter += 3;
		}

		// Index buffer desc
		D3D11_BUFFER_DESC indexDesc;
		indexDesc.Usage = D3D11_USAGE_DEFAULT;
		indexDesc.ByteWidth = sizeof(UINT) * totalIndices;
		indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		indexDesc.CPUAccessFlags = 0;
		indexDesc.MiscFlags = 0;

		// Index buffer subresource data
		D3D11_SUBRESOURCE_DATA indexData;
		indexData.pSysMem = pIdx;
		indexData.SysMemPitch = 0;
		indexData.SysMemSlicePitch = 0;

		// Create index buffer
		result = _dxHandle->GetDevice()->CreateBuffer(&indexDesc, &indexData, &pMesh->_pIdxBuf);
		if (FAILED(result))
		{
			_meshes.pop_back();
			delete[] pVerts;
			delete[] pIdx;
			return false;
		}

		pMesh->indexCnt = totalIndices;
		pMesh->vertexCnt = totalVerts;
		pMesh->stride = sizeof(SVertexPNT);
		pMesh->_material = materialID;

		// Add to map
		_mapIdx.insert(std::pair<std::string, int>(id, static_cast<int>(_meshes.size()) - 1));

		delete[] pVerts;
		delete[] pIdx;
		return true;
	}

	// Meshes with vertices which include Position, Normal, Tangent, BiTangent, Texture UV
	bool CreateTrianglePNTT(const std::string& id, float size, const std::string& materialID)
	{
		if (_dxHandle == nullptr)
			return false;

		auto it = _mapIdx.find(id);
		if (it != _mapIdx.end())
			return false;

		// The center of the triangle is based around the origin (0,0,0).
		// Triangle is facing directly down the Z.
		float halfX = size / 2;
		float halfY = size / 2;

		// Dud tangent data, calculated later on
		SVertexPNTT pVerts[] = {
			XMFLOAT3(-halfX, -halfY, 0),
			XMFLOAT3(0, 0, -1),
			XMFLOAT4(0, 0, 0, 0),
			XMFLOAT2(0, 1),
			XMFLOAT3(0, halfY, 0),
			XMFLOAT3(0, 0, -1),
			XMFLOAT4(0, 0, 0, 0),
			XMFLOAT2(0.5, 0),
			XMFLOAT3(halfX, -halfY, 0),
			XMFLOAT3(0, 0, -1),
			XMFLOAT4(0, 0, 0, 0),
			XMFLOAT2(1, 1)
		};

		// Index data
		UINT triangleIdx[3] = {
			0, 1, 2
		};

		CalculateTangents(pVerts, 3, triangleIdx, 3);

		_meshes.push_back(SMesh());
		auto pMesh = &_meshes.back();

		// Vert buffer desc
		D3D11_BUFFER_DESC vertDesc;
		vertDesc.Usage = D3D11_USAGE_DEFAULT;
		vertDesc.ByteWidth = sizeof(SVertexPNTT) * 3;
		vertDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vertDesc.CPUAccessFlags = 0;
		vertDesc.MiscFlags = 0;

		// Vert buffer subresource data
		D3D11_SUBRESOURCE_DATA vertData;
		vertData.pSysMem = pVerts;
		vertData.SysMemPitch = 0;
		vertData.SysMemSlicePitch = 0;

		// Create vertex buffer
		HRESULT result = _dxHandle->GetDevice()->CreateBuffer(&vertDesc, &vertData, pMesh->_pVertBuf.ReleaseAndGetAddressOf());
		if (FAILED(result))
		{
			_meshes.pop_back();
			return false;
		}

		// Index buffer desc
		D3D11_BUFFER_DESC indexDesc;
		indexDesc.Usage = D3D11_USAGE_DEFAULT;
		indexDesc.ByteWidth = sizeof(UINT) * 3;
		indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		indexDesc.CPUAccessFlags = 0;
		indexDesc.MiscFlags = 0;

		// Index buffer subresource data
		D3D11_SUBRESOURCE_DATA indexData;
		indexData.pSysMem = triangleIdx;
		indexData.SysMemPitch = 0;
		indexData.SysMemSlicePitch = 0;

		// Create index buffer
		result = _dxHandle->GetDevice()->CreateBuffer(&indexDesc, &indexData, pMesh->_pIdxBuf.ReleaseAndGetAddressOf());
		if (FAILED(result))
		{
			_meshes.pop_back();
			return false;
		}

		pMesh->indexCnt = 3;
		pMesh->vertexCnt = 3;
		pMesh->stride = sizeof(SVertexPNTT);
		pMesh->_material = materialID;

		// Add to map
		_mapIdx.insert(std::pair<std::string, int>(id, static_cast<int>(_meshes.size()) - 1));
		return true;
	}
	bool CreateQuadPNTT(const std::string& id, float size, const std::string& materialID)
	{
		if (_dxHandle == nullptr)
			return false;

		auto find = _mapIdx.find(id);
		if (find != _mapIdx.end())
			return false;

		float halfX = size / 2.f;
		float halfY = size / 2.f;

		SVertexPNTT quad[] = {
			XMFLOAT3(-halfX, halfY, 0),
			XMFLOAT3(0, 0, -1),
			XMFLOAT4(0, 0, 0, 0),
			XMFLOAT2(0, 0),
			XMFLOAT3(halfX, halfY, 0),
			XMFLOAT3(0, 0, -1),
			XMFLOAT4(0, 0, 0, 0),
			XMFLOAT2(1, 0),
			XMFLOAT3(-halfX, -halfY, 0),
			XMFLOAT3(0, 0, -1),
			XMFLOAT4(0, 0, 0, 0),
			XMFLOAT2(0, 1),
			XMFLOAT3(halfX, -halfY, 0),
			XMFLOAT3(0, 0, -1),
			XMFLOAT4(0, 0, 0, 0),
			XMFLOAT2(1, 1),
		};

		// Index data
		UINT triangleIdx[6] = {
			0, 3, 2,
			0, 1, 3
		};

		CalculateTangents(quad, 4, triangleIdx, 6);

		_meshes.push_back(SMesh());
		auto pMesh = &_meshes.back();

		// Vert buffer desc
		D3D11_BUFFER_DESC vertDesc;
		vertDesc.Usage = D3D11_USAGE_DEFAULT;
		vertDesc.ByteWidth = sizeof(SVertexPNTT) * 4;
		vertDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vertDesc.CPUAccessFlags = 0;
		vertDesc.MiscFlags = 0;

		// Vert buffer subresource data
		D3D11_SUBRESOURCE_DATA vertData;
		vertData.pSysMem = quad;
		vertData.SysMemPitch = 0;
		vertData.SysMemSlicePitch = 0;

		// Create vertex buffer
		HRESULT result = _dxHandle->GetDevice()->CreateBuffer(&vertDesc, &vertData, &pMesh->_pVertBuf);
		if (FAILED(result))
		{
			_meshes.pop_back();
			return false;
		}

		// Index buffer desc
		D3D11_BUFFER_DESC indexDesc;
		indexDesc.Usage = D3D11_USAGE_DEFAULT;
		indexDesc.ByteWidth = sizeof(UINT) * 6;
		indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		indexDesc.CPUAccessFlags = 0;
		indexDesc.MiscFlags = 0;

		// Index buffer subresource data
		D3D11_SUBRESOURCE_DATA indexData;
		indexData.pSysMem = triangleIdx;
		indexData.SysMemPitch = 0;
		indexData.SysMemSlicePitch = 0;

		// Create index buffer
		result = _dxHandle->GetDevice()->CreateBuffer(&indexDesc, &indexData, &pMesh->_pIdxBuf);
		if (FAILED(result))
		{
			_meshes.pop_back();
			return false;
		}

		pMesh->indexCnt = 6;
		pMesh->vertexCnt = 4;
		pMesh->stride = sizeof(SVertexPNTT);
		pMesh->_material = materialID;

		// Add to map
		_mapIdx.insert(std::pair<std::string, int>(id, static_cast<int>(_meshes.size()) - 1));
		return true;
	}
	bool CreatePlanePNTT(const std::string& id, float size, const std::string& materialID, int subDiv = 1)
	{
		if (_dxHandle == nullptr)
			return false;

		// Existing check
		auto find = _mapIdx.find(id);
		if (find != _mapIdx.end())
			return false;

		// subDiv check
		if (subDiv < 1)
			subDiv = 1;

		float minX = -(size / 2);
		float minY = -(size / 2);

		float xInc = size / (float)(subDiv + 1);
		float yInc = size / (float)(subDiv + 1);

		int totalVerts = (subDiv + 1) * (subDiv + 1);
		// Generate the vertices 
		SVertexPNTT* pVerts = new SVertexPNTT[totalVerts];
		int counter = 0;
		for (int y = 0; y < subDiv + 1; y++)
		{
			for (int x = 0; x < subDiv + 1; x++)
			{
				pVerts[counter].position.x = minX + (xInc * x);
				pVerts[counter].position.y = 0;
				pVerts[counter].position.z = (-minY) - (yInc * y);
				pVerts[counter].normal = XMFLOAT3(0, 1, 0);
				pVerts[counter].textureUV.x = static_cast<float>(x) / static_cast<float>(subDiv);
				pVerts[counter].textureUV.y = static_cast<float>(y) / static_cast<float>(subDiv);
				counter++;
			}
		}

		// New mesh
		_meshes.push_back(SMesh());
		auto pMesh = &_meshes.back();

		// Index data
		int totalIdx = subDiv * subDiv * 6;
		UINT* pIdx = new UINT[totalIdx];
		int cntIdx = 0;
		int vIdx = 0;
		for (int yFace = 0; yFace < subDiv; yFace++)
		{
			for (int xFace = 0; xFace < subDiv; xFace++)
			{
				pIdx[cntIdx++] = vIdx;
				pIdx[cntIdx++] = vIdx + (subDiv + 1) + 1;
				pIdx[cntIdx++] = vIdx + (subDiv + 1);
				pIdx[cntIdx++] = vIdx;
				pIdx[cntIdx++] = vIdx + 1;
				pIdx[cntIdx++] = vIdx + (subDiv + 1) + 1;
				vIdx++;
			}
			vIdx++;
		}

		pMesh->indexCnt = totalIdx;
		pMesh->vertexCnt = totalVerts;
		pMesh->stride = sizeof(SVertexPNTT);
		pMesh->_material = materialID;

		CalculateTangents(pVerts, totalVerts, pIdx, totalIdx);

		// Vert buffer desc
		D3D11_BUFFER_DESC vertDesc;
		vertDesc.Usage = D3D11_USAGE_DEFAULT;
		vertDesc.ByteWidth = sizeof(SVertexPNTT) * totalVerts;
		vertDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vertDesc.CPUAccessFlags = 0;
		vertDesc.MiscFlags = 0;

		// Vert buffer subresource data
		D3D11_SUBRESOURCE_DATA vertData;
		vertData.pSysMem = pVerts;
		vertData.SysMemPitch = 0;
		vertData.SysMemSlicePitch = 0;

		// Create vertex buffer
		HRESULT result = _dxHandle->GetDevice()->CreateBuffer(&vertDesc, &vertData, &pMesh->_pVertBuf);
		if (FAILED(result))
		{
			_meshes.pop_back();
			delete[] pVerts;
			return false;
		}

		// Index buffer desc
		D3D11_BUFFER_DESC indexDesc;
		indexDesc.Usage = D3D11_USAGE_DEFAULT;
		indexDesc.ByteWidth = sizeof(UINT) * totalIdx;
		indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		indexDesc.CPUAccessFlags = 0;
		indexDesc.MiscFlags = 0;

		// Index buffer subresource data
		D3D11_SUBRESOURCE_DATA indexData;
		indexData.pSysMem = pIdx;
		indexData.SysMemPitch = 0;
		indexData.SysMemSlicePitch = 0;

		// Create index buffer
		result = _dxHandle->GetDevice()->CreateBuffer(&indexDesc, &indexData, &pMesh->_pIdxBuf);
		if (FAILED(result))
		{
			_meshes.pop_back();
			delete[] pVerts;
			delete[] pIdx;
			return true;
		}

		// Add to map
		_mapIdx.insert(std::pair<std::string, int>(id, static_cast<int>(_meshes.size()) - 1));

		delete[] pVerts;
		delete[] pIdx;
		return true;
	}
	bool CreateCubePNTT(const std::string& id, float size, const std::string& materialID, int subDiv = 1)
	{
		if (_dxHandle == nullptr)
			return false;

		// Existing check
		auto find = _mapIdx.find(id);
		if (find != _mapIdx.end())
			return false;

		// subDiv check
		if (subDiv < 1)
			subDiv = 1;

		float halfX = size / 2.f;
		float halfY = size / 2.f;
		float halfZ = size / 2.f;

		float xInc = size / (float)(subDiv);
		float yInc = size / (float)(subDiv);
		float zInc = size / (float)(subDiv);

		int cubeSides = 6;
		int totalVert = (subDiv + 1) * (subDiv + 1) * cubeSides;

		SVertexPNTT* pVerts = new SVertexPNTT[totalVert];
		int counter = 0;

		// Generating faces of cube.
		// Directions based on cube's orientation

		// Front
		for (int y = 0; y < subDiv + 1; y++)
		{
			for (int x = 0; x < subDiv + 1; x++)
			{
				pVerts[counter].position.x = (-halfX) + (xInc * x);
				pVerts[counter].position.y = halfY - (yInc * y);
				pVerts[counter].position.z = -halfZ;
				pVerts[counter].normal = XMFLOAT3(0, 0, -1);
				pVerts[counter].textureUV.x = static_cast<float>(x) / static_cast<float>(subDiv);
				pVerts[counter].textureUV.y = static_cast<float>(y) / static_cast<float>(subDiv);
				counter++;
			}
		}

		// Left
		for (int y = 0; y < subDiv + 1; y++)
		{
			for (int z = 0; z < subDiv + 1; z++)
			{
				pVerts[counter].position.x = halfX;
				pVerts[counter].position.y = halfY - (yInc * y);
				pVerts[counter].position.z = (-halfZ) + (zInc * z);
				pVerts[counter].normal = XMFLOAT3(1, 0, 0);
				pVerts[counter].textureUV.x = static_cast<float>(z) / static_cast<float>(subDiv);
				pVerts[counter].textureUV.y = static_cast<float>(y) / static_cast<float>(subDiv);
				counter++;
			}
		}

		// Back 
		for (int y = 0; y < subDiv + 1; y++)
		{
			for (int x = 0; x < subDiv + 1; x++)
			{
				pVerts[counter].position.x = halfX - (xInc * x);
				pVerts[counter].position.y = halfY - (yInc * y);
				pVerts[counter].position.z = halfZ;
				pVerts[counter].normal = XMFLOAT3(0, 0, 1);
				pVerts[counter].textureUV.x = static_cast<float>(x) / static_cast<float>(subDiv);
				pVerts[counter].textureUV.y = static_cast<float>(y) / static_cast<float>(subDiv);
				counter++;
			}
		}

		// Right
		for (int y = 0; y < subDiv + 1; y++)
		{
			for (int z = 0; z < subDiv + 1; z++)
			{
				pVerts[counter].position.x = -halfX;
				pVerts[counter].position.y = halfY - (yInc * y);
				pVerts[counter].position.z = halfZ - (zInc * z);
				pVerts[counter].normal = XMFLOAT3(-1, 0, 0);
				pVerts[counter].textureUV.x = static_cast<float>(z) / static_cast<float>(subDiv);
				pVerts[counter].textureUV.y = static_cast<float>(y) / static_cast<float>(subDiv);
				counter++;
			}
		}

		// Bottom
		for (int z = 0; z < subDiv + 1; z++)
		{
			for (int x = 0; x < subDiv + 1; x++)
			{
				pVerts[counter].position.x = (-halfX) + (xInc * x);
				pVerts[counter].position.y = -halfY;
				pVerts[counter].position.z = (-halfZ) + (zInc * z);
				pVerts[counter].normal = XMFLOAT3(0, -1, 0);
				pVerts[counter].textureUV.x = static_cast<float>(x) / static_cast<float>(subDiv);
				pVerts[counter].textureUV.y = static_cast<float>(z) / static_cast<float>(subDiv);
				counter++;
			}
		}

		// Top
		for (int z = 0; z < subDiv + 1; z++)
		{
			for (int x = 0; x < subDiv + 1; x++)
			{
				pVerts[counter].position.x = (-halfX) + (xInc * x);
				pVerts[counter].position.y = halfY;
				pVerts[counter].position.z = halfZ - (zInc * z);
				pVerts[counter].normal = XMFLOAT3(0, 1, 0);
				pVerts[counter].textureUV.x = static_cast<float>(x) / static_cast<float>(subDiv);
				pVerts[counter].textureUV.y = static_cast<float>(z) / static_cast<float>(subDiv);
				counter++;
			}
		}

		// Index data
		int totalIdx = (subDiv * subDiv * 6) * cubeSides;
		UINT* pIdx = new UINT[totalIdx];
		int cntIdx = 0;
		int vIdx = 0;
		for (int sides = 0; sides < cubeSides; sides++)
		{
			for (int yFace = 0; yFace < subDiv; yFace++)
			{
				for (int xFace = 0; xFace < subDiv; xFace++)
				{
					pIdx[cntIdx++] = vIdx;
					pIdx[cntIdx++] = vIdx + (subDiv + 1) + 1;
					pIdx[cntIdx++] = vIdx + (subDiv + 1);
					pIdx[cntIdx++] = vIdx;
					pIdx[cntIdx++] = vIdx + 1;
					pIdx[cntIdx++] = vIdx + (subDiv + 1) + 1;
					vIdx++;
				}
				vIdx++;
			}

			vIdx += (subDiv + 1);
		}

		CalculateTangents(pVerts, totalVert, pIdx, totalIdx);

		_meshes.push_back(SMesh());
		auto pMesh = &_meshes.back();

		// Vert buffer desc
		D3D11_BUFFER_DESC vertDesc;
		vertDesc.Usage = D3D11_USAGE_DEFAULT;
		vertDesc.ByteWidth = sizeof(SVertexPNTT) * totalVert;
		vertDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vertDesc.CPUAccessFlags = 0;
		vertDesc.MiscFlags = 0;

		// Vert buffer subresource data
		D3D11_SUBRESOURCE_DATA vertData;
		vertData.pSysMem = pVerts;
		vertData.SysMemPitch = 0;
		vertData.SysMemSlicePitch = 0;

		// Create vertex buffer
		HRESULT result = _dxHandle->GetDevice()->CreateBuffer(&vertDesc, &vertData, &pMesh->_pVertBuf);
		if (FAILED(result))
		{
			_meshes.pop_back();
			delete[] pVerts;
			delete[] pIdx;
			return false;
		}

		// Index buffer desc
		D3D11_BUFFER_DESC indexDesc;
		indexDesc.Usage = D3D11_USAGE_DEFAULT;
		indexDesc.ByteWidth = sizeof(UINT) * totalIdx;
		indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		indexDesc.CPUAccessFlags = 0;
		indexDesc.MiscFlags = 0;

		// Index buffer subresource data
		D3D11_SUBRESOURCE_DATA indexData;
		indexData.pSysMem = pIdx;
		indexData.SysMemPitch = 0;
		indexData.SysMemSlicePitch = 0;

		// Create index buffer
		result = _dxHandle->GetDevice()->CreateBuffer(&indexDesc, &indexData, &pMesh->_pIdxBuf);
		if (FAILED(result))
		{
			_meshes.pop_back();
			delete[] pVerts;
			delete[] pIdx;
			return false;
		}

		pMesh->indexCnt = totalIdx;
		pMesh->vertexCnt = totalVert;
		pMesh->stride = sizeof(SVertexPNTT);
		pMesh->_material = materialID;

		// Add to map
		_mapIdx.insert(std::pair<std::string, int>(id, static_cast<int>(_meshes.size()) - 1));

		delete[] pVerts;
		delete[] pIdx;
		return true;
	}
	bool CreateSpherePNTT(const std::string& id, float size, const std::string& materialID, int subDiv = 1)
	{
		if (_dxHandle == nullptr)
			return false;

		UINT subDivisions = subDiv;
		// Min subdivisions needs to be 3
		if (subDivisions < 3) subDivisions = 3;

		int vertSegments = subDivisions;
		int horizSegments = subDivisions;

		int totalVerts = (vertSegments - 1) * horizSegments + 2;
		int totalIndices = (2 * horizSegments * 3) + (((vertSegments - 2) * horizSegments) * 6);

		SVertexPNTT* pVerts = new SVertexPNTT[totalVerts];
		int counter = 0;
		float radius = size / 2;

		// First vertex
		pVerts[counter].position = XMFLOAT3(0, (radius * -1), 0);
		pVerts[counter].normal = XMFLOAT3(0, -1, 0);
		pVerts[counter].textureUV = XMFLOAT2(1, 1);
		counter++;

		// Create rings of vertices at higher latitidues
		for (int i = 0; i < (vertSegments - 1); i++)
		{
			float latitude = ((i + 1) * XM_PI / vertSegments) - (XM_PI / 2);

			float dy = (float)sin(latitude);
			float dxz = (float)cos(latitude);

			// Create a single ring of vertices at this latitude
			for (int j = 0; j < horizSegments; j++)
			{
				float longitude = j * (2 * XM_PI) / horizSegments;

				float dx = cos(longitude) * dxz;
				float dz = sin(longitude) * dxz;

				float u = 1.f - (asinf(dx) / XM_PI + 0.5f);
				float v = 1.f - (asinf(dy) / XM_PI + 0.5f);

				pVerts[counter].position = XMFLOAT3((radius * dx), (radius * dy), (radius * dz));
				pVerts[counter].normal = XMFLOAT3(dx, dy, dz);
				pVerts[counter].textureUV = XMFLOAT2(u, v);

				counter++;
			}
		}

		// Last vertex
		pVerts[counter].position = XMFLOAT3(0, radius, 0);
		pVerts[counter].normal = XMFLOAT3(0, 1, 0);
		pVerts[counter].textureUV = XMFLOAT2(0, 0);

		// Index data
		UINT* pIdx = new UINT[totalIndices];
		int indexCounter = 0;

		// Create a fan connecting the bottom vertex to the bottom of the latitude ring
		for (int i = 0; i < horizSegments; i++) {
			pIdx[indexCounter] = 0;
			pIdx[indexCounter + 2] = 1 + (i + 1) % horizSegments;
			pIdx[indexCounter + 1] = 1 + i;

			indexCounter += 3;
		}

		// Fill the sphere body with triangles joining each of latitude rings
		for (int i = 0; i < vertSegments - 2; i++) {
			for (int j = 0; j < horizSegments; j++) {
				int nextI = i + 1;
				int nextJ = (j + 1) % horizSegments;

				pIdx[indexCounter] = 1 + i * horizSegments + j;
				pIdx[indexCounter + 2] = 1 + i * horizSegments + nextJ;
				pIdx[indexCounter + 1] = 1 + nextI * horizSegments + j;

				pIdx[indexCounter + 3] = (1 + i * horizSegments + nextJ);
				pIdx[indexCounter + 5] = 1 + nextI * horizSegments + nextJ;
				pIdx[indexCounter + 4] = 1 + nextI * horizSegments + j;

				indexCounter += 6;
			}
		}

		// Create a fan connecting the top vertex to the top latitude ring.
		for (int i = 0; i < horizSegments; i++) {
			pIdx[indexCounter] = totalVerts - 1;
			pIdx[indexCounter + 2] = totalVerts - 2 - (i + 1) % horizSegments;
			pIdx[indexCounter + 1] = totalVerts - 2 - i;

			indexCounter += 3;
		}

		CalculateTangents(pVerts, totalVerts, pIdx, totalIndices);

		_meshes.push_back(SMesh());
		auto pMesh = &_meshes.back();

		// Vert buffer desc
		D3D11_BUFFER_DESC vertDesc;
		vertDesc.Usage = D3D11_USAGE_DEFAULT;
		vertDesc.ByteWidth = sizeof(SVertexPNTT) * totalVerts;
		vertDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		vertDesc.CPUAccessFlags = 0;
		vertDesc.MiscFlags = 0;

		// Vert buffer subresource data
		D3D11_SUBRESOURCE_DATA vertData;
		vertData.pSysMem = pVerts;
		vertData.SysMemPitch = 0;
		vertData.SysMemSlicePitch = 0;

		// Create vertex buffer
		HRESULT result = _dxHandle->GetDevice()->CreateBuffer(&vertDesc, &vertData, &pMesh->_pVertBuf);
		if (FAILED(result))
		{
			_meshes.pop_back();
			return false;
		}

		// Index buffer desc
		D3D11_BUFFER_DESC indexDesc;
		indexDesc.Usage = D3D11_USAGE_DEFAULT;
		indexDesc.ByteWidth = sizeof(UINT) * totalIndices;
		indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		indexDesc.CPUAccessFlags = 0;
		indexDesc.MiscFlags = 0;

		// Index buffer subresource data
		D3D11_SUBRESOURCE_DATA indexData;
		indexData.pSysMem = pIdx;
		indexData.SysMemPitch = 0;
		indexData.SysMemSlicePitch = 0;

		// Create index buffer
		result = _dxHandle->GetDevice()->CreateBuffer(&indexDesc, &indexData, &pMesh->_pIdxBuf);
		if (FAILED(result))
		{
			_meshes.pop_back();
			delete[] pVerts;
			delete[] pIdx;
			return false;
		}

		pMesh->indexCnt = totalIndices;
		pMesh->vertexCnt = totalVerts;
		pMesh->stride = sizeof(SVertexPNTT);
		pMesh->_material = materialID;

		// Add to map
		_mapIdx.insert(std::pair<std::string, int>(id, static_cast<int>(_meshes.size()) - 1));

		delete[] pVerts;
		delete[] pIdx;
		return true;
	}

	SMesh* GetMesh(const std::string& id)
	{
		auto find = _mapIdx.find(id);
		if (find != _mapIdx.end())
			return &_meshes[find->second];

		return nullptr;
	}

	void Close()
	{
		_meshes.clear();
		_mapIdx.clear();
	}

private:
	// Code based off:
	// Lengyel, Eric. “Computing Tangent Space Basis Vectors for an Arbitrary Mesh”. Terathon Software, 2001. http://terathon.com/code/tangent.html
	void CalculateTangents(SVertexPNTT* pVerts, UINT totalVerts, UINT* pIndices, UINT totalIndices)
	{
		XMVECTOR* tangents = new XMVECTOR[totalVerts];
		XMVECTOR* biTangents = new XMVECTOR[totalVerts];
		ZeroMemory(tangents, (sizeof(XMVECTOR) * totalVerts));
		ZeroMemory(biTangents, (sizeof(XMVECTOR) * totalVerts));

		for (UINT idx = 0; idx < totalIndices; idx += 3)
		{
			XMVECTOR v0 = XMLoadFloat3(&pVerts[pIndices[idx]].position);
			XMVECTOR v1 = XMLoadFloat3(&pVerts[pIndices[idx+1]].position);
			XMVECTOR v2 = XMLoadFloat3(&pVerts[pIndices[idx+2]].position);

			XMVECTOR tUV0 = XMLoadFloat2(&pVerts[pIndices[idx]].textureUV);
			XMVECTOR tUV1 = XMLoadFloat2(&pVerts[pIndices[idx+1]].textureUV);
			XMVECTOR tUV2 = XMLoadFloat2(&pVerts[pIndices[idx+2]].textureUV);

			XMVECTOR deltaPos1 = v1 - v0;
			XMVECTOR deltaPos2 = v2 - v0;

			XMVECTOR deltaUV1 = tUV1 - tUV0;
			XMVECTOR deltaUV2 = tUV2 - tUV0;

			XMFLOAT3 dPos1;
			XMFLOAT3 dPos2;
			XMFLOAT2 dUV1;
			XMFLOAT2 dUV2;

			XMStoreFloat3(&dPos1, deltaPos1);
			XMStoreFloat3(&dPos2, deltaPos2);
			XMStoreFloat2(&dUV1, deltaUV1);
			XMStoreFloat2(&dUV2, deltaUV2);

			float r = 1.0f / (dUV1.x * dUV2.y - dUV1.y * dUV2.x);
			XMFLOAT3 tan = XMFLOAT3(1, 0, 0);
			tan.x = (dPos1.x * dUV2.y - dPos2.x * dUV1.y) * r;
			tan.y = (dPos1.y * dUV2.y - dPos2.y * dUV1.y) * r;
			tan.z = (dPos1.z * dUV2.y - dPos2.z * dUV1.y) * r;

			XMFLOAT3 biTan = XMFLOAT3(0, 0, 1);
			biTan.x = (dPos2.x * dUV1.x - dPos1.x * dUV2.x) * r;
			biTan.y = (dPos2.y * dUV1.x - dPos1.y * dUV2.x) * r;
			biTan.z = (dPos2.z * dUV1.x - dPos1.z * dUV2.x) * r;

			XMVECTOR test = XMLoadFloat3(&tan);
			tangents[pIndices[idx]] += XMLoadFloat3(&tan);
			tangents[pIndices[idx+1]] += XMLoadFloat3(&tan);
			tangents[pIndices[idx+2]] += XMLoadFloat3(&tan);

			// We could also store and upload biTangent data to the shader
			biTangents[pIndices[idx]] += XMLoadFloat3(&biTan);
			biTangents[pIndices[idx+1]] += XMLoadFloat3(&biTan);
			biTangents[pIndices[idx+2]] += XMLoadFloat3(&biTan);
		}

		for (UINT vert = 0; vert < totalVerts; vert++)
		{
			XMVECTOR n = XMLoadFloat3(&pVerts[vert].normal);
			XMVECTOR t = tangents[vert];

			// Gram-Schmidt orthogonalize
			tangents[vert] = (t - n * XMVector3Dot(n, t).m128_f32[0]);
			tangents[vert] = XMVector3Normalize(tangents[vert]);

			// Calculate handedness
			tangents[vert].m128_f32[3] = (XMVector3Dot(XMVector3Cross(n, t), biTangents[vert]).m128_f32[0] < 0.0F) ? -1.0F : 1.0F;
			XMStoreFloat4(&pVerts[vert].tangent, tangents[vert]);
		}

		delete[] tangents;
		delete[] biTangents;
	}

private:
	std::vector<SMesh> _meshes;
	std::map<std::string, int> _mapIdx;
	CDirectX* _dxHandle = nullptr;
};

class CTextureMGR
{
public:
	CTextureMGR() = default;
	~CTextureMGR() {}

	bool Initialise(CDirectX* pDx) { _dxHdl = pDx; return true; }
	bool LoadTexture(const std::string& fileDir, const std::string& id)
	{
		// Check for existing ID entry
		if (_mapToIdx.find(id) != _mapToIdx.end())
			return false;

		// Load texture and create shader resource
		_textures.push_back(ComPtr<ID3D11ShaderResourceView>(CreateTexture(fileDir)));
		if (_textures.back() == nullptr) 
			return false;

		int idx = static_cast<int>(_textures.size()) - 1;
		_mapToIdx.insert(std::pair<std::string, int>(id, idx));
		return true;
	}
	ComPtr<ID3D11ShaderResourceView> GetTexture(const std::string& id)
	{
		auto find = _mapToIdx.find(id);
		if (find != _mapToIdx.end())
			return _textures[find->second];

		return nullptr;
	}

private:
	ComPtr<ID3D11ShaderResourceView> CreateTexture(const std::string& fileDir)
	{
		ComPtr<ID3D11ShaderResourceView> pRes = nullptr;
		std::wstring wFileTextureName = std::wstring(fileDir.begin(), fileDir.end());
		HRESULT result = CreateWICTextureFromFile(_dxHdl->GetDevice().Get(), _dxHdl->GetContext().Get(), wFileTextureName.c_str(), NULL, pRes.ReleaseAndGetAddressOf());
		if (FAILED(result)) { SAFE_RELEASE(pRes); return nullptr; }
		return pRes;
	}

private:
	std::vector<ComPtr<ID3D11ShaderResourceView>> _textures;
	std::map<std::string, int> _mapToIdx;
	CDirectX* _dxHdl = nullptr;
};

class CLightMGR
{
public:
	CLightMGR() = default;
	~CLightMGR() {}

	void EnableDirectionalLight(bool bEnable)
	{
		if (bEnable)
			_lightData.dirLightData.dirLightCol.w = 1.0f;
		else
			_lightData.dirLightData.dirLightCol.w = 0;
	}
	void SetDirectionalLightColor(const XMFLOAT3& col)
	{
		_lightData.dirLightData.dirLightCol.x = col.x;
		_lightData.dirLightData.dirLightCol.y = col.y;
		_lightData.dirLightData.dirLightCol.z = col.z;
	}
	void SetDirectionalLightDir(const XMFLOAT3& dir)
	{
		_lightData.dirLightData.dirLight = XMFLOAT4(dir.x, dir.y, dir.z, 0);
	}

	void SetActivePointLights(UINT activeLights)
	{
		_lightData.activePointLights = activeLights;
		if (_lightData.activePointLights > MAX_POINT_LIGHTS)
			_lightData.activePointLights = MAX_POINT_LIGHTS;
	}
	bool SetPointLightPos(UINT lightIdx, const XMFLOAT3& pos)
	{
		if (lightIdx >= _lightData.activePointLights)
			return false;

		_lightData.pointLightData[lightIdx].pos.x = pos.x;
		_lightData.pointLightData[lightIdx].pos.y = pos.y;
		_lightData.pointLightData[lightIdx].pos.z = pos.z;
		return true;
	}
	bool SetPointLightColor(UINT lightIdx, const XMFLOAT3& color)
	{
		if (lightIdx >= _lightData.activePointLights)
			return false;
		
		_lightData.pointLightData[lightIdx].color.x = color.x;
		_lightData.pointLightData[lightIdx].color.y = color.y;
		_lightData.pointLightData[lightIdx].color.z = color.z;
		return true;
	}
	bool SetPointLightAttenuation(UINT lightIdx, float value)
	{
		if (lightIdx >= _lightData.activePointLights)
			return false;

		_lightData.pointLightData->pos.w = value;
		return true;
	}
	bool SetPointLightIntensity(UINT lightIdx, float value)
	{
		if (lightIdx >= _lightData.activePointLights)
			return false;

		_lightData.pointLightData->color.w = value;
		return true;
	}
	bool SetPointLightData(UINT lightIdx, const XMFLOAT4& pos, const XMFLOAT4& color)
	{
		if (lightIdx >= _lightData.activePointLights)
			return false;

		_lightData.pointLightData[lightIdx].pos = pos;
		_lightData.pointLightData[lightIdx].color = color;
		return true;
	}

	bool SetActiveSpotLights(UINT activeLights)
	{
		_lightData.activeSpotLights = activeLights;
		if (_lightData.activeSpotLights > MAX_SPOT_LIGHTS)
			_lightData.activeSpotLights = MAX_SPOT_LIGHTS;

		return true;
	}
	bool SetSpotLightPos(UINT lightIdx, const XMFLOAT3& pos)
	{
		if (lightIdx >= MAX_SPOT_LIGHTS)
			return false;

		_lightData.spotLightData[lightIdx].pos.x = pos.x;
		_lightData.spotLightData[lightIdx].pos.y = pos.y;
		_lightData.spotLightData[lightIdx].pos.z = pos.z;
		return true;
	}
	bool SetSpotLightColor(UINT lightIdx, const XMFLOAT3& color)
	{
		if (lightIdx >= MAX_SPOT_LIGHTS)
			return false;

		_lightData.spotLightData[lightIdx].color.x = color.x;
		_lightData.spotLightData[lightIdx].color.y = color.y;
		_lightData.spotLightData[lightIdx].color.z = color.z;
		return true;
	}
	bool SetSpotLightAttenuation(UINT lightIdx, float value)
	{
		if (lightIdx >= MAX_SPOT_LIGHTS)
			return false;
		
		_lightData.spotLightData[lightIdx].pos.w = value;
		return true;

	}
	bool SetSpotLightIntensity(UINT lightIdx, float value)
	{
		if (lightIdx >= MAX_SPOT_LIGHTS)
			return false;

		_lightData.spotLightData[lightIdx].color.w = value;
		return true;
	}
	bool SetSpotLightDirection(UINT lightIdx, const XMFLOAT3& dir)
	{
		if (lightIdx >= MAX_SPOT_LIGHTS)
			return false;

		_lightData.spotLightData[lightIdx].dir.x = dir.x;
		_lightData.spotLightData[lightIdx].dir.y = dir.y;
		_lightData.spotLightData[lightIdx].dir.z = dir.z;
		return true;
	}
	bool SetSpotLightOuterConeAngle(UINT lightIdx, float value)
	{
		if (lightIdx >= MAX_SPOT_LIGHTS)
			return false;

		_lightData.spotLightData[lightIdx].dir.w = value;
		return true;
	}
	bool SetSpotLightInnerConeAngle(UINT lightIdx, float value)
	{
		if (lightIdx >= MAX_SPOT_LIGHTS)
			return false;

		_lightData.spotLightData[lightIdx].innerConeAngle = value;
		return true;
	}
	bool SetSpotLightData(UINT lightIdx, const XMFLOAT4& pos, const XMFLOAT4& color, const XMFLOAT4& dir, float innerConeAngle)
	{
		if (lightIdx >= MAX_SPOT_LIGHTS)
			return false;

		_lightData.spotLightData[lightIdx].pos = pos;
		_lightData.spotLightData[lightIdx].color = color;
		_lightData.spotLightData[lightIdx].dir = dir;
		_lightData.spotLightData[lightIdx].innerConeAngle = innerConeAngle;
		return true;
	}

	void SetEyePos(const XMFLOAT3& pos) { _lightData.eyePos = XMFLOAT4(pos.x, pos.y, pos.z, 0);	}
	void SetSpecularValue(float value) { _lightData.specular = value; }
	void SetSpecularColor(const XMFLOAT3& color)
	{
		_lightData.specularColor.x = color.x;
		_lightData.specularColor.y = color.y;
		_lightData.specularColor.z = color.z;
	}
	void SetSpecularIntensity(float value)
	{
		_lightData.specularColor.w = value;
	}
	void SetSpecularData(const XMFLOAT4& color, float specPow)
	{
		_lightData.specularColor = color;
		_lightData.specular = specPow;
	}

	const PixelCBufferStructLightData::DirectionalLightData& GetDirectionalLightData() const {	return _lightData.dirLightData;	}
	const PixelCBufferStructLightData::PointLightData& GetPointLightData(UINT lightIdx) const 
	{
		if (lightIdx >= _lightData.activePointLights)
			lightIdx = _lightData.activePointLights - 1;

		return _lightData.pointLightData[lightIdx];
	}

	PixelCBufferStructLightData* GetLightDataStruct() { return &_lightData; }

private:
	PixelCBufferStructLightData _lightData;
};

class CKeyboard
{
public:
	CKeyboard(int keyThreshold) : _keyThreshold(keyThreshold) { std::fill_n(_keys, 256, 'u'); }
	CKeyboard() { std::fill_n(_keys, 256, 'u'); ZeroMemory(_keyTimers, sizeof(int) * 256); }
	~CKeyboard() {}

	void SetKeyThreshold(int time) { _keyThreshold = time; }

	bool IsKeyDown(int key)
	{
		if (_keys[key] == 'd')
			return true;

		return false;
	}

	bool IsKeyUp(int key)
	{
		if (_keys[key] == 'u')
			return true;

		return false;
	}

	bool IsKeyHeld(int key)
	{
		if (_keys[key] == 'h')
			return true;

		return false;
	}

	void KeyDown(int key)
	{
		if (_keys[key] == 'u')
			_keys[key] = 'd';
	}

	void KeyUp(int key)
	{
		_keys[key] = 'u';
		_keyTimers[key] = 0;
	}

	void Update(int elapsedTimeMicroSecs)
	{
		for (int i = 0; i < 256; i++)
		{
			if (_keys[i] == 'd')
			{
				_keyTimers[i] += elapsedTimeMicroSecs;
				// Convert to millisecond for threshold check
				if ((_keyTimers[i] / 1000) > _keyThreshold)
					_keys[i] = 'h';
			}
		}
	}

private:
	unsigned char _keys[256];
	long _keyTimers[256];				// Timer on each key being held down (in micro seconds)
	int _keyThreshold = 100;			// Threshold in milliseconds
};

class CMouse
{
public:
	CMouse() = default;
	~CMouse() {}

	void Event(UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch (message)
		{
		case WM_LBUTTONDOWN:
			GetCursorPos(&_leftDownPos);
			ScreenToClient(GetActiveWindow(), &_leftDownPos);
			_leftDown = true;
			break;
		case WM_LBUTTONUP:
			GetCursorPos(&_leftUpPos);
			ScreenToClient(GetActiveWindow(), &_leftUpPos);
			_leftDown = false;
			break;
		case WM_RBUTTONDOWN:
			GetCursorPos(&_rightDownPos);
			ScreenToClient(GetActiveWindow(), &_rightDownPos);
			_rightDown = true;
			break;
		case WM_RBUTTONUP:
			GetCursorPos(&_rightUpPos);
			ScreenToClient(GetActiveWindow(), &_rightUpPos);
			_rightDown = false;
			break;
		case WM_MOUSEMOVE:
			GetCursorPos(&_pos);
			ScreenToClient(GetActiveWindow(), &_pos);
			break;
		case WM_MOUSEWHEEL:
			_wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
			break;
		}
	}

	bool IsLeftDown() const { return _leftDown; }
	bool IsRightDown() const { return _rightDown; }
	POINT GetMousePos() const { return _pos; }
	POINT GetMouseLeftDown() const { return _leftDownPos; }
	POINT GetMouseLeftUp() const { return _leftUpPos; }
	POINT GetMouseRightDown() const { return _rightDownPos; }
	POINT GetMouseRightUp() const { return _rightUpPos; }

	void SetMousePos(POINT pos)
	{
		POINT setPos(pos);
		_pos = setPos;
		ClientToScreen(GetActiveWindow(), &setPos);
		::SetCursorPos(setPos.x, setPos.y);
	}

	LONG GetMouseWheelDeltaAndReset() 
	{ 
		LONG returnData = _wheelDelta;
		_wheelDelta = 0;
		return returnData;
	}

private:
	POINT _leftDownPos;
	POINT _rightDownPos;
	POINT _leftUpPos;
	POINT _rightUpPos;

	POINT _pos;

	LONG _wheelDelta = 0;

	bool _leftDown = false;
	bool _rightDown = false;
};

class CCamera
{
	enum CameraDir
	{
		CamForward,
		CamBackward,
		CamLeft,
		CamRight,
		CamTotalDir
	};

public:
	CCamera() = default;
	~CCamera() {};

	void SetPerspective(float fovDegrees, float aspRatio, float zNear, float zFar)
	{
		_fovDegrees = fovDegrees;
		_aspectRatio = aspRatio;
		_near = zNear;
		_far = zFar;
	}

	void SetView(const XMFLOAT3& pos, const XMFLOAT3& up)
	{
		_pos = XMLoadFloat3(&pos);
		_up = XMLoadFloat3(&up);

		_look = _pos;
		_look.m128_f32[2] += 1;
	}

	void Forward(float dist)
	{
		_dir[CamForward] = true;
		_dirDist[CamForward] = dist;
	}

	void Backward(float dist)
	{
		_dir[CamBackward] = true;
		_dirDist[CamBackward] = dist;
	}

	void StrafeLeft(float dist)
	{
		_dir[CamLeft] = true;
		_dirDist[CamLeft] = dist;
	}

	void StrafeRight(float dist)
	{
		_dir[CamRight] = true;
		_dirDist[CamRight] = dist;
	}

	void RotateH(float degrees)
	{
		_rotH = true;
		_rotHDist += degrees;
	}

	void RotateV(float degrees)
	{
		_rotV = true;
		_rotVDist += degrees;
	}

	void Update()
	{
		_prevHAngle = _hAngle;
		_prevVAngle = _vAngle;
		_prevPos = _pos;
		_prevLook = _look;

		if (_rotH)
		{
			_hAngle += _rotHDist;

			// Keep within -360 to 360
			if (_hAngle > 360.f)
			{
				_hAngle -= 360.f;
				_prevHAngle -= 360.f;
			}

			if (_hAngle < 0.f)
			{
				_hAngle += 360.f;
				_prevHAngle += 360.f;
			}
		}

		if (_rotV)
		{
			_vAngle += _rotVDist;

			// Clamp
			if (_vAngle > 89.99f)
				_vAngle = 89.99f;

			if (_vAngle < -89.99f)
				_vAngle = -89.99f;
		}

		// Update rotation
		_look.m128_f32[0] = _pos.m128_f32[0] - sin(XMConvertToRadians(_hAngle))*cos(XMConvertToRadians(_vAngle));
		_look.m128_f32[1] = _pos.m128_f32[1] - sin(XMConvertToRadians(_vAngle));
		_look.m128_f32[2] = _pos.m128_f32[2] + cos(XMConvertToRadians(_hAngle))*cos(XMConvertToRadians(_vAngle));

		XMVECTOR viewDir;
		viewDir.m128_f32[0] = _look.m128_f32[0] - _pos.m128_f32[0];
		viewDir.m128_f32[1] = _look.m128_f32[1] - _pos.m128_f32[1];
		viewDir.m128_f32[2] = _look.m128_f32[2] - _pos.m128_f32[2];

		if (_dir[CamForward])
		{
			_pos.m128_f32[0] += viewDir.m128_f32[0] * _dirDist[CamForward];
			_pos.m128_f32[1] += viewDir.m128_f32[1] * _dirDist[CamForward];
			_pos.m128_f32[2] += viewDir.m128_f32[2] * _dirDist[CamForward];

			_look = _pos;
			_look.m128_f32[0] += viewDir.m128_f32[0];
			_look.m128_f32[1] += viewDir.m128_f32[1];
			_look.m128_f32[2] += viewDir.m128_f32[2];
		}

		if (_dir[CamBackward])
		{
			_pos.m128_f32[0] -= viewDir.m128_f32[0] * _dirDist[CamBackward];
			_pos.m128_f32[1] -= viewDir.m128_f32[1] * _dirDist[CamBackward];
			_pos.m128_f32[2] -= viewDir.m128_f32[2] * _dirDist[CamBackward];

			_look = _pos;
			_look.m128_f32[0] += viewDir.m128_f32[0];
			_look.m128_f32[1] += viewDir.m128_f32[1];
			_look.m128_f32[2] += viewDir.m128_f32[2];
		}

		if (_dir[CamLeft])
		{
			_pos.m128_f32[0] -= cos(XMConvertToRadians(_hAngle)) * _dirDist[CamLeft];
			_pos.m128_f32[2] -= sin(XMConvertToRadians(_hAngle)) * _dirDist[CamLeft];

			_look = _pos;
			_look.m128_f32[0] += viewDir.m128_f32[0];
			_look.m128_f32[1] += viewDir.m128_f32[1];
			_look.m128_f32[2] += viewDir.m128_f32[2];
		}

		if (_dir[CamRight])
		{
			_pos.m128_f32[0] += cos(XMConvertToRadians(_hAngle)) * _dirDist[CamRight];
			_pos.m128_f32[2] += sin(XMConvertToRadians(_hAngle)) * _dirDist[CamRight];

			_look = _pos;
			_look.m128_f32[0] += viewDir.m128_f32[0];
			_look.m128_f32[1] += viewDir.m128_f32[1];
			_look.m128_f32[2] += viewDir.m128_f32[2];
		}

		// Reset
		for (int i = 0; i < CamTotalDir; i++)
		{
			_dir[i] = false;
			_dirDist[i] = 0.0;
		}
		_rotH = false;
		_rotV = false;
		_rotHDist = 0.0;
		_rotVDist = 0.0;
	}

	XMMATRIX GeneratePerspective()
	{
		return XMMatrixPerspectiveFovLH(XMConvertToRadians(_fovDegrees), _aspectRatio, _near, _far);
	}

	XMMATRIX GenerateView()
	{
		return XMMatrixLookAtLH(_pos, _look, _up);
	}

	XMMATRIX GenerateView(float interpVal)
	{
		XMVECTOR pos = _prevPos;
		XMVECTOR look = _prevLook; 
		XMVECTOR viewDir;
		float hAngleDiff = _hAngle - _prevHAngle;
		float vAngleDiff = _vAngle - _prevVAngle;
		float hAngle = _prevHAngle + (hAngleDiff * interpVal);
		float vAngle = _prevVAngle + (vAngleDiff * interpVal);

		// Update rotation
		look.m128_f32[0] = pos.m128_f32[0] - sin(XMConvertToRadians(hAngle))*cos(XMConvertToRadians(vAngle));
		look.m128_f32[1] = pos.m128_f32[1] - sin(XMConvertToRadians(vAngle));
		look.m128_f32[2] = pos.m128_f32[2] + cos(XMConvertToRadians(hAngle))*cos(XMConvertToRadians(vAngle));

		viewDir.m128_f32[0] = look.m128_f32[0] - pos.m128_f32[0];
		viewDir.m128_f32[1] = look.m128_f32[1] - pos.m128_f32[1];
		viewDir.m128_f32[2] = look.m128_f32[2] - pos.m128_f32[2];

		XMVECTOR posDiff = _pos - _prevPos;
		posDiff.m128_f32[0] *= interpVal;
		posDiff.m128_f32[1] *= interpVal;
		posDiff.m128_f32[2] *= interpVal;
		pos += posDiff;

		look = pos;
		look.m128_f32[0] += viewDir.m128_f32[0];
		look.m128_f32[1] += viewDir.m128_f32[1];
		look.m128_f32[2] += viewDir.m128_f32[2];

		return XMMatrixLookAtLH(pos, look, _up);
	}

	XMFLOAT3 GetPos() const
	{
		XMFLOAT3 pos;
		XMStoreFloat3(&pos, _pos);
		return pos;
	}
	
	XMFLOAT3 GetPos(float interpVal)
	{
		XMVECTOR pos = _prevPos;
		XMVECTOR posDiff = _pos - _prevPos;
		posDiff.m128_f32[0] *= interpVal;
		posDiff.m128_f32[1] *= interpVal;
		posDiff.m128_f32[2] *= interpVal;
		pos += posDiff;
		XMFLOAT3 returnVal;
		XMStoreFloat3(&returnVal, pos);
		return returnVal;
	}

private:
	XMVECTOR _pos;
	XMVECTOR _look;
	XMVECTOR _up;

	XMVECTOR _prevPos;
	XMVECTOR _prevLook;

	float _hAngle = 0.f;
	float _vAngle = 0.f;

	float _prevHAngle = 0.f;
	float _prevVAngle = 0.f;

	float _fovDegrees = 0.f;
	float _aspectRatio = 0.f;
	float _near = 0.f;
	float _far = 0.f;

	// Variables for applying movement / rotation
	bool _dir[CamTotalDir] = { false };
	float _dirDist[CamTotalDir] = { 0.0 };

	bool _rotH = false;
	bool _rotV = false;
	float _rotHDist = 0.0;
	float _rotVDist = 0.0;
};

#pragma endregion

#pragma region Globals

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

UINT g_winWidth = 800;
UINT g_winHeight = 600;

long long g_LastFPSDelta = 0;
long long g_fpsCounter = 0;
UINT g_fpsUpdateFreq = 100;
UINT g_fps = 0;

bool g_bRun = false;

CDirectX		g_dx;
CDirect2D		g_2dx;
CShaderMGR		g_shaderMGR;
CMeshMGR		g_meshMGR;
CMaterialMGR	g_materialMGR;
CTextureMGR		g_textureMGR;
CLightMGR		g_lightMGR;

CKeyboard   g_keyboard;
CMouse		g_mouse;
CCamera		g_camera;

VertexCBufferStruct				g_worldViewProj;
VertexCBufferStruct2			g_worldViewProj2;

SModel g_testModel;

// Menu controls
int g_shadingType = 0;		// 0 - basic color, 1 - basic texture, 2 - lighting, 3 - bump mapping, 4 - parallax mapping
int g_previousShadingType = g_shadingType;

EMenuObjectSelection g_selection = eCubeSelection;
EMenuObjectSelection g_newSel = g_selection;

bool g_bSpinY = false;
bool g_bMoveReset = false;

// Directional light
// Light input handling flags.
int g_incLightHAngle = 0;		// -1 (left), 0 (no input), 1 (right)
int g_incLightVAngle = 0;		// -1 (up), 0 (no input), 1 (down)
float g_dirLightHAngle = 0;		// Current horizontal & vertical angle in use
float g_dirLightVAngle = 0;

#pragma endregion

#pragma region Functions

// Win32 Callback Functions 
std::string OnFileOpen(HWND hWnd)
{
	IFileOpenDialog* pOpenDlg;
	HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL,
		IID_IFileOpenDialog, reinterpret_cast<void**>(&pOpenDlg));

	PWSTR pszFilePath;
	std::wstring str;
	if (SUCCEEDED(hr))
	{
		hr = pOpenDlg->Show(NULL);
		if (SUCCEEDED(hr))
		{
			IShellItem* pItem;
			hr = pOpenDlg->GetResult(&pItem);
			if (SUCCEEDED(hr))
			{
				hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
				if (SUCCEEDED(hr))
				{
					str = pszFilePath;
					CoTaskMemFree(pszFilePath);
				}
				pItem->Release();
			}
		}
		pOpenDlg->Release();
	}

	std::string strReturn(str.begin(), str.end());
	return strReturn;
}

#pragma endregion

#pragma region Fwd Functions

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

bool LoadResources();
bool LoadShaders();

void UpdateSelection();
void SpinModel();
void UpdateDirectionalLight();

#pragma endregion

#pragma region Globals dependent functions

void RenderDebugText()
{
	// Display fps
	std::string text = "FPS: " + std::to_string(g_fps);
	g_2dx.RenderText(0, 0, text);
}

void RenderModel(SMesh* obj)
{
	// Get current meshes material
	SMaterial* pMat = g_materialMGR.GetMaterial(obj->_material);
	g_lightMGR.SetSpecularData(XMFLOAT4(
		pMat->_specularColor.x,
		pMat->_specularColor.y,
		pMat->_specularColor.z,
		pMat->_specularIntensity), pMat->_specular);

	// Update constants for vertex shader
	if (g_shadingType >= 2)
	{
		g_dx.GetContext()->UpdateSubresource(
			g_shaderMGR.GetCBuffer("worldViewProj2").Get(),
			0,
			nullptr,
			&g_worldViewProj2,
			0,
			0
		);

		// Update constants for pixel shader
		g_dx.GetContext()->UpdateSubresource(
			g_shaderMGR.GetCBuffer("lightData").Get(),
			0,
			nullptr,
			g_lightMGR.GetLightDataStruct(),
			0,
			0
		);
	}
	else
	{
		g_dx.GetContext()->UpdateSubresource(
			g_shaderMGR.GetCBuffer("worldViewProj").Get(),
			0,
			nullptr,
			&g_worldViewProj,
			0,
			0
		);
	}

	// Set up the IA stage by setting the input topology and layout.
	UINT stride = obj->stride;
	UINT offset = 0;

	// Set vertex buffer
	ID3D11Buffer* pVertBuf = obj->_pVertBuf.Get();
	g_dx.GetContext()->IASetVertexBuffers(
		0,
		1,
		&pVertBuf,
		&stride,
		&offset
	);

	// Set index buffer
	g_dx.GetContext()->IASetIndexBuffer(
		obj->_pIdxBuf.Get(),
		DXGI_FORMAT_R32_UINT,
		0
	);

	// Select topology
	g_dx.GetContext()->IASetPrimitiveTopology(
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST
	);

	// Set shader vertex layout
	CShaderMGR::SShader* pShader = g_shaderMGR.GetShader(pMat->_shaderID);
	g_dx.GetContext()->IASetInputLayout(pShader->_inputLayout.Get());

	// Set up the vertex shader stage.
	g_dx.GetContext()->VSSetShader(
		pShader->_vertexShader.Get(),
		nullptr,
		0
	);

	ID3D11Buffer* pBuffer = nullptr;
	if (g_shadingType >= 2)
		pBuffer = g_shaderMGR.GetCBuffer("worldViewProj2").Get();
	else
		pBuffer = g_shaderMGR.GetCBuffer("worldViewProj").Get();
	// Set constants for vertex shader
	g_dx.GetContext()->VSSetConstantBuffers(
		0,
		1,
		&pBuffer
	);

	// Set up the pixel shader stage.
	g_dx.GetContext()->PSSetShader(
		pShader->_pixelShader.Get(),
		nullptr,
		0
	);

	// Set constants for pixel shader
	if (g_shadingType > 0)
	{
		if (g_shadingType >= 3)
		{
			if (g_shadingType >= 4)
			{
				auto pDiffuse = g_textureMGR.GetTexture(pMat->_diffuseTextureID);
				auto pNormalMap = g_textureMGR.GetTexture(pMat->_normalMapTextureID);
				auto pDisplacement = g_textureMGR.GetTexture(pMat->_displacementMapTextureID);
				ID3D11ShaderResourceView* pTextures[] = { pDiffuse.Get(), pNormalMap.Get(), pDisplacement.Get() };
				if (pDiffuse && pNormalMap && pDisplacement)
				{
					g_dx.GetContext()->PSSetShaderResources(0, 3, pTextures);
				}
			}
			else
			{
				auto pDiffuse = g_textureMGR.GetTexture(pMat->_diffuseTextureID);
				auto pNormalMap = g_textureMGR.GetTexture(pMat->_normalMapTextureID);
				ID3D11ShaderResourceView* pTextures[] = { pDiffuse.Get(), pNormalMap.Get() };
				if (pDiffuse && pNormalMap)
				{
					g_dx.GetContext()->PSSetShaderResources(0, 2, pTextures);
				}
			}
		}
		else
		{
			auto pRes = g_textureMGR.GetTexture(pMat->_diffuseTextureID);
			if (pRes)
				g_dx.GetContext()->PSSetShaderResources(0, 1, pRes.GetAddressOf());
		}

		if (g_shadingType >= 2)
		{
			pBuffer = g_shaderMGR.GetCBuffer("lightData").Get();
			g_dx.GetContext()->PSSetConstantBuffers(0, 1, &pBuffer);
		}
	}

	// Calling Draw tells Direct3D to start sending commands to the graphics device.
	g_dx.GetContext()->DrawIndexed(
		obj->indexCnt,
		0,
		0
	);
}

void Render(float interpVal)
{
	// Update Proj & view matrices
	XMStoreFloat4x4(&g_worldViewProj.view, XMMatrixTranspose(g_camera.GenerateView(interpVal)));
	XMStoreFloat4x4(&g_worldViewProj.projection, XMMatrixTranspose(g_camera.GeneratePerspective()));
	XMStoreFloat4x4(&g_worldViewProj2.view, XMMatrixTranspose(g_camera.GenerateView(interpVal)));
	XMStoreFloat4x4(&g_worldViewProj2.projection, XMMatrixTranspose(g_camera.GeneratePerspective()));

	// Update eye vector in lights
	g_lightMGR.SetEyePos(g_camera.GetPos());

	// Drawing
	g_dx.Begin(XMFLOAT4(0.2f, 0.f, 0.2f, 0.f));

	// Do rendering
	XMMATRIX rotX = XMMatrixRotationX(XMConvertToRadians(g_testModel._rot.x));
	XMMATRIX rotY = XMMatrixRotationY(XMConvertToRadians(g_testModel._rot.y));
	XMMATRIX rotZ = XMMatrixRotationZ(XMConvertToRadians(g_testModel._rot.z));
	XMMATRIX rotMat = XMMatrixMultiply(rotX, rotY);
	rotMat = XMMatrixMultiply(rotMat, rotZ);

	XMMATRIX trans = XMMatrixTranslation(g_testModel._pos.x, g_testModel._pos.y, g_testModel._pos.z);
	XMMATRIX worldMat = XMMatrixMultiply(trans, rotMat);
	XMStoreFloat4x4(&g_worldViewProj.world, XMMatrixTranspose(worldMat));
	XMStoreFloat4x4(&g_worldViewProj2.world, XMMatrixTranspose(worldMat));
	XMStoreFloat4x4(&g_worldViewProj2.invTransWorld, XMMatrixTranspose(XMMatrixInverse(nullptr, XMLoadFloat4x4(&g_worldViewProj2.world))));
	RenderModel(g_meshMGR.GetMesh(g_testModel._meshId));

	// Draw any debug text on top
	RenderDebugText();

	g_dx.End();
}

void HandleKeyboard()
{
	if (g_keyboard.IsKeyDown('N'))
		g_dx.SetWireFrame(false);

	if (g_keyboard.IsKeyDown('M'))
		g_dx.SetWireFrame(true);

	if (g_keyboard.IsKeyDown(VK_ESCAPE))
		g_bRun = false;

	// Camera
	float camSpeed = 2.5f;
	if (g_keyboard.IsKeyDown('W') || g_keyboard.IsKeyHeld('W'))
		g_camera.Forward(camSpeed);

	if (g_keyboard.IsKeyDown('S') || g_keyboard.IsKeyHeld('S'))
		g_camera.Backward(camSpeed);

	if (g_keyboard.IsKeyDown('A') || g_keyboard.IsKeyHeld('A'))
		g_camera.StrafeLeft(camSpeed);

	if (g_keyboard.IsKeyDown('D') || g_keyboard.IsKeyHeld('D'))
		g_camera.StrafeRight(camSpeed);

	if (g_keyboard.IsKeyDown('I') || g_keyboard.IsKeyHeld('I'))
		g_camera.RotateV(0.02f);

	if (g_keyboard.IsKeyDown('K') || g_keyboard.IsKeyHeld('K'))
		g_camera.RotateV(-0.02f);

	// This just moves the directional light only
	if (g_keyboard.IsKeyDown(VK_LEFT) || g_keyboard.IsKeyHeld(VK_LEFT))
		g_incLightHAngle = -1;

	if (g_keyboard.IsKeyDown(VK_RIGHT) || g_keyboard.IsKeyHeld(VK_RIGHT))
		g_incLightHAngle = 1;

	if (g_keyboard.IsKeyDown(VK_UP) || g_keyboard.IsKeyHeld(VK_UP))
		g_incLightVAngle = -1;

	if (g_keyboard.IsKeyDown(VK_DOWN) || g_keyboard.IsKeyHeld(VK_DOWN))
		g_incLightVAngle = 1;

	// Adjust the color of the directional to pre-defined values
	if (g_keyboard.IsKeyDown(VK_NUMPAD0))
		g_lightMGR.SetDirectionalLightColor(XMFLOAT3(1, 1, 1));

	if (g_keyboard.IsKeyDown(VK_NUMPAD1))
		g_lightMGR.SetDirectionalLightColor(XMFLOAT3(1, 0, 0));

	if (g_keyboard.IsKeyDown(VK_NUMPAD2))
		g_lightMGR.SetDirectionalLightColor(XMFLOAT3(0, 1, 0));

	if (g_keyboard.IsKeyDown(VK_NUMPAD3))
		g_lightMGR.SetDirectionalLightColor(XMFLOAT3(0, 0, 1));
}

void HandleMouse()
{
	// Handle rotating the FPS camera
	if (g_mouse.IsLeftDown())
	{
		POINT downPos = g_mouse.GetMouseLeftDown();
		POINT cur = g_mouse.GetMousePos();

		float camRotSpd = 0.5f;
		float h = (downPos.x - cur.x) * camRotSpd;
		float v = (downPos.y - cur.y) * camRotSpd;

		g_camera.RotateH(h);
		g_camera.RotateV(-v);

		g_mouse.SetMousePos(downPos);
	}

	// Handle the ability to move camera in/out using the mouse wheel
	float camSpeed = 14.0f;
	long mouseWheelDelta = g_mouse.GetMouseWheelDeltaAndReset();
	if (mouseWheelDelta > 0)
	{
		g_camera.Forward(camSpeed);
	}

	if (mouseWheelDelta < 0)
	{
		g_camera.Backward(camSpeed);
	}
}

#pragma endregion

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // Initialise COM library for use for current thread (for open file dialog).
	HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED |
		COINIT_DISABLE_OLE1DDE);

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_MODELVIEWERXD, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_MODELVIEWERXD));

    MSG msg;

	std::chrono::high_resolution_clock timer;
	auto lastUpdate = timer.now();
	auto lastFrameUpdate = timer.now();
	float t = 1;

	while (g_bRun)
	{
		// Main message loop:
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			//if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
		else
		{
			// Current frame time
			auto current = timer.now();
			// Elapsed time from last frame (in microseconds)
			auto elapsedFrame = std::chrono::duration_cast<std::chrono::microseconds>(current - lastFrameUpdate);
			lastFrameUpdate = current;

			// Update handle input
			// Calling in every frame update
			g_keyboard.Update((int)elapsedFrame.count());
			HandleMouse();
			HandleKeyboard();

			// Time update per 'tick' for logic simulation
			auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(current - lastUpdate);
			if (elapsed.count() >= 50)
			{
				UpdateSelection();
				SpinModel();
				UpdateDirectionalLight();
				lastUpdate = current;

				// Update camera and render
				g_camera.Update();
				t = 0;
			}
			else
			{
				t = float(elapsed.count()) / 50.f;
			}
			
			// Draw
			Render(t);

			// Calculate FPS
			g_fpsCounter++;
			g_LastFPSDelta += static_cast<long>(elapsedFrame.count());
			// Convert to milliseconds for update check
			if ((g_LastFPSDelta / 1000.f) >= g_fpsUpdateFreq)
			{
				float denom = 1000.f / static_cast<float>(g_fpsUpdateFreq);
				g_fps = static_cast<UINT>(g_fpsCounter * denom);
				g_LastFPSDelta = 0;
				g_fpsCounter = 0;
			}
		}
	}

	// Close COM
	CoUninitialize();
    return (int) msg.wParam;
}



//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MODELVIEWERXD));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_MODELVIEWERXD);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // Store instance handle in our global variable

   HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
      return FALSE;
   
   // Recalculate window to get client area to correct size
   RECT rClient, rWindow;
   POINT ptDiff;
   GetClientRect(hWnd, &rClient);
   GetWindowRect(hWnd, &rWindow);
   ptDiff.x = (rWindow.right - rWindow.left) - rClient.right;
   ptDiff.y = (rWindow.bottom - rWindow.top) - rClient.bottom;
   MoveWindow(hWnd, rWindow.left, rWindow.top, g_winWidth + ptDiff.x, g_winHeight + ptDiff.y, TRUE);

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   // DirectX init
   bool OK = g_dx.Initialize(hWnd, g_winWidth, g_winHeight);
   if (OK)
   {
	   // Subsystem init
	   g_2dx.Initialise(&g_dx);
	   g_shaderMGR.Initialise(&g_dx);
	   g_meshMGR.Init(&g_dx);
	   g_textureMGR.Initialise(&g_dx);

	   if (LoadResources())
		   g_bRun = true;
	   else
		   g_bRun = false;
   }

   return TRUE;
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE: Processes messages for the main window.
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            // Parse the menu selections:
            switch (wmId)
            {
            case IDM_ABOUT:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                break;
            case IDM_EXIT:
				g_bRun = false;
                DestroyWindow(hWnd);
                break;
			case IDM_FILE_OPEN:
				OnFileOpen(hWnd);
				break;
			case ID_OBJECT_DEBUGTRIANGLE:
				g_newSel = eTriangleDebugSelection;
				break;
			case ID_OBJECT_TRIANGLE:
				g_newSel = eTriangleSelection;
				break;
			case ID_OBJECT_QUAD:
				g_newSel = eQuadSelection;
				break;
			case ID_OBJECT_PLANE:
				g_newSel = ePlaneSelection;
				break;
			case ID_OBJECT_CUBE:
				g_newSel = eCubeSelection;
				break;
			case ID_OBJECT_SPHERE:
				g_newSel = eSphereSelection;
				break;
			case ID_OBJECTSHADING_BASICCOLOR:
				g_shadingType = 0;
				break;
			case ID_OBJECTSHADING_BASICTEXTURE:
				g_shadingType = 1;
				break;
			case ID_OBJECTSHADING_LIGHTING:
				g_shadingType = 2;
				break;
			case ID_OBJECTSHADING_NORMALMAP:
				g_shadingType = 3;
				break;
			case ID_OBJECTSHADING_PARALLAXMAP:
				g_shadingType = 4;
				break;
			case ID_OBJECTMOVEMENT_TOGGLESPINY:
				g_bSpinY = !g_bSpinY;
				break;
			case ID_OBJECTMOVEMENT_RESET:
				g_bMoveReset = true;
				break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            // TODO: Add any drawing code that uses hdc here...
            EndPaint(hWnd, &ps);
        }
        break;
    case WM_DESTROY:
		g_bRun = false;
        PostQuitMessage(0);
        break;
	case WM_KEYDOWN:
		g_keyboard.KeyDown(static_cast<int>(wParam));
		//HandleKeyboardInput(wParam, lParam);
		break;
	case WM_KEYUP:
		g_keyboard.KeyUp(static_cast<int>(wParam));
		break;
	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_MOUSEMOVE:
	case WM_MOUSEWHEEL:
		g_mouse.Event(message, wParam, lParam);
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Message handler for about box.m
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}


bool LoadResources()
{
	// Resources
	//
	if (!LoadShaders())
		return false;

	// Materials
	SMaterial basicMat;
	basicMat._color = XMFLOAT3(0.3f, 0.4f, 0.6f);
	basicMat._shaderID = "basic";
	g_materialMGR.AddMaterial("basicMat", basicMat);

	SMaterial basicTextureMat;
	basicTextureMat._shaderID = "basicTexture";
	basicTextureMat._diffuseTextureID = "default";
	g_materialMGR.AddMaterial("basicTexMat", basicTextureMat);

	SMaterial lambertMat;
	lambertMat._specular = 64.f;
	lambertMat._specularColor = XMFLOAT3(0.6f, 0.9f, 0.6f);
	lambertMat._specularIntensity = 0.7f;
	lambertMat._shaderID = "lighting";
	lambertMat._diffuseTextureID = "default";
	g_materialMGR.AddMaterial("litTextureMat", lambertMat);

	SMaterial bumpMat;
	bumpMat._specular = 64.f;
	bumpMat._specularColor = XMFLOAT3(0.7f, 0.9f, 0.7f);
	bumpMat._specularIntensity = 0.5f;
	bumpMat._shaderID = "lightingBump";
	bumpMat._diffuseTextureID = "wallDiffuse";
	bumpMat._normalMapTextureID = "wallNormal";
	g_materialMGR.AddMaterial("bumpMat", bumpMat);

	SMaterial parallaxMat;
	parallaxMat._specular = 64.f;
	parallaxMat._specularColor = XMFLOAT3(0.7f, 0.9f, 0.7f);
	parallaxMat._specularIntensity = 0.5f;
	parallaxMat._shaderID = "lightingParallax";
	parallaxMat._diffuseTextureID = "diffuseMap";
	parallaxMat._normalMapTextureID = "normalMap";
	parallaxMat._displacementMapTextureID = "heightMap";
	g_materialMGR.AddMaterial("parallaxMat", parallaxMat);

	// Load/Create mesh
	g_meshMGR.CreateTriangleDebugPC("debugTriangle", 6, "basicMat");
	g_meshMGR.CreateTrianglePC("myTriangle", 6, "basicMat", XMFLOAT3(0, 1, 0));
	g_meshMGR.CreateQuadPC("myQuad", 8, "basicMat", XMFLOAT3(0.2f, 0.6f, 0));
	g_meshMGR.CreatePlanePC("myPlane", 100, "basicMat", XMFLOAT3(0, 0.6f, 0.4f), 25);
	g_meshMGR.CreateCubePC("myCube", 10, "basicMat", XMFLOAT3(0.4f, 0.2f, 0.5f), 4);
	g_meshMGR.CreateSpherePC("mySphere", 10, "basicMat", XMFLOAT3(0.7f, 0.5f, 0.3f), 25);

	g_meshMGR.CreateTrianglePT("myTriangleTex", 6, "basicTexMat");
	g_meshMGR.CreateQuadPT("myQuadTex", 8, "basicTexMat");
	g_meshMGR.CreatePlanePT("myPlaneTex", 100, "basicTexMat", 25);
	g_meshMGR.CreateCubePT("myCubeTex", 10, "basicTexMat", 4);
	g_meshMGR.CreateSpherePT("mySphereTex", 10, "basicTexMat", 25);

	g_meshMGR.CreateTrianglePNT("myTriangleLit", 6, "litTextureMat");
	g_meshMGR.CreateQuadPNT("myQuadLit", 8, "litTextureMat");
	g_meshMGR.CreatePlanePNT("myPlaneLit", 100, "litTextureMat", 25);
	g_meshMGR.CreateCubePNT("myCubeLit", 10, "litTextureMat", 4);
	g_meshMGR.CreateSpherePNT("mySphereLit", 10, "litTextureMat", 50);

	g_meshMGR.CreateTrianglePNTT("myTriangleBump", 6, "bumpMat");
	g_meshMGR.CreateQuadPNTT("myQuadBump", 8, "bumpMat");
	g_meshMGR.CreatePlanePNTT("myPlaneBump", 100, "bumpMat", 25);
	g_meshMGR.CreateCubePNTT("myCubeBump", 10, "bumpMat", 400);
	g_meshMGR.CreateSpherePNTT("mySphereBump", 10, "bumpMat", 50);

	g_meshMGR.CreateTrianglePNTT("myTriangleParallax", 6, "parallaxMat");
	g_meshMGR.CreateQuadPNTT("myQuadParallax", 8, "parallaxMat");
	g_meshMGR.CreatePlanePNTT("myPlaneParallax", 100, "parallaxMat", 25);
	g_meshMGR.CreateCubePNTT("myCubeParallax", 10, "parallaxMat", 4);
	g_meshMGR.CreateSpherePNTT("mySphereParallax", 10, "parallaxMat", 50);

	// TESTING
	// Setup cam defaults
	XMFLOAT3 pos(0, 0, -15);
	XMFLOAT3 up(0, 1, 0);
	g_camera.SetPerspective(70.f, (float)g_winWidth / (float)g_winHeight, 0.01f, 1000.f);
	g_camera.SetView(pos, up);
	g_testModel._meshId = "myCube";

	// Init world matrix
	XMStoreFloat4x4(&g_worldViewProj.world, XMMatrixIdentity());
	XMStoreFloat4x4(&g_worldViewProj2.world, XMMatrixIdentity());
	XMStoreFloat4x4(&g_worldViewProj2.invTransWorld, XMMatrixIdentity());

	// Init light data
	// Direction light
	g_lightMGR.EnableDirectionalLight(true);
	g_lightMGR.SetDirectionalLightDir(XMFLOAT3(0, 0, 1));
	g_lightMGR.SetDirectionalLightColor(XMFLOAT3(1, 1, 1));

	// Enable test point light
	g_lightMGR.SetActivePointLights(0);
	g_lightMGR.SetPointLightData(0, XMFLOAT4(0, 6, 0, 5), XMFLOAT4(1, 1, 1, 1));

	// Spot light
	g_lightMGR.SetActiveSpotLights(0);
	g_lightMGR.SetSpotLightData(0, XMFLOAT4(0, 10, 0, 40), XMFLOAT4(1, 1, 1, 1), XMFLOAT4(0, -1, 0, 45), 20);

	return true;
}

bool LoadShaders()
{
	// Load basic shader
	// Vertex description (position & color)
	D3D11_INPUT_ELEMENT_DESC desc[] = {
		 { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,
		 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },

		 { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT,
		 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	std::string vertShader = "..\\Assets\\basic.vs";
	std::string pixelShader = "..\\Assets\\basic.ps";
	std::string shaderID = "basic";
	if (!g_shaderMGR.LoadShader(vertShader, pixelShader, desc, ARRAYSIZE(desc), shaderID))
		return false;

	// Load basic texture shader
	// Vertex description (position & texture UV).
	D3D11_INPUT_ELEMENT_DESC descPT[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,
		0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },

		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,
		0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	vertShader = "..\\Assets\\basicTexture.vs";
	pixelShader = "..\\Assets\\basicTexture.ps";
	shaderID = "basicTexture";
	if (!g_shaderMGR.LoadShader(vertShader, pixelShader, descPT, ARRAYSIZE(descPT), shaderID))
		return false;

	// Load light shader
	// Vertex description (position. normal & texture UV).
	D3D11_INPUT_ELEMENT_DESC descPNT[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,
		0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },

		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT,
		0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },

		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,
		0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	vertShader = "..\\Assets\\lighting.vs";
	pixelShader = "..\\Assets\\lighting.ps";
	shaderID = "lighting";
	if (!g_shaderMGR.LoadShader(vertShader, pixelShader, descPNT, ARRAYSIZE(descPNT), shaderID))
		return false;

	D3D11_INPUT_ELEMENT_DESC descPNTT[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,
		0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },

		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT,
		0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },

		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT,
		0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },

		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,
		0, 40, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};

	vertShader = "..\\Assets\\lightingBump.vs";
	pixelShader = "..\\Assets\\lightingBump.ps";
	shaderID = "lightingBump";
	if (!g_shaderMGR.LoadShader(vertShader, pixelShader, descPNTT, ARRAYSIZE(descPNTT), shaderID))
		return false;

	vertShader = "..\\Assets\\lightingParallax.vs";
	pixelShader = "..\\Assets\\lightingParallax.ps";
	shaderID = "lightingParallax";
	if (!g_shaderMGR.LoadShader(vertShader, pixelShader, descPNTT, ARRAYSIZE(descPNTT), shaderID))
		return false;
	
	// Load textures
	std::string textureID = "default";
	std::string textureDir = "..\\Assets\\default.bmp";
	if (!g_textureMGR.LoadTexture(textureDir, textureID))
		return false;

	textureID = "wallNormal";
	textureDir = "..\\Assets\\bricksNormal.jpg";
	if (!g_textureMGR.LoadTexture(textureDir, textureID))
		return false;

	textureID = "wallDiffuse";
	textureDir = "..\\Assets\\bricksDiffuse.jpg";
	if (!g_textureMGR.LoadTexture(textureDir, textureID))
		return false;

	textureID = "diffuseMap";
	textureDir = "..\\Assets\\bricksDiffuse.jpg";
	if (!g_textureMGR.LoadTexture(textureDir, textureID))
		return false;

	textureID = "normalMap";
	textureDir = "..\\Assets\\bricksNormal.jpg";
	if (!g_textureMGR.LoadTexture(textureDir, textureID))
		return false;

	textureID = "heightMap";
	textureDir = "..\\Assets\\bricksDisp.jpg";
	if (!g_textureMGR.LoadTexture(textureDir, textureID))
		return false;

	// Bind shader constant buffers
	std::string vertexBufferID = "worldViewProj";
	CD3D11_BUFFER_DESC bufferDesc(
		sizeof(VertexCBufferStruct),
		D3D11_BIND_CONSTANT_BUFFER
	);
	if (!g_shaderMGR.BindConstant(vertexBufferID, &bufferDesc))
		return false;

	vertexBufferID = "worldViewProj2";
	CD3D11_BUFFER_DESC bufferLightDesc(
		sizeof(VertexCBufferStruct2),
		D3D11_BIND_CONSTANT_BUFFER
	);
	if (!g_shaderMGR.BindConstant(vertexBufferID, &bufferLightDesc))
		return false;

	std::string pixelBufferID = "lightData";
	CD3D11_BUFFER_DESC bufferLightDataDesc(
		sizeof(PixelCBufferStructLightData),
		D3D11_BIND_CONSTANT_BUFFER
	);
	if (!g_shaderMGR.BindConstant(pixelBufferID, &bufferLightDataDesc))
		return false;

	return true;
}

void UpdateSelection()	
{
	if (g_selection != g_newSel ||
		g_previousShadingType != g_shadingType)
	{
		g_selection = g_newSel;
		g_previousShadingType = g_shadingType;
		switch (g_selection)
		{
		case eTriangleDebugSelection:
			g_testModel._meshId = "debugTriangle";
			break;
		case eTriangleSelection:
			switch (g_shadingType)
			{
			case 0:
				g_testModel._meshId = "myTriangle";
				break;
			case 1:
				g_testModel._meshId = "myTriangleTex";
				break;
			case 2:
				g_testModel._meshId = "myTriangleLit";
				break;
			case 3:
				g_testModel._meshId = "myTriangleBump";
				break;
			case 4:
				g_testModel._meshId = "myTriangleParallax";
				break;
			default:
				break;
			}
			break;
		case eQuadSelection:
			switch (g_shadingType)
			{
			case 0:
				g_testModel._meshId = "myQuad";
				break;
			case 1:
				g_testModel._meshId = "myQuadTex";
				break;
			case 2:
				g_testModel._meshId = "myQuadLit";
				break;
			case 3:
				g_testModel._meshId = "myQuadBump";
				break;
			case 4:
				g_testModel._meshId = "myQuadParallax";
				break;
			default:
				break;
			}
			break;
		case ePlaneSelection:
			switch (g_shadingType)
			{
			case 0:
				g_testModel._meshId = "myPlane";
				break;
			case 1:
				g_testModel._meshId = "myPlaneTex";
				break;
			case 2:
				g_testModel._meshId = "myPlaneLit";
				break;
			case 3:
				g_testModel._meshId = "myPlaneBump";
				break;
			case 4:
				g_testModel._meshId = "myPlaneParallax";
				break;
			default:
				break;
			}
			break;
		case eCubeSelection:
			switch (g_shadingType)
			{
			case 0:
				g_testModel._meshId = "myCube";
				break;
			case 1:
				g_testModel._meshId = "myCubeTex";
				break;
			case 2:
				g_testModel._meshId = "myCubeLit";
				break;
			case 3:
				g_testModel._meshId = "myCubeBump";
				break;
			case 4:
				g_testModel._meshId = "myCubeParallax";
				break;
			default:
				break;
			}
			break;
		case eSphereSelection:
			switch (g_shadingType)
			{
			case 0:
				g_testModel._meshId = "mySphere";
				break;
			case 1:
				g_testModel._meshId = "mySphereTex";
				break;
			case 2:
				g_testModel._meshId = "mySphereLit";
				break;
			case 3:
				g_testModel._meshId = "mySphereBump";
				break;
			case 4:
				g_testModel._meshId = "mySphereParallax";
				break;
			default:
				break;
			}
			break;
		default:
			break;
		}
	}
}

void SpinModel()
{
	if (g_bMoveReset)
	{
		g_testModel._rot = XMFLOAT3(0, 0, 0);
		g_bMoveReset = false;
	}

	if (g_bSpinY)
	{
		g_testModel._rot.y += 15;
		if (g_testModel._rot.y > 360)
			g_testModel._rot.y -= 360;
	}
}

void UpdateDirectionalLight()
{
	float amount = 10.0;

	if (g_incLightHAngle < 0) g_dirLightHAngle += amount;
	if (g_incLightHAngle > 0) g_dirLightHAngle -= amount;
	if (g_incLightVAngle < 0) g_dirLightVAngle -= amount;
	if (g_incLightVAngle > 0) g_dirLightVAngle += amount;

	if (g_dirLightHAngle > 360) g_dirLightHAngle -= 360;
	if (g_dirLightHAngle < 0) g_dirLightHAngle += 360;
	if (g_dirLightVAngle > 360) g_dirLightHAngle -= 360;
	if (g_dirLightVAngle < 0) g_dirLightVAngle += 360;
	float x = sin(XMConvertToRadians(g_dirLightHAngle))*cos(XMConvertToRadians(g_dirLightVAngle));
	float y = sin(XMConvertToRadians(g_dirLightVAngle));
	float z = cos(XMConvertToRadians(g_dirLightHAngle))*cos(XMConvertToRadians(g_dirLightVAngle));
	g_lightMGR.SetDirectionalLightDir(XMFLOAT3(x, y, z));

	g_incLightHAngle = 0;
	g_incLightVAngle = 0;
}