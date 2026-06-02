#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cmath>

// ─── Palette — from DESIGN.md Modern Dark system ────────────────────────────
constexpr COLORREF C_BG     = RGB(30,  30,  30);   // background
constexpr COLORREF C_BAR_BG = RGB(60,  60,  60);    // button normal
constexpr COLORREF C_BAR_FG = RGB(43,  121, 194);   // #2B79C2 primary accent
constexpr COLORREF C_WARN   = RGB(194, 60,  44);    // over-limit red
constexpr COLORREF C_TEXT   = RGB(230, 230, 230);   // text primary
constexpr COLORREF C_DIM    = RGB(150, 150, 150);   // text secondary

// ─── Owner-drawn menu items ───────────────────────────────────────────────────
struct OdItem { const wchar_t* label; bool has_arrow; };
static OdItem g_od[16];
static int    g_od_n;
static OdItem* od(const wchar_t* lbl, bool arr = false) {
    g_od[g_od_n] = { lbl, arr }; return &g_od[g_od_n++];
}

// ─── Info window ─────────────────────────────────────────────────────────────
struct Line { std::wstring text; COLORREF col; bool small; bool spacer; };
static std::vector<Line> g_ilines;
static HWND g_info   = nullptr;
static int  g_info_h = 0;

struct DayData { wchar_t date[12]; long long v[4]; };
static std::vector<DayData> g_daily;
static constexpr COLORREF C_CHART[4] = {
    RGB( 43, 121, 194),  // input        — blue  (= C_BAR_FG)
    RGB(140,  82, 194),  // output       — purple
    RGB( 50, 168,  82),  // cache read   — green
    RGB(194, 154,  43),  // cache create — amber
};

struct ModelData { std::wstring name; long long total; };
static std::vector<ModelData> g_models;
static constexpr COLORREF C_MODEL[] = {
    RGB( 43, 121, 194),  // blue   (= C_BAR_FG)
    RGB(194,  60,  44),  // red    (= C_WARN)
    RGB( 50, 168,  82),  // green
    RGB(194, 154,  43),  // amber
    RGB(140,  82, 194),  // purple
    RGB( 43, 194, 154),  // teal
};

constexpr int WW = 430, WH = 150;
// Compact two-cell "Simple mode". Paddings follow the simple.png mockup; the cells
// are sized so the digits (auto-fit to fill each cell) are ~2x the earlier size.
constexpr int SIMPLE_W = 87, SIMPLE_H = 105;
constexpr int SIMPLE_PADX = 7, SIMPLE_PADY = 6, SIMPLE_GAP = 5;
constexpr int PAD = 14, BAR_H = 16, TEXT_W = 90;
constexpr int BAR_W = WW - PAD * 2 - TEXT_W - 8;
constexpr UINT WM_DATA_READY = WM_USER + 1;

// ─── Shared data ─────────────────────────────────────────────────────────────
struct Metrics {
    // Row 1 — "Current session" (extension) or "Current block" (ccusage)
    std::wstring row1_label = L"Current session";
    double       row1_frac  = 0;
    std::wstring row1_right;     // "3% used"
    std::wstring row1_sub;       // "Resets 3:10pm (MSK)"

    // Row 2 — "Current week (all models)"
    std::wstring row2_label = L"Current week (all models)";
    double       row2_frac  = 0;
    std::wstring row2_right;     // "10% used"
    std::wstring row2_sub;       // "Resets Jun 2, 10am (MSK)"

    bool ok      = false;
    bool loading = true;
};

static Metrics          g_m;
static CRITICAL_SECTION g_cs;
static std::atomic<int>       g_pending{ 0 };
static std::atomic<ULONGLONG> g_load_start{ 0 };   // tick when current load began (watchdog)
static HWND             g_hwnd = nullptr;
static POINT            g_drag0, g_win0;
static bool             g_dragging = false;
static BYTE             g_opacity  = 255;            // 0=invisible, 255=opaque
static bool             g_simple   = false;           // compact two-cell display

// ─── Time helpers ─────────────────────────────────────────────────────────────
static bool parse_iso(const std::string& s, SYSTEMTIME& st) {
    memset(&st, 0, sizeof(st));
    if (s.size() < 10) return false;
    auto tok = [](const char* p, int n) {
        int v = 0;
        for (int i = 0; i < n; ++i) v = v * 10 + (p[i] - '0');
        return v;
    };
    const char* p = s.c_str();
    st.wYear  = static_cast<WORD>(tok(p,     4));
    st.wMonth = static_cast<WORD>(tok(p + 5, 2));
    st.wDay   = static_cast<WORD>(tok(p + 8, 2));
    if (s.size() >= 19) {
        st.wHour   = static_cast<WORD>(tok(p + 11, 2));
        st.wMinute = static_cast<WORD>(tok(p + 14, 2));
        st.wSecond = static_cast<WORD>(tok(p + 17, 2));
    }
    return st.wYear > 0;
}

static ULONGLONG st_to_ull(const SYSTEMTIME& st) {
    FILETIME ft; SystemTimeToFileTime(&st, &ft);
    ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
    return u.QuadPart;
}

static double block_time_frac(const SYSTEMTIME& start_utc, const SYSTEMTIME& end_utc) {
    SYSTEMTIME now{}; GetSystemTime(&now);
    ULONGLONG us = st_to_ull(start_utc), ue = st_to_ull(end_utc), un = st_to_ull(now);
    if (ue <= us) return 1.0;
    return std::clamp(static_cast<double>(un - us) / static_cast<double>(ue - us), 0.0, 1.0);
}

static std::wstring fmt_local_time(SYSTEMTIME utc) {
    SYSTEMTIME loc{};
    SystemTimeToTzSpecificLocalTime(nullptr, &utc, &loc);
    int h = loc.wHour % 12; if (!h) h = 12;
    wchar_t buf[16];
    swprintf(buf, 16, L"%d:%02d%s", h, loc.wMinute, loc.wHour >= 12 ? L"pm" : L"am");
    return buf;
}

