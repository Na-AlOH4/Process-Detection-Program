// ============================================================================
//  设置程序（进程检测实用工具 - 配置程序）
//  - 读取 / 写入可执行文件同目录下的 Settings.ini
//  - 背景图片 bg.jpg / bg.png / bg.bmp（与可执行文件同目录）
//  - 半透明白色面板 + 左侧“普通 / 高级”紧密排列的小选项卡 + 右侧滚动条
//  - 底部统一“确定 / 取消”按钮
//  - 主程序按通配符“进程检测实用工具*.exe”查找，避免硬编码版本号
//
//  编译环境：Visual Studio 2022，C++20，字符集使用 Unicode
//  本文件以 UTF-8(BOM) 保存，可直接在 VS2022 中编译；依赖 Windows API 与 GDI+。
//
//  显示可靠性说明（重要）：
//  除“文本输入框 EDIT”与“滚动条”外，所有标签、复选框、颜色预览、按钮、选项卡
//  均直接在主窗口画布上绘制（不使用子控件），因此不会出现“控件被背景覆盖而不显示”
//  的问题；背景（bg + 半透明面板）一次性预渲染为缓存位图，重绘时只做 BitBlt 与
//  文字绘制，滚动流畅、不闪烁。
// ============================================================================

#define _WIN32_WINNT 0x0600
#define NTDDI_VERSION 0x06000000
#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <gdiplus.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <cwctype>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include "Resource.h"
#include <objbase.h>
#pragma comment(lib, "ole32.lib")

#include <tlhelp32.h>
#pragma comment(lib, "kernel32.lib")

using namespace std;

// ----------------------------------------------------------------------------
//  控件 ID
// ----------------------------------------------------------------------------
#define IDC_TAB_NORMAL        201   // 左侧“普通”选项卡
#define IDC_TAB_ADV           202   // 左侧“高级”选项卡
#define IDC_SCROLLBAR         203   // 右侧滚动条

// 普通 - 颜色设置
#define IDC_LBL_COLOR_HEAD    300
#define IDC_LBL_COLOR(i)      (301 + (i))   // 301..303
#define IDC_PV_COLOR(i)       (304 + (i))   // 304..306 颜色预览块
#define IDC_ED_R(i)           (307 + (i)*3) // 307/310/313
#define IDC_ED_G(i)           (308 + (i)*3) // 308/311/314
#define IDC_ED_B(i)           (309 + (i)*3) // 309/312/315
#define IDC_BTN_APPLY(i)      (316 + (i))   // 316..318 “更新”按钮

// 普通 - 检测时间间隔
#define IDC_LBL_TIME_HEAD     320
#define IDC_STATIC_CUR_INT    321
#define IDC_LBL_MODIFY        322
#define IDC_EDIT_INTERVAL     323

// 普通 - 目标进程名称
#define IDC_LBL_TARGET_HEAD   324
#define IDC_LBL_TARGET        325
#define IDC_EDIT_TARGET       326

// 普通 - 程序当前版本
#define IDC_LBL_VER_HEAD      327
#define IDC_STATIC_VERSION    328

// 高级 - 搜索设置
#define IDC_LBL_SEARCH_HEAD   330
#define IDC_LBL_BFS           331
#define IDC_CHK_BFS           332
#define IDC_LBL_SPATH         333
#define IDC_EDIT_SPATH        334

// 高级 - 用户级别
#define IDC_LBL_LEVEL_HEAD    335
#define IDC_LBL_LEVEL         336
#define IDC_EDIT_LEVEL        337
#define IDC_LBL_HINT1         338
#define IDC_LBL_HINT2         339

// 高级 - 执行行为
#define IDC_LBL_ACTION_HEAD   340
#define IDC_LBL_ACTION(i)     (341 + (i))   // 341..347
#define IDC_CHK_ACTION(i)     (351 + (i))   // 351..357

// 高级 - 权限设置
#define IDC_LBL_RIGHTS_HEAD   360
#define IDC_LBL_RIGHTS(i)     (361 + (i))   // 361..363
#define IDC_CHK_RIGHTS(i)     (371 + (i))   // 371..373

// 高级 - 日志设置
#define IDC_LBL_LOG_HEAD      380
#define IDC_LBL_CLEARLOG      381
#define IDC_CHK_CLEARLOG      382

// 高级 - 通信程序
#define IDC_LBL_COMM_HEAD     383
#define IDC_LBL_COMM          384
#define IDC_CHK_COMM          385

// ----------------------------------------------------------------------------
//  全局数据（对应 Settings.ini 各节）
// ----------------------------------------------------------------------------
COLORREF g_colBtn = RGB(70, 130, 220);    // [Color] COLOR_BTN
COLORREF g_colBg = RGB(195, 255, 255);   // [Color] COLOR_OUTPUT_BG
COLORREF g_colText = RGB(0, 0, 0);         // [Color] COLOR_OUTPUT_TEXT
int      g_interval = 30;                   // [TimeInterval] Interval
wstring  g_targetProgram = L"Notepad.exe";   // [Target] TargetProgram
int      g_bfs = 0;                         // [Path] BFS
wstring  g_specifiedPath = L"C:\\Program Files (x86)"; // [Path] SpecifiedPath
int      g_userLevel = 0;                   // [UserLevel] UserLevel

// [Action] 依次对应 7 项
int g_action[7] = { 1, 0, 0, 0, 0, 0, 0 };
// [Rights] 依次对应 3 项
int g_rights[3] = { 0, 0, 0 };

int      g_clearLogs = 0;                   // [ClearLogs] ClearLogs
int      g_guiComm = 1;                     // [CommunicationProgram] GUI
wstring  g_version = L"1.2.0";              // [Version] Version

// 主程序通配符（不再硬编码版本号）
const wchar_t* MAIN_PROGRAM_PATTERN = L"进程检测实用工具*.exe";

// ----------------------------------------------------------------------------
//  全局句柄 / 状态
// ----------------------------------------------------------------------------
HWND     hWndMain = nullptr;
HWND     g_hScroll = nullptr;
HINSTANCE g_hInst = nullptr;
wstring  g_exeDir;                           // 可执行文件所在目录

HFONT hFontNormal = nullptr;                 // 普通字体 Consolas
HFONT hFontHeading = nullptr;                // 小标题字体（略大、加粗）
HFONT hFontSmall = nullptr;                  // 提示文本字体（略小）
HFONT hFontTab = nullptr;                    // 选项卡字体

using Gdiplus::Image;
Image* g_bgImage = nullptr;
IStream* g_bgStream = nullptr;   // 保活：Image 存活期间流不能释放
ULONG_PTR g_gdiplusToken = 0;

int g_tab = 0;                               // 0=普通 1=高级
int g_scrollY = 0;                           // 当前滚动偏移

// 内容区几何（像素）
int g_contentX = 84;
int g_contentTop = 12;
int g_contentBottom = 200;
int g_contentH[2] = { 0, 0 };

