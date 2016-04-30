#include "Utils.hpp"

#include <cstring>
#include <vector>

static const char windowClassName[] = "SVBRDFOculusWindow";

static LRESULT CALLBACK windowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CLOSE:
        PostQuitMessage(0);
        break;
    default:
        break;
    }

    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

static ATOM registerWindowClass()
{
    WNDCLASSEX c;
    zero(c);
    c.cbSize        = sizeof(c);
    c.cbWndExtra    = 2 * sizeof(uintptr_t);
    c.style  		= CS_HREDRAW | CS_VREDRAW;
    c.lpfnWndProc   = &windowProc;
    c.hInstance     = GetModuleHandle(nullptr);
    c.lpszClassName = windowClassName;
    return RegisterClassEx(&c);
}

Window::Window(const char *title, int w, int h, int x, int y)
{
    static ATOM windowClass = registerWindowClass();
    
    x = (x < 0) ? CW_USEDEFAULT : x;
    y = (y < 0) ? CW_USEDEFAULT : y;

    RECT rect;
    rect.left   = 0;
    rect.top    = 0;
    rect.right  = w;
    rect.bottom = h;

    DWORD style = WS_SYSMENU | WS_OVERLAPPEDWINDOW | WS_VISIBLE;
    AdjustWindowRectEx(&rect, style, FALSE, 0);

    w = rect.right - rect.left;
    h = rect.bottom - rect.top;

    hWnd = CreateWindowExA(
        0,
        windowClassName,
        title,
        style,
        x, y,
        w, h,
        nullptr,
        nullptr,
        GetModuleHandle(nullptr),
        nullptr);

    checkLastError();

    SetWindowLongPtrA(hWnd, 0, reinterpret_cast<LONG_PTR>(this));
}

Window::~Window()
{
    DestroyWindow(hWnd);
}

void Window::run(std::function<bool(Window &)> idle)
{
    bool continueRunning = true;

    while (continueRunning)
    {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE) != 0)
        {
            if (msg.message == WM_QUIT)
                continueRunning = false;

            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        if (idle)
            continueRunning = continueRunning && idle(*this);
    }
}

bool detail::checkImpl(bool cond, const char * fmt, ...)
{
    if (!cond)
    {
        log("ERROR: ");

        if (fmt)
        {
            va_list ap;
            va_start(ap, fmt);
            vlog(fmt, ap);
            va_end(ap);
        }
        else
        {
            log("Unknown error");
        }

        log("\n");

        return false;
    }
    else
    {
        return true;
    }
}

bool detail::checkHRImpl(HRESULT hr)
{
    if (!SUCCEEDED(hr))
    {
        _com_error err(hr);
        return checkImpl(false, "%s", err.ErrorMessage());
    }
    else
    {
        return true;
    }
}

bool detail::checkLastErrorImpl()
{
    return checkHRImpl(HRESULT_FROM_WIN32(GetLastError()));
}

void vlog(const char * fmt, va_list ap)
{
    if (IsDebuggerPresent())
    {
        char msg[1024];
        vsprintf_s(msg, fmt, ap);
        OutputDebugStringA(msg);
    }
    else
    {
        vprintf_s(fmt, ap);
    }
}

void log(const char * fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vlog(fmt, ap);
    va_end(ap);
}

static bool &keyStatus(int virtualKeyCode)
{
    static bool status[256] = { false };
    return status[virtualKeyCode % 256];
}

static HWND keyboardHwnd = nullptr;

void keyboardWindow(HWND hwnd)
{
    keyboardHwnd = hwnd;
}

bool keyHeld(int virtualKeyCode)
{
    bool held;

    if (keyboardHwnd && GetFocus() != keyboardHwnd)
        held = false;
    else
        held = (GetAsyncKeyState(virtualKeyCode) & 0x80000000) != 0;

    keyStatus(virtualKeyCode) = held;
    return held;
}

bool keyPressed(int virtualKeyCode)
{
    bool wasHeld = keyStatus(virtualKeyCode);
    bool isHeld  = keyHeld(virtualKeyCode);
    return !wasHeld && isHeld;
}

std::wstring convertToWide(const char * str)
{
    auto len = strlen(str);
    std::vector<wchar_t> wideBuf(len + 1);
    size_t converted;
    auto err = mbstowcs_s(&converted, wideBuf.data(), wideBuf.size(), str, len);
    check(err == 0, "Failed to convert string");
    return std::wstring(wideBuf.data());
}