static std::wstring fmt_date(int Y, int M, int D) {
    constexpr const wchar_t* MON[] = { L"",
        L"Jan",L"Feb",L"Mar",L"Apr",L"May",L"Jun",
        L"Jul",L"Aug",L"Sep",L"Oct",L"Nov",L"Dec" };
    // Add 7 days to get next-week reset
    SYSTEMTIME st{};
    st.wYear = static_cast<WORD>(Y); st.wMonth = static_cast<WORD>(M); st.wDay = static_cast<WORD>(D);
    FILETIME ft; SystemTimeToFileTime(&st, &ft);
    ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
    u.QuadPart += 7ULL * 24 * 60 * 60 * 10000000ULL;
    ft.dwLowDateTime = u.LowPart; ft.dwHighDateTime = u.HighPart;
    FileTimeToSystemTime(&ft, &st);
    wchar_t buf[16];
    swprintf(buf, 16, L"%s %d", MON[st.wMonth], st.wDay);
    return buf;
}

// ─── Position persistence ────────────────────────────────────────────────────
static std::filesystem::path pos_path() {
    wchar_t prof[MAX_PATH]{};
    GetEnvironmentVariableW(L"USERPROFILE", prof, MAX_PATH);
    return std::filesystem::path(std::wstring(prof)) / L".claude" / L"widget_pos.txt";
}

static void save_pos(HWND hw) {
    RECT wr; GetWindowRect(hw, &wr);
    std::ofstream f(pos_path());
    if (f) f << wr.left << ' ' << wr.top << '\n';
}

static bool load_pos(int& x, int& y) {
    std::ifstream f(pos_path());
    return f && (f >> x >> y);
}

// ─── Opacity persistence ─────────────────────────────────────────────────────
static std::filesystem::path opacity_path() {
    wchar_t prof[MAX_PATH]{};
    GetEnvironmentVariableW(L"USERPROFILE", prof, MAX_PATH);
    return std::filesystem::path(std::wstring(prof)) / L".claude" / L"widget_opacity.txt";
}

static void save_opacity() {
    std::ofstream f(opacity_path());
    if (f) f << static_cast<int>(g_opacity) << '\n';
}

static void load_opacity() {
    std::ifstream f(opacity_path());
    int v; if (f && (f >> v) && v >= 25 && v <= 255) g_opacity = static_cast<BYTE>(v);
}

static void apply_opacity(HWND hw) {
    SetLayeredWindowAttributes(hw, 0, g_opacity, LWA_ALPHA);
}

// ─── Simple-mode persistence ─────────────────────────────────────────────────
static std::filesystem::path simple_path() {
    wchar_t prof[MAX_PATH]{};
    GetEnvironmentVariableW(L"USERPROFILE", prof, MAX_PATH);
    return std::filesystem::path(std::wstring(prof)) / L".claude" / L"widget_simple.txt";
}

static void save_simple() {
    std::ofstream f(simple_path());
    if (f) f << (g_simple ? 1 : 0) << '\n';
}

static void load_simple() {
    std::ifstream f(simple_path());
    int v; if (f && (f >> v)) g_simple = (v != 0);
}

// Resize the window + clip region to match the current mode, then repaint.
// The bottom-left corner stays fixed across mode switches, so Simple mode sits
// in the lower-left corner of where the full widget was (and vice-versa).
static void apply_mode(HWND hw) {
    int w = g_simple ? SIMPLE_W : WW;
    int h = g_simple ? SIMPLE_H : WH;
    RECT r; GetWindowRect(hw, &r);          // current rect, screen coords (WS_POPUP)
    int x = r.left;                         // keep left edge
    int y = r.bottom - h;                   // keep bottom edge → anchor bottom-left
    SetWindowPos(hw, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowRgn(hw, CreateRoundRectRgn(0, 0, w, h, 16, 16), TRUE);
    InvalidateRect(hw, nullptr, TRUE);
}

// ─── String helpers ───────────────────────────────────────────────────────────
static std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' '))
        s.pop_back();
    auto i = s.find_first_not_of(" \t\r\n");
    return (i == std::string::npos) ? "" : s.substr(i);
}

static std::vector<std::string> split_pipe(const std::string& s) {
    std::vector<std::string> v;
    std::string cur;
    for (char c : s) {
        if (c == '|') { v.push_back(cur); cur.clear(); }
        else cur += c;
    }
    v.push_back(cur);
    return v;
}

static double to_double(const std::string& s) {
    try { return std::stod(s); } catch (...) { return 0.0; }
}

// ─── Bounded pipe read ─────────────────────────────────────────────────────────
// Reads a child's stdout with a hard deadline. The old code looped on a blocking
// ReadFile and only applied a WaitForSingleObject timeout *after* the loop — so a
// child that never closed its stdout (e.g. a hung node/ccusage call) blocked the
// load thread forever, which latched g_pending and froze all future refreshes.
// On timeout we terminate the child so this can never happen.
static std::string read_pipe_bounded(HANDLE pr, HANDLE hproc, DWORD timeout_ms) {
    std::string out;
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    char buf[4096];
    for (;;) {
        DWORD avail = 0;
        if (PeekNamedPipe(pr, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            DWORD want = avail > sizeof(buf) ? (DWORD)sizeof(buf) : avail, n = 0;
            if (ReadFile(pr, buf, want, &n, nullptr) && n > 0) { out.append(buf, n); continue; }
            break;                                   // pipe closed / read error
        }
        if (WaitForSingleObject(hproc, 0) == WAIT_OBJECT_0) {
            // Process exited — drain anything still buffered, then stop.
            for (;;) {
                DWORD a = 0, n = 0;
                if (!PeekNamedPipe(pr, nullptr, 0, nullptr, &a, nullptr) || a == 0) break;
                DWORD want = a > sizeof(buf) ? (DWORD)sizeof(buf) : a;
                if (!ReadFile(pr, buf, want, &n, nullptr) || n == 0) break;
                out.append(buf, n);
            }
            break;
        }
        if (GetTickCount64() >= deadline) { TerminateProcess(hproc, 1); break; }
        Sleep(20);                                   // idle — avoid busy-spin
    }
    return out;
}

// ─── Subprocess: write .ps1 to temp dir, run, capture stdout ─────────────────
static std::string run_ps(const char* script) {
    wchar_t tmp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmp);
    std::wstring ps = std::wstring(tmp) + L"cuw_widget.ps1";

    if (FILE* f = _wfopen(ps.c_str(), L"w")) { fputs(script, f); fclose(f); }

    wchar_t cmd[1024]{};
    swprintf(cmd, 1024,
        L"\"C:\\Program Files\\PowerShell\\7\\pwsh.exe\""
        L" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"%s\"",
        ps.c_str());

    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE pipe_r, pipe_w;
    if (!CreatePipe(&pipe_r, &pipe_w, &sa, 0)) { DeleteFileW(ps.c_str()); return ""; }
    SetHandleInformation(pipe_r, HANDLE_FLAG_INHERIT, 0);

    // NULL_DEVICE for stderr — prevents error text from contaminating stdout output
    HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE,
                             &sa, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW si{};
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput  = pipe_w;
    si.hStdError   = nul;
    si.hStdInput   = INVALID_HANDLE_VALUE;

    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd, nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pipe_r); CloseHandle(pipe_w); DeleteFileW(ps.c_str()); return "";
    }
    CloseHandle(pipe_w);
    CloseHandle(nul);

    std::string out = read_pipe_bounded(pipe_r, pi.hProcess, 25000);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(pipe_r);
    DeleteFileW(ps.c_str());
    return out;
}