// ----------------------------------------------------------------------------
//  画布元素（直接绘制，非子控件）
// ----------------------------------------------------------------------------
enum ElemKind { DK_LABEL, DK_CHECK, DK_COLOR, DK_BUTTON };
struct DrawItem {
    int     tab;                 // 0=普通 1=高级
    int     x, y, w, h;          // 内容坐标
    ElemKind kind;
    int     id;
    int     state;               // 复选框状态（0/1）
    wstring text;
    HFONT   font;
};
vector<DrawItem> g_draw;

// ----------------------------------------------------------------------------
//  文本输入框（唯一的“内容”子控件）
// ----------------------------------------------------------------------------
struct EditItem {
    HWND hwnd;
    int  tab;
    int  x, y, w, h;
};
vector<EditItem> g_edits;

// ----------------------------------------------------------------------------
//  背景缓存（bg + 半透明面板，预渲染一次）
// ----------------------------------------------------------------------------
HBITMAP g_bgCache = nullptr;
int     g_cacheW = 0, g_cacheH = 0;

// ----------------------------------------------------------------------------
//  字符串辅助函数
// ----------------------------------------------------------------------------
static wstring TrimW(const wstring& s) {
    size_t b = s.find_first_not_of(L" \t\r\n");
    if (b == wstring::npos) return L"";
    size_t e = s.find_last_not_of(L" \t\r\n");
    return s.substr(b, e - b + 1);
}

static string TrimA(const string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static wstring AnsiToWide(const char* s, UINT cp = CP_ACP) {
    if (!s) return L"";
    int len = MultiByteToWideChar(cp, 0, s, -1, nullptr, 0);
    if (len <= 0) return L"";
    wstring w(len - 1, L'\0');
    MultiByteToWideChar(cp, 0, s, -1, &w[0], len);
    return w;
}

static string WideToAnsi(const wstring& w, UINT cp = CP_ACP) {
    if (w.empty()) return "";
    int len = WideCharToMultiByte(cp, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    string s(len - 1, '\0');
    WideCharToMultiByte(cp, 0, w.c_str(), -1, &s[0], len, nullptr, nullptr);
    return s;
}

static bool IEqualsA(const string& a, const char* b) {
    return _stricmp(a.c_str(), b) == 0;
}

static wstring ToLowerW(wstring s) {
    for (wchar_t& c : s) c = (wchar_t)towlower(c);
    return s;
}

// ----------------------------------------------------------------------------
//  TaskDialog 封装
// ----------------------------------------------------------------------------
static int ShowTaskDialog(HWND hwndParent, const wchar_t* title, const wchar_t* heading,
    const wchar_t* content, TASKDIALOG_COMMON_BUTTON_FLAGS buttons,
    PCWSTR icon = TD_ERROR_ICON, int defaultButton = IDOK) {
    TASKDIALOGCONFIG config = { sizeof(config) };
    config.hwndParent = hwndParent;
    config.pszWindowTitle = title;
    config.pszMainInstruction = heading;
    config.pszContent = content;
    config.pszMainIcon = icon;
    config.dwCommonButtons = buttons;
    config.nDefaultButton = defaultButton;
    int result = 0;
    TaskDialogIndirect(&config, &result, nullptr, nullptr);
    return result;
}

// ----------------------------------------------------------------------------
//  读取 Settings.ini（按节 / 键=值 解析，保持原有风格）
// ----------------------------------------------------------------------------
static void LoadSettingsFromFile() {
    wstring path = g_exeDir + L"\\Settings.ini";
    FILE* f = _wfopen(path.c_str(), L"r");
    if (!f) return;

    char line[1024];
    char section[64] = "";
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        char* p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == 0 || *p == '#' || *p == ';') continue;   // 跳过注释、空行

        if (*p == '[') {
            char* e = strchr(p, ']');
            if (e) { *e = 0; strncpy(section, p + 1, sizeof(section) - 1); section[sizeof(section) - 1] = 0; }
            continue;
        }

        char* eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        string key = TrimA(p);
        string val = TrimA(eq + 1);

        int r, g, b;
        if (IEqualsA(section, "Color")) {
            if (sscanf(val.c_str(), "RGB(%d,%d,%d)", &r, &g, &b) == 3) {
                COLORREF c = RGB(r, g, b);
                if (IEqualsA(key, "COLOR_BTN"))         g_colBtn = c;
                else if (IEqualsA(key, "COLOR_OUTPUT_BG"))   g_colBg = c;
                else if (IEqualsA(key, "COLOR_OUTPUT_TEXT")) g_colText = c;
            }
        }
        else if (IEqualsA(section, "TimeInterval")) {
            if (IEqualsA(key, "Interval")) g_interval = atoi(val.c_str());
        }
        else if (IEqualsA(section, "Target")) {
            if (IEqualsA(key, "TargetProgram")) g_targetProgram = AnsiToWide(val.c_str());
        }
        else if (IEqualsA(section, "Path")) {
            if (IEqualsA(key, "BFS")) g_bfs = atoi(val.c_str()) ? 1 : 0;
            else if (IEqualsA(key, "SpecifiedPath")) {
                string v = val;
                if (!v.empty() && v.front() == '"')  v.erase(v.begin());
                if (!v.empty() && v.back() == '"')  v.pop_back();
                g_specifiedPath = AnsiToWide(v.c_str());
            }
        }
        else if (IEqualsA(section, "UserLevel")) {
            if (IEqualsA(key, "UserLevel")) {
                wstring wv = AnsiToWide(val.c_str());
                if (!wv.empty() && iswdigit(wv[0])) {
                    int n = _wtoi(wv.c_str());
                    g_userLevel = (n >= 0 && n <= 3) ? n : 0;
                }
                else {
                    wstring low = ToLowerW(wv);
                    if (low == L"normal")        g_userLevel = 0;
                    else if (low == L"senior")        g_userLevel = 1;
                    else if (low == L"administrator") g_userLevel = 2;
                    else if (low == L"debug")         g_userLevel = 3;
                    else                              g_userLevel = 0;
                }
            }
        }
        else if (IEqualsA(section, "Action")) {
            if (IEqualsA(key, "LaunchCommandlineProgram")) g_action[0] = atoi(val.c_str()) ? 1 : 0;
            else if (IEqualsA(key, "RegAction"))                g_action[1] = atoi(val.c_str()) ? 1 : 0;
            else if (IEqualsA(key, "RecoverReg"))               g_action[2] = atoi(val.c_str()) ? 1 : 0;
            else if (IEqualsA(key, "SetDefenderRule"))          g_action[3] = atoi(val.c_str()) ? 1 : 0;
            else if (IEqualsA(key, "DeleteDefenderRule"))       g_action[4] = atoi(val.c_str()) ? 1 : 0;
            else if (IEqualsA(key, "BlockPort"))                g_action[5] = atoi(val.c_str()) ? 1 : 0;
            else if (IEqualsA(key, "RestorePort"))              g_action[6] = atoi(val.c_str()) ? 1 : 0;
        }
        else if (IEqualsA(section, "Rights")) {
            if (IEqualsA(key, "SystmRightKill"))      g_rights[0] = atoi(val.c_str()) ? 1 : 0;
            else if (IEqualsA(key, "LowPriorityRestart"))  g_rights[1] = atoi(val.c_str()) ? 1 : 0;
            else if (IEqualsA(key, "SystemRightRunCMD"))   g_rights[2] = atoi(val.c_str()) ? 1 : 0;
        }
        else if (IEqualsA(section, "ClearLogs")) {
            if (IEqualsA(key, "ClearLogs")) g_clearLogs = atoi(val.c_str()) ? 1 : 0;
        }
        else if (IEqualsA(section, "CommunicationProgram")) {
            if (IEqualsA(key, "GUI")) g_guiComm = atoi(val.c_str()) ? 1 : 0;
        }
        else if (IEqualsA(section, "Version")) {
            if (IEqualsA(key, "Version")) g_version = AnsiToWide(val.c_str());
        }
    }
    fclose(f);
}