std::vector<std::string> listFiles(const std::string &path, const std::string &pattern)
{
    std::vector<std::string> files;
    WIN32_FIND_DATAA findData;
    zero(findData);

    auto findPat = path + "/" + pattern;

    auto hnd = FindFirstFileA(findPat.c_str(), &findData);

    bool haveFiles = hnd != INVALID_HANDLE_VALUE;
    while (haveFiles)
    {
        files.emplace_back(findData.cFileName);
        haveFiles = !!FindNextFileA(hnd, &findData);
    }

    FindClose(hnd);

    return files;
}

std::vector<std::string> searchFiles(const std::string &path, const std::string &pattern)
{
    std::vector<std::string> files;

    auto matches = listFiles(path, pattern);
    auto prefix  = path + "/";

    auto addFiles = [&](const std::vector<std::string> &fs, const std::string &prefix = std::string())
    {
        for (auto &f : fs)
        {
            files.emplace_back(prefix + f);
        }
    };

    addFiles(listFiles(path, pattern), prefix);

    auto allFilesInDir = listFiles(path);
    for (auto &f : allFilesInDir)
    {
        if ((GetFileAttributesA(f.c_str()) & FILE_ATTRIBUTE_DIRECTORY) &&
            (f.find(".", 0) == std::string::npos))
        {
            addFiles(searchFiles(prefix + f, pattern));
        }
    }

    return files;
}

std::string replaceAll(std::string s, const std::string &replacedString, const std::string &replaceWith)
{
    auto oldLen = replacedString.length();
    auto newLen = replaceWith.length();

    auto pos = s.find(replacedString, 0);

    while (pos != std::string::npos)
    {
        s.replace(pos, oldLen, replaceWith); 
        pos += newLen;
        pos = s.find(replacedString, pos);
    }

    return s;
}

std::vector<std::string> tokenize(const std::string &s, const std::string &delimiters)
{
    std::vector<std::string> tokens;

    std::string::size_type pos = 0;
    bool delim = false;

    while (pos != std::string::npos)
    {
        if (delim)
        {
            pos = s.find_first_not_of(delimiters, pos);
        }
        else
        {
            auto end = s.find_first_of(delimiters, pos);
            if (end == std::string::npos) end = s.length();
            tokens.emplace_back(s.substr(pos, end - pos));
            if (tokens.back().empty())
                tokens.pop_back();
            pos = end;
        }

        delim = !delim;
    }

    return tokens;
}

std::vector<std::string> splitPath(const std::string & path)
{
    auto canonical = replaceAll(path, "\\", "/");
    auto pathParts = tokenize(canonical, "/");
    return pathParts;
}

std::string fileOpenDialog(const std::string &description, const std::string &pattern)
{
    auto filter = description + '\0' + pattern + '\0' + '\0';

    char fileName[MAX_PATH + 2];
    zero(fileName);

    OPENFILENAME fn;
    zero(fn);
    fn.lStructSize     = sizeof(fn);
    fn.lpstrFilter     = filter.c_str();
    fn.nFilterIndex    = 1;
    fn.lpstrFile       = fileName;
    fn.nMaxFile        = sizeof(fileName) - 1;
    fn.lpstrInitialDir = ".";
    fn.Flags |= OFN_NOCHANGEDIR;

    if (!GetOpenFileNameA(&fn))
        return std::string();
    else
        return std::string(fn.lpstrFile);
}

std::string fileSaveDialog(const std::string & description, const std::string & pattern)
{
    auto filter = description + '\0' + pattern + '\0' + '\0';

    char fileName[MAX_PATH + 2];
    zero(fileName);

    OPENFILENAME fn;
    zero(fn);
    fn.lStructSize     = sizeof(fn);
    fn.lpstrFilter     = filter.c_str();
    fn.nFilterIndex    = 1;
    fn.lpstrFile       = fileName;
    fn.nMaxFile        = sizeof(fileName) - 1;
    fn.lpstrInitialDir = ".";
    fn.Flags |= OFN_NOCHANGEDIR;

    if (!GetSaveFileNameA(&fn))
        return std::string();
    else
        return std::string(fn.lpstrFile);
}

std::string absolutePath(const std::string & path)
{
    char absPath[MAX_PATH + 2] = { 0 };
    if (!GetFullPathNameA(path.c_str(), sizeof(absPath), absPath, nullptr))
        return std::string();
    else
        return std::string(absPath);
}

Timer::Timer()
{
    LARGE_INTEGER f;
    LARGE_INTEGER now;

    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&now);

    period = 1.0 / static_cast<double>(f.QuadPart);
    start  = now.QuadPart;
}

double Timer::seconds() const
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    uint64_t ticks = now.QuadPart - start;
    return static_cast<double>(ticks) * period;
}