// ─── Python detection ────────────────────────────────────────────────────────
static std::wstring find_python() {
    static std::wstring cached;
    if (!cached.empty()) return cached;

    // 1. Ask the shell — skip Microsoft Store shims (WindowsApps)
    {
        SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
        HANDLE pr, pw;
        if (CreatePipe(&pr, &pw, &sa, 0)) {
            SetHandleInformation(pr, HANDLE_FLAG_INHERIT, 0);
            HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE,
                                     &sa, OPEN_EXISTING, 0, nullptr);
            wchar_t cmd[] = L"cmd.exe /c where python";
            STARTUPINFOW si{};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            si.hStdOutput = pw; si.hStdError = nul; si.hStdInput = INVALID_HANDLE_VALUE;
            PROCESS_INFORMATION pi{};
            if (CreateProcessW(nullptr, cmd, nullptr, nullptr, TRUE,
                               CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                CloseHandle(pw); CloseHandle(nul);
                std::string out; char buf[1024]; DWORD n;
                while (ReadFile(pr, buf, sizeof(buf), &n, nullptr) && n > 0)
                    out.append(buf, n);
                WaitForSingleObject(pi.hProcess, 5000);
                CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
                size_t pos = 0;
                while (pos < out.size()) {
                    auto nl = out.find_first_of("\r\n", pos);
                    std::string line = trim(out.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
                    if (!line.empty() && line.find("WindowsApps") == std::string::npos) {
                        CloseHandle(pr);
                        cached = std::wstring(line.begin(), line.end());
                        return cached;
                    }
                    if (nl == std::string::npos) break;
                    pos = nl + 1;
                }
                CloseHandle(pr);
            } else {
                CloseHandle(pw); CloseHandle(nul); CloseHandle(pr);
            }
        }
    }

    // 2. Common fixed paths (newest version first)
    static const wchar_t* kCandidates[] = {
        L"C:\\Python314\\python.exe", L"C:\\Python313\\python.exe",
        L"C:\\Python312\\python.exe", L"C:\\Python311\\python.exe",
        L"C:\\Python310\\python.exe", L"C:\\Python39\\python.exe",
    };
    for (auto p : kCandidates)
        if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES)
            { cached = p; return cached; }

    // 3. %LOCALAPPDATA%\Programs\Python\PythonXXX (user-scope installer)
    wchar_t la[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH)) {
        static const wchar_t* kSubdirs[] = {
            L"Python314", L"Python313", L"Python312",
            L"Python311", L"Python310", L"Python39"
        };
        for (auto d : kSubdirs) {
            auto p = (std::filesystem::path(la) / L"Programs" / L"Python" / d / L"python.exe").wstring();
            if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES)
                { cached = p; return cached; }
        }
    }

    cached = L"python.exe"; // last resort — rely on PATH
    return cached;
}

// ─── Load data (runs on background thread) ───────────────────────────────────
static std::wstring to_wstr(const std::string& s) { return { s.begin(), s.end() }; }

// Run Python directly (no PowerShell) and capture stdout
static std::string run_python(const wchar_t* script_path) {
    std::wstring wcmd = L"\"" + find_python() + L"\" \"" + script_path + L"\"";
    for (wchar_t& c : wcmd) if (c == L'\\') c = L'/';
    std::vector<wchar_t> cmd(wcmd.begin(), wcmd.end());
    cmd.push_back(0);

    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE pr, pw;
    if (!CreatePipe(&pr, &pw, &sa, 0)) return "";
    SetHandleInformation(pr, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW si{};
    si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE; si.hStdOutput = pw; si.hStdError = pw; si.hStdInput = INVALID_HANDLE_VALUE;

    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pr); CloseHandle(pw); CloseHandle(nul); return "";
    }
    CloseHandle(pw); CloseHandle(nul);

    std::string out = read_pipe_bounded(pr, pi.hProcess, 15000);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(pr);
    return out;
}

static void load_extension(Metrics& m) {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    auto script = (std::filesystem::path(exe).parent_path() / L"get_limits.py").wstring();
    std::string out = trim(run_python(script.c_str()));
    if (out.empty() || out.starts_with("FALLBACK") || out.starts_with("DEBUG")) return;

    // Format: session_pct|session_reset|week_pct|week_reset
    auto p = split_pipe(out);
    if (p.size() < 4) return;

    double sp = to_double(trim(p[0]));
    double wp = to_double(trim(p[2]));
    if (sp == 0 && wp == 0) return;

    m.row1_label = L"Current session";
    m.row1_frac  = std::clamp(sp / 100.0, 0.0, 1.0);
    m.row1_right = to_wstr(trim(p[0])) + L"% used";
    m.row1_sub   = L"Resets " + to_wstr(trim(p[1]));

    m.row2_label = L"Current week (all models)";
    m.row2_frac  = std::clamp(wp / 100.0, 0.0, 1.0);
    m.row2_right = to_wstr(trim(p[2])) + L"% used";
    m.row2_sub   = L"Resets " + to_wstr(trim(p[3]));

    m.ok = true;
}

