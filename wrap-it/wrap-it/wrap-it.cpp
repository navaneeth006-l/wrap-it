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
#include <algorithm> // Required for string cleaning

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "urlmon.lib")
using namespace Microsoft::WRL;

#define WM_TRAYICON (WM_USER + 1)
#define TRAY_ICON_ID 1
#define ID_TRAY_EXIT 2001
#define ID_TRAY_UPDATE 2002
#define ID_TRAY_SETTINGS 2003
#define WM_WAKEUP (WM_USER + 10)
#define WM_REBOOT (WM_USER + 11)
#define WM_UPDATE_NOTIFY (WM_USER + 12)
#define WM_UPDATE_MANUAL (WM_USER + 13)
#define WM_UPDATE_LATEST (WM_USER + 14)

HINSTANCE hInst;
HWND mainWindow;
ComPtr<ICoreWebView2Controller> webviewController;
ComPtr<ICoreWebView2> webview;

HANDLE hMutexGlobal = NULL;
std::wstring exePathGlobal;

std::wstring windowTitleGlobal;
std::wstring targetUrlGlobal;
bool enableDarkModeGlobal = false;
bool aggressiveMemorySave = false;
bool quitOnClose = false;
bool openLinksInBrowserGlobal = false;
bool autoUpdateGlobal = true;

NOTIFYICONDATA nid = {};
const double CURRENT_VERSION = 3.3;

std::wstring GITHUB_VERSION_URL = L"https://raw.githubusercontent.com/navaneeth006-l/wrap-it/refs/heads/main/wrap-it/version.txt";
std::wstring GITHUB_EXE_URL = L"https://github.com/navaneeth006-l/wrap-it/releases/latest/download/wrap-it.exe";

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void MinimizeToTray(HWND hWnd);
void RestoreFromTray(HWND hWnd);
void TrimMemory();
void RunAutoUpdater(std::wstring basePath, std::wstring exePathRaw, bool isManualCheck);

