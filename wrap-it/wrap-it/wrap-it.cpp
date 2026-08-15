#include <windows.h>
#include <wrl.h>
#include "WebView2.h"
#include "WebView2EnvironmentOptions.h"
#include <fstream>
#include <string>
#include <psapi.h>
#include "Resource.h"
#include <TlHelp32.h>
#include <urlmon.h>
#include <thread>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "urlmon.lib")
using namespace Microsoft::WRL;

#define WM_TRAYICON (WM_USER + 1)
#define TRAY_ICON_ID 1
#define ID_TRAY_EXIT 2001
#define WM_WAKEUP (WM_USER + 2)

HINSTANCE hInst;
HWND mainWindow;
ComPtr<ICoreWebView2Controller> webviewController;
ComPtr<ICoreWebView2> webview;


std::wstring windowTitleGlobal;
bool aggressiveMemorySave = false; 
bool quitOnClose = false;
bool openLinksInBrowserGlobal = false;
NOTIFYICONDATA nid = {};

const double CURRENT_VERSION = 3.1;

std::wstring GITHUB_VERSION_URL = L"https://raw.githubusercontent.com/navaneeth006-l/wrap-it/main/version.txt";
std::wstring GITHUB_EXE_URL = L"https://github.com/navaneeth006-l/wrap-it/releases/latest/download/wrap-it.exe";

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void MinimizeToTray(HWND hWnd);
void RestoreFromTray(HWND hWnd);
void TrimMemory();
void RunAutoUpdater(std::wstring basePath, std::wstring exePathRaw);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    hInst = hInstance;
    const wchar_t CLASS_NAME[] = L"AnyWrapClass";

    wchar_t exePathRaw[MAX_PATH];
    GetModuleFileNameW(NULL, exePathRaw, MAX_PATH);
    std::wstring basePath(exePathRaw);

    size_t lastSlash = basePath.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        basePath = basePath.substr(0, lastSlash + 1); // Leaves the trailing slash (e.g., "C:\App\")
    }

    std::wstring oldExePath = basePath + L"wrap it.old.exe";
    DeleteFileW(oldExePath.c_str());

    std::wstring configPath = basePath + L"config.txt";

    
    std::string windowTitleStr = "Generic Wrapper";
    std::string targetUrlStr = "https://google.com";
    std::string darkModeStr = "false";
    //std::string memoryToggleStr = "false";
    //std::string quitOnCloseStr = "false";
    bool enableDarkMode = false;

        
    std::ifstream configFile(configPath);
        if (configFile.is_open()) {
            std::string line;
            while (std::getline(configFile, line)) {
               
                size_t delimiterPos = line.find('=');
                if (delimiterPos != std::string::npos) {
                   
                    std::string key = line.substr(0, delimiterPos);
                    std::string value = line.substr(delimiterPos + 1);

                    
                    auto trim = [](std::string& str) {
                        size_t first = str.find_first_not_of(" \t\r\n\"");
                        if (std::string::npos == first) { str.clear(); return; }
                        size_t last = str.find_last_not_of(" \t\r\n\"");
                        str = str.substr(first, (last - first + 1));
                        };

                    trim(key);
                    trim(value);

                    
                    for (auto& c : key) c = tolower(c);

                    
                    if (key == "name") {
                        windowTitleStr = value;
                    }
                    else if (key == "link") {
                        targetUrlStr = value;
                    }
                    else {
                        
                        std::string lowerVal = value;
                        for (auto& c : lowerVal) c = tolower(c);

                        if (key == "darkmode" && lowerVal == "true") enableDarkMode = true;
                        else if (key == "aggressivememory" && lowerVal == "true") aggressiveMemorySave = true;
                        else if (key == "quitonclose" && lowerVal == "true") quitOnClose = true;
                        else if (key == "openlinksinbrowser" && lowerVal == "true") openLinksInBrowserGlobal = true;
                    }
                }
            }
            configFile.close();
        }

    
    std::wstring windowTitle(windowTitleStr.begin(), windowTitleStr.end());
    std::wstring targetUrl(targetUrlStr.begin(), targetUrlStr.end());
    windowTitleGlobal = windowTitle;
    

    //if (darkModeStr.find("true") != std::string::npos || darkModeStr.find("1") != std::string::npos) {
    //    enableDarkMode = true;
    //}

    std::wstring mutexName = L"WrapIt_Mutex_" + windowTitle;
    HANDLE hMutex = CreateMutexW(NULL, TRUE, mutexName.c_str());

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        
        HWND existingApp = FindWindowW(CLASS_NAME, windowTitle.c_str());
        if (existingApp) {
            
            PostMessage(existingApp, WM_WAKEUP, 0, 0);
        }
        
        return 0;
    }

    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = CLASS_NAME;
    wcex.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_ICON1));
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_ICON1));

    RegisterClassExW(&wcex);
    
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
    nid.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
    wcscpy_s(nid.szTip, ARRAYSIZE(nid.szTip), windowTitle.c_str()); 

    std::thread updaterThread(RunAutoUpdater, basePath, std::wstring(exePathRaw));
    updaterThread.detach();
    
    auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();

    std::wstring args =
        L"--disable-extensions "
        L"--disable-pdf-extension "
        L"--disable-plugins "
        L"--disable-background-networking "
        L"--disable-sync "
        L"--disable-metrics "
        L"--disable-component-update "
        L"--renderer-process-limit=1 ";

    options->put_AdditionalBrowserArguments(args.c_str());

    CreateCoreWebView2EnvironmentWithOptions(nullptr, nullptr, options.Get(),
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

                            webview->add_NewWindowRequested(
                                Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                                    [](ICoreWebView2* sender, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                                        if (openLinksInBrowserGlobal) {
                                            LPWSTR uri;
                                            args->get_Uri(&uri);
                                            ShellExecuteW(nullptr, L"open", uri, nullptr, nullptr, SW_SHOWNORMAL);
                                            args->put_Handled(TRUE);
                                            CoTaskMemFree(uri);
                                        }
                                        return S_OK;
                                    }
                                ).Get(), nullptr
                            );

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