static void load_ccusage(Metrics& m) {
    // ── Active block ─────────────────────────────────────────────────────────
    std::string block_out = run_ps(
        "$ErrorActionPreference = 'SilentlyContinue'\n"
        "$r = & 'C:/Users/il720506/AppData/Roaming/npm/ccusage.ps1' blocks --json 2>$null | ConvertFrom-Json\n"
        "$a = $r.blocks | Where-Object { $_.isActive -eq $true } | Select-Object -Last 1\n"
        "if ($a) {\n"
        "    $cost = [math]::Round($a.costUSD, 4)\n"
        "    $s = ([DateTime]$a.startTime).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')\n"
        "    $e = ([DateTime]$a.endTime).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')\n"
        "    Write-Output \"$cost|$s|$e\"\n"
        "} else { Write-Output '0|0|0' }\n"
    );
    auto bp = split_pipe(trim(block_out));
    if (bp.size() >= 3 && trim(bp[1]) != "0") {
        double cost = to_double(trim(bp[0]));
        SYSTEMTIME st_s{}, st_e{};
        if (parse_iso(trim(bp[1]), st_s) && parse_iso(trim(bp[2]), st_e)) {
            double frac = block_time_frac(st_s, st_e);
            wchar_t buf[64];
            swprintf(buf, 64, L"%.0f%% ($%.2f)", frac * 100.0, cost);
            m.row1_label = L"Current block";
            m.row1_frac  = frac;
            m.row1_right = buf;
            m.row1_sub   = L"Resets " + fmt_local_time(st_e);
            m.ok = true;
        }
    }

    // ── Weekly ────────────────────────────────────────────────────────────────
    std::string week_out = run_ps(
        "$ErrorActionPreference = 'SilentlyContinue'\n"
        "$r = & 'C:/Users/il720506/AppData/Roaming/npm/ccusage.ps1' weekly --json 2>$null | ConvertFrom-Json\n"
        "$a = $r.weekly | Where-Object { $_.agent -eq 'all' } | Select-Object -Last 1\n"
        "if ($a) {\n"
        "    $cost = [math]::Round($a.totalCost, 4)\n"
        "    Write-Output \"$cost|$($a.period)\"\n"
        "} else { Write-Output '0|2026-01-01' }\n"
    );
    auto wp = split_pipe(trim(week_out));
    if (wp.size() >= 2) {
        double wcost  = to_double(trim(wp[0]));
        double budget = 100.0;
        {
            wchar_t prof[MAX_PATH]{};
            GetEnvironmentVariableW(L"USERPROFILE", prof, MAX_PATH);
            std::ifstream jf(std::filesystem::path(std::wstring(prof) + L"\\.claude\\.usage_metrics.json"));
            if (jf) {
                std::string j((std::istreambuf_iterator<char>(jf)), {});
                auto pos = j.find("\"budget\"");
                if (pos != std::string::npos) {
                    pos = j.find(':', pos);
                    while (pos != std::string::npos && j[++pos] == ' ') {}
                    try { budget = std::stod(j.substr(pos)); } catch (...) {}
                }
            }
        }
        std::string period = trim(wp[1]);
        auto tok = [](const char* p, int n) {
            int v = 0; for (int i = 0; i < n; ++i) v = v * 10 + (p[i] - '0'); return v;
        };
        int Y = 2026, Mo = 1, D = 1;
        if (period.size() >= 10) {
            Y  = tok(period.c_str(),     4);
            Mo = tok(period.c_str() + 5, 2);
            D  = tok(period.c_str() + 8, 2);
        }
        double frac  = (budget > 0) ? std::min(wcost / budget, 1.0) : 0.0;
        double pct_v = (budget > 0) ? wcost / budget * 100.0 : 0.0;
        wchar_t buf[64];
        swprintf(buf, 64, L"%.0f%% ($%.2f)", pct_v, wcost);
        m.row2_label = L"This week (all models)";
        m.row2_frac  = frac;
        m.row2_right = buf;
        m.row2_sub   = L"Resets " + fmt_date(Y, Mo, D) + L"  ·  $" +
                       to_wstr(std::to_string(static_cast<int>(budget))) + L" budget";
        m.ok = true;
    }
}

static void load_data() {
    Metrics m{};
    m.loading = false;

    load_extension(m);          // prefer real rate-limit data from browser
    if (!m.ok) load_ccusage(m); // fall back to local ccusage cost data

    EnterCriticalSection(&g_cs);
    g_m = std::move(m);
    LeaveCriticalSection(&g_cs);
    PostMessageW(g_hwnd, WM_DATA_READY, 0, 0);
}

DWORD WINAPI load_thread(LPVOID) { load_data(); return 0; }

static void start_load() {
    if (g_pending.fetch_add(1) > 0) { g_pending.fetch_sub(1); return; }
    g_load_start.store(GetTickCount64());
    EnterCriticalSection(&g_cs);
    g_m.loading = true;
    LeaveCriticalSection(&g_cs);
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
    if (HANDLE t = CreateThread(nullptr, 0, load_thread, nullptr, 0, nullptr))
        CloseHandle(t);
    else
        g_pending.fetch_sub(1);
}

// ─── Drawing ─────────────────────────────────────────────────────────────────
static void fillr(HDC hdc, int x, int y, int w, int h, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    RECT r{ x, y, x + w, y + h };
    FillRect(hdc, &r, b);
    DeleteObject(b);
}

// frac is set at runtime from loaded usage data (g_m.row1_frac / row2_frac); the
// data-flow inspection can't see that cross-thread global write and wrongly thinks
// it's always the default 0.0.
// ReSharper disable once CppDFAConstantParameter
static void drawbar(HDC hdc, int y, double frac, bool warn = false) {
    fillr(hdc, PAD, y, BAR_W, BAR_H, C_BAR_BG);
    int fw = static_cast<int>(BAR_W * std::clamp(frac, 0.0, 1.0));
    if (fw > 0) fillr(hdc, PAD, y, fw, BAR_H, warn ? C_WARN : C_BAR_FG);
}

static HFONT makefont(int sz, int weight = FW_NORMAL) {
    return CreateFontW(sz, 0, 0, 0, weight, 0, 0, 0,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
}

static void put(HDC hdc, const wchar_t* s, int x, int y, COLORREF c) {
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, c);
    TextOutW(hdc, x, y, s, static_cast<int>(wcslen(s)));
}

