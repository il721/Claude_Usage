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

// ─── Palette — from DESIGN.md Modern Dark system ────────────────────────────
constexpr COLORREF C_BG     = RGB(30,  30,  30);   // background
constexpr COLORREF C_BAR_BG = RGB(60,  60,  60);    // button normal
constexpr COLORREF C_BAR_FG = RGB(43,  121, 194);   // #2B79C2 primary accent
constexpr COLORREF C_WARN   = RGB(194, 60,  44);    // over-limit red
constexpr COLORREF C_TEXT   = RGB(230, 230, 230);   // text primary
constexpr COLORREF C_DIM    = RGB(150, 150, 150);   // text secondary

constexpr int WW = 430, WH = 150;
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
static std::atomic<int> g_pending{ 0 };
static HWND             g_hwnd = nullptr;
static POINT            g_drag0, g_win0;
static bool             g_dragging = false;
static BYTE             g_opacity  = 255;            // 0=invisible, 255=opaque

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

    std::string out;
    char buf[4096]; DWORD n;
    while (ReadFile(pipe_r, buf, sizeof(buf), &n, nullptr) && n > 0)
        out.append(buf, n);

    WaitForSingleObject(pi.hProcess, 20000);
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

    std::string out; char buf[4096]; DWORD n;
    while (ReadFile(pr, buf, sizeof(buf), &n, nullptr) && n > 0) out.append(buf, n);
    WaitForSingleObject(pi.hProcess, 10000);
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

static void drawbar(HDC hdc, int x, int y, int w, double frac, bool warn = false) {
    fillr(hdc, x, y, w, BAR_H, C_BAR_BG);
    int fw = static_cast<int>(w * std::clamp(frac, 0.0, 1.0));
    if (fw > 0) fillr(hdc, x, y, fw, BAR_H, warn ? C_WARN : C_BAR_FG);
}

static HFONT makefont(int sz) {
    return CreateFontW(sz, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
}

static void put(HDC hdc, const wchar_t* s, int x, int y, COLORREF c) {
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, c);
    TextOutW(hdc, x, y, s, static_cast<int>(wcslen(s)));
}

static void paint(HWND hw) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hw, &ps);

    fillr(hdc, 0, 0, WW, WH, C_BG);



    Metrics m;
    EnterCriticalSection(&g_cs);
    m = g_m;
    LeaveCriticalSection(&g_cs);

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

    drawbar(hdc, PAD, y + 1, BAR_W, m.row1_frac);
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
    drawbar(hdc, PAD, y + 1, BAR_W, m.row2_frac, over);
    put(hdc, m.row2_right.c_str(), PAD + BAR_W + 8, y, over ? C_WARN : C_TEXT);
    y += BAR_H + 6;

    SelectObject(hdc, fs);
    put(hdc, m.row2_sub.c_str(), PAD, y, C_DIM);

    DeleteObject(fn); DeleteObject(fs);
    EndPaint(hw, &ps);
}

// ─── Window procedure ─────────────────────────────────────────────────────────
LRESULT CALLBACK WndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        g_hwnd = hw;
        SetWindowRgn(hw, CreateRoundRectRgn(0, 0, WW, WH, 16, 16), FALSE);
        apply_opacity(hw);
        SetTimer(hw, 1, 60000, nullptr);
        start_load();
        return 0;

    case WM_TIMER:
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

    case WM_RBUTTONUP: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ClientToScreen(hw, &pt);

        // Opacity submenu (command IDs 100…105 → 25%…100%)
        HMENU op = CreatePopupMenu();
        const struct { const wchar_t* label; BYTE alpha; } levels[] = {
            { L"25%",  64  },
            { L"50%",  128 },
            { L"75%",  191 },
            { L"90%",  230 },
            { L"100%", 255 },
        };
        for (int i = 0; i < 5; ++i) {
            UINT flags = MF_STRING;
            if (g_opacity == levels[i].alpha) flags |= MF_CHECKED;
            AppendMenuW(op, flags, 100 + i, levels[i].label);
        }

        HMENU m = CreatePopupMenu();
        AppendMenuW(m, MF_STRING,             1, L"Refresh now");
        AppendMenuW(m, MF_POPUP, reinterpret_cast<UINT_PTR>(op), L"Opacity");
        AppendMenuW(m, MF_SEPARATOR,          0, nullptr);
        AppendMenuW(m, MF_STRING,             2, L"Exit");
        int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hw, nullptr);
        DestroyMenu(m);
        if (cmd == 1) start_load();
        else if (cmd == 2) DestroyWindow(hw);
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
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hi;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"ClaudeUsageWidget";
    RegisterClassExW(&wc);

    load_opacity();

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