void RunAutoUpdater(std::wstring basePath, std::wstring exePathRaw) {
    std::wstring tempVersionFile = basePath + L"temp_version.txt";

    HRESULT hr = URLDownloadToFileW(NULL, GITHUB_VERSION_URL.c_str(), tempVersionFile.c_str(), 0, NULL);

    if (hr == S_OK) {
        std::ifstream vFile(tempVersionFile);
        std::string vStr;

        
        if (std::getline(vFile, vStr)) {
            try {
                double onlineVersion = std::stod(vStr);

                
                if (onlineVersion > CURRENT_VERSION) {

                    
                    std::wstring newExePath = basePath + L"update.exe";
                    HRESULT hr2 = URLDownloadToFileW(NULL, GITHUB_EXE_URL.c_str(), newExePath.c_str(), 0, NULL);

                    if (hr2 == S_OK) {
                        std::wstring oldExePath = basePath + L"wrap it.old.exe";

                        
                        if (_wrename(exePathRaw.c_str(), oldExePath.c_str()) == 0) {

                            
                            if (_wrename(newExePath.c_str(), exePathRaw.c_str()) == 0) {

                                
                                ShellExecuteW(NULL, L"open", exePathRaw.c_str(), NULL, NULL, SW_SHOWNORMAL);

                              
                                PostMessage(mainWindow, WM_CLOSE, 0, 0);
                            }
                        }
                    }
                }
            }
            catch (...) {
                
            }
        }
        vFile.close();
        DeleteFileW(tempVersionFile.c_str()); 
    }
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
    case WM_WAKEUP:
        RestoreFromTray(hWnd);
        SetForegroundWindow(hWnd);
        break;
    case WM_TRAYICON:
        if (lParam == WM_LBUTTONDBLCLK) {
            RestoreFromTray(hWnd);
        }
        else if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            std::wstring quitText = L"Quit " + windowTitleGlobal;
            InsertMenu(hMenu, 0, MF_BYPOSITION | MF_STRING, ID_TRAY_EXIT, quitText.c_str());

            SetForegroundWindow(hWnd);
            TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, NULL);
            DestroyMenu(hMenu);
        }
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == ID_TRAY_EXIT) {
            Shell_NotifyIcon(NIM_DELETE, &nid);
            PostQuitMessage(0);
        }
        break;
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_CLOSE) {
            if (quitOnClose) {
                DestroyWindow(hWnd);
            }
            else {
                MinimizeToTray(hWnd);
                if (aggressiveMemorySave) {
                    Sleep(100);
                    TrimMemory();
                }
            }
            return 0;
        }
        return DefWindowProc(hWnd, message, wParam, lParam);
    case WM_CLOSE:
        Shell_NotifyIcon(NIM_DELETE, &nid);
        PostQuitMessage(0);
        break;
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
    
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
    EmptyWorkingSet(GetCurrentProcess());

    
    if (webview != nullptr) {
        UINT32 browserPid = 0;
        webview->get_BrowserProcessId(&browserPid);

        if (browserPid != 0) {
            HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (hSnap != INVALID_HANDLE_VALUE) {
                PROCESSENTRY32 pe32;
                pe32.dwSize = sizeof(PROCESSENTRY32);
                if (Process32First(hSnap, &pe32)) {
                    do {
                        if (pe32.th32ProcessID == browserPid || pe32.th32ParentProcessID == browserPid) {
                            HANDLE hProcess = OpenProcess(PROCESS_SET_QUOTA | PROCESS_QUERY_INFORMATION, FALSE, pe32.th32ProcessID);
                            if (hProcess != NULL) {
                                SetProcessWorkingSetSize(hProcess, (SIZE_T)-1, (SIZE_T)-1);
                                EmptyWorkingSet(hProcess);
                                CloseHandle(hProcess);
                            }
                        }
                    } while (Process32Next(hSnap, &pe32)); 
                }
                CloseHandle(hSnap);
            }
            
        }
    }
}

void MinimizeToTray(HWND hWnd) {
    Shell_NotifyIcon(NIM_ADD, &nid);
    ShowWindow(hWnd, SW_HIDE);
    if (webviewController != nullptr) {
        webviewController->put_IsVisible(FALSE);    
    }
}

void RestoreFromTray(HWND hWnd) {
    ShowWindow(hWnd, SW_SHOW);
    ShowWindow(hWnd, SW_RESTORE);
    Shell_NotifyIcon(NIM_DELETE, &nid);
    if (webviewController != nullptr) {
        webviewController->put_IsVisible(TRUE);
    }
}