// One cell of Simple mode: a horizontal progress bar (width = % used) with the
// big percentage number overlaid. Bar is blue, turning red at >= 90%.
static void draw_simple_cell(HDC hdc, int x, int y, int w, int h, double frac) {
    int pct = static_cast<int>(std::lround(std::clamp(frac, 0.0, 1.0) * 100.0));
    bool warn = pct >= 90;
    fillr(hdc, x, y, w, h, C_BAR_BG);
    int fw = static_cast<int>(w * std::clamp(frac, 0.0, 1.0));
    if (fw > 0) fillr(hdc, x, y, fw, h, warn ? C_WARN : C_BAR_FG);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, C_TEXT);
    wchar_t buf[8];
    int len = swprintf(buf, 8, L"%d", pct);

    // Auto-fit the digits to fill the cell: measure at a reference em=100, then
    // scale so the glyph (~0.62 em) fills ~95% of the cell height, bounded by the
    // cell width so 3-digit values (e.g. 100) shrink instead of clipping.
    HFONT probe = makefont(100, FW_BOLD);
    auto  pf    = static_cast<HFONT>(SelectObject(hdc, probe));
    SIZE  s100; GetTextExtentPoint32W(hdc, buf, len, &s100);
    SelectObject(hdc, pf); DeleteObject(probe);
    double by_w = static_cast<double>(w - 6) / (s100.cx ? s100.cx : 1);
    double by_h = (h * 0.95) / (100 * 0.62);
    int fh = std::max(8, static_cast<int>(100 * std::min(by_w, by_h)));
    HFONT f    = makefont(fh, FW_BOLD);
    auto  prev = static_cast<HFONT>(SelectObject(hdc, f));
    RECT r{ x, y, x + w, y + h };
    DrawTextW(hdc, buf, -1, &r, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    SelectObject(hdc, prev); DeleteObject(f);
}

static void paint_simple(HDC hdc, const Metrics& m) {
    fillr(hdc, 0, 0, SIMPLE_W, SIMPLE_H, C_BG);
    if (!m.ok) {
        HFONT fs = makefont(13);
        auto prev = static_cast<HFONT>(SelectObject(hdc, fs));
        put(hdc, m.loading ? L"Loading…" : L"No data", PAD, PAD + 6, C_DIM);
        SelectObject(hdc, prev); DeleteObject(fs);
        return;
    }
    int cw = SIMPLE_W - SIMPLE_PADX * 2;
    int ch = (SIMPLE_H - SIMPLE_PADY * 2 - SIMPLE_GAP) / 2;
    draw_simple_cell(hdc, SIMPLE_PADX, SIMPLE_PADY,                    cw, ch, m.row1_frac);  // session
    draw_simple_cell(hdc, SIMPLE_PADX, SIMPLE_PADY + ch + SIMPLE_GAP, cw, ch, m.row2_frac);  // week
}

static void paint(HWND hw) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hw, &ps);

    Metrics m;
    EnterCriticalSection(&g_cs);
    m = g_m;
    LeaveCriticalSection(&g_cs);

    if (g_simple) {
        paint_simple(hdc, m);
        EndPaint(hw, &ps);
        return;
    }

    fillr(hdc, 0, 0, WW, WH, C_BG);

    HFONT fn = makefont(15), fs = makefont(13);

    if (!m.ok) {
        SelectObject(hdc, fs);
        put(hdc, m.loading ? L"Loading…" : L"No data — right-click to refresh",
            PAD, PAD + 6, C_DIM);
        DeleteObject(fn); DeleteObject(fs);
        EndPaint(hw, &ps);
        return;
    }

    int y = PAD;

    // ── Row 1 ─────────────────────────────────────────────────────────────────
    SelectObject(hdc, fn);
    put(hdc, m.row1_label.c_str(), PAD, y, C_TEXT);
    y += 20;

    drawbar(hdc, y + 1, m.row1_frac);
    put(hdc, m.row1_right.c_str(), PAD + BAR_W + 8, y, C_TEXT);
    y += BAR_H + 6;

    SelectObject(hdc, fs);
    put(hdc, m.row1_sub.c_str(), PAD, y, C_DIM);
    y += 18 + 8;

    // ── Row 2 ─────────────────────────────────────────────────────────────────
    SelectObject(hdc, fn);
    put(hdc, m.row2_label.c_str(), PAD, y, C_TEXT);
    y += 20;

    bool over = m.row2_frac >= 1.0;
    drawbar(hdc, y + 1, m.row2_frac, over);
    put(hdc, m.row2_right.c_str(), PAD + BAR_W + 8, y, over ? C_WARN : C_TEXT);
    y += BAR_H + 6;

    SelectObject(hdc, fs);
    put(hdc, m.row2_sub.c_str(), PAD, y, C_DIM);

    DeleteObject(fn); DeleteObject(fs);
    EndPaint(hw, &ps);
}

// ─── Info window ─────────────────────────────────────────────────────────────
static constexpr int IW = 600, LH = 20, SH = 8;
static constexpr int CHART_H_BARS = 100;
static constexpr int CHART_SEC_H  = 22 + 22 + CHART_H_BARS + 1 + 20 + PAD;
static constexpr int DONUT_R_OUT  = 80;
static constexpr int DONUT_R_IN   = 46;
static constexpr int DONUT_SEC_H  = 22 + (DONUT_R_OUT * 2 + 20) + 46 + PAD;