// ----------------------------------------------------------------------------
//  写入 Settings.ini（完整写出，格式与给出的 Settings(拟) 保持一致）
// ----------------------------------------------------------------------------
static void PutLine(FILE* f, const wstring& w) {
    string s = WideToAnsi(w, CP_ACP);
    fwrite(s.data(), 1, s.size(), f);
}

static void SaveSettingsToFile() {
    wstring path = g_exeDir + L"\\Settings.ini";
    FILE* f = _wfopen(path.c_str(), L"w");
    if (!f) return;

    PutLine(f, L"[Color]\n");
    PutLine(f, L"#主程序个性化颜色配置\n");
    {
        wchar_t buf[128];
#define PUT_COLOR(name, col) \
        swprintf(buf, 128, L"COLORREF %ls = RGB(%d,%d,%d)\n", name, \
                 GetRValue(col), GetGValue(col), GetBValue(col)); \
        PutLine(f, buf)
        PUT_COLOR(L"COLOR_BTN", g_colBtn);
        PUT_COLOR(L"COLOR_OUTPUT_BG", g_colBg);
        PUT_COLOR(L"COLOR_OUTPUT_TEXT", g_colText);
    }

    PutLine(f, L"[TimeInterval]\n");
    PutLine(f, L"#自动检测时间间隔\n");
    PutLine(f, L"Interval = " + to_wstring(g_interval) + L"\n");

    PutLine(f, L"[Target]\n");
    PutLine(f, L"TargetProgram = " + g_targetProgram + L"\n");
    PutLine(f, L"#这里是你想要关闭的进程(程序)名称\n");

    PutLine(f, L"[Path]\n");
    PutLine(f, L"BFS = " + to_wstring(g_bfs) + L"\n");
    PutLine(f, L"#启用此选项以在常用目录中查找目标程序\n");
    PutLine(f, L"SpecifiedPath = \"" + g_specifiedPath + L"\"\n");
    PutLine(f, L"#默认路径为Program Files (x86)\n");

    PutLine(f, L"[UserLevel]\n");
    PutLine(f, L"#可用级别:0=普通用户(Normal);1=高级用户(Senior);2=管理员用户(Administrator);3=调试权限(Debug)\n");
    PutLine(f, L"#此项默认为0;若配置为无效项值,则按普通用户处理.\n");
    PutLine(f, L"UserLevel = " + to_wstring(g_userLevel) + L"\n");

    PutLine(f, L"[Action]\n");
    PutLine(f, L"#注意:此项的配置仅在设置为支持这些选项的用户级别时生效.\n");
    {
        const wchar_t* keys[7] = {
            L"LaunchCommandlineProgram", L"RegAction", L"RecoverReg",
            L"SetDefenderRule", L"DeleteDefenderRule", L"BlockPort", L"RestorePort" };
        const wchar_t* comments[7] = {
            L"#启动主程序时同时启动命令行辅助程序\n",
            L"#使用命令行禁用注册表编辑器\n",
            L"#使用命令行恢复对注册表编辑器的访问\n",
            L"#创建Windows Defender 防火墙规则\n",
            L"#删除Windows Defender 防火墙规则\n",
            L"#阻止目标进程访问端口\n",
            L"#恢复目标进程对端口的访问\n" };
        for (int i = 0; i < 7; ++i) {
            PutLine(f, wstring(keys[i]) + L" = " + to_wstring(g_action[i]) + L"\n");
            PutLine(f, comments[i]);
        }
    }

    PutLine(f, L"[Rights]\n");
    PutLine(f, L"#注意:此项的配置仅在设置为支持这些选项的用户级别时生效.\n");
    {
        const wchar_t* keys[3] = { L"SystmRightKill", L"LowPriorityRestart", L"SystemRightRunCMD" };
        const wchar_t* comments[3] = {
            L"#使用ProcessManager以管理员权限结束进程\n",
            L"#以低(Low)优先级重启目标进程\n",
            L"#使用ProcessManager以管理员权限打开命令行\n" };
        for (int i = 0; i < 3; ++i) {
            PutLine(f, wstring(keys[i]) + L" = " + to_wstring(g_rights[i]) + L"\n");
            PutLine(f, comments[i]);
        }
    }

    PutLine(f, L"[ClearLogs]\n");
    PutLine(f, L"#每次启动主程序时清空日志\n");
    PutLine(f, L"ClearLogs = " + to_wstring(g_clearLogs) + L"\n");

    PutLine(f, L"[CommunicationProgram]\n");
    PutLine(f, L"GUI = " + to_wstring(g_guiComm) + L"\n");
    PutLine(f, L"#使用GUI版本的通信程序\n");

    PutLine(f, L"[Version]\n");
    PutLine(f, L"Version = " + g_version + L"\n");

    fclose(f);
}

// ----------------------------------------------------------------------------
//  用户级别输入框：子类化，只允许数字与英文字母（不支持汉字输入）
// ----------------------------------------------------------------------------
WNDPROC g_oldLevelEditProc = nullptr;
LRESULT CALLBACK LevelEditProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CHAR || msg == WM_IME_CHAR) {
        wchar_t c = (wchar_t)wParam;
        if (c >= 0x20 && c != 0x7f) {
            bool allowed = (c >= L'0' && c <= L'9') ||
                (c >= L'a' && c <= L'z') ||
                (c >= L'A' && c <= L'Z');
            if (!allowed) return 0;
        }
    }
    return CallWindowProcW(g_oldLevelEditProc, hWnd, msg, wParam, lParam);
}

// ----------------------------------------------------------------------------
//  解析用户级别：支持数字与英文名称（不区分大小写），无效值按 0 处理
// ----------------------------------------------------------------------------
static int ParseUserLevel(wstring s) {
    s = TrimW(s);
    if (s.empty()) return 0;
    bool allDigits = true;
    for (wchar_t c : s) if (!iswdigit(c)) { allDigits = false; break; }
    if (allDigits) {
        int n = _wtoi(s.c_str());
        return (n >= 0 && n <= 3) ? n : 0;
    }
    wstring low = ToLowerW(s);
    if (low == L"normal")        return 0;
    else if (low == L"senior")        return 1;
    else if (low == L"administrator") return 2;
    else if (low == L"debug")         return 3;
    return 0;
}

