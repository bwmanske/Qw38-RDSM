#include "aboutdialog.h"

#include <windows.h>
#include <objidl.h>
#include "gdiplus_prelude.h"
#include "build_info.h"
#include "resources/resources.h"

#include <cstring>
#include <string>

using namespace Gdiplus;

namespace {

const wchar_t* kDialogClass = L"RDSMAboutDialog";

// Client area layout (logical pixels).
const int kClientW = 320;
const int kClientH = 330;
const int kImageSize = 128;
const int kImageTop = 16;
const int kButtonW = 88;
const int kButtonH = 28;
const int kMargin = 16;

// The version/timestamp macros are narrow (ASCII) string literals; convert to
// wide for GDI+ text drawing.
std::wstring toWide(const char* s) {
    std::wstring w;
    for (const char* p = s; *p; ++p) {
        w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*p)));
    }
    return w;
}

// Loads the embedded RDSM_128.png resource into a GDI+ Bitmap.
// Returns nullptr on failure. The caller owns the returned Bitmap.
//
// The resource bytes are copied into a fresh GlobalAlloc HGLOBAL before
// CreateStreamOnHGlobal: a resource HGLOBAL from LoadResource is not a standard
// moveable HGLOBAL, so a stream built directly on it does not work with
// Bitmap::FromStream. The stream is created with fDelete=TRUE so it frees the
// copy on release.
Bitmap* loadAboutImage(HINSTANCE hInst) {
    HRSRC hRes = FindResourceW(hInst, MAKEINTRESOURCEW(IDR_ABOUT_PNG), RT_RCDATA);
    if (!hRes) {
        return nullptr;
    }
    HGLOBAL hGlob = LoadResource(hInst, hRes);
    if (!hGlob) {
        return nullptr;
    }
    const void* pData = LockResource(hGlob);
    if (!pData) {
        return nullptr;
    }
    const DWORD dwSize = SizeofResource(hInst, hRes);
    if (dwSize == 0) {
        return nullptr;
    }
    HGLOBAL hStreamData = GlobalAlloc(GMEM_MOVEABLE, dwSize);
    if (!hStreamData) {
        return nullptr;
    }
    void* pWrite = GlobalLock(hStreamData);
    if (!pWrite) {
        GlobalFree(hStreamData);
        return nullptr;
    }
    memcpy(pWrite, pData, dwSize);
    GlobalUnlock(hStreamData);

    IStream* pStream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(hStreamData, TRUE, &pStream))) {
        GlobalFree(hStreamData);
        return nullptr;
    }
    Bitmap* bmp = Bitmap::FromStream(pStream, FALSE);
    pStream->Release(); // frees hStreamData (fDelete was TRUE)
    if (!bmp || bmp->GetLastStatus() != Ok) {
        delete bmp;
        return nullptr;
    }
    return bmp;
}

} // namespace

void AboutDialog::show(HWND parent) {
    AboutDialog dlg;
    dlg.hInst_ = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &AboutDialog::WndProc;
    wc.hInstance = dlg.hInst_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kDialogClass;
    RegisterClassExW(&wc);

    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    RECT rc{0, 0, kClientW, kClientH};
    AdjustWindowRectEx(&rc, style, FALSE, WS_EX_CONTROLPARENT);

    dlg.hwnd_ = CreateWindowExW(
        WS_EX_CONTROLPARENT, kDialogClass, L"About RDSM",
        style | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
        parent, nullptr, dlg.hInst_, &dlg);
    if (!dlg.hwnd_) {
        return;
    }

    EnableWindow(parent, FALSE);
    SetForegroundWindow(dlg.hwnd_);

    MSG msg{};
    while (dlg.hwnd_ && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dlg.hwnd_, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
}

LRESULT CALLBACK AboutDialog::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AboutDialog* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<AboutDialog*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<AboutDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) {
        return self->handleMsg(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT AboutDialog::handleMsg(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            onCreate(hwnd);
            return 0;
        case WM_PAINT:
            onPaint(hwnd);
            return 0;
        case WM_COMMAND:
            onCommand(wParam);
            return 0;
        case WM_DESTROY:
            delete image_;
            image_ = nullptr;
            hwnd_ = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void AboutDialog::onCreate(HWND hwnd) {
    hwnd_ = hwnd;
    image_ = loadAboutImage(hInst_);

    HWND btnOk = CreateWindowExW(
        0, L"BUTTON", L"OK",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        0, 0, kButtonW, kButtonH,
        hwnd_, reinterpret_cast<HMENU>(IDOK), hInst_, nullptr);

    RECT rc{};
    GetClientRect(hwnd, &rc);
    MoveWindow(btnOk, (rc.right - kButtonW) / 2, rc.bottom - kMargin - kButtonH,
               kButtonW, kButtonH, TRUE);
}

void AboutDialog::onPaint(HWND hwnd) {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int w = rc.right;

    Graphics g(hdc);

    // App image, top center.
    if (image_) {
        g.DrawImage(image_,
                    static_cast<REAL>((w - kImageSize) / 2),
                    static_cast<REAL>(kImageTop),
                    static_cast<REAL>(kImageSize),
                    static_cast<REAL>(kImageSize));
    }

    SolidBrush textBrush(Color(0, 0, 0));
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);

    Font titleFont(L"Segoe UI", 13.0f, FontStyleBold, UnitPoint);
    RectF titleRect(0.0f, 152.0f, static_cast<REAL>(w), 28.0f);
    g.DrawString(L"Remote Disk Space Monitor", -1, &titleFont, titleRect, &fmt, &textBrush);

    Font infoFont(L"Segoe UI", 9.5f, FontStyleRegular, UnitPoint);
    std::wstring versionText = L"Version " + toWide(RDSM_VERSION);
    RectF versionRect(0.0f, 184.0f, static_cast<REAL>(w), 20.0f);
    g.DrawString(versionText.c_str(), -1, &infoFont, versionRect, &fmt, &textBrush);

    std::wstring builtText = L"Built: " + toWide(RDSM_BUILD_TIMESTAMP);
    RectF builtRect(0.0f, 206.0f, static_cast<REAL>(w), 20.0f);
    g.DrawString(builtText.c_str(), -1, &infoFont, builtRect, &fmt, &textBrush);

    // Short program description, below "Built" and above the OK button.
    const wchar_t* kDescription =
        L"Queries free space on a list of UNC network shares and Windows drive letters.";
    RectF descRect(0.0f, 240.0f, static_cast<REAL>(w), 32.0f);
    g.DrawString(kDescription, -1, &infoFont, descRect, &fmt, &textBrush);

    EndPaint(hwnd, &ps);
}

void AboutDialog::onCommand(WPARAM wParam) {
    if (LOWORD(wParam) == IDOK) {
        DestroyWindow(hwnd_);
    }
}
