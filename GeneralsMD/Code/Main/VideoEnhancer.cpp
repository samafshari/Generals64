#include "VideoEnhancer.h"
#ifdef _WIN32
#include <windows.h>
#include <commctrl.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <atomic>

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#endif

// Tool paths relative to the working directory (GameData/)
static const char* FFMPEG_EXE      = "extern\\tools\\ffmpeg.exe";
static const char* PYTHON_EXE      = "python";
static const char* UPSCALE_SCRIPT  = "extern\\tools\\upscale_esrgan.py";
static const char* MODEL_FILE      = "extern\\tools\\RealESRGAN_x4plus.pth";
static const char* MOVIES_DIR      = "Data\\Movies";
static const char* ENHANCED_DIR    = "Data\\Movies_Enhanced";
static const char* ENHANCED_EXT    = "mp4";
static const char* MARKER_FILE     = "Data\\Movies_Enhanced\\.enhance_complete";
static const int   UPSCALE         = 4;
static bool        g_useNvenc      = false;

// ── Utility ──

static bool FileExists(const char* path)
{
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

static void EnsureDir(const char* path)
{
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES)
        CreateDirectoryA(path, nullptr);
}

static void CloseHandleSafe(HANDLE& handle)
{
    if (handle && handle != INVALID_HANDLE_VALUE)
        CloseHandle(handle);
    handle = nullptr;
}

// ── Process Management ──