// ----------------------------------------------------------------------------
//  画布元素 / 输入框创建辅助
// ----------------------------------------------------------------------------
static void AddLabel(int tab, int x, int y, int w, int h, const wstring& text,
    int id, HFONT font = nullptr) {
    g_draw.push_back({ tab, x, y, w, h, DK_LABEL, id, 0, text,
                       font ? font : hFontNormal });
}
static void AddCheck(int tab, int x, int y, int w, int h, int id, int state) {
    g_draw.push_back({ tab, x, y, w, h, DK_CHECK, id, state, L"", hFontNormal });
}
static void AddColor(int tab, int x, int y, int w, int h, int id) {
    g_draw.push_back({ tab, x, y, w, h, DK_COLOR, id, 0, L"", hFontNormal });
}
static void AddButton(int tab, int x, int y, int w, int h, const wstring& text, int id) {
    g_draw.push_back({ tab, x, y, w, h, DK_BUTTON, id, 0, text, hFontNormal });
}
static HWND AddEdit(int tab, int x, int y, int w, int h, int id) {
    HWND hwnd = CreateWindowW(L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
        x, y, w, h, hWndMain, (HMENU)(INT_PTR)id, g_hInst, nullptr);
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)hFontNormal, TRUE);
    g_edits.push_back({ hwnd, tab, x, y, w, h });
    return hwnd;
}

// ----------------------------------------------------------------------------
//  创建全部画布元素与输入框
// ----------------------------------------------------------------------------
static void CreateUI() {
    // ===================== 普通 =====================
    // [1] 颜色设置
    AddLabel(0, 0, 0, 300, 24, L"颜色设置", IDC_LBL_COLOR_HEAD, hFontHeading);
    const wchar_t* colorNames[3] = { L"按钮颜色:", L"输出框颜色:", L"文字颜色:" };
    for (int i = 0; i < 3; ++i) {
        int ry = 28 + i * 28;
        AddLabel(0, 0, ry, 80, 26, colorNames[i], IDC_LBL_COLOR(i));
        AddColor(0, 84, ry + 1, 24, 24, IDC_PV_COLOR(i));
        AddEdit(0, 112, ry, 30, 24, IDC_ED_R(i));
        AddEdit(0, 146, ry, 30, 24, IDC_ED_G(i));
        AddEdit(0, 180, ry, 30, 24, IDC_ED_B(i));
        AddButton(0, 214, ry, 50, 26, L"更新", IDC_BTN_APPLY(i));
    }

    // [2] 检测时间间隔
    AddLabel(0, 0, 116, 300, 24, L"检测时间间隔", IDC_LBL_TIME_HEAD, hFontHeading);
    {
        wchar_t buf[32];
        swprintf(buf, 32, L"现有:%ds", g_interval);
        AddLabel(0, 0, 144, 90, 24, buf, IDC_STATIC_CUR_INT);
    }
    AddLabel(0, 96, 144, 48, 24, L"修改为:", IDC_LBL_MODIFY);
    AddEdit(0, 148, 144, 60, 24, IDC_EDIT_INTERVAL);

    // [3] 目标进程名称
    AddLabel(0, 0, 176, 300, 24, L"目标进程名称", IDC_LBL_TARGET_HEAD, hFontHeading);
    AddLabel(0, 0, 204, 100, 24, L"目标进程名称", IDC_LBL_TARGET);
    AddEdit(0, 104, 204, 248, 24, IDC_EDIT_TARGET);

    // [4] 程序当前版本
    AddLabel(0, 0, 236, 300, 24, L"程序当前版本", IDC_LBL_VER_HEAD, hFontHeading);
    AddLabel(0, 0, 264, 300, 24, L"当前版本: " + g_version, IDC_STATIC_VERSION);

    // ===================== 高级 =====================
    // [1] 搜索设置
    AddLabel(1, 0, 0, 300, 24, L"搜索设置", IDC_LBL_SEARCH_HEAD, hFontHeading);
    AddLabel(1, 0, 28, 180, 24, L"启用搜索", IDC_LBL_BFS);
    AddCheck(1, 186, 28, 20, 24, IDC_CHK_BFS, g_bfs);
    AddLabel(1, 0, 56, 80, 24, L"搜索路径:", IDC_LBL_SPATH);
    AddEdit(1, 86, 56, 266, 24, IDC_EDIT_SPATH);

    // [2] 用户级别
    AddLabel(1, 0, 90, 300, 24, L"用户级别", IDC_LBL_LEVEL_HEAD, hFontHeading);
    AddLabel(1, 0, 118, 80, 24, L"用户级别:", IDC_LBL_LEVEL);
    AddEdit(1, 86, 118, 100, 24, IDC_EDIT_LEVEL);
    AddLabel(1, 0, 148, 354, 18,
        L"可用级别:0=普通用户(Normal);1=高级用户(Senior);",
        IDC_LBL_HINT1, hFontSmall);
    AddLabel(1, 0, 166, 354, 18,
        L"2=管理员用户(Administrator);3=调试权限(Debug)",
        IDC_LBL_HINT2, hFontSmall);

    // [3] 执行行为
    AddLabel(1, 0, 194, 300, 24, L"执行行为", IDC_LBL_ACTION_HEAD, hFontHeading);
    const wchar_t* actionLabels[7] = {
        L"启动命令行辅助程序", L"注册表操作", L"恢复注册表项", L"创建防火墙规则",
        L"删除防火墙规则", L"阻止端口访问", L"恢复端口访问" };
    for (int i = 0; i < 7; ++i) {
        int y = 222 + i * 28;
        AddLabel(1, 0, y, 200, 24, actionLabels[i], IDC_LBL_ACTION(i));
        AddCheck(1, 206, y, 20, 24, IDC_CHK_ACTION(i), g_action[i]);
    }

    // [4] 权限设置
    AddLabel(1, 0, 418, 300, 24, L"权限设置", IDC_LBL_RIGHTS_HEAD, hFontHeading);
    const wchar_t* rightsLabels[3] = {
        L"以管理员权限结束进程", L"以低优先级重启进程", L"以管理员权限打开命令行" };
    for (int i = 0; i < 3; ++i) {
        int y = 446 + i * 28;
        AddLabel(1, 0, y, 200, 24, rightsLabels[i], IDC_LBL_RIGHTS(i));
        AddCheck(1, 206, y, 20, 24, IDC_CHK_RIGHTS(i), g_rights[i]);
    }

    // [5] 清除日志
    AddLabel(1, 0, 530, 300, 24, L"日志设置", IDC_LBL_LOG_HEAD, hFontHeading);
    AddLabel(1, 0, 558, 200, 24, L"自动清理日志", IDC_LBL_CLEARLOG);
    AddCheck(1, 206, 558, 20, 24, IDC_CHK_CLEARLOG, g_clearLogs);

    // [6] 通信程序
    AddLabel(1, 0, 590, 300, 24, L"通信程序", IDC_LBL_COMM_HEAD, hFontHeading);
    AddLabel(1, 0, 618, 200, 24, L"使用图形化版本", IDC_LBL_COMM);
    AddCheck(1, 206, 618, 20, 24, IDC_CHK_COMM, g_guiComm);

    // ---- 输入框初始值（启动时显示配置文件中的值）----
    for (int i = 0; i < 3; ++i) {
        COLORREF c = (i == 0) ? g_colBtn : (i == 1) ? g_colBg : g_colText;
        SetDlgItemInt(hWndMain, IDC_ED_R(i), GetRValue(c), FALSE);
        SetDlgItemInt(hWndMain, IDC_ED_G(i), GetGValue(c), FALSE);
        SetDlgItemInt(hWndMain, IDC_ED_B(i), GetBValue(c), FALSE);
    }
    SetWindowTextW(GetDlgItem(hWndMain, IDC_EDIT_TARGET), g_targetProgram.c_str());
    SetWindowTextW(GetDlgItem(hWndMain, IDC_EDIT_SPATH), g_specifiedPath.c_str());
    SetDlgItemInt(hWndMain, IDC_EDIT_LEVEL, g_userLevel, FALSE);

    // 用户级别输入框子类化
    g_oldLevelEditProc = (WNDPROC)SetWindowLongPtrW(
        GetDlgItem(hWndMain, IDC_EDIT_LEVEL), GWLP_WNDPROC, (LONG_PTR)LevelEditProc);
}