static void load_daily() {
    g_daily.clear();
    g_models.clear();
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    auto script = (std::filesystem::path(exe).parent_path() / L"get_daily.py").wstring();
    std::string out = run_python(script.c_str());
    size_t pos = 0;
    while (pos < out.size()) {
        auto nl = out.find('\n', pos);
        std::string ln = trim(out.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        pos = (nl == std::string::npos) ? out.size() : nl + 1;
        if (ln.empty()) continue;
        if (ln.starts_with("MODEL|")) {
            auto p = split_pipe(ln);
            if (p.size() >= 3) {
                ModelData md{};
                md.name = to_wstr(trim(p[1]));
                try { md.total = std::stoll(trim(p[2])); } catch (...) {}
                if (md.total > 0) g_models.push_back(md);
            }
            continue;
        }
        auto p = split_pipe(ln);
        if (p.size() < 5) continue;
        DayData d{};
        std::string ds = trim(p[0]);
        for (size_t i = 0; i < ds.size() && i < 11; ++i) d.date[i] = (wchar_t)ds[i];
        for (int j = 0; j < 4; ++j) { try { d.v[j] = std::stoll(trim(p[j+1])); } catch (...) {} }
        g_daily.push_back(d);
    }
}

static void build_info_lines() {
    g_ilines.clear();
    load_daily();
    g_info_h = PAD * 2
             + (!g_daily.empty()  ? CHART_SEC_H          : LH)
             + (!g_models.empty() ? DONUT_SEC_H + SH * 2 : 0);
}

static void draw_model_chart(HDC hdc, int y0) {
    if (g_models.empty()) return;
    int n = (int)g_models.size();

    // Title
    HFONT fn = makefont(14);
    SelectObject(hdc, fn);
    put(hdc, L"By Model", PAD, y0, C_TEXT);
    DeleteObject(fn);
    y0 += 22;

    long long total = 0;
    for (auto& m : g_models) total += m.total;
    if (total == 0) return;

    int cx = IW / 2, cy = y0 + DONUT_R_OUT + 10;
    int x1 = cx - DONUT_R_OUT, y1 = cy - DONUT_R_OUT;
    int x2 = cx + DONUT_R_OUT, y2 = cy + DONUT_R_OUT;

    SetArcDirection(hdc, AD_CLOCKWISE);

    if (n == 1) {
        HBRUSH br  = CreateSolidBrush(C_MODEL[0]);
        HPEN   pen = CreatePen(PS_SOLID, 1, C_BG);
        auto   ob  = static_cast<HBRUSH>(SelectObject(hdc, br));
        auto   op  = static_cast<HPEN>(SelectObject(hdc, pen));
        Ellipse(hdc, x1, y1, x2, y2);
        SelectObject(hdc, ob); SelectObject(hdc, op);
        DeleteObject(br); DeleteObject(pen);
    } else {
        static constexpr double PI = 3.14159265358979323846;
        double angle = -PI / 2.0;
        for (int i = 0; i < n; ++i) {
            double sweep    = static_cast<double>(g_models[i].total) / static_cast<double>(total) * 2.0 * PI;
            double end_ang  = angle + sweep;
            int sx = cx + static_cast<int>(std::lround(DONUT_R_OUT * std::cos(angle)));
            int sy = cy + static_cast<int>(std::lround(DONUT_R_OUT * std::sin(angle)));
            int ex = cx + static_cast<int>(std::lround(DONUT_R_OUT * std::cos(end_ang)));
            int ey = cy + static_cast<int>(std::lround(DONUT_R_OUT * std::sin(end_ang)));
            // Sub-pixel slice: if both radial endpoints round to the same point,
            // GDI Pie() draws the ENTIRE ellipse (painting the whole donut this
            // slice's color). Skip the draw but keep advancing the angle so the
            // remaining slices stay proportional.
            if (sx == ex && sy == ey) { angle = end_ang; continue; }
            HBRUSH br  = CreateSolidBrush(C_MODEL[i % 6]);
            HPEN   pen = CreatePen(PS_SOLID, 2, C_BG);
            auto   ob  = static_cast<HBRUSH>(SelectObject(hdc, br));
            auto   op  = static_cast<HPEN>(SelectObject(hdc, pen));
            Pie(hdc, x1, y1, x2, y2, sx, sy, ex, ey);
            SelectObject(hdc, ob); SelectObject(hdc, op);
            DeleteObject(br); DeleteObject(pen);
            angle = end_ang;
        }
    }

    SetArcDirection(hdc, AD_COUNTERCLOCKWISE);

    // Donut hole
    {
        HBRUSH br  = CreateSolidBrush(C_BG);
        HPEN   pen = CreatePen(PS_SOLID, 1, C_BG);
        auto   ob  = static_cast<HBRUSH>(SelectObject(hdc, br));
        auto   op  = static_cast<HPEN>(SelectObject(hdc, pen));
        Ellipse(hdc, cx-DONUT_R_IN, cy-DONUT_R_IN, cx+DONUT_R_IN, cy+DONUT_R_IN);
        SelectObject(hdc, ob); SelectObject(hdc, op);
        DeleteObject(br); DeleteObject(pen);
    }

    // Legend (2 items per row, model name with "claude-" prefix stripped)
    HFONT fs = makefont(11);
    SelectObject(hdc, fs);
    SetBkMode(hdc, TRANSPARENT);
    int ley  = cy + DONUT_R_OUT + 14;
    int colw = (IW - 2 * PAD) / 2;
    for (int i = 0; i < n; ++i) {
        int lx = PAD + (i % 2) * colw;
        int ly = ley + (i / 2) * 20;
        fillr(hdc, lx, ly + 4, 9, 9, C_MODEL[i % 6]);
        std::wstring name = g_models[i].name;
        if (name.starts_with(L"claude-")) name = name.substr(7);
        SetTextColor(hdc, C_DIM);
        TextOutW(hdc, lx + 13, ly + 1, name.c_str(), (int)name.size());
    }
    DeleteObject(fs);
}

static void draw_chart(HDC hdc, int y0) {
    if (g_daily.empty()) return;
    int n   = (int)g_daily.size();
    int cx0 = PAD, cx1 = IW - PAD, cw = cx1 - cx0;

    // Title
    HFONT fn = makefont(14);
    SelectObject(hdc, fn);
    put(hdc, L"Daily Token Usage — Last 30 Days", cx0, y0, C_TEXT);
    DeleteObject(fn);
    y0 += 22;

    // Legend
    static const wchar_t* const LBL[4] = { L"Input", L"Output", L"Cache Read", L"Cache Creation" };
    HFONT fs = makefont(11);
    SelectObject(hdc, fs);
    SetBkMode(hdc, TRANSPARENT);
    int lx = cx0;
    for (int i = 0; i < 4; ++i) {
        fillr(hdc, lx, y0 + 4, 9, 9, C_CHART[i]);
        SIZE sz{}; GetTextExtentPoint32W(hdc, LBL[i], (int)wcslen(LBL[i]), &sz);
        SetTextColor(hdc, C_DIM);
        TextOutW(hdc, lx + 12, y0 + 1, LBL[i], (int)wcslen(LBL[i]));
        lx += 12 + sz.cx + 16;
    }
    y0 += 22;

    // Bar area bounds
    int btop = y0, bbot = y0 + CHART_H_BARS;

    // Grid lines
    {
        HPEN gp = CreatePen(PS_SOLID, 1, RGB(48, 48, 48));
        HPEN op = (HPEN)SelectObject(hdc, gp);
        for (int i = 1; i <= 3; ++i) {
            int gy = btop + CHART_H_BARS * i / 4;
            MoveToEx(hdc, cx0, gy, nullptr); LineTo(hdc, cx1, gy);
        }
        SelectObject(hdc, op); DeleteObject(gp);
    }
    // X-axis
    {
        HPEN xp = CreatePen(PS_SOLID, 1, C_DIM);
        HPEN op = (HPEN)SelectObject(hdc, xp);
        MoveToEx(hdc, cx0, bbot, nullptr); LineTo(hdc, cx1, bbot);
        SelectObject(hdc, op); DeleteObject(xp);
    }

    // Per-metric maxima (for independent normalisation)
    long long mx[4] = {};
    for (auto& d : g_daily)
        for (int j = 0; j < 4; ++j)
            mx[j] = std::max(mx[j], d.v[j]);

    int gw = cw / n;
    int bw = std::max(1, (gw - 3) / 4);

    // How often to show date labels (avoid overlap: "05/28" ≈ 32px wide)
    int skip = std::max(1, (32 + gw - 1) / gw);

    SetBkMode(hdc, TRANSPARENT);
    SetTextAlign(hdc, TA_CENTER | TA_TOP);

    for (int i = 0; i < n; ++i) {
        int gx = cx0 + i * gw;

        for (int j = 0; j < 4; ++j) {
            if (mx[j] == 0) continue;
            int bh = (int)((double)g_daily[i].v[j] / (double)mx[j] * CHART_H_BARS);
            if (bh < 1) continue;
            fillr(hdc, gx + j * (bw + 1), bbot - bh, bw, bh, C_CHART[j]);
        }

        if (i % skip == 0) {
            const wchar_t* d = g_daily[i].date;  // "2026-05-28"
            wchar_t lbl[6]{};
            if (wcslen(d) >= 10) {
                lbl[0] = d[5]; lbl[1] = d[6];  // MM
                lbl[2] = L'/';
                lbl[3] = d[8]; lbl[4] = d[9];  // DD
            }
            SelectObject(hdc, fs);
            SetTextColor(hdc, C_DIM);
            TextOutW(hdc, gx + gw / 2, bbot + 4, lbl, 5);
        }
    }

    SetTextAlign(hdc, TA_LEFT | TA_TOP);
    DeleteObject(fs);
}

LRESULT CALLBACK InfoWndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        SetWindowRgn(hw, CreateRoundRectRgn(0, 0, IW, g_info_h, 16, 16), FALSE);
        SetLayeredWindowAttributes(hw, 0, g_opacity, LWA_ALPHA);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hw, &ps);
        fillr(hdc, 0, 0, IW, g_info_h, C_BG);
        HFONT fn = makefont(14), fs = makefont(12);
        int y = PAD;
        for (auto& l : g_ilines) {
            if (l.spacer) { y += SH; continue; }
            SelectObject(hdc, l.small ? fs : fn);
            put(hdc, l.text.c_str(), PAD, y, l.col);
            y += LH;
        }
        draw_chart(hdc, y);
        draw_model_chart(hdc, y + (!g_daily.empty() ? CHART_SEC_H + SH * 2 : 0));
        DeleteObject(fn); DeleteObject(fs);
        EndPaint(hw, &ps);
        return 0;
    }
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
        DestroyWindow(hw);
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) DestroyWindow(hw);
        return 0;
    case WM_DESTROY:
        g_info = nullptr;
        return 0;
    default:
        return DefWindowProcW(hw, msg, wp, lp);
    }
}

