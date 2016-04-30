#pragma once

#include <Windows.h>
#include <comdef.h>

#include <atlbase.h>

#undef min
#undef max

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <cstddef>
#include <cstdint>

#include <functional>
#include <iterator>
#include <array>
#include <string>
#include <vector>

typedef uint32_t uint;
typedef std::array<uint32_t, 2> uint2;
typedef std::array<uint32_t, 3> uint3;
typedef std::array<uint32_t, 4> uint4;
typedef std::array<int32_t, 2> int2;
typedef std::array<int32_t, 3> int3;
typedef std::array<int32_t, 4> int4;
typedef std::array<float, 2> float2;
typedef std::array<float, 3> float3;
typedef std::array<float, 4> float4;

template <typename T>
size_t size(const T &t)
{
    using std::begin;
    using std::end;
    return end(t) - begin(t);
}

template <typename T>
size_t sizeBytes(const T &t)
{
    return ::size(t) * sizeof(t[0]);
}

template <typename T>
auto dataPtr(T &&t) -> decltype(t.data())
{
    return t.data();
}

template <typename T, size_t N>
T *dataPtr(T (&t)[N])
{
    return t;
}

template <typename T>
void zero(T &t)
{
    memset(&t, 0, sizeof(t));
}

static uint32_t divRoundUp(uint32_t x, uint32_t y)
{
    return (x + y - 1) / y;
}

// adapted from http://stackoverflow.com/questions/466204/rounding-up-to-nearest-power-of-2
static uint64_t roundUpToPowerOf2(uint64_t v)
{
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    v++;
    return v;
}

void vlog(const char *fmt, va_list ap);
void log(const char *fmt, ...);

void keyboardWindow(HWND hwnd);
bool keyHeld(int virtualKeyCode);
bool keyPressed(int virtualKeyCode);

namespace detail
{
    bool checkImpl(bool cond, const char *fmt, ...);
    bool checkHRImpl(HRESULT hr);
    bool checkLastErrorImpl();
}

#define DEBUG_BREAK_IF_FALSE(cond) if (!cond) \
{ \
    if (IsDebuggerPresent()) \
        DebugBreak(); \
    else \
        TerminateProcess(GetCurrentProcess(), 1); \
}

#define check(cond, ...) DEBUG_BREAK_IF_FALSE(::detail::checkImpl(cond, ## __VA_ARGS__))
#define checkHR(hr)      DEBUG_BREAK_IF_FALSE(::detail::checkHRImpl(hr))
#define checkLastError() DEBUG_BREAK_IF_FALSE(::detail::checkLastErrorImpl())

struct Window
{
    HWND hWnd;

    Window(const char *title, int w, int h, int x = -1, int y = -1);
    ~Window();

    void run(std::function<bool(Window &)> idle);
};

std::wstring convertToWide(const char *str);

class Timer
{
    double period;
    uint64_t start;
public:
    Timer();
    double seconds() const;
};

std::vector<std::string> listFiles(const std::string &path, const std::string &pattern = "*");
std::vector<std::string> searchFiles(const std::string &path, const std::string &pattern);
std::string replaceAll(std::string s, const std::string &replacedString, const std::string &replaceWith);
std::vector<std::string> tokenize(const std::string &s, const std::string &delimiters);
std::vector<std::string> splitPath(const std::string &path);

std::string fileOpenDialog(const std::string &description, const std::string &pattern);
std::string fileSaveDialog(const std::string &description, const std::string &pattern);
std::string absolutePath(const std::string &path);

template <typename Iter>
std::string join(Iter begin, Iter end, std::string separator)
{
    std::string s;

    if (begin == end)
        return s;

    auto it = begin;
    s = *it;
    ++it;

    while (it != end)
    {
        s += separator;
        s += *it;
        ++it;
    }

    return s;
}

class FontRasterizer
{
    HFONT hFont;
    HDC memoryDC;
    HBITMAP bitmap;
    int bitmapW;
    int bitmapH;

    void ensureBitmap(int w, int h);
public:
    FontRasterizer(const std::vector<std::string> &fontNamesInPreferenceOrder,
                   int pointSize = 16);
    ~FontRasterizer();

    struct TextPixels
    {
        static const unsigned BytesPerPixel = 4;

        unsigned width;
        unsigned height;
        std::vector<uint8_t> pixels;

        TextPixels(unsigned width = 0, unsigned height = 0);

        unsigned rowPitch() const
        {
            return width * BytesPerPixel;
        }
    };
    TextPixels renderText(const std::string &text);
};