// ----------------------------------------------------------------------------
//  背景缓存：bg + 半透明面板（窗口尺寸变化时重建一次）
// ----------------------------------------------------------------------------
static void BuildCache(int cw, int ch) {
    if (g_bgCache && g_cacheW == cw && g_cacheH == ch) return;
    if (g_bgCache) { DeleteObject(g_bgCache); g_bgCache = nullptr; }

    HDC screen = GetDC(nullptr);
    g_bgCache = CreateCompatibleBitmap(screen, cw, ch);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP old = (HBITMAP)SelectObject(mem, g_bgCache);

    {
        Gdiplus::Graphics gfx(mem);
        gfx.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        if (g_bgImage) {
            gfx.DrawImage(g_bgImage, 0, 0, cw, ch);
        }
        else {
            Gdiplus::SolidBrush fb(Gdiplus::Color(232, 238, 248));
            gfx.FillRectangle(&fb, 0, 0, cw, ch);
        }
        // 半透明白色面板（alpha=150）
        Gdiplus::SolidBrush pn(Gdiplus::Color(150, 255, 255, 255));
        gfx.FillRectangle(&pn, 8, 8, cw - 40, ch - 16);
    }

    SelectObject(mem, old);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    g_cacheW = cw; g_cacheH = ch;
}

// ----------------------------------------------------------------------------
//  绘制：选项卡 / 底部按钮（轻量矩形）
// ----------------------------------------------------------------------------
static void PaintRectButton(HDC hdc, const RECT& r, const wchar_t* text,
    bool fill, HFONT font) {
    HBRUSH b = CreateSolidBrush(fill ? RGB(228, 228, 228) : RGB(255, 255, 255));
    FillRect(hdc, &r, b);
    DeleteObject(b);
    FrameRect(hdc, &r, (HBRUSH)GetStockObject(GRAY_BRUSH));
    HFONT oldF = (HFONT)SelectObject(hdc, font);
    DrawTextW(hdc, text, -1, const_cast<RECT*>(&r),
        DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldF);
}