std::wstring GetEmbeddedHTML(int resourceID) {
    HRSRC hRes = FindResource(hInst, MAKEINTRESOURCE(resourceID), RT_HTML);
    if (!hRes) hRes = FindResource(hInst, MAKEINTRESOURCE(resourceID), L"HTML");
    if (!hRes) return L"<html><body><h1>Error: UI Resource Missing</h1></body></html>";

    HGLOBAL hData = LoadResource(hInst, hRes);
    DWORD size = SizeofResource(hInst, hRes);
    const char* data = (const char*)LockResource(hData);

    int wSize = MultiByteToWideChar(CP_UTF8, 0, data, size, NULL, 0);
    std::wstring html(wSize, 0);
    MultiByteToWideChar(CP_UTF8, 0, data, size, &html[0], wSize);

    std::wstring jsonPayload = L"{\"name\":\"" + windowTitleGlobal +
        L"\",\"link\":\"" + targetUrlGlobal +
        L"\",\"darkmode\":" + (enableDarkModeGlobal ? L"true" : L"false") +
        L",\"aggressivememory\":" + (aggressiveMemorySave ? L"true" : L"false") +
        L",\"quitonclose\":" + (quitOnClose ? L"true" : L"false") +
        L",\"openlinksinbrowser\":" + (openLinksInBrowserGlobal ? L"true" : L"false") +
        L",\"autoupdate\":" + (autoUpdateGlobal ? L"true" : L"false") + L"}";

    size_t pos = html.find(L"[[JSON_PAYLOAD]]");
    if (pos != std::wstring::npos) {
        html.replace(pos, 16, jsonPayload);
    }

    return html;
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    hInst = hInstance;
    const wchar_t CLASS_NAME[] = L"AnyWrapClass";

    wchar_t exePathRaw[MAX_PATH];
    GetModuleFileNameW(NULL, exePathRaw, MAX_PATH);
    exePathGlobal = exePathRaw;
    std::wstring basePath(exePathRaw);

    size_t lastSlash = basePath.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        basePath = basePath.substr(0, lastSlash + 1);
    }

    std::wstring oldExePath = std::wstring(exePathRaw) + L".old";
    DeleteFileW(oldExePath.c_str());

    std::wstring userDataFolder = basePath + L"ProfileData";
    std::wstring jsonPath = userDataFolder + L"\\settings.json";

    std::string windowTitleStr = "Wrap-It Setup";
    std::string targetUrlStr = "";

    std::wstring configPath = basePath + L"config.txt";
    if (GetFileAttributesW(configPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        std::ifstream configFile(configPath);
        if (configFile.is_open()) {
            std::string line;
            std::string migName = "New App", migLink = "", migDark = "false", migMem = "false", migQuit = "false", migOpen = "false", migAuto = "true";

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
                    trim(key); trim(value);
                    for (auto& c : key) c = tolower(c);

                    if (key == "name") migName = value;
                    else if (key == "link") migLink = value;
                    else {
                        std::string lowerVal = value;
                        for (auto& c : lowerVal) c = tolower(c);
                        if (key == "darkmode") migDark = lowerVal;
                        else if (key == "aggressivememory") migMem = lowerVal;
                        else if (key == "quitonclose") migQuit = lowerVal;
                        else if (key == "openlinksinbrowser") migOpen = lowerVal;
                        else if (key == "autoupdate") migAuto = lowerVal;
                    }
                }
            }
            configFile.close();

            CreateDirectoryW(userDataFolder.c_str(), NULL);
            std::ofstream jsonOut(jsonPath);
            jsonOut << "{\n  \"name\":\"" << migName << "\",\n  \"link\":\"" << migLink << "\",\n  \"darkmode\":" << migDark << ",\n  \"aggressivememory\":" << migMem << ",\n  \"quitonclose\":" << migQuit << ",\n  \"openlinksinbrowser\":" << migOpen << ",\n  \"autoupdate\":" << migAuto << "\n}";
            jsonOut.close();

            DeleteFileW(configPath.c_str());
        }
    }

    std::ifstream jsonFile(jsonPath);
    if (jsonFile.is_open()) {
        std::string jsonContent((std::istreambuf_iterator<char>(jsonFile)), std::istreambuf_iterator<char>());
        jsonFile.close();

        auto extractStr = [&](std::string key) {
            size_t pos = jsonContent.find("\"" + key + "\":\"");
            if (pos != std::string::npos) {
                pos += key.length() + 4;
                size_t end = jsonContent.find("\"", pos);
                if (end != std::string::npos) {
                    return jsonContent.substr(pos, end - pos);
                }
            }
            return std::string("");
            };
        auto extractBool = [&](std::string key) {
            return jsonContent.find("\"" + key + "\":true") != std::string::npos;
            };

        std::string parsedName = extractStr("name");
        if (!parsedName.empty()) windowTitleStr = parsedName;

        targetUrlStr = extractStr("link");
        enableDarkModeGlobal = extractBool("darkmode");
        aggressiveMemorySave = extractBool("aggressivememory");
        quitOnClose = extractBool("quitonclose");
        openLinksInBrowserGlobal = extractBool("openlinksinbrowser");
        autoUpdateGlobal = extractBool("autoupdate");
    }

    std::wstring windowTitle(windowTitleStr.begin(), windowTitleStr.end());
    std::wstring targetUrl(targetUrlStr.begin(), targetUrlStr.end());

    windowTitleGlobal = windowTitle;
    targetUrlGlobal = targetUrl;

    std::wstring mutexName = L"WrapIt_Mutex_" + windowTitle;
    hMutexGlobal = CreateMutexW(NULL, TRUE, mutexName.c_str());

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

    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIcon(NIM_ADD, &nid);
    Shell_NotifyIcon(NIM_SETVERSION, &nid);

    std::thread updaterThread(RunAutoUpdater, basePath, std::wstring(exePathRaw), false);
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

    CreateCoreWebView2EnvironmentWithOptions(nullptr, userDataFolder.c_str(), options.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [hWnd = mainWindow, userDataFolder](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {

                env->CreateCoreWebView2Controller(hWnd, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [hWnd, userDataFolder](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
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

                            // 👇 FIX 1: RESTORED THE SILENT UNREAD BADGE (With the NIF_MESSAGE fix!)
                            webview->add_DocumentTitleChanged(
                                Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
                                    [](ICoreWebView2* sender, IUnknown* args) -> HRESULT {
                                        LPWSTR title;
                                        sender->get_DocumentTitle(&title);
                                        if (title != nullptr) {
                                            std::wstring wTitle(title);

                                            int currentCount = 0;
                                            size_t start = wTitle.find(L"(");
                                            size_t end = wTitle.find(L")");

                                            if (start != std::wstring::npos && end != std::wstring::npos && end > start + 1) {
                                                try {
                                                    currentCount = std::stoi(wTitle.substr(start + 1, end - start - 1));
                                                }
                                                catch (...) {}
                                            }

                                            std::wstring tooltipText = windowTitleGlobal;
                                            if (currentCount > 0) {
                                                tooltipText += L" (" + std::to_wstring(currentCount) + L" Unread)";
                                            }

                                            wcscpy_s(nid.szTip, ARRAYSIZE(nid.szTip), tooltipText.substr(0, 127).c_str());

                                            // KEEP NIF_MESSAGE SO THE TRAY MENU NEVER BREAKS
                                            nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
                                            nid.szInfo[0] = L'\0'; // Clear old popups
                                            nid.szInfoTitle[0] = L'\0';
                                            Shell_NotifyIcon(NIM_MODIFY, &nid);

                                            CoTaskMemFree(title);
                                        }
                                        return S_OK;
                                    }
                                ).Get(), nullptr
                            );

                            webview->add_PermissionRequested(
                                Callback<ICoreWebView2PermissionRequestedEventHandler>(
                                    [](ICoreWebView2* sender, ICoreWebView2PermissionRequestedEventArgs* args) -> HRESULT {
                                        COREWEBVIEW2_PERMISSION_KIND kind;
                                        args->get_PermissionKind(&kind);
                                        if (kind == COREWEBVIEW2_PERMISSION_KIND_NOTIFICATIONS ||
                                            kind == COREWEBVIEW2_PERMISSION_KIND_MICROPHONE ||
                                            kind == COREWEBVIEW2_PERMISSION_KIND_CAMERA) {
                                            args->put_State(COREWEBVIEW2_PERMISSION_STATE_ALLOW);
                                        }
                                        return S_OK;
                                    }
                                ).Get(), nullptr
                            );

                            // 👇 FIX 2: BULLETPROOF JSON PARSING
                            webview->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [userDataFolder](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        LPWSTR message;
                                        args->TryGetWebMessageAsString(&message);

                                        if (message != nullptr) {
                                            std::wstring wsMessage(message);

                                            // IS THIS A HIJACKED NOTIFICATION?
                                            if (wsMessage.find(L"NativeToast") != std::wstring::npos) {

                                                auto extract = [&](std::wstring key) {
                                                    size_t keyPos = wsMessage.find(key);
                                                    if (keyPos != std::wstring::npos) {
                                                        size_t colonPos = wsMessage.find(L":", keyPos);
                                                        if (colonPos != std::wstring::npos) {
                                                            size_t valStart = wsMessage.find_first_not_of(L" \":\\", colonPos + 1);
                                                            if (valStart != std::wstring::npos) {
                                                                size_t valEnd = wsMessage.find_first_of(L"\"\\", valStart);
                                                                if (valEnd != std::wstring::npos) {
                                                                    return wsMessage.substr(valStart, valEnd - valStart);
                                                                }
                                                            }
                                                        }
                                                    }
                                                    return std::wstring(L"");
                                                    };

                                                std::wstring nTitle = extract(L"title");
                                                std::wstring nBody = extract(L"body");

                                                nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_INFO;
                                                wcscpy_s(nid.szInfoTitle, ARRAYSIZE(nid.szInfoTitle), nTitle.substr(0, 63).c_str());
                                                wcscpy_s(nid.szInfo, ARRAYSIZE(nid.szInfo), nBody.substr(0, 255).c_str());
                                                nid.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
                                                Shell_NotifyIcon(NIM_MODIFY, &nid);

                                                FLASHWINFO fi;
                                                fi.cbSize = sizeof(FLASHWINFO);
                                                fi.hwnd = mainWindow;
                                                fi.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG;
                                                fi.uCount = 3;
                                                fi.dwTimeout = 0;
                                                FlashWindowEx(&fi);
                                            }
                                            // IS THIS THE SETTINGS MENU SAVING?
                                            else if (wsMessage.find(L"link") != std::wstring::npos) {
                                                CreateDirectoryW(userDataFolder.c_str(), NULL);
                                                std::wstring jsonPath = userDataFolder + L"\\settings.json";
                                                std::wofstream outFile(jsonPath);
                                                outFile << message;
                                                outFile.close();

                                                PostMessage(mainWindow, WM_REBOOT, 0, 0);
                                            }
                                            CoTaskMemFree(message);
                                        }
                                        return S_OK;
                                    }
                                ).Get(), nullptr
                            );

                            if (enableDarkModeGlobal) {
                                std::wstring js_inject = L"document.addEventListener('DOMContentLoaded', function() {"
                                    L"  var css = 'html { filter: invert(100%) hue-rotate(180deg); } img, video { filter: invert(100%) hue-rotate(180deg); }';"
                                    L"  var style = document.createElement('style');"
                                    L"  document.head.appendChild(style);"
                                    L"  style.type = 'text/css';"
                                    L"  style.appendChild(document.createTextNode(css));"
                                    L"});";
                                webview->AddScriptToExecuteOnDocumentCreated(js_inject.c_str(), nullptr);
                            }

                            // 👇 FIX 3: ADDED SERVICE WORKER SUPPORT FOR WHATSAPP/DISCORD
                            // 👇 FIX 3: ADDED JSON.STRINGIFY BACK SO C++ CAN ACTUALLY READ IT
                            std::wstring js_notification_hijack = LR"(
                                const OriginalNotification = window.Notification;
                                function CustomNotification(title, options) {
                                    const safeTitle = title ? title.replace(/["\\]/g, '') : 'New Message';
                                    const safeBody = (options && options.body) ? options.body.replace(/["\\]/g, '') : '';
                                    
                                    // Must be JSON.stringify or C++ will silently reject it!
                                    window.chrome.webview.postMessage(JSON.stringify({ type: 'NativeToast', title: safeTitle, body: safeBody }));
                                    
                                    return new OriginalNotification(title, options);
                                }
                                CustomNotification.permission = 'granted';
                                CustomNotification.requestPermission = () => Promise.resolve('granted');
                                window.Notification = CustomNotification;

                                if (window.ServiceWorkerRegistration) {
                                    const origShow = window.ServiceWorkerRegistration.prototype.showNotification;
                                    window.ServiceWorkerRegistration.prototype.showNotification = function(title, options) {
                                        const safeTitle = title ? title.replace(/["\\]/g, '') : 'New Message';
                                        const safeBody = (options && options.body) ? options.body.replace(/["\\]/g, '') : '';
                                        
                                        // Must be JSON.stringify here too!
                                        window.chrome.webview.postMessage(JSON.stringify({ type: 'NativeToast', title: safeTitle, body: safeBody }));
                                        
                                        return origShow.call(this, title, options);
                                    };
                                }
                            )";
                            webview->AddScriptToExecuteOnDocumentCreated(js_notification_hijack.c_str(), nullptr);
                            webview->AddScriptToExecuteOnDocumentCreated(js_notification_hijack.c_str(), nullptr);

                            if (targetUrlGlobal.empty()) {
                                std::wstring embeddedHtml = GetEmbeddedHTML(IDR_HTML6);
                                webview->NavigateToString(embeddedHtml.c_str());
                            }
                            else {
                                webview->Navigate(targetUrlGlobal.c_str());
                            }
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

void RunAutoUpdater(std::wstring basePath, std::wstring exePathRaw, bool isManualCheck) {
    std::wstring tempVersionFile = basePath + L"temp_version.txt";
    std::wstring noCacheVersionUrl = GITHUB_VERSION_URL + L"?t=" + std::to_wstring(GetTickCount());

    if (URLDownloadToFileW(NULL, noCacheVersionUrl.c_str(), tempVersionFile.c_str(), 0, NULL) == S_OK) {
        std::ifstream vFile(tempVersionFile);
        std::string vStr;

        if (std::getline(vFile, vStr)) {
            try {
                double onlineVersion = std::stod(vStr);
                if (onlineVersion > CURRENT_VERSION) {
                    if (autoUpdateGlobal) {
                        PostMessage(mainWindow, WM_UPDATE_NOTIFY, 0, 0);
                        std::wstring newExePath = basePath + L"update.exe";
                        std::wstring noCacheExeUrl = GITHUB_EXE_URL + L"?t=" + std::to_wstring(GetTickCount());

                        if (URLDownloadToFileW(NULL, noCacheExeUrl.c_str(), newExePath.c_str(), 0, NULL) == S_OK) {
                            std::wstring oldExePath = std::wstring(exePathRaw) + L".old";
                            if (_wrename(exePathRaw.c_str(), oldExePath.c_str()) == 0) {
                                if (_wrename(newExePath.c_str(), exePathRaw.c_str()) == 0) {
                                    PostMessage(mainWindow, WM_REBOOT, 0, 0);
                                }
                            }
                        }
                    }
                    else {
                        PostMessage(mainWindow, WM_UPDATE_MANUAL, 0, 0);
                    }
                }
                else if (isManualCheck) {
                    PostMessage(mainWindow, WM_UPDATE_LATEST, 0, 0);
                }
            }
            catch (...) {}
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
    case WM_REBOOT:
        if (hMutexGlobal) {
            ReleaseMutex(hMutexGlobal);
            CloseHandle(hMutexGlobal);
        }
        ShellExecuteW(NULL, L"open", exePathGlobal.c_str(), NULL, NULL, SW_SHOWNORMAL);
        Shell_NotifyIcon(NIM_DELETE, &nid);
        PostQuitMessage(0);
        break;
    case WM_UPDATE_NOTIFY:
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_INFO;
        wcscpy_s(nid.szInfoTitle, ARRAYSIZE(nid.szInfoTitle), L"Update Found");
        {
            std::wstring infoText = windowTitleGlobal + L" is downloading a new update in the background. It will restart automatically!";
            wcscpy_s(nid.szInfo, ARRAYSIZE(nid.szInfo), infoText.c_str());
        }
        nid.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
        Shell_NotifyIcon(NIM_MODIFY, &nid);
        break;
    case WM_UPDATE_MANUAL:
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_INFO;
        wcscpy_s(nid.szInfoTitle, ARRAYSIZE(nid.szInfoTitle), L"New Version Available");
        {
            std::wstring infoText = windowTitleGlobal + L" has an update! Check GitHub to download the latest version.";
            wcscpy_s(nid.szInfo, ARRAYSIZE(nid.szInfo), infoText.c_str());
        }
        nid.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
        Shell_NotifyIcon(NIM_MODIFY, &nid);
        break;
    case WM_UPDATE_LATEST:
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_INFO;
        wcscpy_s(nid.szInfoTitle, ARRAYSIZE(nid.szInfoTitle), L"Up to Date");
        {
            std::wstring infoText = windowTitleGlobal + L" is already running the latest version.";
            wcscpy_s(nid.szInfo, ARRAYSIZE(nid.szInfo), infoText.c_str());
        }
        nid.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
        Shell_NotifyIcon(NIM_MODIFY, &nid);
        break;
    case WM_TRAYICON:
        if (LOWORD(lParam) == WM_LBUTTONDBLCLK) {
            RestoreFromTray(hWnd);
        }
        else if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();

            InsertMenu(hMenu, 0, MF_BYPOSITION | MF_STRING, ID_TRAY_SETTINGS, L"Settings & Configuration");
            InsertMenu(hMenu, 1, MF_BYPOSITION | MF_STRING, ID_TRAY_UPDATE, L"Check for Updates (GitHub)");

            std::wstring quitText = L"Quit " + windowTitleGlobal;
            InsertMenu(hMenu, 2, MF_BYPOSITION | MF_STRING, ID_TRAY_EXIT, quitText.c_str());

            SetForegroundWindow(hWnd);
            TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hWnd, NULL);

            PostMessage(hWnd, WM_NULL, 0, 0);
            DestroyMenu(hMenu);
        }
        else if (LOWORD(lParam) == NIN_BALLOONUSERCLICK) {
            autoUpdateGlobal = true;
            std::wstring basePath = exePathGlobal.substr(0, exePathGlobal.find_last_of(L"\\/") + 1);
            std::thread manualUpdater(RunAutoUpdater, basePath, exePathGlobal, true);
            manualUpdater.detach();
        }
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == ID_TRAY_EXIT) {
            DestroyWindow(hWnd);
        }
        else if (LOWORD(wParam) == ID_TRAY_SETTINGS) {
            if (webview != nullptr) {
                std::wstring embeddedHtml = GetEmbeddedHTML(IDR_HTML6);
                webview->NavigateToString(embeddedHtml.c_str());
                RestoreFromTray(hWnd);
            }
        }
        else if (LOWORD(wParam) == ID_TRAY_UPDATE) {
            autoUpdateGlobal = true;
            std::wstring basePath = exePathGlobal.substr(0, exePathGlobal.find_last_of(L"\\/") + 1);
            std::thread manualUpdater(RunAutoUpdater, basePath, exePathGlobal, true);
            manualUpdater.detach();
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
        DestroyWindow(hWnd);
        break;
    case WM_DESTROY:
        if (webviewController != nullptr) {
            webviewController->Close();
            webviewController = nullptr;
            webview = nullptr;
        }
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
    ShowWindow(hWnd, SW_HIDE);
    if (webviewController != nullptr) {
        webviewController->put_IsVisible(FALSE);
    }
}

void RestoreFromTray(HWND hWnd) {
    ShowWindow(hWnd, SW_SHOW);
    ShowWindow(hWnd, SW_RESTORE);

    if (webviewController != nullptr) {
        webviewController->put_IsVisible(TRUE);
    }
}