static DWORD RunProcess(const char* cmdLine, std::atomic<bool>& cancel, DWORD timeoutMs = INFINITE)
{
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    char buf[4096];
    strncpy(buf, cmdLine, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;

    if (!CreateProcessA(nullptr, buf, nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return (DWORD)-1;

    DWORD elapsed = 0;
    const DWORD pollMs = 250;
    while (true)
    {
        DWORD wait = WaitForSingleObject(pi.hProcess, pollMs);
        if (wait == WAIT_OBJECT_0)
            break;
        elapsed += pollMs;
        if (cancel.load())
        {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 2000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return (DWORD)-1;
        }
        if (timeoutMs != INFINITE && elapsed >= timeoutMs)
        {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 2000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return (DWORD)-1;
        }
    }

    DWORD exitCode = (DWORD)-1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return exitCode;
}

struct ChildProcess
{
    HANDLE process = nullptr;
    HANDLE thread  = nullptr;
    HANDLE pipe    = nullptr;
};

static bool StartProcessWithStdoutPipe(const char* cmdLine, bool mergeStdErr, ChildProcess& child)
{
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE readHandle = nullptr;
    HANDLE writeHandle = nullptr;
    HANDLE nulHandle = INVALID_HANDLE_VALUE;

    if (!CreatePipe(&readHandle, &writeHandle, &sa, 0))
        return false;

    SetHandleInformation(readHandle, HANDLE_FLAG_INHERIT, 0);

    nulHandle = CreateFileA("NUL", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (nulHandle == INVALID_HANDLE_VALUE)
    {
        CloseHandleSafe(readHandle);
        CloseHandleSafe(writeHandle);
        return false;
    }

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = nulHandle;
    si.hStdOutput = writeHandle;
    si.hStdError = mergeStdErr ? writeHandle : nulHandle;

    PROCESS_INFORMATION pi = {};
    char cmdBuf[4096];
    strncpy(cmdBuf, cmdLine, sizeof(cmdBuf) - 1);
    cmdBuf[sizeof(cmdBuf) - 1] = 0;

    if (!CreateProcessA(nullptr, cmdBuf, nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        CloseHandleSafe(nulHandle);
        CloseHandleSafe(readHandle);
        CloseHandleSafe(writeHandle);
        return false;
    }

    CloseHandleSafe(nulHandle);
    CloseHandleSafe(writeHandle);

    child.process = pi.hProcess;
    child.thread = pi.hThread;
    child.pipe = readHandle;
    return true;
}

static void KillChildProcess(ChildProcess& child)
{
    if (child.process)
    {
        TerminateProcess(child.process, 1);
        WaitForSingleObject(child.process, 2000);
    }
    CloseHandleSafe(child.pipe);
    CloseHandleSafe(child.thread);
    CloseHandleSafe(child.process);
}

static DWORD RunProcessCaptureOutput(const char* cmdLine, std::string& output,
    std::atomic<bool>& cancel, DWORD timeoutMs = INFINITE)
{
    ChildProcess child;
    if (!StartProcessWithStdoutPipe(cmdLine, true, child))
        return (DWORD)-1;

    DWORD elapsed = 0;
    const DWORD pollMs = 50;
    char buffer[4096];

    while (true)
    {
        DWORD bytesAvailable = 0;
        BOOL pipeOk = PeekNamedPipe(child.pipe, nullptr, 0, nullptr, &bytesAvailable, nullptr);
        if (!pipeOk)
            break;

        if (bytesAvailable > 0)
        {
            DWORD readBytes = 0;
            DWORD toRead = bytesAvailable < sizeof(buffer) ? bytesAvailable : (DWORD)sizeof(buffer);
            if (!ReadFile(child.pipe, buffer, toRead, &readBytes, nullptr))
                break;
            if (readBytes > 0)
                output.append(buffer, readBytes);
            continue;
        }

        DWORD wait = WaitForSingleObject(child.process, pollMs);
        if (wait == WAIT_OBJECT_0)
            break;

        elapsed += pollMs;
        if (cancel.load())
        {
            KillChildProcess(child);
            return (DWORD)-1;
        }
        if (timeoutMs != INFINITE && elapsed >= timeoutMs)
        {
            KillChildProcess(child);
            return (DWORD)-1;
        }
    }

    if (child.pipe)
    {
        DWORD readBytes = 0;
        while (ReadFile(child.pipe, buffer, sizeof(buffer), &readBytes, nullptr) && readBytes > 0)
            output.append(buffer, readBytes);
    }

    DWORD exitCode = (DWORD)-1;
    GetExitCodeProcess(child.process, &exitCode);
    CloseHandleSafe(child.pipe);
    CloseHandleSafe(child.thread);
    CloseHandleSafe(child.process);
    return exitCode;
}

// ── Video Probing ──

struct VideoInfo
{
    int width  = 0;
    int height = 0;
    float fps  = 15.0f;
    int totalFrames = 0;
};

static bool ParseDimensionsFromLine(const char* line, int& width, int& height)
{
    for (const char* p = line; *p; ++p)
    {
        if (*p < '0' || *p > '9')
            continue;
        char* widthEnd = nullptr;
        long parsedW = strtol(p, &widthEnd, 10);
        if (widthEnd == p || *widthEnd != 'x')
            continue;
        char* heightEnd = nullptr;
        long parsedH = strtol(widthEnd + 1, &heightEnd, 10);
        if (heightEnd == widthEnd + 1)
            continue;
        if (parsedW > 32 && parsedW <= 8192 && parsedH > 32 && parsedH <= 8192)
        {
            width = (int)parsedW;
            height = (int)parsedH;
            return true;
        }
        p = heightEnd - 1;
    }
    return false;
}

static bool ParseFpsFromLine(const char* line, float& fps)
{
    char* fpsStr = strstr(const_cast<char*>(line), " fps");
    if (!fpsStr)
        return false;
    char* p = fpsStr - 1;
    while (p > line && (*p == '.' || (*p >= '0' && *p <= '9')))
        --p;
    float parsed = (float)atof(p + 1);
    if (parsed > 0.0f && parsed < 120.0f)
    {
        fps = parsed;
        return true;
    }
    return false;
}

static bool ParseFrameCount(const char* line, int& count)
{
    // Look for "NUMBER_OF_FRAMES" or nb_frames in probe output
    const char* key = strstr(line, "nb_read_frames=");
    if (!key)
        return false;
    int val = atoi(key + 15);
    if (val > 0)
    {
        count = val;
        return true;
    }
    return false;
}

static bool ProbeVideoInfo(const char* srcPath, VideoInfo& info, std::atomic<bool>& cancel)
{
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "\"%s\" -hide_banner -i \"%s\"", FFMPEG_EXE, srcPath);

    std::string output;
    RunProcessCaptureOutput(cmd, output, cancel, 30000);

    size_t start = 0;
    while (start < output.size())
    {
        size_t end = output.find('\n', start);
        if (end == std::string::npos)
            end = output.size();

        std::string line = output.substr(start, end - start);
        if (!line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);

        if (line.find("Video:") != std::string::npos)
        {
            ParseDimensionsFromLine(line.c_str(), info.width, info.height);
            ParseFpsFromLine(line.c_str(), info.fps);
        }

        start = end + 1;
    }

    // Try to get frame count via ffprobe-style probe
    if (info.width > 0 && info.height > 0)
    {
        output.clear();
        snprintf(cmd, sizeof(cmd),
            "\"%s\" -v error -count_frames -select_streams v:0 "
            "-show_entries stream=nb_read_frames -of csv=p=0 \"%s\"",
            FFMPEG_EXE, srcPath);
        if (RunProcessCaptureOutput(cmd, output, cancel, 120000) == 0)
        {
            int count = atoi(output.c_str());
            if (count > 0)
                info.totalFrames = count;
        }
    }

    return info.width > 0 && info.height > 0;
}

// ── File Operations ──

struct BikFile
{
    std::string srcDir;   // e.g. "Data\\Movies" or "Data\\English\\Movies"
    std::string fileName; // e.g. "CHINA_end.bik"
};

static void FindBikFilesInDir(const char* dir, std::vector<BikFile>& out)
{
    char pattern[MAX_PATH];
    snprintf(pattern, MAX_PATH, "%s\\*.bik", dir);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            out.push_back({ dir, fd.cFileName });
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

static std::vector<BikFile> FindAllBikFiles()
{
    std::vector<BikFile> files;

    // Primary: Data\Movies
    FindBikFilesInDir(MOVIES_DIR, files);

    // Localized: Data\*\Movies (e.g. Data\English\Movies)
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA("Data\\*", &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                continue;
            if (fd.cFileName[0] == '.')
                continue;
            // Skip "Movies" and "Movies_Enhanced" subdirs of Data itself
            if (_stricmp(fd.cFileName, "Movies") == 0 ||
                _stricmp(fd.cFileName, "Movies_Enhanced") == 0)
                continue;

            char langMoviesDir[MAX_PATH];
            snprintf(langMoviesDir, MAX_PATH, "Data\\%s\\Movies", fd.cFileName);
            DWORD attr = GetFileAttributesA(langMoviesDir);
            if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
                FindBikFilesInDir(langMoviesDir, files);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    return files;
}

static int CountPending(const std::vector<BikFile>& files)
{
    int pending = 0;
    for (auto& f : files)
    {
        char base[MAX_PATH];
        strncpy(base, f.fileName.c_str(), MAX_PATH - 1);
        base[MAX_PATH - 1] = 0;
        char* dot = strrchr(base, '.');
        if (dot) *dot = 0;

        char dstPath[MAX_PATH];
        snprintf(dstPath, MAX_PATH, "%s\\%s.%s", ENHANCED_DIR, base, ENHANCED_EXT);
        if (!FileExists(dstPath))
            ++pending;
    }
    return pending;
}

// ── Environment Detection ──

static void DetectNvenc()
{
    std::atomic<bool> cancel{false};
    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
        "\"%s\" -y -f lavfi -i nullsrc=s=64x64:d=0.04 -c:v h264_nvenc -f null NUL",
        FFMPEG_EXE);
    g_useNvenc = RunProcess(cmd, cancel, 10000) == 0;
}

static bool CheckPythonTorch()
{
    std::atomic<bool> cancel{false};
    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
        "%s -c \"import torch; assert torch.cuda.is_available()\"",
        PYTHON_EXE);
    return RunProcess(cmd, cancel, 30000) == 0;
}

static bool DownloadModel()
{
    if (FileExists(MODEL_FILE))
        return true;
    std::atomic<bool> cancel{false};
    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
        "curl.exe -L --silent -o \"%s\" "
        "\"https://github.com/xinntao/Real-ESRGAN/releases/download/v0.1.0/RealESRGAN_x4plus.pth\"",
        MODEL_FILE);
    return RunProcess(cmd, cancel, 300000) == 0 && FileExists(MODEL_FILE);
}

// ── Public API ──

void VideoEnhancer::GetEnhancedPath(const char* originalPath, char* outPath, int outPathSize)
{
    const char* fn = strrchr(originalPath, '\\');
    if (!fn) fn = strrchr(originalPath, '/');
    if (fn) fn++; else fn = originalPath;

    char base[MAX_PATH];
    strncpy(base, fn, MAX_PATH - 1);
    base[MAX_PATH - 1] = 0;
    char* dot = strrchr(base, '.');
    if (dot) *dot = 0;

    snprintf(outPath, outPathSize, "%s\\%s.%s", ENHANCED_DIR, base, ENHANCED_EXT);
}

bool VideoEnhancer::HasEnhancedVideo(const char* originalPath)
{
    char p[MAX_PATH];
    GetEnhancedPath(originalPath, p, MAX_PATH);
    return FileExists(p);
}

bool VideoEnhancer::AreToolsAvailable()
{
    return FileExists(FFMPEG_EXE) && FileExists(UPSCALE_SCRIPT);
}

// ── Progress Dialog ──

struct EnhanceState
{
    HWND hWnd        = nullptr;
    HWND hProgress   = nullptr;
    HWND hLabel      = nullptr;
    HWND hFileLabel  = nullptr;
    HWND hStepLabel  = nullptr;
    HWND hCancelBtn  = nullptr;
    HFONT hFont      = nullptr;
    std::atomic<bool> cancelled{false};
    std::atomic<bool> finished{false};
    std::atomic<int>  totalFiles{0};
    std::atomic<int>  completedFiles{0};
    std::atomic<int>  upscaleTotal{0};
    std::atomic<int>  upscaleDone{0};
    char currentFileName[MAX_PATH] = {};
    char currentStep[128]          = {};
    bool success = false;
};

static EnhanceState* g_enhState = nullptr;

static LRESULT CALLBACK EnhanceDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_COMMAND:
        if (LOWORD(wParam) == IDCANCEL && g_enhState)
        {
            if (g_enhState->finished)
                DestroyWindow(hwnd);
            else
                g_enhState->cancelled = true;
        }
        return 0;
    case WM_CLOSE:
        if (g_enhState) g_enhState->cancelled = true;
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static HWND CreateProgressDlg(HINSTANCE hInst, EnhanceState& st)
{
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_PROGRESS_CLASS };
    InitCommonControlsEx(&icc);

    const char* cls = "GeneralsVideoEnhance";
    WNDCLASSEXA wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = EnhanceDlgProc;
    wc.hInstance      = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName  = cls;
    wc.hIcon          = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExA(&wc);

    int w = 540, h = 220;
    int sx = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    int sy = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;

    HWND hwnd = CreateWindowExA(WS_EX_TOPMOST, cls,
        "Generals  - AI Video Enhancement",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        sx, sy, w, h, nullptr, nullptr, hInst, nullptr);

    st.hFont = CreateFontA(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");

    auto makeLabel = [&](const char* text, int x, int y, int lw, int lh, DWORD style = SS_LEFT) -> HWND {
        HWND h2 = CreateWindowExA(0, "STATIC", text,
            WS_CHILD | WS_VISIBLE | style, x, y, lw, lh, hwnd, nullptr, hInst, nullptr);
        SendMessage(h2, WM_SETFONT, (WPARAM)st.hFont, TRUE);
        return h2;
    };

    st.hLabel     = makeLabel("Real-ESRGAN AI is enhancing your cutscene videos.\n"
                              "This only needs to happen once.", 20, 12, 490, 36);
    st.hFileLabel = makeLabel("Preparing...", 20, 55, 490, 18);
    st.hStepLabel = makeLabel("", 20, 75, 490, 18);

    st.hProgress = CreateWindowExA(0, PROGRESS_CLASSA, nullptr,
        WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
        20, 100, 490, 22, hwnd, nullptr, hInst, nullptr);

    st.hCancelBtn = CreateWindowExA(0, "BUTTON", "Cancel",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        225, 140, 90, 30, hwnd, (HMENU)IDCANCEL, hInst, nullptr);
    SendMessage(st.hCancelBtn, WM_SETFONT, (WPARAM)st.hFont, TRUE);

    st.hWnd = hwnd;
    return hwnd;
}

// ── Per-Video Processing ──
//
// Fully in-memory pipeline — zero temp files on disk:
//   ffmpeg (decode raw RGB) → pipe → Python (GPU 4x upscale) → pipe → ffmpeg (NVENC encode)
//
// Three processes connected by pipes, all running simultaneously.
// The C++ code creates the pipes and wires them together, then monitors
// the Python process's stderr for progress updates.

struct PipelineProcess
{
    HANDLE process = nullptr;
    HANDLE thread  = nullptr;
    HANDLE stdinWrite = nullptr;  // our end to write to the process's stdin
    HANDLE stdoutRead = nullptr;  // our end to read from the process's stdout
    HANDLE stderrRead = nullptr;  // our end to read from the process's stderr
};

static void KillPipelineProcess(PipelineProcess& p)
{
    if (p.process)
    {
        TerminateProcess(p.process, 1);
        WaitForSingleObject(p.process, 2000);
    }
    CloseHandleSafe(p.stdinWrite);
    CloseHandleSafe(p.stdoutRead);
    CloseHandleSafe(p.stderrRead);
    CloseHandleSafe(p.thread);
    CloseHandleSafe(p.process);
}

// Shuttle data between two handles in a background thread.
// Reads from `src`, writes to `dst`, then closes `dst`.
struct ShuttleArgs
{
    HANDLE src;
    HANDLE dst;
    std::atomic<bool>* cancel;
};

static DWORD WINAPI ShuttleThread(LPVOID param)
{
    ShuttleArgs* args = (ShuttleArgs*)param;
    char buf[256 * 1024]; // 256 KB buffer for throughput
    DWORD read, written;

    while (true)
    {
        if (args->cancel && args->cancel->load())
            break;
        if (!ReadFile(args->src, buf, sizeof(buf), &read, nullptr) || read == 0)
            break;

        DWORD totalWritten = 0;
        while (totalWritten < read)
        {
            if (args->cancel && args->cancel->load())
                break;
            if (!WriteFile(args->dst, buf + totalWritten, read - totalWritten, &written, nullptr))
                goto done;
            totalWritten += written;
        }
    }
done:
    CloseHandle(args->dst);
    args->dst = nullptr;
    delete args;
    return 0;
}

static bool CreatePipelinePart(const char* cmdLine,
    HANDLE hStdinRead,  // child reads from this (can be nullptr)
    HANDLE hStdoutWrite, // child writes to this (can be nullptr)
    HANDLE hStderrPipeRead, // if non-null, we capture stderr separately
    PipelineProcess& proc)
{
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE nulHandle = CreateFileA("NUL", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (nulHandle == INVALID_HANDLE_VALUE)
        return false;

    HANDLE stderrWrite = nullptr;
    HANDLE stderrRead = nullptr;
    if (hStderrPipeRead == INVALID_HANDLE_VALUE)
    {
        // Caller wants a stderr pipe — create one
        if (!CreatePipe(&stderrRead, &stderrWrite, &sa, 0))
        {
            CloseHandle(nulHandle);
            return false;
        }
        SetHandleInformation(stderrRead, HANDLE_FLAG_INHERIT, 0);
    }

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdInput  = hStdinRead ? hStdinRead : nulHandle;
    si.hStdOutput = hStdoutWrite ? hStdoutWrite : nulHandle;
    si.hStdError  = stderrWrite ? stderrWrite : nulHandle;

    PROCESS_INFORMATION pi = {};
    char cmdBuf[4096];
    strncpy(cmdBuf, cmdLine, sizeof(cmdBuf) - 1);
    cmdBuf[sizeof(cmdBuf) - 1] = 0;

    BOOL ok = CreateProcessA(nullptr, cmdBuf, nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    CloseHandle(nulHandle);
    if (stderrWrite)
        CloseHandle(stderrWrite);

    if (!ok)
    {
        if (stderrRead) CloseHandle(stderrRead);
        return false;
    }

    proc.process = pi.hProcess;
    proc.thread  = pi.hThread;
    proc.stderrRead = stderrRead;
    return true;
}

static bool ProcessVideo(const BikFile& bik, EnhanceState* st)
{
    char srcPath[MAX_PATH], dstPath[MAX_PATH], baseName[MAX_PATH];
    snprintf(srcPath, MAX_PATH, "%s\\%s", bik.srcDir.c_str(), bik.fileName.c_str());
    strncpy(baseName, bik.fileName.c_str(), MAX_PATH - 1);
    baseName[MAX_PATH - 1] = 0;
    char* dot = strrchr(baseName, '.');
    if (dot) *dot = 0;
    snprintf(dstPath, MAX_PATH, "%s\\%s.%s", ENHANCED_DIR, baseName, ENHANCED_EXT);

    if (FileExists(dstPath))
        return true;

    strncpy(st->currentFileName, bik.fileName.c_str(), MAX_PATH);
    st->currentFileName[MAX_PATH - 1] = 0;
    st->upscaleTotal = 0;
    st->upscaleDone = 0;

    // ── Probe video ──
    snprintf(st->currentStep, sizeof(st->currentStep), "Probing video...");
    VideoInfo info;
    if (!ProbeVideoInfo(srcPath, info, st->cancelled) || st->cancelled)
        return false;

    st->upscaleTotal = info.totalFrames;
    snprintf(st->currentStep, sizeof(st->currentStep), "AI upscaling...");

    int outW = info.width * UPSCALE;
    int outH = info.height * UPSCALE;

    // ── Build the in-memory pipeline ──
    //
    // ffmpeg_decode ──stdout──pipe──stdin──> python_upscale ──stdout──pipe──stdin──> ffmpeg_encode
    //                                            stderr──> progress monitoring

    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };

    // Pipe 1: ffmpeg_decode.stdout → python.stdin
    HANDLE pipe1Read = nullptr, pipe1Write = nullptr;
    if (!CreatePipe(&pipe1Read, &pipe1Write, &sa, 1024 * 1024))
        return false;

    // Pipe 2: python.stdout → ffmpeg_encode.stdin
    HANDLE pipe2Read = nullptr, pipe2Write = nullptr;
    if (!CreatePipe(&pipe2Read, &pipe2Write, &sa, 1024 * 1024))
    {
        CloseHandle(pipe1Read);
        CloseHandle(pipe1Write);
        return false;
    }

    // Pipe 3: python.stderr → progress monitoring
    HANDLE pipe3Read = nullptr, pipe3Write = nullptr;
    if (!CreatePipe(&pipe3Read, &pipe3Write, &sa, 4096))
    {
        CloseHandle(pipe1Read); CloseHandle(pipe1Write);
        CloseHandle(pipe2Read); CloseHandle(pipe2Write);
        return false;
    }
    SetHandleInformation(pipe3Read, HANDLE_FLAG_INHERIT, 0);

    HANDLE nulHandle = CreateFileA("NUL", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    PipelineProcess decoder = {}, upscaler = {}, encoder = {};
    char cmd[4096];
    bool success = false;

    // ── Start ffmpeg decoder ──
    // Outputs raw RGB24 frames to stdout
    {
        STARTUPINFOA si = {};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
        si.wShowWindow = SW_HIDE;
        si.hStdInput = nulHandle;
        si.hStdOutput = pipe1Write;
        si.hStdError = nulHandle;

        PROCESS_INFORMATION pi = {};
        snprintf(cmd, sizeof(cmd),
            "\"%s\" -v error -i \"%s\" -an -sn -vsync 0 -f rawvideo -pix_fmt rgb24 -",
            FFMPEG_EXE, srcPath);
        char cmdBuf[4096];
        strncpy(cmdBuf, cmd, sizeof(cmdBuf) - 1);
        cmdBuf[sizeof(cmdBuf) - 1] = 0;

        if (!CreateProcessA(nullptr, cmdBuf, nullptr, nullptr, TRUE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
            goto cleanup;
        decoder.process = pi.hProcess;
        decoder.thread = pi.hThread;
    }

    // Close the write end of pipe1 in this process (decoder owns it now)
    CloseHandle(pipe1Write);
    pipe1Write = nullptr;

    // ── Start Python upscaler ──
    // Reads raw RGB24 from stdin, writes upscaled raw RGB24 to stdout
    {
        STARTUPINFOA si = {};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
        si.wShowWindow = SW_HIDE;
        si.hStdInput = pipe1Read;
        si.hStdOutput = pipe2Write;
        si.hStdError = pipe3Write;

        PROCESS_INFORMATION pi = {};
        snprintf(cmd, sizeof(cmd),
            "%s \"%s\" %d %d \"%s\" %d",
            PYTHON_EXE, UPSCALE_SCRIPT,
            info.width, info.height, MODEL_FILE, info.totalFrames);
        char cmdBuf[4096];
        strncpy(cmdBuf, cmd, sizeof(cmdBuf) - 1);
        cmdBuf[sizeof(cmdBuf) - 1] = 0;

        if (!CreateProcessA(nullptr, cmdBuf, nullptr, nullptr, TRUE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
            goto cleanup;
        upscaler.process = pi.hProcess;
        upscaler.thread = pi.hThread;
    }

    // Close pipe ends owned by children
    CloseHandle(pipe1Read);  pipe1Read = nullptr;
    CloseHandle(pipe2Write); pipe2Write = nullptr;
    CloseHandle(pipe3Write); pipe3Write = nullptr;

    // ── Start ffmpeg encoder ──
    // Reads raw RGB24 upscaled frames from stdin, encodes to MP4
    {
        STARTUPINFOA si = {};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
        si.wShowWindow = SW_HIDE;
        si.hStdInput = pipe2Read;
        si.hStdOutput = nulHandle;
        si.hStdError = nulHandle;

        PROCESS_INFORMATION pi = {};
        if (g_useNvenc)
        {
            snprintf(cmd, sizeof(cmd),
                "\"%s\" -y -f rawvideo -pix_fmt rgb24 -video_size %dx%d -framerate %g -i - "
                "-i \"%s\" -map 0:v:0 -map 1:a:0? "
                "-c:v h264_nvenc -preset p7 -cq 18 -pix_fmt yuv420p "
                "-movflags +faststart -c:a aac -b:a 192k -shortest \"%s\"",
                FFMPEG_EXE, outW, outH, info.fps, srcPath, dstPath);
        }
        else
        {
            snprintf(cmd, sizeof(cmd),
                "\"%s\" -y -f rawvideo -pix_fmt rgb24 -video_size %dx%d -framerate %g -i - "
                "-i \"%s\" -map 0:v:0 -map 1:a:0? "
                "-c:v libx264 -preset medium -crf 18 -pix_fmt yuv420p "
                "-movflags +faststart -c:a aac -b:a 192k -shortest \"%s\"",
                FFMPEG_EXE, outW, outH, info.fps, srcPath, dstPath);
        }
        char cmdBuf[4096];
        strncpy(cmdBuf, cmd, sizeof(cmdBuf) - 1);
        cmdBuf[sizeof(cmdBuf) - 1] = 0;

        if (!CreateProcessA(nullptr, cmdBuf, nullptr, nullptr, TRUE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
            goto cleanup;
        encoder.process = pi.hProcess;
        encoder.thread = pi.hThread;
    }

    // Close remaining pipe ends in this process
    CloseHandle(pipe2Read); pipe2Read = nullptr;
    CloseHandle(nulHandle); nulHandle = nullptr;

    // ── Monitor pipeline via Python's stderr ──
    {
        std::string stderrBuf;
        char readBuf[1024];

        while (true)
        {
            // Check if the upscaler is still running
            DWORD upWait = WaitForSingleObject(upscaler.process, 200);

            if (st->cancelled)
                goto cleanup;

            // Read any available stderr from Python
            DWORD avail = 0;
            while (PeekNamedPipe(pipe3Read, nullptr, 0, nullptr, &avail, nullptr) && avail > 0)
            {
                DWORD readBytes = 0;
                DWORD toRead = avail < sizeof(readBuf) ? avail : (DWORD)sizeof(readBuf) - 1;
                if (ReadFile(pipe3Read, readBuf, toRead, &readBytes, nullptr) && readBytes > 0)
                {
                    stderrBuf.append(readBuf, readBytes);

                    // Parse PROGRESS lines
                    size_t pos;
                    while ((pos = stderrBuf.find('\n')) != std::string::npos)
                    {
                        std::string line = stderrBuf.substr(0, pos);
                        stderrBuf.erase(0, pos + 1);

                        if (line.find("PROGRESS ") == 0)
                        {
                            int done = 0, total = 0;
                            if (sscanf(line.c_str() + 9, "%d/%d", &done, &total) == 2)
                            {
                                st->upscaleDone = done;
                                if (total > 0)
                                    st->upscaleTotal = total;
                            }
                        }
                    }
                }
                avail = 0;
            }

            if (upWait == WAIT_OBJECT_0)
                break;
        }
    }

    // Wait for encoder to finish (decoder should be done by now too)
    WaitForSingleObject(encoder.process, 60000);
    WaitForSingleObject(decoder.process, 5000);

    {
        DWORD upscalerExit = (DWORD)-1, encoderExit = (DWORD)-1;
        GetExitCodeProcess(upscaler.process, &upscalerExit);
        GetExitCodeProcess(encoder.process, &encoderExit);

        if (upscalerExit == 0 && encoderExit == 0 && FileExists(dstPath))
            success = true;
    }

cleanup:
    KillPipelineProcess(decoder);
    KillPipelineProcess(upscaler);
    KillPipelineProcess(encoder);
    if (pipe1Read) CloseHandle(pipe1Read);
    if (pipe1Write) CloseHandle(pipe1Write);
    if (pipe2Read) CloseHandle(pipe2Read);
    if (pipe2Write) CloseHandle(pipe2Write);
    if (pipe3Read) CloseHandle(pipe3Read);
    if (pipe3Write) CloseHandle(pipe3Write);
    if (nulHandle && nulHandle != INVALID_HANDLE_VALUE) CloseHandle(nulHandle);

    if (!success)
        DeleteFileA(dstPath);

    return success;
}

// ── Worker Thread ──

static void WorkerThread(EnhanceState* st)
{
    auto files = FindAllBikFiles();
    st->totalFiles = (int)files.size();

    for (int i = 0; i < (int)files.size() && !st->cancelled; ++i)
    {
        if (!ProcessVideo(files[i], st) && !st->cancelled)
        {
            char dbg[256];
            snprintf(dbg, sizeof(dbg), "VideoEnhancer: FAILED %s\\%s\n",
                files[i].srcDir.c_str(), files[i].fileName.c_str());
            OutputDebugStringA(dbg);
        }
        st->completedFiles.fetch_add(1);
    }

    if (!st->cancelled)
    {
        FILE* f = fopen(MARKER_FILE, "w");
        if (f) { fprintf(f, "done\n"); fclose(f); }
        st->success = true;
    }
    st->finished = true;
}

// ── Entry Points ──

bool VideoEnhancer_TryEnhance(HINSTANCE hInstance)
{
    return VideoEnhancer::EnhanceAllVideos(hInstance);
}

bool VideoEnhancer::EnhanceAllVideos(HINSTANCE hInstance)
{
    if (!AreToolsAvailable())
        return false;

    if (FileExists(MARKER_FILE))
        return true;

    auto files = FindAllBikFiles();
    if (files.empty())
        return false;

    int pending = CountPending(files);
    if (pending == 0)
    {
        EnsureDir(ENHANCED_DIR);
        FILE* f = fopen(MARKER_FILE, "w");
        if (f) { fprintf(f, "done\n"); fclose(f); }
        return true;
    }

    if (!CheckPythonTorch())
        return false;

    if (!FileExists(MODEL_FILE))
    {
        int dlAnswer = MessageBoxA(nullptr,
            "The Real-ESRGAN AI model (67 MB) needs to be downloaded.\n\n"
            "Download now?",
            "Generals  - Model Download",
            MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND);
        if (dlAnswer != IDYES)
            return false;
        if (!DownloadModel())
        {
            MessageBoxA(nullptr,
                "Failed to download the AI model.\n"
                "Check your internet connection and try again.",
                "Download Failed", MB_OK | MB_ICONERROR);
            return false;
        }
    }

    DetectNvenc();

    char msg[512];
    snprintf(msg, sizeof(msg),
        "AI video enhancement is available!\n\n"
        "Real-ESRGAN will upscale %d cutscene video%s to 4x resolution "
        "using your GPU%s. This only happens once.\n\n"
        "You can skip this and play normally  - the game will ask again next time.\n\n"
        "Enhance videos now?",
        pending, pending > 1 ? "s" : "",
        g_useNvenc ? " with NVENC hardware encoding" : "");

    int answer = MessageBoxA(nullptr, msg, "Generals  - Video Enhancement",
        MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND);
    if (answer != IDYES)
        return false;

    EnsureDir(ENHANCED_DIR);

    EnhanceState state;
    g_enhState = &state;
    HWND hwnd = CreateProgressDlg(hInstance, state);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    SendMessage(state.hProgress, PBM_SETRANGE32, 0, (LPARAM)(files.size() * 1000));
    SendMessage(state.hProgress, PBM_SETPOS, 0, 0);

    std::thread worker(WorkerThread, &state);

    int lastCompleted = -1;
    int lastUpscaleDone = -1;
    while (!state.finished)
    {
        MSG m;
        while (PeekMessage(&m, nullptr, 0, 0, PM_REMOVE))
        {
            if (m.message == WM_QUIT) { state.cancelled = true; break; }
            TranslateMessage(&m);
            DispatchMessage(&m);
        }

        int completed = state.completedFiles.load();
        int upTotal = state.upscaleTotal.load();
        int upDone = state.upscaleDone.load();

        int subProgress = (upTotal > 0) ? (int)((long long)upDone * 1000 / upTotal) : 0;
        int totalProgress = completed * 1000 + subProgress;
        SendMessage(state.hProgress, PBM_SETPOS, totalProgress, 0);

        if (completed != lastCompleted || upDone != lastUpscaleDone)
        {
            lastCompleted = completed;
            lastUpscaleDone = upDone;

            char lbl[MAX_PATH + 64];
            int total = state.totalFiles.load();
            if (completed < total)
                snprintf(lbl, sizeof(lbl), "Video %d / %d: %s",
                    completed + 1, total, state.currentFileName);
            else
                snprintf(lbl, sizeof(lbl), "Completed %d / %d videos", completed, total);
            SetWindowTextA(state.hFileLabel, lbl);

            char step[196];
            if (upTotal > 0 && upDone > 0)
                snprintf(step, sizeof(step), "%s (%d / %d frames)",
                    state.currentStep, upDone, upTotal);
            else
                snprintf(step, sizeof(step), "%s", state.currentStep);
            SetWindowTextA(state.hStepLabel, step);
        }

        Sleep(100);
    }

    worker.join();

    SendMessage(state.hProgress, PBM_SETPOS, state.totalFiles.load() * 1000, 0);

    if (state.success)
    {
        SetWindowTextA(state.hFileLabel, "All videos enhanced successfully!");
        SetWindowTextA(state.hStepLabel,
            g_useNvenc ? "4x AI upscale with NVENC hardware encoding" : "4x AI upscale");
        SetWindowTextA(state.hCancelBtn, "OK");
        EnableWindow(state.hCancelBtn, TRUE);

        state.cancelled = false;
        state.finished = false;
        MSG m;
        while (GetMessage(&m, nullptr, 0, 0))
        {
            TranslateMessage(&m);
            DispatchMessage(&m);
            if (state.cancelled || state.finished) break;
        }
    }
    else if (state.cancelled)
    {
        MessageBoxA(nullptr,
            "Video enhancement was cancelled.\n"
            "Already-processed videos will be used. The game will offer to "
            "finish the remaining videos on next launch.",
            "Enhancement Cancelled", MB_OK | MB_ICONINFORMATION);
    }

    DestroyWindow(hwnd);
    if (state.hFont) DeleteObject(state.hFont);
    g_enhState = nullptr;
    return state.success;
}