// ----------------------------------------------------------------------------
//  绘制：当前选项卡的画布元素（标签 / 复选框 / 颜色预览 / 按钮）
// ----------------------------------------------------------------------------
static void PaintContent(HDC hdc) {
    SetBkMode(hdc, TRANSPARENT);
    for (const DrawItem& it : g_draw) {
        if (it.tab != g_tab) continue;
        int top = g_contentTop + it.y - g_scrollY;
        int bot = top + it.h;
        if (bot < g_contentTop || top > g_contentBottom) continue;

        int x = g_contentX + it.x;
        HFONT useFont = it.font ? it.font : hFontNormal;
        HFONT oldF = (HFONT)SelectObject(hdc, useFont);

        if (it.kind == DK_LABEL) {
            RECT rc = { x, top, x + it.w, bot };
            DrawTextW(hdc, it.text.c_str(), -1, &rc,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        else if (it.kind == DK_CHECK) {
            const int box = 16;
            int by = top + (it.h - box) / 2;
            RECT brc = { x, by, x + box, by + box };
            UINT st = DFCS_BUTTONCHECK;
            if (it.state) st |= DFCS_CHECKED;
            DrawFrameControl(hdc, &brc, DFC_BUTTON, st);
        }
        else if (it.kind == DK_COLOR) {
            COLORREF c;
            if (it.id == IDC_PV_COLOR(0)) c = g_colBtn;
            else if (it.id == IDC_PV_COLOR(1)) c = g_colBg;
            else                               c = g_colText;
            RECT rc = { x, top, x + it.w, bot };
            HBRUSH b = CreateSolidBrush(c);
            FillRect(hdc, &rc, b);
            DeleteObject(b);
            FrameRect(hdc, &rc, (HBRUSH)GetStockObject(GRAY_BRUSH));
        }
        else { // DK_BUTTON
            RECT rc = { x, top, x + it.w, bot };
            HBRUSH b = CreateSolidBrush(RGB(255, 255, 255));
            FillRect(hdc, &rc, b);
            DeleteObject(b);
            FrameRect(hdc, &rc, (HBRUSH)GetStockObject(GRAY_BRUSH));
            DrawTextW(hdc, it.text.c_str(), -1, &rc,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        SelectObject(hdc, oldF);
    }
}

// ----------------------------------------------------------------------------
//  输入框定位（仅显示当前页、内容区内的输入框）
// ----------------------------------------------------------------------------
static void PositionEdits() {
    // 批量移动（DeferWindowPos + SWP_NOREDRAW），避免逐个控件同步重绘导致卡顿
    HDWP dwp = BeginDeferWindowPos((int)g_edits.size());
    if (dwp) {
        for (const EditItem& it : g_edits) {
            int top = g_contentTop + it.y - g_scrollY;
            dwp = DeferWindowPos(dwp, it.hwnd, nullptr,
                g_contentX + it.x, top, it.w, it.h,
                SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
        }
        EndDeferWindowPos(dwp);
    }
    // 再统一显示 / 隐藏
    for (const EditItem& it : g_edits) {
        int top = g_contentTop + it.y - g_scrollY;
        int bot = top + it.h;
        bool show = (it.tab == g_tab) &&
            (top >= g_contentTop) && (bot <= g_contentBottom);
        ShowWindow(it.hwnd, show ? SW_SHOWNOACTIVATE : SW_HIDE);
    }
}

// 仅重绘内容区（而非整个窗口），减少闪烁与工作量
static void InvalidateContent() {
    RECT rc = { g_contentX, g_contentTop, g_contentX + 360, g_contentBottom };
    InvalidateRect(hWndMain, &rc, FALSE);
}

// ----------------------------------------------------------------------------
//  滚动信息
// ----------------------------------------------------------------------------
static void UpdateScrollInfo() {
    int viewport = g_contentBottom - g_contentTop;
    SCROLLINFO si = { sizeof(si) };
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = g_contentH[g_tab] - 1;
    si.nPage = (UINT)viewport;
    si.nPos = g_scrollY;
    SetScrollInfo(g_hScroll, SB_CTL, &si, TRUE);
}

// ----------------------------------------------------------------------------
//  布局
// ----------------------------------------------------------------------------
static void Arrange() {
    RECT rc;
    GetClientRect(hWndMain, &rc);
    int cw = rc.right, ch = rc.bottom;

    // 右侧滚动条
    SetWindowPos(g_hScroll, nullptr, cw - 24, 8, 16, ch - 16,
        SWP_NOZORDER | SWP_NOACTIVATE);

    // 内容区
    g_contentX = 84;
    g_contentTop = 12;
    g_contentBottom = ch - 50;

    // 统计内容高度（画布元素）
    g_contentH[0] = g_contentH[1] = 0;
    for (const DrawItem& it : g_draw) {
        int bottom = it.y + it.h;
        if (bottom > g_contentH[it.tab]) g_contentH[it.tab] = bottom;
    }
    g_contentH[0] += 4;
    g_contentH[1] += 4;

    // 限制滚动范围
    int viewport = g_contentBottom - g_contentTop;
    int maxScroll = max(0, g_contentH[g_tab] - viewport);
    if (g_scrollY > maxScroll) g_scrollY = maxScroll;
    if (g_scrollY < 0) g_scrollY = 0;

    // 背景缓存（尺寸不变则复用）
    BuildCache(cw, ch);

    PositionEdits();
    UpdateScrollInfo();
    InvalidateRect(hWndMain, nullptr, FALSE);
}

// ----------------------------------------------------------------------------
//  按通配符查找主程序并重启（不再硬编码版本号）
// ----------------------------------------------------------------------------
// 通过进程快照按可执行文件名结束进程（不调用 taskkill）
// 返回 true 表示至少成功结束了一个匹配的进程
static bool KillProcessByExeName(const wstring& exeName) {
    // 防止误杀自己
    DWORD selfPid = GetCurrentProcessId();

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe = { 0 };
    pe.dwSize = sizeof(pe);

    bool killed = false;
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (pe.th32ProcessID == selfPid) continue;   // 跳过自己
            if (_wcsicmp(pe.szExeFile, exeName.c_str()) != 0) continue;

            HANDLE hProc = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE,
                FALSE, pe.th32ProcessID);
            if (!hProc) continue;   // 权限不够或进程已退出

            if (TerminateProcess(hProc, 0)) {
                // 等待进程真正退出，避免紧接着启动新实例时冲突
                WaitForSingleObject(hProc, 3000);
                killed = true;
            }
            CloseHandle(hProc);
        } while (Process32NextW(hSnap, &pe));
    }

    CloseHandle(hSnap);
    return killed;
}

static bool RestartMainProgram() {
    wstring pattern = g_exeDir + L"\\" + MAIN_PROGRAM_PATTERN;

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        ShowTaskDialog(hWndMain, L"提示", L"未找到主程序",
            L"设置已保存，但未在同目录下找到符合“进程检测实用工具*.exe”的主程序。",
            TDCBF_OK_BUTTON, TD_WARNING_ICON);
        return false;
    }
    vector<wstring> names;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            names.push_back(fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    if (names.empty()) {
        ShowTaskDialog(hWndMain, L"提示", L"未找到主程序",
            L"设置已保存，但未在同目录下找到符合通配符的主程序。",
            TDCBF_OK_BUTTON, TD_WARNING_ICON);
        return false;
    }

    sort(names.begin(), names.end());
    wstring name = names.back();   // 多个匹配时取名称排序最靠后者（通常即版本最高者）

    // 结束旧的主程序（使用进程快照 API，不调用 taskkill）
    KillProcessByExeName(name);
    // 回收句柄/文件锁
    Sleep(200);

    // 重新启动主程序
    wstring full = g_exeDir + L"\\" + name;
    ShellExecuteW(nullptr, L"open", full.c_str(), nullptr, g_exeDir.c_str(), SW_SHOWNORMAL);
    return true;
}

// ----------------------------------------------------------------------------
//  从输入框与画布收集设置并校验；成功返回 true
// ----------------------------------------------------------------------------
static bool CollectFromControls() {
    // 颜色：三个 R/G/B 输入框，任一解析失败则保留当前值
    for (int i = 0; i < 3; ++i) {
        BOOL okR, okG, okB;
        int r = GetDlgItemInt(hWndMain, IDC_ED_R(i), &okR, FALSE);
        int g = GetDlgItemInt(hWndMain, IDC_ED_G(i), &okG, FALSE);
        int b = GetDlgItemInt(hWndMain, IDC_ED_B(i), &okB, FALSE);
        if (okR && okG && okB) {
            r = max(0, min(255, r));
            g = max(0, min(255, g));
            b = max(0, min(255, b));
            COLORREF c = RGB(r, g, b);
            if (i == 0) g_colBtn = c;
            else if (i == 1) g_colBg = c;
            else             g_colText = c;
        }
    }

    // 时间间隔：为空则保持原值，否则必须在 1~3600
    {
        wchar_t buf[64] = { 0 };
        GetDlgItemTextW(hWndMain, IDC_EDIT_INTERVAL, buf, 64);
        wstring s = TrimW(buf);
        if (!s.empty()) {
            int n = _wtoi(s.c_str());
            if (n < 1 || n > 3600) {
                ShowTaskDialog(hWndMain, L"提示", L"输入错误",
                    L"检测时间间隔请输入 1~3600 之间的整数秒。",
                    TDCBF_OK_BUTTON, TD_WARNING_ICON);
                return false;
            }
            g_interval = n;
        }
    }

    // 目标进程名称
    {
        wchar_t buf[MAX_PATH] = { 0 };
        GetDlgItemTextW(hWndMain, IDC_EDIT_TARGET, buf, MAX_PATH);
        wstring s = TrimW(buf);
        if (s.empty()) {
            ShowTaskDialog(hWndMain, L"提示", L"输入错误",
                L"目标进程名称不能为空。",
                TDCBF_OK_BUTTON, TD_WARNING_ICON);
            return false;
        }
        g_targetProgram = s;
    }

    // 搜索设置
    {
        auto byId = [](int id) -> DrawItem* {
            for (DrawItem& it : g_draw)
                if (it.id == id) return &it;
            return nullptr;
        };
        if (DrawItem* p = byId(IDC_CHK_BFS)) g_bfs = p->state;
    }
    {
        wchar_t buf[MAX_PATH] = { 0 };
        GetDlgItemTextW(hWndMain, IDC_EDIT_SPATH, buf, MAX_PATH);
        wstring s = TrimW(buf);
        if (!s.empty()) {
            if (s.front() == L'"') s.erase(s.begin());
            if (s.back() == L'"') s.pop_back();
            g_specifiedPath = TrimW(s);
        }
    }

    // 用户级别（写入时只写数字）
    {
        wchar_t buf[64] = { 0 };
        GetDlgItemTextW(hWndMain, IDC_EDIT_LEVEL, buf, 64);
        g_userLevel = ParseUserLevel(buf);
    }

    // 执行行为 / 权限 / 日志 / 通信（画布复选框）
    auto getState = [](int id) {
        for (const DrawItem& it : g_draw)
            if (it.id == id) return it.state;
        return 0;
    };
    for (int i = 0; i < 7; ++i) g_action[i] = getState(IDC_CHK_ACTION(i));
    for (int i = 0; i < 3; ++i) g_rights[i] = getState(IDC_CHK_RIGHTS(i));
    g_clearLogs = getState(IDC_CHK_CLEARLOG);
    g_guiComm = getState(IDC_CHK_COMM);

    return true;
}

// ----------------------------------------------------------------------------
//  颜色对话框（点击颜色预览）
// ----------------------------------------------------------------------------
static void OpenColorDialog(int i) {
    COLORREF cur = (i == 0) ? g_colBtn : (i == 1) ? g_colBg : g_colText;
    static COLORREF cust[16] = { 0 };
    CHOOSECOLOR cc = { sizeof(cc) };
    cc.hwndOwner = hWndMain;
    cc.lpCustColors = cust;
    cc.Flags = CC_RGBINIT | CC_FULLOPEN | CC_ANYCOLOR;
    cc.rgbResult = cur;
    if (ChooseColor(&cc)) {
        if (i == 0) g_colBtn = cc.rgbResult;
        else if (i == 1) g_colBg = cc.rgbResult;
        else             g_colText = cc.rgbResult;
        SetDlgItemInt(hWndMain, IDC_ED_R(i), GetRValue(cc.rgbResult), FALSE);
        SetDlgItemInt(hWndMain, IDC_ED_G(i), GetGValue(cc.rgbResult), FALSE);
        SetDlgItemInt(hWndMain, IDC_ED_B(i), GetBValue(cc.rgbResult), FALSE);
    }
}

// ----------------------------------------------------------------------------
//  “更新”按钮：从 R/G/B 输入框读取并更新预览
// ----------------------------------------------------------------------------
static void ApplyColorFromEdits(int i) {
    BOOL okR, okG, okB;
    int r = GetDlgItemInt(hWndMain, IDC_ED_R(i), &okR, FALSE);
    int g = GetDlgItemInt(hWndMain, IDC_ED_G(i), &okG, FALSE);
    int b = GetDlgItemInt(hWndMain, IDC_ED_B(i), &okB, FALSE);
    if (okR && okG && okB) {
        r = max(0, min(255, r));
        g = max(0, min(255, g));
        b = max(0, min(255, b));
        COLORREF c = RGB(r, g, b);
        if (i == 0) g_colBtn = c;
        else if (i == 1) g_colBg = c;
        else             g_colText = c;
    }
    else {
        ShowTaskDialog(hWndMain, L"提示", L"输入错误",
            L"请输入 0~255 之间的整数。",
            TDCBF_OK_BUTTON, TD_WARNING_ICON);
    }
}

// ----------------------------------------------------------------------------
//  画布命中测试：返回点击位置对应的可交互元素（复选框 / 颜色 / 按钮）
// ----------------------------------------------------------------------------
static DrawItem* HitTest(int px, int py) {
    int cx = px - g_contentX;
    int cy = py - g_contentTop + g_scrollY;
    for (DrawItem& it : g_draw) {
        if (it.tab != g_tab) continue;
        if (cx >= it.x && cx < it.x + it.w &&
            cy >= it.y && cy < it.y + it.h) {
            if (it.kind == DK_CHECK || it.kind == DK_COLOR || it.kind == DK_BUTTON)
                return &it;
        }
    }
    return nullptr;
}

// ----------------------------------------------------------------------------
//  背景图片加载
// ----------------------------------------------------------------------------

// 从 exe 的 RCDATA 资源中加载 GDI+ 图像
// resId   : 资源 ID（如 IDR_BGIMAGE）
// 返回值  : 成功返回 Image*，失败返回 nullptr
static Image* LoadImageFromResource(int resId) {
    HRSRC hRes = FindResourceW(g_hInst, MAKEINTRESOURCEW(resId), RT_RCDATA);
    if (!hRes) return nullptr;

    DWORD size = SizeofResource(g_hInst, hRes);
    HGLOBAL hData = LoadResource(g_hInst, hRes);
    if (!hData || size == 0) return nullptr;

    LPVOID pData = LockResource(hData);
    if (!pData) return nullptr;

    // 复制到可移动内存，交给 CreateStreamOnHGlobal 托管
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!hMem) return nullptr;

    void* pMem = GlobalLock(hMem);
    if (!pMem) { GlobalFree(hMem); return nullptr; }
    memcpy(pMem, pData, size);
    GlobalUnlock(hMem);

    // 第二个参数 TRUE：流析构时自动 GlobalFree(hMem)
    IStream* pStream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(hMem, TRUE, &pStream))) {
        GlobalFree(hMem);
        return nullptr;
    }

    Image* img = Image::FromStream(pStream);
    if (img && img->GetLastStatus() == Gdiplus::Ok) {
        g_bgStream = pStream;   // 保活，程序退出时释放
        return img;
    }

    if (img) delete img;
    pStream->Release();
    return nullptr;
}