void FontRasterizer::ensureBitmap(int w, int h)
{
    if (bitmapW >= w && bitmapH >= h)
        return;

    if (bitmap)
        DeleteObject(bitmap);

    bitmap = CreateCompatibleBitmap(GetDC(nullptr), w, h);
    check(bitmap != nullptr, "Could not get bitmap");
    bitmapW = w;
    bitmapH = h;

    SelectObject(memoryDC, bitmap);
}

FontRasterizer::FontRasterizer(const std::vector<std::string> &fontNamesInPreferenceOrder,
                               int pointSize)
{
    memoryDC = CreateCompatibleDC(nullptr);
    check(memoryDC != nullptr, "Could not get memory hDC");

    auto bpp = GetDeviceCaps(memoryDC, BITSPIXEL);

    int height = -MulDiv(pointSize, GetDeviceCaps(memoryDC, LOGPIXELSY), 72);

    std::vector<std::string> names(fontNamesInPreferenceOrder.begin(), fontNamesInPreferenceOrder.end());
    // Try the default last
    names.emplace_back("");

    for (auto &n : names)
    {
        for (DWORD quality : { ANTIALIASED_QUALITY, DEFAULT_QUALITY })
        {
            hFont = CreateFontA(
                height,
                0, // default width
                0, 0, // no tilt
                FW_NORMAL, FALSE, FALSE, FALSE, // normal weight
                ANSI_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                quality,
                FIXED_PITCH | FF_DONTCARE,
                n.c_str());

            if (hFont)
                break;
        }

        if (hFont)
            break;
    }

    check(hFont != nullptr, "Could not get font");
    SelectObject(memoryDC, hFont);
    SetTextColor(memoryDC, RGB(255, 255, 255));
    SetBkColor(memoryDC, RGB(0, 0, 0));

    bitmap = nullptr;
    bitmapW = 0;
    bitmapH = 0;
}

FontRasterizer::~FontRasterizer()
{
    if (bitmap)
        DeleteObject(bitmap);
    DeleteObject(hFont);
    DeleteDC(memoryDC);
}

FontRasterizer::TextPixels FontRasterizer::renderText(const std::string &text)
{
    // Find out how big the text is first, so we can make sure we have enough room
    SIZE textSize = {0};
    GetTextExtentPoint32A(memoryDC,
                          text.c_str(),
                          static_cast<int>(text.size()),
                          &textSize);

    TextPixels textPixels(textSize.cx, textSize.cy);

    ensureBitmap(textPixels.width, textPixels.height);

    RECT rect;
    rect.left   = 0;
    rect.top    = 0;
    rect.right  = textPixels.width;
    rect.bottom = textPixels.height;

    // Draw the actual text in the bitmap
    auto foo = GetTextColor(memoryDC);
    TextOutA(memoryDC, 0, 0, text.c_str(), static_cast<int>(text.size()));
    //DrawTextA(memoryDC, text.c_str(), static_cast<int>(text.size()), &rect, DT_NOCLIP);

    BITMAP bmp;
    GetObject(bitmap, sizeof(bmp), &bmp);

    // Recover the rasterized image
    BITMAPINFO info;
    zero(info);
    auto &h = info.bmiHeader;
    h.biSize = sizeof(h);
    h.biWidth = textPixels.width;
    h.biHeight = textPixels.height;
    h.biPlanes = 1;
    h.biBitCount = 32;
    h.biCompression = BI_RGB;
    h.biSizeImage = 0;
    h.biXPelsPerMeter = 0;
    h.biYPelsPerMeter = 0;
    h.biClrUsed = 0;
    h.biClrImportant = 0;

    int linesCopied = GetDIBits(
        memoryDC, bitmap,
        0, textPixels.height,
        textPixels.pixels.data(),
        &info,
        DIB_RGB_COLORS);

    check(linesCopied == textPixels.height, "Could not get all scan lines of rasterized font.");

    // The scanlines are upside down, so reverse them.
    std::vector<uint8_t> scanlineTemp(textPixels.width * TextPixels::BytesPerPixel);

    auto rowPtr = [&](size_t row)
    {
        return textPixels.pixels.data() + row * textPixels.rowPitch();
    };
    for (size_t i = 0; i < textPixels.height / 2; ++i)
    {
        size_t j = textPixels.height - 1 - i;
        size_t N = sizeBytes(scanlineTemp);
        memcpy(scanlineTemp.data(), rowPtr(i),           N);
        memcpy(rowPtr(i),           rowPtr(j),           N);
        memcpy(rowPtr(j),           scanlineTemp.data(), N);
    }

    return textPixels;
}

FontRasterizer::TextPixels::TextPixels(unsigned width, unsigned height)
    : width(width)
    , height(height)
    , pixels(width * height * BytesPerPixel)
{}
