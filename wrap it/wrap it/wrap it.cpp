#include <windows.h>
#include <wrl.h>
#include "WebView2.h"
#include <fstream>
#include <string>
#include <psapi.h>

#pragma comment(lib, "psapi.lib")

using namespace Microsoft::WRL;

#define WM_TRAYICON (WM_USER + 1)

#define TRAY_ICON_ID 1

HINSTANCE hInst;
HWND mainWindow;

ComPtr<ICoreWebView2Controller> webviewController;
ComPtr<ICoreWebView2> webview;

std::wstring windowTitleGlobal;

NOTIFYICONDATA nid = {};

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void MinimizeToTray(HWND hWnd);
void RestoreFromTray(HWND hWnd);
void TrimMemory();
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	hInst = hInstance;
	const wchar_t CLASS_NAME[] = L"AnyWrapClass";

	WNDCLASSEXW wcex = {};
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.hInstance = hInstance;
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszClassName = CLASS_NAME;
	wcex.hIcon = LoadIcon(hInstance, IDI_APPLICATION);
	wcex.hIconSm = LoadIcon(wcex.hInstance, IDI_APPLICATION);

	RegisterClassExW(&wcex);


	std::wstring windowTitle = L"Generic Wrapper";
	std::wstring targetUrl = L"https://google.com";
	std::wstring darkModeStr = L"false";
	bool enableDarkMode = false;
	std::wifstream configFile("config.txt");
	if (configFile.is_open()) {
		std::getline(configFile, windowTitle);
		std::getline(configFile, targetUrl);
		std::getline(configFile, darkModeStr);
		configFile.close();
	}
	windowTitleGlobal = windowTitle;

	if (darkModeStr.find(L"true") != std::wstring::npos || darkModeStr.find(L"1") != std::wstring::npos) {
		enableDarkMode = true;
	}

	int screenWidth = GetSystemMetrics(SM_CXSCREEN);
	int screenHeight = GetSystemMetrics(SM_CYSCREEN);

	int appWidth = (screenWidth * 8) / 10;
	int appHeight = (screenHeight * 8) / 10;

	int appX = (screenWidth - appWidth) / 2;
	int appY = (screenHeight - appHeight) / 2;
	mainWindow = CreateWindowExW(
		0, CLASS_NAME, windowTitle.c_str(),
		WS_OVERLAPPEDWINDOW,
		appX, appY,
		appWidth, appHeight,
		nullptr, nullptr, hInstance, nullptr
	);

	if (!mainWindow) return FALSE;

	ShowWindow(mainWindow, nCmdShow);
	UpdateWindow(mainWindow);

	nid.cbSize = sizeof(NOTIFYICONDATA);
	nid.hWnd = mainWindow;
	nid.uID = TRAY_ICON_ID;
	nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	nid.uCallbackMessage = WM_TRAYICON;
	nid.hIcon = LoadIcon(hInstance, IDI_APPLICATION);
	lstrcpyn(nid.szTip, windowTitle.c_str(), sizeof(nid.szTip) / sizeof(TRAY_ICON_ID));

	CreateCoreWebView2EnvironmentWithOptions(nullptr, nullptr, nullptr,
		Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
			[hWnd = mainWindow, targetUrl, enableDarkMode](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {

				env->CreateCoreWebView2Controller(hWnd, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
					[hWnd, targetUrl, enableDarkMode](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
						if (controller != nullptr) {
							webviewController = controller;
							webviewController->get_CoreWebView2(&webview);

							RECT bounds;
							GetClientRect(hWnd, &bounds);
							webviewController->put_Bounds(bounds);


							if (enableDarkMode) {
								std::wstring js_inject = L"document.addEventListener('DOMContentLoaded', function() {"
									L"  var css = 'html { filter: invert(100%) hue-rotate(180deg); } img, video { filter: invert(100%) hue-rotate(180deg); }';"
									L"  var style = document.createElement('style');"
									L"  document.head.appendChild(style);"
									L"  style.type = 'text/css';"
									L"  style.appendChild(document.createTextNode(css));"
									L"});";
								webview->AddScriptToExecuteOnDocumentCreated(js_inject.c_str(), nullptr);
							}
							webview->Navigate(targetUrl.c_str());
						}
						return S_OK;
					}).Get());
				return S_OK;
			}).Get());

	MSG msg;
	while (GetMessage(&msg, nullptr, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return (int)msg.wParam;
}
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
	case WM_SIZE:
		if (webviewController != nullptr) {
			RECT bounds;
			GetClientRect(hWnd, &bounds);
			webviewController->put_Bounds(bounds);
		}
		break;
	case WM_TRAYICON:
		if (lParam == WM_LBUTTONDBLCLK) {
			RestoreFromTray(hWnd);
		}
		else if (lParam == WM_RBUTTONUP) {
			RestoreFromTray(hWnd);
		}
		break;
	case WM_SYSCOMMAND:
		if ((wParam & 0xFFF0) == SC_MINIMIZE) {
			MinimizeToTray(hWnd);
			return 0;
		}
		return DefWindowProc(hWnd, message, wParam, lParam);
	case WM_DESTROY:
		Shell_NotifyIcon(NIM_DELETE, &nid);
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

void TrimMemory() {
	if (EmptyWorkingSet(GetCurrentProcess())) {
		OutputDebugString(L"AnyWrap: Memory trimmed successfully.\n");
	}
}

void MinimizeToTray(HWND hWnd) {
	Shell_NotifyIcon(NIM_ADD, &nid);
	ShowWindow(hWnd, SW_HIDE);
	TrimMemory();
}

void RestoreFromTray(HWND hWnd) {
	ShowWindow(hWnd, SW_SHOW);
	ShowWindow(hWnd, SW_RESTORE);
	Shell_NotifyIcon(NIM_DELETE, &nid);
}