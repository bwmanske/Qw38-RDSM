#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include "gdiplus_prelude.h"

#include "app.h"
#include "shares.h"

#include <string>

using namespace Gdiplus;

namespace {

// Parses the command line for "--JSON <FileName>". If found, sets the custom
// config file name (placed in the default config directory) and returns true.
// Returns false if the option is not present.
bool parseCommandLine(PWSTR cmdLine) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(cmdLine, &argc);
    if (!argv) {
        return false;
    }
    bool found = false;
    for (int i = 0; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"--JSON") == 0 && i + 1 < argc) {
            rdsmon::setConfigFileName(argv[i + 1]);
            found = true;
            break;
        }
    }
    LocalFree(argv);
    return found;
}

} // namespace

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int) {
    parseCommandLine(pCmdLine);

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    GdiplusStartupInput gdiInput;
    ULONG_PTR gdiToken = 0;
    GdiplusStartup(&gdiToken, &gdiInput, nullptr);

    int result = 0;
    App* app = App::instance();
    if (app->init(hInstance)) {
        result = app->run();
    }

    GdiplusShutdown(gdiToken);
    return result;
}
