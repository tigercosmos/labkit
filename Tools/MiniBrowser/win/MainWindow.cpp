/*
 * Copyright (C) 2018 Sony Interactive Entertainment Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "stdafx.h"
#include "MainWindow.h"

#include "Common.h"
#include "MiniBrowser.h"
#include "MiniBrowserLibResource.h"

namespace WebCore {
float deviceScaleFactorForWindow(HWND);
}

static constexpr int controlButtonWidth = 24;

static HWND hCacheWnd;
static WNDPROC DefEditProc = nullptr;
static WNDPROC DefButtonProc = nullptr;

static LRESULT CALLBACK EditProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK BackButtonProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK ForwardButtonProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK ReloadButtonProc(HWND, UINT, WPARAM, LPARAM);
static INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);
static INT_PTR CALLBACK CustomUserAgent(HWND, UINT, WPARAM, LPARAM);
static INT_PTR CALLBACK Caches(HWND, UINT, WPARAM, LPARAM);

std::wstring MainWindow::s_windowClass;

static std::wstring loadString(int id)
{
    constexpr size_t length = 100;
    wchar_t buff[length];
    LoadString(hInst, id, buff, length);
    return buff;
}

void MainWindow::registerClass(HINSTANCE hInstance)
{
    static bool initialized = false;
    if (initialized)
        return;
    initialized = true;

    s_windowClass = loadString(IDC_MINIBROWSER);

    WNDCLASSEX wcex;
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MINIBROWSER));
    wcex.hCursor        = LoadCursor(0, IDC_ARROW);
    wcex.hbrBackground  = 0;
    wcex.lpszMenuName   = MAKEINTRESOURCE(IDC_MINIBROWSER);
    wcex.lpszClassName  = s_windowClass.c_str();
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    RegisterClassEx(&wcex);
}

bool MainWindow::init(HINSTANCE hInstance, bool usesLayeredWebView, bool pageLoadTesting)
{
    registerClass(hInstance);

    auto title = loadString(IDS_APP_TITLE);

    m_hMainWnd = CreateWindow(s_windowClass.c_str(), title.c_str(), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, 0, 0, hInstance, 0);

    if (!m_hMainWnd)
        return false;

    float scaleFactor = WebCore::deviceScaleFactorForWindow(nullptr);
    m_hBackButtonWnd = CreateWindow(L"BUTTON", L"<", WS_CHILD | WS_VISIBLE  | BS_TEXT, 0, 0, 0, 0, m_hMainWnd, 0, hInstance, 0);
    m_hForwardButtonWnd = CreateWindow(L"BUTTON", L">", WS_CHILD | WS_VISIBLE | BS_TEXT, scaleFactor * controlButtonWidth, 0, 0, 0, m_hMainWnd, 0, hInstance, 0);
    m_hURLBarWnd = CreateWindow(L"EDIT", 0, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT | ES_AUTOVSCROLL, scaleFactor * controlButtonWidth * 2, 0, 0, 0, m_hMainWnd, 0, hInstance, 0);

    DefEditProc = reinterpret_cast<WNDPROC>(GetWindowLongPtr(m_hURLBarWnd, GWLP_WNDPROC));
    DefButtonProc = reinterpret_cast<WNDPROC>(GetWindowLongPtr(m_hBackButtonWnd, GWLP_WNDPROC));
    SetWindowLongPtr(m_hURLBarWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(EditProc));
    SetWindowLongPtr(m_hBackButtonWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(BackButtonProc));
    SetWindowLongPtr(m_hForwardButtonWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ForwardButtonProc));

    m_browserWindow = std::make_unique<MiniBrowser>(m_hMainWnd, m_hURLBarWnd, usesLayeredWebView, pageLoadTesting);
    if (!m_browserWindow)
        return false;
    HRESULT hr = m_browserWindow->init();
    if (FAILED(hr))
        return false;

    resizeSubViews();
    SetFocus(m_hURLBarWnd);
    return true;
}

void MainWindow::resizeSubViews()
{
    float scaleFactor = WebCore::deviceScaleFactorForWindow(m_hMainWnd);

    RECT rcClient;
    GetClientRect(m_hMainWnd, &rcClient);

    constexpr int urlBarHeight = 24;
    int height = scaleFactor * urlBarHeight;
    int width = scaleFactor * controlButtonWidth;

    MoveWindow(m_hBackButtonWnd, 0, 0, width, height, TRUE);
    MoveWindow(m_hForwardButtonWnd, width, 0, width, height, TRUE);
    MoveWindow(m_hURLBarWnd, width * 2, 0, rcClient.right, height, TRUE);

    ::SendMessage(m_hURLBarWnd, static_cast<UINT>(WM_SETFONT), reinterpret_cast<WPARAM>(m_browserWindow->urlBarFont()), TRUE);

    if (m_browserWindow->usesLayeredWebView() || !m_browserWindow->hwnd())
        return;
    MoveWindow(m_browserWindow->hwnd(), 0, height, rcClient.right, rcClient.bottom - height, TRUE);
}

LRESULT CALLBACK MainWindow::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_COMMAND: {
        int wmId = LOWORD(wParam);
        int wmEvent = HIWORD(wParam);
        if (wmId >= IDM_HISTORY_LINK0 && wmId <= IDM_HISTORY_LINK9) {
            if (gMiniBrowser)
                gMiniBrowser->navigateToHistory(hWnd, wmId);
            break;
        }
        // Parse the menu selections:
        switch (wmId) {
        case IDM_ABOUT:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            break;
        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
        case IDM_PRINT:
            gMiniBrowser->print();
            break;
        case IDM_WEB_INSPECTOR:
            if (gMiniBrowser)
                gMiniBrowser->launchInspector();
            break;
        case IDM_CACHES:
            if (!::IsWindow(hCacheWnd)) {
                hCacheWnd = CreateDialog(hInst, MAKEINTRESOURCE(IDD_CACHES), hWnd, Caches);
                ::ShowWindow(hCacheWnd, SW_SHOW);
            }
            break;
        case IDM_HISTORY_BACKWARD:
        case IDM_HISTORY_FORWARD:
            if (gMiniBrowser)
                gMiniBrowser->navigateForwardOrBackward(hWnd, wmId);
            break;
        case IDM_UA_OTHER:
            if (wmEvent)
                gMainWindow->toggleMenuItem(wmId);
            else
                DialogBox(hInst, MAKEINTRESOURCE(IDD_USER_AGENT), hWnd, CustomUserAgent);
            break;
        case IDM_ACTUAL_SIZE:
            if (gMiniBrowser)
                gMiniBrowser->resetZoom();
            break;
        case IDM_ZOOM_IN:
            if (gMiniBrowser)
                gMiniBrowser->zoomIn();
            break;
        case IDM_ZOOM_OUT:
            if (gMiniBrowser)
                gMiniBrowser->zoomOut();
            break;
        case IDM_SHOW_LAYER_TREE:
            if (gMiniBrowser)
                gMiniBrowser->showLayerTree();
            break;
        default:
            if (!gMainWindow->toggleMenuItem(wmId))
                return DefWindowProc(hWnd, message, wParam, lParam);
        }
        }
        break;
    case WM_DESTROY:
#if USE(CF)
        CFRunLoopStop(CFRunLoopGetMain());
#endif
        PostQuitMessage(0);
        break;
    case WM_SIZE:
        if (!gMiniBrowser || !gMiniBrowser->hasWebView())
            return DefWindowProc(hWnd, message, wParam, lParam);

        gMainWindow->resizeSubViews();
        break;
    case WM_DPICHANGED:
        if (gMiniBrowser)
            gMiniBrowser->updateDeviceScaleFactor();
        return DefWindowProc(hWnd, message, wParam, lParam);
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    return 0;
}

static bool menuItemIsChecked(const MENUITEMINFO& info)
{
    return info.fState & MFS_CHECKED;
}

static void turnOffOtherUserAgents(HMENU menu)
{
    MENUITEMINFO info;
    ::memset(&info, 0x00, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = MIIM_STATE;

    // Must unset the other menu items:
    for (UINT menuToClear = IDM_UA_DEFAULT; menuToClear <= IDM_UA_OTHER; ++menuToClear) {
        if (!::GetMenuItemInfo(menu, menuToClear, FALSE, &info))
            continue;
        if (!menuItemIsChecked(info))
            continue;

        info.fState = MFS_UNCHECKED;
        ::SetMenuItemInfo(menu, menuToClear, FALSE, &info);
    }
}

bool MainWindow::toggleMenuItem(UINT menuID)
{
    HMENU menu = ::GetMenu(hwnd());

    switch (menuID) {
    case IDM_UA_DEFAULT:
    case IDM_UA_SAFARI_8_0:
    case IDM_UA_SAFARI_IOS_8_IPHONE:
    case IDM_UA_SAFARI_IOS_8_IPAD:
    case IDM_UA_IE_11:
    case IDM_UA_CHROME_MAC:
    case IDM_UA_CHROME_WIN:
    case IDM_UA_FIREFOX_MAC:
    case IDM_UA_FIREFOX_WIN:
        m_browserWindow->setUserAgent(menuID);
        turnOffOtherUserAgents(menu);
        break;
    case IDM_UA_OTHER:
        // The actual user agent string will be set by the custom user agent dialog
        turnOffOtherUserAgents(menu);
        break;
    }

    MENUITEMINFO info = { };
    info.cbSize = sizeof(info);
    info.fMask = MIIM_STATE;

    if (!::GetMenuItemInfo(menu, menuID, FALSE, &info))
        return false;

    BOOL newState = !menuItemIsChecked(info);
    info.fState = (newState) ? MFS_CHECKED : MFS_UNCHECKED;
    ::SetMenuItemInfo(menu, menuID, FALSE, &info);

    m_browserWindow->setPreference(menuID, newState);

    return true;
}

LRESULT CALLBACK EditProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CHAR:
        if (wParam == 13) {
            // Enter Key
            wchar_t strPtr[INTERNET_MAX_URL_LENGTH];
            *((LPWORD)strPtr) = INTERNET_MAX_URL_LENGTH;
            int strLen = SendMessage(hDlg, EM_GETLINE, 0, (LPARAM)strPtr);

            strPtr[strLen] = 0;
            _bstr_t bstr(strPtr);
            gMainWindow->loadURL(bstr.GetBSTR());

            return 0;
        }
    default:
        return CallWindowProc(DefEditProc, hDlg, message, wParam, lParam);
    }
}

LRESULT CALLBACK BackButtonProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_LBUTTONUP:
        gMiniBrowser->goBack();
    default:
        return CallWindowProc(DefButtonProc, hDlg, message, wParam, lParam);
    }
}

LRESULT CALLBACK ForwardButtonProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_LBUTTONUP:
        gMiniBrowser->goForward();
    default:
        return CallWindowProc(DefButtonProc, hDlg, message, wParam, lParam);
    }
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message) {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

INT_PTR CALLBACK Caches(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message) {
    case WM_INITDIALOG:
        ::SetTimer(hDlg, IDT_UPDATE_STATS, 1000, nullptr);
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            ::KillTimer(hDlg, IDT_UPDATE_STATS);
            ::DestroyWindow(hDlg);
            hCacheWnd = 0;
            return (INT_PTR)TRUE;
        }
        break;

    case IDT_UPDATE_STATS:
        ::InvalidateRect(hDlg, nullptr, FALSE);
        return (INT_PTR)TRUE;

    case WM_PAINT:
        gMiniBrowser->updateStatistics(hDlg);
        break;
    }

    return (INT_PTR)FALSE;
}

INT_PTR CALLBACK CustomUserAgent(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message) {
    case WM_INITDIALOG: {
        HWND edit = ::GetDlgItem(hDlg, IDC_USER_AGENT_INPUT);
        _bstr_t userAgent;
        if (gMiniBrowser)
            userAgent = gMiniBrowser->userAgent();

        ::SetWindowText(edit, static_cast<LPCTSTR>(userAgent));
        return (INT_PTR)TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            HWND edit = ::GetDlgItem(hDlg, IDC_USER_AGENT_INPUT);

            TCHAR buffer[1024];
            int strLen = ::GetWindowText(edit, buffer, 1024);
            buffer[strLen] = 0;

            _bstr_t bstr(buffer);
            if (bstr.length()) {
                gMiniBrowser->setUserAgent(bstr);
                ::PostMessage(gMainWindow->hwnd(), static_cast<UINT>(WM_COMMAND), MAKELPARAM(IDM_UA_OTHER, 1), 0);
            }
        }

        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            ::EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

static INT_PTR CALLBACK authDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_INITDIALOG: {
        HWND edit = ::GetDlgItem(hDlg, IDC_AUTH_USER);
        ::SetWindowText(edit, static_cast<LPCTSTR>(L""));

        edit = ::GetDlgItem(hDlg, IDC_AUTH_PASSWORD);
        ::SetWindowText(edit, static_cast<LPCTSTR>(L""));
        return (INT_PTR)TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            INT_PTR result { };

            if (LOWORD(wParam) == IDOK) {
                TCHAR user[256];
                int strLen = ::GetWindowText(::GetDlgItem(hDlg, IDC_AUTH_USER), user, 256);
                user[strLen] = 0;

                TCHAR pass[256];
                strLen = ::GetWindowText(::GetDlgItem(hDlg, IDC_AUTH_PASSWORD), pass, 256);
                pass[strLen] = 0;

                result = reinterpret_cast<INT_PTR>(new std::pair<std::wstring, std::wstring>(user, pass));
            }

            ::EndDialog(hDlg, result);
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

HRESULT MainWindow::displayAuthDialog(std::wstring& username, std::wstring& password)
{
    auto result = DialogBox(hInst, MAKEINTRESOURCE(IDD_AUTH), hwnd(), authDialogProc);
    if (!result)
        return E_FAIL;

    auto pair = reinterpret_cast<std::pair<std::wstring, std::wstring>*>(result);
    username = pair->first;
    password = pair->second;
    delete pair;

    return S_OK;
}

void MainWindow::loadURL(BSTR url)
{
    if (FAILED(m_browserWindow->loadURL(url)))
        return;

    SetFocus(m_browserWindow->hwnd());
}