static void LoadBackground() {
    // 1) 优先从 exe 内置资源加载（图片已编译进程序）
    g_bgImage = LoadImageFromResource(IDR_BGIMAGE);
    if (g_bgImage) return;

    // 2) 兜底：如果资源没找到，仍尝试同目录下的外部图片
    //    （方便临时替换背景，不需要重新编译）
    const wchar_t* files[3] = { L"bg.jpg", L"bg.png", L"bg.bmp" };
    for (const wchar_t* fn : files) {
        wstring p = g_exeDir + L"\\" + fn;
        DWORD attr = GetFileAttributesW(p.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) continue;
        Image* im = Image::FromFile(p.c_str(), FALSE);
        if (im && im->GetLastStatus() == Gdiplus::Ok) {
            g_bgImage = im;
            break;
        }
        delete im;
    }
}

// ----------------------------------------------------------------------------
//  主窗口过程
// ----------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    case WM_CREATE: {
        // 关键：WM_CREATE 发生在 CreateWindowW 返回之前，全局 hWndMain 尚未赋值，
        // 这里先把它设置为当前窗口句柄，供子控件创建时作为父窗口使用。
        hWndMain = hWnd;

        // 固定子控件：滚动条
        g_hScroll = CreateWindowW(L"SCROLLBAR", nullptr,
            WS_CHILD | WS_VISIBLE | SBS_VERT | WS_TABSTOP,
            0, 0, 16, 200, hWnd, (HMENU)(INT_PTR)IDC_SCROLLBAR, g_hInst, nullptr);
        CreateUI();
        return 0;
    }

    case WM_SIZE:
        if (g_hScroll) Arrange();
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        int cw = rc.right, ch = rc.bottom;
        if (!g_bgCache) BuildCache(cw, ch);

        // 双缓冲
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, cw, ch);
        HBITMAP oldBmp = (HBITMAP)SelectObject(mem, bmp);

        // 1) 背景缓存
        HDC cacheDC = CreateCompatibleDC(hdc);
        HBITMAP oldC = (HBITMAP)SelectObject(cacheDC, g_bgCache);
        BitBlt(mem, 0, 0, cw, ch, cacheDC, 0, 0, SRCCOPY);
        SelectObject(cacheDC, oldC);
        DeleteDC(cacheDC);

        // 2) 选项卡（缩小、紧密排列、位于面板最左侧）
        PaintRectButton(mem, RECT{ 12, 12, 76, 40 }, L"普通", g_tab == 0, hFontTab);
        PaintRectButton(mem, RECT{ 12, 40, 76, 68 }, L"高级", g_tab == 1, hFontTab);

        // 3) 当前页内容
        PaintContent(mem);

        // 4) 底部“确定 / 取消”
        PaintRectButton(mem, RECT{ cw - 192, ch - 44, cw - 120, ch - 16 },
            L"确定", false, hFontNormal);
        PaintRectButton(mem, RECT{ cw - 112, ch - 44, cw - 40, ch - 16 },
            L"取消", false, hFontNormal);

        // 提交（WS_CLIPCHILDREN 自动排除输入框与滚动条区域）
        BitBlt(hdc, 0, 0, cw, ch, mem, 0, 0, SRCCOPY);

        SelectObject(mem, oldBmp);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;   // 背景在 WM_PAINT 中整体绘制

    case WM_LBUTTONDOWN: {
        int px = (short)LOWORD(lParam);
        int py = (short)HIWORD(lParam);

        // 选项卡
        if (px >= 12 && px < 76 && py >= 12 && py < 40) {
            if (g_tab != 0) { g_tab = 0; g_scrollY = 0; Arrange(); }
            return 0;
        }
        if (px >= 12 && px < 76 && py >= 40 && py < 68) {
            if (g_tab != 1) { g_tab = 1; g_scrollY = 0; Arrange(); }
            return 0;
        }

        // 底部按钮
        RECT rc;
        GetClientRect(hWnd, &rc);
        int cw = rc.right, ch = rc.bottom;
        if (px >= cw - 192 && px < cw - 120 && py >= ch - 44 && py < ch - 16) {
            // 确定
            if (!CollectFromControls()) return 0;
            SaveSettingsToFile();
            RestartMainProgram();
            DestroyWindow(hWnd);
            return 0;
        }
        if (px >= cw - 112 && px < cw - 40 && py >= ch - 44 && py < ch - 16) {
            // 取消
            DestroyWindow(hWnd);
            return 0;
        }

        // 画布元素
        if (DrawItem* hit = HitTest(px, py)) {
            if (hit->kind == DK_CHECK) {
                hit->state = hit->state ? 0 : 1;
                RECT cr = { g_contentX + hit->x, g_contentTop + hit->y - g_scrollY,
                            g_contentX + hit->x + hit->w,
                            g_contentTop + hit->y - g_scrollY + hit->h };
                InvalidateRect(hWnd, &cr, FALSE);
            }
            else if (hit->kind == DK_COLOR) {
                OpenColorDialog(hit->id - IDC_PV_COLOR(0));
                InvalidateRect(hWnd, nullptr, FALSE);
            }
            else { // DK_BUTTON
                ApplyColorFromEdits(hit->id - IDC_BTN_APPLY(0));
                InvalidateRect(hWnd, nullptr, FALSE);
            }
        }
        return 0;
    }

    case WM_VSCROLL: {
        int viewport = g_contentBottom - g_contentTop;
        int target = g_scrollY;
        switch (LOWORD(wParam)) {
        case SB_LINEUP:        target -= 28; break;
        case SB_LINEDOWN:      target += 28; break;
        case SB_PAGEUP:        target -= viewport; break;
        case SB_PAGEDOWN:      target += viewport; break;
        case SB_TOP:           target = 0; break;
        case SB_BOTTOM:        target = g_contentH[g_tab]; break;
        case SB_THUMBTRACK: {
            SCROLLINFO si = { sizeof(si) };
            si.fMask = SIF_TRACKPOS;
            GetScrollInfo(g_hScroll, SB_CTL, &si);
            target = si.nTrackPos;
            break;
        }
        }
        int maxScroll = max(0, g_contentH[g_tab] - viewport);
        target = max(0, min(maxScroll, target));
        if (target != g_scrollY) {
            g_scrollY = target;
            PositionEdits();
            UpdateScrollInfo();
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        int lines = (int)(delta / WHEEL_DELTA) * 28;
        int viewport = g_contentBottom - g_contentTop;
        int target = g_scrollY - lines;
        int maxScroll = max(0, g_contentH[g_tab] - viewport);
        target = max(0, min(maxScroll, target));
        if (target != g_scrollY) {
            g_scrollY = target;
            PositionEdits();
            UpdateScrollInfo();
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_CLOSE:
        // 标题栏关闭按钮等同于“取消”
        DestroyWindow(hWnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// ----------------------------------------------------------------------------
//  字体创建
// ----------------------------------------------------------------------------
static HFONT MakeFont(int size, int weight) {
    return CreateFontW(size, 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
}

// ----------------------------------------------------------------------------
//  入口（沿用原工程的 WinMain 入口，Unicode/多字节字符集均可）
// ----------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    g_hInst = hInstance;

    // 可执行文件所在目录
    wchar_t selfPath[MAX_PATH];
    GetModuleFileNameW(nullptr, selfPath, MAX_PATH);
    wstring p = selfPath;
    size_t pos = p.find_last_of(L"\\/");
    g_exeDir = (pos == wstring::npos) ? L"." : p.substr(0, pos);

    // 读取配置
    LoadSettingsFromFile();

    // GDI+ 初始化
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, nullptr);
    LoadBackground();

    // 字体
    hFontNormal = MakeFont(16, FW_NORMAL);
    hFontHeading = MakeFont(19, FW_BOLD);
    hFontSmall = MakeFont(13, FW_NORMAL);
    hFontTab = MakeFont(16, FW_BOLD);

    InitCommonControls();

    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"SettingsWindow";
    RegisterClassW(&wc);

    // 窗口大小与原先相同（480x300），标题“设置”
    hWndMain = CreateWindowW(L"SettingsWindow", L"设置",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 480, 300,
        nullptr, nullptr, hInstance, nullptr);

    ShowWindow(hWndMain, nCmdShow);
    UpdateWindow(hWndMain);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hWndMain, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    // 清理
    if (g_bgImage) { delete g_bgImage; g_bgImage = nullptr; }
    if (g_bgStream) { g_bgStream->Release(); g_bgStream = nullptr; }
    if (g_bgCache) { DeleteObject(g_bgCache); g_bgCache = nullptr; }
    Gdiplus::GdiplusShutdown(g_gdiplusToken);
    DeleteObject(hFontNormal);
    DeleteObject(hFontHeading);
    DeleteObject(hFontSmall);
    DeleteObject(hFontTab);

    return (int)msg.wParam;
}