// ─── Window procedure ─────────────────────────────────────────────────────────
LRESULT CALLBACK WndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        g_hwnd = hw;
        apply_mode(hw);   // sets size + rounded region for the current mode
        apply_opacity(hw);
        SetTimer(hw, 1, 60000, nullptr);   // auto-refresh every 1 minute
        start_load();
        return 0;

    case WM_TIMER:
        // Watchdog: if a load has been "in flight" far longer than any real load
        // could take (subprocess reads are capped at ~25s each), assume its
        // WM_DATA_READY was lost and clear the latch so refresh resumes.
        if (g_pending.load() > 0 && GetTickCount64() - g_load_start.load() > 120000ULL)
            g_pending.store(0);
        start_load();
        return 0;

    case WM_DATA_READY:
        g_pending.fetch_sub(1);
        InvalidateRect(hw, nullptr, FALSE);
        return 0;

    case WM_PAINT:
        paint(hw);
        return 0;

    case WM_LBUTTONDOWN:
        SetCapture(hw); g_dragging = true;
        g_drag0 = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        { RECT wr; GetWindowRect(hw, &wr); g_win0 = { wr.left, wr.top }; }
        return 0;

    case WM_MOUSEMOVE:
        if (g_dragging) {
            SetWindowPos(hw, nullptr,
                g_win0.x + GET_X_LPARAM(lp) - g_drag0.x,
                g_win0.y + GET_Y_LPARAM(lp) - g_drag0.y,
                0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }
        return 0;

    case WM_LBUTTONUP:
        ReleaseCapture();
        if (g_dragging) save_pos(hw);
        g_dragging = false;
        return 0;

    case WM_LBUTTONDBLCLK:
        // Double-click toggles between Simple and full mode (same as the
        // right-click menu's "Simple mode" item). The preceding down/up of the
        // double-click leaves g_dragging cleared, so cancel any stray capture.
        ReleaseCapture();
        g_dragging = false;
        g_simple = !g_simple;
        save_simple();
        apply_mode(hw);
        return 0;

    case WM_MEASUREITEM: {
        auto* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
        if (mis->CtlType != ODT_MENU) return DefWindowProcW(hw, msg, wp, lp);
        auto* it = reinterpret_cast<OdItem*>(mis->itemData);
        mis->itemHeight = (it && it->label) ? 26 : 8;
        mis->itemWidth  = 160;
        return TRUE;
    }

    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if (dis->CtlType != ODT_MENU) return DefWindowProcW(hw, msg, wp, lp);
        auto* it  = reinterpret_cast<OdItem*>(dis->itemData);
        HDC   dc  = dis->hDC;
        RECT  rc  = dis->rcItem;
        bool  sel = (dis->itemState & ODS_SELECTED) != 0;
        bool  chk = (dis->itemState & ODS_CHECKED)  != 0;

        fillr(dc, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
              sel ? C_BAR_BG : C_BG);

        if (!it || !it->label) {
            HPEN pen = CreatePen(PS_SOLID, 1, C_BAR_BG);
            HPEN old = static_cast<HPEN>(SelectObject(dc, pen));
            int  y   = (rc.top + rc.bottom) / 2;
            MoveToEx(dc, rc.left + 8, y, nullptr);
            LineTo(dc, rc.right - 8, y);
            SelectObject(dc, old); DeleteObject(pen);
        } else {
            SetBkMode(dc, TRANSPARENT);
            HFONT f    = makefont(14);
            auto  prev = static_cast<HFONT>(SelectObject(dc, f));
            if (chk) {
                SetTextColor(dc, sel ? C_TEXT : C_BAR_FG);
                RECT cr{ rc.left + 2, rc.top, rc.left + 20, rc.bottom };
                DrawTextW(dc, L"\u2713", 1, &cr, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
            }
            SetTextColor(dc, C_TEXT);
            RECT tr{ rc.left + 22, rc.top, it->has_arrow ? rc.right - 18 : rc.right - 4, rc.bottom };
            DrawTextW(dc, it->label, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
            if (it->has_arrow) {
                SetTextColor(dc, sel ? C_TEXT : C_DIM);
                RECT ar{ rc.right - 18, rc.top, rc.right - 2, rc.bottom };
                DrawTextW(dc, L"\u25BA", 1, &ar, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
            }
            SelectObject(dc, prev); DeleteObject(f);
        }
        return TRUE;
    }

    case WM_RBUTTONUP: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ClientToScreen(hw, &pt);

        g_od_n = 0;
        const struct { const wchar_t* label; BYTE alpha; } levels[] = {
            { L"25%", 64 }, { L"50%", 128 }, { L"75%", 191 }, { L"90%", 230 }, { L"100%", 255 },
        };

        HMENU op = CreatePopupMenu();
        for (int i = 0; i < 5; ++i) {
            UINT flags = MF_OWNERDRAW;
            if (g_opacity == levels[i].alpha) flags |= MF_CHECKED;
            AppendMenuW(op, flags, 100 + i, reinterpret_cast<LPCWSTR>(od(levels[i].label)));
        }

        HMENU m = CreatePopupMenu();
        AppendMenuW(m, MF_OWNERDRAW,                              1, reinterpret_cast<LPCWSTR>(od(L"Refresh now")));
        AppendMenuW(m, MF_OWNERDRAW | MF_POPUP, (UINT_PTR)op,       reinterpret_cast<LPCWSTR>(od(L"Opacity", true)));
        AppendMenuW(m, MF_OWNERDRAW,                              3, reinterpret_cast<LPCWSTR>(od(L"More info...")));
        AppendMenuW(m, MF_OWNERDRAW | (g_simple ? MF_CHECKED : 0), 4, reinterpret_cast<LPCWSTR>(od(L"Simple mode")));
        AppendMenuW(m, MF_OWNERDRAW | MF_GRAYED,                  0, reinterpret_cast<LPCWSTR>(od(nullptr)));
        AppendMenuW(m, MF_OWNERDRAW,                              2, reinterpret_cast<LPCWSTR>(od(L"Exit")));
        int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hw, nullptr);
        DestroyMenu(m);
        if (cmd == 1) start_load();
        else if (cmd == 2) DestroyWindow(hw);
        else if (cmd == 3) {
            if (g_info) { DestroyWindow(g_info); g_info = nullptr; }
            build_info_lines();
            RECT wr; GetWindowRect(hw, &wr);
            int ix = wr.left, iy = wr.bottom + 4;
            MONITORINFO mi{}; mi.cbSize = sizeof(mi);
            GetMonitorInfo(MonitorFromWindow(hw, MONITOR_DEFAULTTONEAREST), &mi);
            RECT& mr = mi.rcWork;
            if (ix + IW > mr.right)  ix = mr.right - IW;
            if (ix < mr.left)        ix = mr.left;
            if (iy + g_info_h > mr.bottom) iy = wr.top - g_info_h - 4;
            if (iy < mr.top)         iy = mr.top;
            g_info = CreateWindowExW(
                WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
                L"ClaudeUsageInfo", nullptr,
                WS_POPUP | WS_VISIBLE,
                ix, iy, IW, g_info_h,
                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (g_info) SetFocus(g_info);
        }
        else if (cmd == 4) {
            g_simple = !g_simple;
            save_simple();
            apply_mode(hw);
        }
        else if (cmd >= 100 && cmd <= 104) {
            g_opacity = levels[cmd - 100].alpha;
            apply_opacity(hw);
            save_opacity();
        }
        return 0;
    }

    case WM_DESTROY:
        save_pos(hw);
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hw, msg, wp, lp);
    }
}

// ─── Entry point ──────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int) {
    InitializeCriticalSection(&g_cs);
    find_python(); // cache on main thread before background loads start

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;  // CS_DBLCLKS → WM_LBUTTONDBLCLK
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hi;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"ClaudeUsageWidget";
    RegisterClassExW(&wc);

    WNDCLASSEXW wi{};
    wi.cbSize        = sizeof(wi);
    wi.style         = CS_HREDRAW | CS_VREDRAW;
    wi.lpfnWndProc   = InfoWndProc;
    wi.hInstance     = hi;
    wi.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wi.lpszClassName = L"ClaudeUsageInfo";
    RegisterClassExW(&wi);

    load_opacity();
    load_simple();

    int px, py;
    if (!load_pos(px, py))
        px = GetSystemMetrics(SM_CXSCREEN) - WW - 20, py = 40;

    CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        L"ClaudeUsageWidget", L"Claude Usage",
        WS_POPUP | WS_VISIBLE,
        px, py, WW, WH,
        nullptr, nullptr, hi, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DeleteCriticalSection(&g_cs);
    return 0;
}
