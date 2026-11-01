#define _WIN32_WINNT 0x0600
#define NTDDI_VERSION 0x06000000
#define _CRT_SECURE_NO_WARNINGS
#include <string>
#include <dos.h>
#include <graphics.h>
#include <conio.h>
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <sstream>
#include <ctime>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include<iostream>
#include <commctrl.h>                
#include <filesystem>

#include "resource.h" 

#pragma comment(lib, "comctl32.lib")
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include <graphics.h>
#pragma comment(lib, "MSIMG32.LIB")
#pragma comment(lib, "LoggerLib.lib")
#include "Logger.h"

#include "PortBlocker.h"
#include <initializer_list>

#include "AsyncRules.h"

#include <functional>
#include <thread>   
#include <atomic>      
#include <mutex>  

// 使用 AlphaBlend 绘制带透明通道的图片
void putimage_alpha(int x, int y, IMAGE* pImg) {
    int w = pImg->getwidth();
    int h = pImg->getheight();

    HDC dstDC = GetImageHDC(NULL); // 获取窗口的 HDC
    HDC srcDC = GetImageHDC(pImg); // 获取图片的 HDC

    // AlphaBlend 会自动使用源图片的 Alpha 通道
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(dstDC, x, y, w, h, srcDC, 0, 0, w, h, bf);
}

using namespace portblocker;
extern void AddMessage(const std::wstring& rawOutput);

using namespace std;

// ---------- 常量 ----------
const wchar_t* WINDOW_TITLE = L"进程检测实用程序1.2.0 By Fe(NO3)2";
const wchar_t* GUI_TITLE = L"进程检测实用工具";  // GUI 内部绘制的标题
const int WIN_W = 560;                     // 宽度
const int WIN_H = 350;                     // 更矮的高度,+30用于显示文字
const int MARGIN = 15;
const int FONT_SIZE = 18;
const int LINE_HEIGHT = 22;
const int TITLE_H = 45;
const int SEP_H = 2;
const int BTN_W = 105;
const int BTN_H = 32;
const int CHECKBOX_SIZE = 20;
const int SPACING = 11;
const int SCROLLBAR_W = 16;
int footerY;

// 颜色定义
const COLORREF COLOR_BG = RGB(80, 80, 80);// 无背景图片时的底色
const COLORREF COLOR_SEP = RGB(100, 100, 100);
COLORREF COLOR_BTN = RGB(70, 130, 220);  //按钮主色(较深的蓝色)
COLORREF COLOR_BTN_HOVER = RGB(90, 150, 240); //按钮悬停色
COLORREF COLOR_BTN_PRESS = RGB(50, 100, 180); // 按钮按下色
const COLORREF COLOR_TEXT_BTN = RGB(255, 255, 255);
const COLORREF COLOR_TEXT_TITLE = RGB(255, 255, 255);
const COLORREF COLOR_OUTPUT_BG = RGB(195, 225, 255); // 输出框背景
COLORREF COLOR_OUTPUT_TEXT = RGB(0, 0, 0);    // 输出框文字
COLORREF COLOR_INFO = RGB(0, 128, 255);   // [Info] 文字色
const COLORREF COLOR_CHECK_BOX = RGB(180, 180, 180);
const COLORREF COLOR_CHECK_GREEN = RGB(0, 200, 0);
int CHECK_INTERVAL_MS = 30000;   // 毫秒，从秒转换


// ---------- 全局变量 ----------
wstring targetExe, restartAltExe, mainExe;
vector<wstring> lines;
vector<int>     msgStartLine;
int scrollPos = 0;
bool autoScroll = true;
int MAX_VISIBLE_LINES = 0;

// 主程序全局变量
wstring g_targetProgram = L"studentmain.exe";      // [Target] TargetProgram
int g_bfs = 0;                                 // [Path] BFS
wstring g_specifiedPath = L"C:\\Program Files (x86)"; // [Path] SpecifiedPath
int g_regAction = 0;                           // [Action] RegAction
int g_recoverReg = 0;                          // [Action] RecoverReg
int g_setDefenderRule = 0;                     // [Action] SetDefenderRule
int g_deleteDefenderRule = 0;                  // [Action] DeleteDefenderRule
int g_systemRightKill = 0;                     // [Rights] SystmRightKill
int g_systemRightRunCMD = 0;                   // [Rights] SystemRightRunCMD
int g_guiComm = 1;                             // [CommunicationProgram] GUI
int g_LaunchCommandlineProgram = 1;            //[Action] LaunchCommandlineProgram

//防火墙相关变量
static constexpr uint16_t     PORT = 7003;
static constexpr Protocol     PROTO = Protocol::TCP;
static constexpr const char* PARENT_DIR = "C:\\PROGRA~2";
static constexpr const char* EXE1 = "StudentMain.exe";
static constexpr const char* EXE2 = "GATESRV.exe";
static constexpr bool         RECURSIVE = true;

// 线程安全消息队列（工作线程 -> UI 线程）
class MessageQueue {
    std::mutex mtx_;
    std::vector<std::wstring> pending_;
public:
    void Push(std::wstring s) {
        std::lock_guard<std::mutex> lk(mtx_);
        pending_.push_back(std::move(s));
    }
    std::vector<std::wstring> Drain() {
        std::lock_guard<std::mutex> lk(mtx_);
        std::vector<std::wstring> out;
        out.swap(pending_);
        return out;
    }
};

static MessageQueue       g_msgQueue;
static std::atomic<bool>  g_busy{false};
static std::thread        g_worker;

//工作线程
static void PostMsg(const std::wstring& s) {
    g_msgQueue.Push(s);
}

// 工具：ACP -> UTF-16
static std::wstring ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(),
        static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.c_str(),
        static_cast<int>(s.size()), w.data(), n);
    return w;
}

// Report：一次操作的结果 -> GUI + 日志，返回是否成功
static bool Report(const std::wstring& label, Result r,
    uint32_t found, uint32_t applied) {
    uint32_t we = LastWindowsError();   //立即捕获

    // ---- 拼结果行（同时用于日志） ----
    std::wostringstream oss;
    oss << std::setw(46) << std::left << label
        << L" -> ["
        << std::setw(3) << std::right << static_cast<int>(r)
        << L"] " << ToStringW(r);
    std::wstring line1 = oss.str();

    bool ok = (r == Result::Ok);

    std::wstring logLine = line1 +
        L" | found=" + std::to_wstring(found) +
        L", applied=" + std::to_wstring(applied);
    if (!ok && we != 0) {
        wchar_t weBuf[32];
        swprintf_s(weBuf, L" | WinErr=0x%08X", we);
        logLine += weBuf;
    }

    if (ok) Logger::Instance().Info(logLine);
    else    Logger::Instance().Error(logLine);

    return ok;
}

// 布局分割线位置
int firstBtnBottom, secondBtnBottom, outputY, outputW, outputH;
int bottomTextY;   // 输出框底部文字的起始 Y 坐标

struct Button {
    int x, y, w, h;
    wstring text;
    bool hover = false, pressed = false, visible = true;
};
struct CheckBox {
    int x, y, size;
    bool checked = false;
};
Button btnClose, btnRestart, btnRealTime;
Button btnMain, btnCmd;
Button btnHelp1, btnHelp2;
Button btnClearScreen;
CheckBox chkRealTime;
Button btnSettings;

bool realTimeCheck = false;
clock_t lastCheckTime = 0;
const clock_t CHECK_INTERVAL = 30000;

// ---------- 辅助函数 ----------

//查找文件
bool isFileUsable(const std::string& path) {
    using namespace std::filesystem;
    std::error_code ec;
    auto check = [&](const std::filesystem::path& p) {
        auto st = status(p, ec);
        if (ec || !exists(st) || !is_regular_file(st)) return false;
        auto perm = st.permissions();
        return (perm & perms::owner_read) != perms::none &&
            (perm & perms::owner_write) != perms::none;
    };

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(path.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;

    std::filesystem::path parent =
        std::filesystem::path(path).parent_path();
    bool ok = false;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            ok = check(parent / fd.cFileName);
    } while (!ok && FindNextFileA(h, &fd));
    FindClose(h);
    return ok;
}

// 依据通配符查找文件名
std::wstring findProgramInCurrentDir(const std::wstring& pattern) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path dir = fs::current_path(ec);
    if (ec) return {};

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir / pattern).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return {};

    std::wstring result;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            result = (dir / fd.cFileName).wstring();
            break;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return result;
}

//防火墙设置
void SetDefenderRules() {
    // ---- GUI：只发一条"开始"提示 ----
    PostMsg(L"[Info] 正在设置防火墙，请稍候...");

    Logger::Instance().Info(L"[SetDefenderRules] 开始阻止 TCP " +
        std::to_wstring(PORT));

    // ---- 逐项执行，只进日志 ----
    uint32_t f1 = 0, a1 = 0;
    Result r1 = BlockPortForProcessByName(
        PORT, PROTO, PARENT_DIR, EXE1, RECURSIVE, &f1, &a1);
    bool ok1 = Report(L"Block " + ToWide(EXE1), r1, f1, a1);

    uint32_t f2 = 0, a2 = 0;
    Result r2 = BlockPortForProcessByName(
        PORT, PROTO, PARENT_DIR, EXE2, RECURSIVE, &f2, &a2);
    bool ok2 = Report(L"Block " + ToWide(EXE2), r2, f2, a2);

    // ---- GUI：只发一条"结果"提示 ----
    if (ok1 && ok2) {
        PostMsg(L"[Info] 防火墙设置成功");
        Logger::Instance().Info(L"[SetDefenderRules] 两个程序已全部阻断");
    }
    else if (ok1 || ok2) {
        PostMsg(L"[WARN] 防火墙设置部分成功，详情见日志");
        Logger::Instance().Warning(L"[SetDefenderRules] 部分成功");
    }
    else {
        PostMsg(L"[FAIL] 防火墙设置失败，详情见日志");
        Logger::Instance().Error(L"[SetDefenderRules] 全部失败");
    }
}
//恢复防火墙设置
void DeleteDefenderRules() {
    PostMsg(L"[Info] 正在恢复防火墙，请稍候...");

    Logger::Instance().Info(L"[DeleteDefenderRules] 开始恢复 TCP " +
        std::to_wstring(PORT));

    uint32_t f1 = 0, a1 = 0;
    Result r3 = UnblockPortForProcessByName(
        PORT, PROTO, PARENT_DIR, EXE1, RECURSIVE, &f1, &a1);
    bool ok3 = Report(L"Unblock " + ToWide(EXE1), r3, f1, a1);

    uint32_t f2 = 0, a2 = 0;
    Result r4 = UnblockPortForProcessByName(
        PORT, PROTO, PARENT_DIR, EXE2, RECURSIVE, &f2, &a2);
    bool ok4 = Report(L"Unblock " + ToWide(EXE2), r4, f2, a2);

    if (ok3 && ok4) {
        PostMsg(L"[Info] 防火墙恢复成功");
        Logger::Instance().Info(L"[DeleteDefenderRules] 两个程序已全部恢复");
    }
    else if (ok3 || ok4) {
        PostMsg(L"[WARN] 防火墙恢复部分成功，详情见日志");
        Logger::Instance().Warning(L"[DeleteDefenderRules] 部分成功");
    }
    else {
        PostMsg(L"[FAIL] 防火墙恢复失败，详情见日志");
        Logger::Instance().Error(L"[DeleteDefenderRules] 全部失败");
    }
}

bool IsRulesBusy() { return g_busy.load(); }

static void LaunchWorker(std::function<void()> job) {
    // 若已有任务在跑，直接忽略
    bool expected = false;
    if (!g_busy.compare_exchange_strong(expected, true)) return;

    // 回收上一个已结束的线程对象
    if (g_worker.joinable()) g_worker.join();

    g_worker = std::thread([job = std::move(job)] {
        job();
        g_busy.store(false);
        });
}

void StartSetRulesAsync() {
    LaunchWorker([] { SetDefenderRules(); });
}

void StartDeleteRulesAsync() {
    LaunchWorker([] { DeleteDefenderRules(); });
}

// ============================================================
// 主循环每帧调用：把后台消息送到 GUI
// ============================================================
void FlushPendingMessages() {
    auto batch = g_msgQueue.Drain();
    for (auto& m : batch) {
        AddMessage(m);      // ★ 只有在 UI 主线程才真正触 GUI
    }
}

void ShutdownRulesWorker() {
    if (g_worker.joinable()) g_worker.join();
}


void LoadSettings() {
    // 默认值
    COLOR_BTN = RGB(70, 130, 220);
    COLOR_BTN_HOVER = RGB(90, 150, 240);
    COLOR_BTN_PRESS = RGB(50, 100, 180);
    //COLOR_OUTPUT_BG = RGB(255, 255, 210);
    COLOR_OUTPUT_TEXT = RGB(0, 0, 0);
    COLOR_INFO = RGB(0, 128, 255);
    int intervalSec = 30;


    // 尝试打开 Settings.ini
    FILE* f = _wfopen(L"Settings.ini", L"r");
    if (!f) return;  // 没有文件则使用默认

    char line[256];
    char section[64] = "";
    while (fgets(line, sizeof(line), f)) {
        // 去掉换行
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '[') { sscanf(line, "[%[^]]]", section); continue; }
        int r, g, b;
        if (strcmp(section, "Color") == 0) {
            if (sscanf(line, "COLORREF COLOR_BTN = RGB(%d,%d,%d)", &r, &g, &b) == 3)
                COLOR_BTN = RGB(r, g, b);
            else if (sscanf(line, "COLORREF COLOR_OUTPUT_BG = RGB(%d,%d,%d)", &r, &g, &b) == 3)
                //COLOR_OUTPUT_BG = RGB(r, g, b);
                int backgroundcolor = 0;
            else if (sscanf(line, "COLORREF COLOR_OUTPUT_TEXT = RGB(%d,%d,%d)", &r, &g, &b) == 3)
                COLOR_OUTPUT_TEXT = RGB(r, g, b);
        }
        else if (strcmp(section, "TimeInterval") == 0) {
            if (sscanf(line, "Interval = %d", &intervalSec) == 1)
                CHECK_INTERVAL_MS = intervalSec * 1000;
        }if (strcmp(section, "TimeInterval") == 0) {
            if (sscanf(line, "Interval = %d", &intervalSec) == 1) {}
        }// 读取 [Target]
        if (strcmp(section, "Target") == 0) {
            char buf[256];
            if (sscanf(line, "TargetProgram = %255s", buf) == 1) {
                // 将窄字符转为wstring（需处理引号）
                wchar_t wbuf[256];
                MultiByteToWideChar(CP_ACP, 0, buf, -1, wbuf, 256);
                g_targetProgram = wbuf;
            }
        }
        // 读取 [Path]
        else if (strcmp(section, "Path") == 0) {
            if (sscanf(line, "BFS = %d", &g_bfs) == 1) {}
            else if (strstr(line, "SpecifiedPath = ") == line) {
                // 提取引号内路径
                char path[256];
                if (sscanf(line, "SpecifiedPath = \"%255[^\"]\"", path) == 1) {
                    wchar_t wbuf[256];
                    MultiByteToWideChar(CP_ACP, 0, path, -1, wbuf, 256);
                    g_specifiedPath = wbuf;
                }
            }
        }
        // 读取 [Action]
        else if (strcmp(section, "Action") == 0) {
            if (sscanf(line, "RegAction = %d", &g_regAction) == 1) {}
            else if (sscanf(line, "RecoverReg = %d", &g_recoverReg) == 1) {}
            else if (sscanf(line, "SetDefenderRule = %d", &g_setDefenderRule) == 1) {}
            else if (sscanf(line, "DeleteDefenderRule = %d", &g_deleteDefenderRule) == 1) {}
            else if (sscanf(line, "LaunchCommandlineProgram = %d", &g_LaunchCommandlineProgram) == 1) {}
        }
        // 读取 [Rights]
        else if (strcmp(section, "Rights") == 0) {
            if (sscanf(line, "SystmRightKill = %d", &g_systemRightKill) == 1) {}
            else if (sscanf(line, "SystemRightRunCMD = %d", &g_systemRightRunCMD) == 1) {}
        }
        // 读取 [CommunicationProgram]
        else if (strcmp(section, "CommunicationProgram") == 0) {
            if (sscanf(line, "GUI = %d", &g_guiComm) == 1) {}
        }
    }
    fclose(f);
    // 衍生颜色：按钮悬停/按下亮度调整（简单处理）
    COLOR_BTN_HOVER = RGB(min(255, GetRValue(COLOR_BTN) + 20),
        min(255, GetGValue(COLOR_BTN) + 20),
        min(255, GetBValue(COLOR_BTN) + 20));
    COLOR_BTN_PRESS = RGB(max(0, GetRValue(COLOR_BTN) - 20),
        max(0, GetGValue(COLOR_BTN) - 20),
        max(0, GetBValue(COLOR_BTN) - 20));
    CHECK_INTERVAL_MS = intervalSec * 1000;
}
wstring ExpandEnv(const wchar_t* path) {
    wchar_t buf[MAX_PATH * 2] = { 0 };
    ExpandEnvironmentStringsW(path, buf, sizeof(buf) / sizeof(wchar_t));
    return wstring(buf);
}
wstring GetDesktopPath(const wstring& filename) {
    wstring userProfile = ExpandEnv(L"%USERPROFILE%");
    return userProfile + L"\\Desktop\\" + filename;
}
wstring GetFileName(const wstring& fullpath) {
    size_t pos = fullpath.find_last_of(L"\\/");
    if (pos != wstring::npos) return fullpath.substr(pos + 1);
    return fullpath;
}
// 在指定目录下查找符合通配符的文件，返回第一个匹配的完整路径
wstring FindFirstMatch(const wstring& directory, const wstring& pattern) {
    wstring searchPath = directory + L"\\" + pattern;
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return L"";   // 未找到

    wstring foundFile = fd.cFileName;
    FindClose(hFind);
    return directory + L"\\" + foundFile;
}
int GetMaxCharsPerLine(int boxWidth) {
    int cw = textwidth(L"A");
    if (cw <= 0) cw = 10;
    return max(1, boxWidth / cw);
}
void AddWrappedLines(const wstring& text, int maxChars) {
    wistringstream iss(text);
    wstring line;
    while (getline(iss, line)) {
        if (line.empty()) {
            lines.push_back(L"");
            continue;
        }
        for (size_t i = 0; i < line.length(); i += maxChars)
            lines.push_back(line.substr(i, maxChars));
    }
}
void AddMessage(const wstring& rawOutput) {
    if (rawOutput.empty()) return;
    int boxContentW = outputW - SCROLLBAR_W - 6;
    int maxChars = GetMaxCharsPerLine(boxContentW);

    msgStartLine.push_back((int)lines.size());

    wistringstream iss(rawOutput);
    wstring firstLine;
    bool first = true;
    while (getline(iss, firstLine)) {
        wstring outputLine;
        if (first) {
            if (firstLine.find(L"[Info]") == 0 || firstLine.find(L"[info]") == 0)
                outputLine = firstLine;
            else
                outputLine = L"[Info] " + firstLine;
            first = false;
        }
        else {
            outputLine = firstLine;
        }
        AddWrappedLines(outputLine, maxChars);
    }

    if (autoScroll) {
        /*
        int totalMsgs = (int)msgStartLine.size();
        if (totalMsgs >= 3)
            scrollPos = msgStartLine[totalMsgs - 3];
        else
            scrollPos = 0;
        int maxScroll = max(0, (int)lines.size() - MAX_VISIBLE_LINES);
        if (scrollPos > maxScroll) scrollPos = maxScroll;
        if (scrollPos < 0) scrollPos = 0;
        */
        if (MAX_VISIBLE_LINES <= 0) {
            MAX_VISIBLE_LINES = 5;
        }
        int maxScroll = max(0, (int)lines.size() - MAX_VISIBLE_LINES);
        scrollPos = maxScroll;   // 滚到最底部
    }
}
void logError(const std::string& errMsg) {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);

    std::ofstream("error.log", std::ios::app)
        << (tm.tm_year + 1900) << '/' << (tm.tm_mon + 1) << '/' << tm.tm_mday
        << ' ' << std::setw(2) << std::setfill('0') << tm.tm_hour << ':'
        << std::setw(2) << std::setfill('0') << tm.tm_min
        << " [Error]:" << errMsg << std::endl;
}
wstring ExecCmd(const wstring& cmd) {
    // 1. 将宽字符命令转换为系统默认代码页（GBK）的窄字符串
    int clen = WideCharToMultiByte(CP_ACP, 0, cmd.c_str(), -1, nullptr, 0, nullptr, nullptr);
    string narrowCmd(clen - 1, '\0');
    WideCharToMultiByte(CP_ACP, 0, cmd.c_str(), -1, &narrowCmd[0], clen, nullptr, nullptr);

    // 2. 以窄字符方式执行命令并读取输出（CMD 输出为 GBK 字节流）
    FILE* pipe = _popen(narrowCmd.c_str(), "r");
    if (!pipe) return L"命令执行失败";

    string result;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    _pclose(pipe);

    // 去除末尾多余换行
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
        result.pop_back();

    // 3. 将 GBK 多字节字符串转换为宽字符（UTF-16）
    int wlen = MultiByteToWideChar(CP_ACP, 0, result.c_str(), -1, nullptr, 0);
    wstring wresult(wlen - 1, L'\0');
    MultiByteToWideChar(CP_ACP, 0, result.c_str(), -1, &wresult[0], wlen);

    return wresult;
}
bool IsProcessRunning(const wstring& exeName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    bool found = false;
    if (Process32FirstW(snapshot, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exeName.c_str()) == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(snapshot, &pe));
    }
    CloseHandle(snapshot);
    return found;
}
void StartProcess(const wstring& path) {
    ShellExecuteW(NULL, L"open", path.c_str(), NULL, NULL, SW_SHOWNORMAL);
}
bool btnDown(Button& btn, int mx, int my) {
    return btn.visible && mx >= btn.x && mx <= btn.x + btn.w && my >= btn.y && my <= btn.y + btn.h;
}
bool chkDown(CheckBox& chk, int mx, int my) {
    return mx >= chk.x && mx <= chk.x + chk.size && my >= chk.y && my <= chk.y + chk.size;
}
bool scrollBarDragging = false;

// ---------- 绘制函数 (双缓冲) ----------
void DrawBackground() {
    IMAGE bg;
    loadimage(&bg, L"background.png");
    if (bg.getwidth() == 0) {
        setbkcolor(COLOR_BG);
        cleardevice();
    }
    else {
        putimage(0, 0, &bg);
    }
}
void DrawButton(const Button& btn) {
    if (!btn.visible) return;
    COLORREF col = COLOR_BTN;
    if (btn.pressed) col = COLOR_BTN_PRESS;
    else if (btn.hover) col = COLOR_BTN_HOVER;
    setfillcolor(col);
    setlinecolor(RGB(50, 100, 150));
    fillrectangle(btn.x, btn.y, btn.x + btn.w, btn.y + btn.h);
    setbkmode(TRANSPARENT);
    settextcolor(COLOR_TEXT_BTN);
    settextstyle(FONT_SIZE, 0, L"Consolas");
    int tw = textwidth(btn.text.c_str());
    outtextxy(btn.x + (btn.w - tw) / 2, btn.y + (btn.h - FONT_SIZE) / 2, btn.text.c_str());
}
void DrawCheckBox(const CheckBox& chk) {
    setlinecolor(COLOR_CHECK_BOX);
    setfillcolor(COLOR_OUTPUT_BG);   // 与输出框同色
    fillrectangle(chk.x, chk.y, chk.x + chk.size, chk.y + chk.size);
    if (chk.checked) {
        settextcolor(COLOR_CHECK_GREEN);
        settextstyle(20, 0, L"Segoe UI Symbol");
        outtextxy(chk.x + 2, chk.y - 2, L"√");
    }
}
void DrawScrollBar(int x, int y, int h, int totalLines, int visibleLines) {
    int maxPos = max(0, totalLines - visibleLines);
    if (maxPos == 0) return;
    setfillcolor(RGB(240, 240, 240));
    setlinecolor(RGB(180, 180, 180));
    fillrectangle(x, y, x + SCROLLBAR_W, y + h);
    float ratio = (float)scrollPos / maxPos;
    int sliderH = max(30, (int)(h * ((float)visibleLines / totalLines)));
    int sliderY = y + (int)((h - sliderH) * ratio);
    setfillcolor(RGB(160, 160, 160));
    fillrectangle(x + 1, sliderY, x + SCROLLBAR_W - 1, sliderY + sliderH);
}
void DrawOutputBox(int x, int y, int w, int h) {
    setfillcolor(COLOR_OUTPUT_BG);
    setlinecolor(RGB(180, 180, 180));
    fillrectangle(x, y, x + w, y + h);

    int textX = x + 5;
    int textY = y + 4;
    int visibleLines = max(1, (h - 8) / LINE_HEIGHT);
    MAX_VISIBLE_LINES = visibleLines;

    settextstyle(FONT_SIZE, 0, L"Consolas");
    setbkmode(TRANSPARENT);
    int lineY = textY;
    for (int i = scrollPos; i < (int)lines.size(); ++i) {
        if (lineY + LINE_HEIGHT > y + h - 3) break;
        const wstring& line = lines[i];
        if (line.find(L"[Info]") == 0 || line.find(L"[info]") == 0) {
            settextcolor(COLOR_INFO);
            outtextxy(textX, lineY, L"[Info]");
            int prefixW = textwidth(L"[Info]");
            settextcolor(COLOR_OUTPUT_TEXT);
            wstring rest = line.substr(6);
            outtextxy(textX + prefixW, lineY, rest.c_str());
        }
        else {
            settextcolor(COLOR_OUTPUT_TEXT);
            outtextxy(textX, lineY, line.c_str());
        }
        lineY += LINE_HEIGHT;
    }

    int sbX = x + w - SCROLLBAR_W - 1;
    DrawScrollBar(sbX, y + 1, h - 2, (int)lines.size(), visibleLines);
}

// ---------- 布局 ----------
void InitLayout() {
    // 第一功能区：标题分隔线下方
    int y1 = TITLE_H + SEP_H;                   // 第一区顶部
    int btnY = y1 + 8;                          // 按钮垂直微调
    btnClose = { MARGIN, btnY, BTN_W, BTN_H, L"关闭程序" };
    btnRestart = { MARGIN + BTN_W + SPACING, btnY, BTN_W, BTN_H, L"重启程序" };
    btnRealTime = { MARGIN + (BTN_W + SPACING) * 2, btnY, BTN_W, BTN_H, L"实时检测" };
    chkRealTime = { btnRealTime.x + BTN_W + SPACING, btnY + (BTN_H - CHECKBOX_SIZE) / 2, CHECKBOX_SIZE, false };
    firstBtnBottom = y1 + BTN_H + 16;           // 第一区底部（分隔线）

    // 第二功能区：垂直分隔线位置
    int vSepX = WIN_W / 2;
    int section2H = 75;
    int y2_top = firstBtnBottom + SEP_H;        // 第二区顶部
    int btn2Y = y2_top + (section2H - BTN_H) / 2;
    btnMain = { MARGIN, btn2Y, BTN_W, BTN_H, L"通信程序" };
    btnCmd = { MARGIN + BTN_W + SPACING, btn2Y, BTN_W, BTN_H, L"命令行" };
    int totalHelpH = BTN_H * 2 + 4;
    int helpULY = y2_top + (section2H - totalHelpH) / 2;
    btnHelp1 = { vSepX + 70, helpULY, BTN_W, BTN_H, L"Openfront" };
    btnHelp2 = { vSepX + 70, helpULY + BTN_H + 4, BTN_W, BTN_H, L"Minecraft" };
    btnSettings = { chkRealTime.x + chkRealTime.size + SPACING, btnClose.y, BTN_W, BTN_H, L"设置..." };
    secondBtnBottom = y2_top + section2H;        // 第二区底部

    // 输出区域（紧贴底部）
    outputY = secondBtnBottom + SEP_H;
    int clearBtnW = 70, clearBtnH = 30;
    outputW = WIN_W - MARGIN * 2 - clearBtnW - SPACING;
    outputH = WIN_H - outputY - 30;                   // 利用剩余全部高度（约142),再预留30给文字
    bottomTextY = outputY + outputH;   // 紧贴输出框下缘
    btnClearScreen = { MARGIN + outputW + SPACING, outputY, clearBtnW, clearBtnH, L"清屏" };
}

// 显示 TaskDialog，返回用户点击的按钮 ID（如 IDOK, IDYES, IDNO 等）
int ShowTaskDialog(HWND hwndParent, const wchar_t* title, const wchar_t* heading,
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

void DrawUI() {
    BeginBatchDraw();        // 双缓冲开始
    DrawBackground();

    // 标题
    settextstyle(24, 0, L"Consolas");
    settextcolor(COLOR_TEXT_TITLE);
    outtextxy(MARGIN, 10, GUI_TITLE);
    setlinecolor(COLOR_SEP);
    line(0, TITLE_H, WIN_W, TITLE_H);

    // 分隔线
    line(0, firstBtnBottom, WIN_W, firstBtnBottom);
    int vSepX = WIN_W / 2;
    line(vSepX, firstBtnBottom + SEP_H, vSepX, secondBtnBottom);
    line(0, secondBtnBottom, WIN_W, secondBtnBottom);

    // 控件
    DrawButton(btnClose);
    DrawButton(btnRestart);
    DrawButton(btnRealTime);
    DrawCheckBox(chkRealTime);
    DrawButton(btnSettings);//设置按钮
    DrawButton(btnMain);
    DrawButton(btnCmd);
    DrawButton(btnHelp1);
    DrawButton(btnHelp2);
    settextstyle(FONT_SIZE, 0, L"Consolas");
    settextcolor(COLOR_TEXT_BTN);
    int gotoY = btnHelp1.y + (BTN_H * 2 + 4 - FONT_SIZE) / 2;
    outtextxy(vSepX + 10, gotoY, L"转到...");

    DrawOutputBox(MARGIN, outputY, outputW, outputH);
    DrawButton(btnClearScreen);

    //显示文字
    // 绘制底部小字
    int fontSizeSmall = FONT_SIZE / 1.5;   //设置字体大小
    settextstyle(fontSizeSmall, 0, L"Consolas");
    settextcolor(RGB(255, 255, 222));
    setbkmode(TRANSPARENT);
    // 第一行
    outtextxy(MARGIN, bottomTextY, L"点击\"设置...\"或修改Settings.ini以修改参数,在Error.log中查看错误信息");
    // 第二行（行距可按需调整，例如行高 = fontSizeSmall + 2）
    outtextxy(MARGIN, bottomTextY + fontSizeSmall + 2, L"点击'通信程序'以打开附带的通信程序,端口号选择默认即可.");
    settextstyle(FONT_SIZE, 0, L"Consolas");


    EndBatchDraw();          // 双缓冲结束
}

// ---------- 事件处理 ----------
void HandleButtonClick(const wstring& action) {
    //MessageBoxW(NULL, (L"Enter HandleButtonClick: " + action).c_str(), L"调试", MB_OK);
    if (action == L"settings") {
        // MessageBoxW(NULL, L"Settings branch entered", L"调试", MB_OK);

         // 获取当前目录
        wchar_t selfPath[MAX_PATH];
        GetModuleFileNameW(NULL, selfPath, MAX_PATH);
        wchar_t* lastSlash = wcsrchr(selfPath, L'\\');
        if (lastSlash) *lastSlash = L'\0';
        std::wstring settingsExe = std::wstring(selfPath) + L"\\Setting.exe";

        // 检查文件存在性
        if (GetFileAttributesW(settingsExe.c_str()) == INVALID_FILE_ATTRIBUTES) {
            wchar_t errMsg[256];
            swprintf_s(errMsg, 256, L"Setting.exe 不存在于:\n%s", settingsExe.c_str());
            ShowTaskDialog(GetHWnd(), L"错误", L"无法找到Setting.exe", L"请检查文件是否存在", TDCBF_RETRY_BUTTON | TDCBF_CANCEL_BUTTON, TD_ERROR_ICON, IDRETRY);
            return;
        }

        // 启动程序
        HINSTANCE ret = ShellExecuteW(NULL, L"open", settingsExe.c_str(), NULL, NULL, SW_SHOWNORMAL);
        if ((INT_PTR)ret <= 32) {
            wchar_t errMsg[256];
            swprintf_s(errMsg, 256, L"启动失败，错误码: %d", (int)(INT_PTR)ret);
            //MessageBoxW(NULL, errMsg, L"错误", MB_ICONERROR);
            ShowTaskDialog(GetHWnd(), L"错误", L"无法找到Setting.exe", L"请检查文件是否存在", TDCBF_RETRY_BUTTON | TDCBF_CANCEL_BUTTON, TD_ERROR_ICON, IDRETRY);
        }
        else {
            //MessageBoxW(NULL, L"Setting.exe 已启动", L"提示", MB_OK);
            ShowTaskDialog(NULL, L"提示", L"Setting.exe 已启动", L"", TDCBF_OK_BUTTON, TD_INFORMATION_ICON);
        }
        return;
    }
    else if (action == L"close") {
        wstring imageName = GetFileName(targetExe);
        if (!IsProcessRunning(imageName)) {
            AddMessage(L"未检测到进程");
        }
        else {
            wstring cmd = L"taskkill /f /im " + imageName + L" /t";
            AddMessage(ExecCmd(cmd));
            if (g_setDefenderRule) {
                if (IsRulesBusy()) {
                    AddMessage(L"[Info] 上一项任务仍在处理中，请稍候");
                }
                else {
                    StartSetRulesAsync();
                }
            }
            AddMessage(L"操作成功完成");
        }
    }
    else if (action == L"restart") {

        if (g_recoverReg) {
            AddMessage(L"正在删除注册表项...");
            wstring cmd6 = L"reg add \"HKLM\\SOFTWARE\\Wow6432Node\\TopDomain\\e-Learning Class\\Student\" /v PreventKill /t REG_DWORD /d 1 /f";
            wstring result5 = ExecCmd(cmd6);
            Sleep(200);
            AddMessage(result5);
        }
        wstring desktop = ExpandEnv(L"%USERPROFILE%\\Desktop");

        if (realTimeCheck) {
            // 实时检测模式下重启：打开桌面匹配的程序
            wstring target = L"C:\\PROGRA~2\\Mythware\\极域课堂管理系统软件v6.0 2019豪华版\\studentmain.exe";
            if (target.empty()) {
                int ret = ShowTaskDialog(GetHWnd(),
                    L"错误",                          // 窗口标题
                    L"找不到文件",                    // 主指令
                    L"无法找到指定的文件(0x0001)，请检查目标程序路径是否正确",
                    TDCBF_RETRY_BUTTON | TDCBF_CANCEL_BUTTON,
                    TD_ERROR_ICON,
                    IDRETRY);
                if (ret == IDRETRY) {
                    target = L"C:\\PROGRA~2\\Mythware\\极域课堂管理系统软件v6.0 2019豪华版\\studentmain.exe";
                    if (!target.empty()) {
                        StartProcess(target);
                        realTimeCheck = false;
                        chkRealTime.checked = false;
                        AddMessage(L"实时检测已停止，已打开目标程序");
                        return;
                    }
                    ShowTaskDialog(GetHWnd(), L"提示", L"重试失败", L"重试后仍未找到文件", TDCBF_OK_BUTTON, TD_WARNING_ICON);
                    logError("Unable to perform restart operation because the target program was not found");
                }
                return;  // 取消或二次失败，终止操作
            }
            // 找到文件，正常启动
            StartProcess(target);
            realTimeCheck = false;
            chkRealTime.checked = false;
            AddMessage(L"实时检测已停止，已打开目标程序");
        }
        //else if (action == L"settings") {
            /*MessageBoxW(GetHWnd(), L"进入设置分支！", L"调试", MB_OK);
            // 获取主程序所在目录
            wchar_t selfPath[MAX_PATH];
            GetModuleFileNameW(NULL, selfPath, MAX_PATH);
            wchar_t* lastSlash = wcsrchr(selfPath, L'\\');
            if (lastSlash) *lastSlash = L'\0';
            std::wstring settingsExe = std::wstring(selfPath) + L"\\Settings.exe";

            STARTUPINFOW si = { sizeof(si) };
            PROCESS_INFORMATION pi;
            if (!CreateProcessW(settingsExe.c_str(), NULL, NULL, NULL, FALSE,
                CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
                // 如果 CreateProcess 失败，尝试 ShellExecuteW
                ShellExecuteW(NULL, L"open", settingsExe.c_str(), NULL, NULL, SW_SHOWNORMAL);
            }
            else {
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            }
        }*/

        else {
            // 普通重启：先强制结束目标进程，再启动匹配的程序
            ExecCmd(L"taskkill /f /im " + GetFileName(targetExe) + L" /t");

            wstring target = L"C:\\PROGRA~2\\Mythware\\极域课堂管理系统软件v6.0 2019豪华版\\studentmain.exe";
            if (target.empty()) {
                int ret = ShowTaskDialog(GetHWnd(),
                    L"错误",                          // 窗口标题
                    L"找不到文件",                    // 主指令
                    L"无法找到指定的文件(0x0001)，请检查目标程序路径是否正确",
                    TDCBF_RETRY_BUTTON | TDCBF_CANCEL_BUTTON,
                    TD_ERROR_ICON,
                    IDRETRY);
                if (ret == IDRETRY) {
                    target = L"C:\\PROGRA~2\\Mythware\\极域课堂管理系统软件v6.0 2019豪华版\\studentmain.exe";
                    if (!target.empty()) {
                        StartProcess(target);
                        AddMessage(L"已重启目标程序");
                        return;
                    }
                    ShowTaskDialog(GetHWnd(), L"提示", L"重试失败", L"重试后仍未找到文件", TDCBF_OK_BUTTON, TD_WARNING_ICON);
                    logError("Unable to perform restart operation because the target program was not found");
                }
                return;
            }
            StartProcess(target);
            if (g_deleteDefenderRule) {
                if (IsRulesBusy()) {
                    AddMessage(L"[Info] 上一项任务仍在处理中，请稍候");
                }
                else {
                    StartDeleteRulesAsync();
                }
            }
            AddMessage(L"已重启目标程序");
        }
    }
    else if (action == L"realtime") {

        realTimeCheck = !realTimeCheck;
        chkRealTime.checked = realTimeCheck;
        if (realTimeCheck) {
            lastCheckTime = clock();
            int intervalSec = CHECK_INTERVAL_MS / 1000;   // 毫秒转秒
            wchar_t buf[64];
            swprintf(buf, 64, L"实时检测已开启，检测周期为 %d 秒", intervalSec);
            AddMessage(buf);
            if (g_setDefenderRule) {
                if (!IsRulesBusy()) {
                    AddMessage(L"[Info] 已提交防火墙设置任务，请稍候...");
                    StartSetRulesAsync();
                }
            }
        }
        else {
            AddMessage(L"实时检测已关闭");
        }
    }
    else if (action == L"main") {
        if (isFileUsable("LanChat*.exe")) {
            std::wstring chatprogrm = findProgramInCurrentDir(L"LanChat*.exe");
            if (!chatprogrm.empty()) {
                StartProcess(chatprogrm);
                Logger::Instance().Info(L"通信程序成功启动");
                AddMessage(L"已打开通信程序");
            }
        }
        else {
            Logger::Instance().Info(L"通信程序未找到");
            AddMessage(L"通信程序未找到，请检查文件是否存在");
        }
    }
    else if (action == L"cmd") {
        system("start cmd");
        Logger::Instance().Info(L"命令行成功启动");
        AddMessage(L"已打开命令行窗口");
    }
    else if (action == L"help1") {
        ShellExecuteW(NULL, L"open", L"https://openfront.io", NULL, NULL, SW_SHOWNORMAL);
        AddMessage(L"正在打开Openfront...");
    }
    else if (action == L"help2") {
        if (isFileUsable("WebsiteSelection.exe")) {
            ShellExecuteW(NULL, L"open", L"WebsiteSelection.exe", NULL, NULL, SW_SHOWNORMAL);
        }
        else {
            ShellExecuteW(NULL, L"open", L"https://mcjs.link/", NULL, NULL, SW_SHOWNORMAL);
        }
        AddMessage(L"正在打开Minecraft...");
    }
    else if (action == L"clear") {
        lines.clear();
        msgStartLine.clear();
        scrollPos = 0;
        autoScroll = true;
    }
}
void ProcessMouse(int mx, int my) {
    btnClose.hover = btnDown(btnClose, mx, my);
    btnRestart.hover = btnDown(btnRestart, mx, my);
    btnRealTime.hover = btnDown(btnRealTime, mx, my);
    btnMain.hover = btnDown(btnMain, mx, my);
    btnCmd.hover = btnDown(btnCmd, mx, my);
    btnHelp1.hover = btnDown(btnHelp1, mx, my);
    btnHelp2.hover = btnDown(btnHelp2, mx, my);
    btnClearScreen.hover = btnDown(btnClearScreen, mx, my);
    btnSettings.hover = btnDown(btnSettings, mx, my);
}
void ProcessLButtonDown(int mx, int my) {

    // 强制重新计算布局
    //InitLayout();
    // 手动检查坐标
    //if (mx >= 394 && mx <= 499 && my >= 55 && my <= 87) {
       // MessageBoxW(NULL, L"坐标匹配，手动触发", L"调试", MB_OK);
       // HandleButtonClick(L"settings");
    //}
    if (btnDown(btnClose, mx, my))          HandleButtonClick(L"close");
    else if (btnDown(btnRestart, mx, my))   HandleButtonClick(L"restart");
    else if (btnDown(btnRealTime, mx, my) || chkDown(chkRealTime, mx, my))
        HandleButtonClick(L"realtime");
    else if (btnDown(btnMain, mx, my))      HandleButtonClick(L"main");
    else if (btnDown(btnCmd, mx, my))       HandleButtonClick(L"cmd");
    else if (btnDown(btnHelp1, mx, my))     HandleButtonClick(L"help1");
    else if (btnDown(btnHelp2, mx, my))     HandleButtonClick(L"help2");
    else if (btnDown(btnClearScreen, mx, my)) HandleButtonClick(L"clear");
    else if (btnDown(btnSettings, mx, my))  HandleButtonClick(L"settings");
    int sbX = MARGIN + outputW - SCROLLBAR_W - 1;
    if (mx >= sbX && mx <= sbX + SCROLLBAR_W && my >= outputY + 1 && my <= outputY + outputH - 1)
        scrollBarDragging = true;
}
void ProcessLButtonUp() { scrollBarDragging = false; }
void ProcessScrollDrag(int my) {
    if (!scrollBarDragging) return;
    float ratio = (float)(my - outputY - 1) / (outputH - 2);
    int maxPos = max(0, (int)lines.size() - MAX_VISIBLE_LINES);
    scrollPos = (int)(ratio * maxPos + 0.5f);
    scrollPos = max(0, min(scrollPos, maxPos));
    autoScroll = false;
}
void ProcessMouseWheel(int delta) {
    scrollPos -= delta / 120;
    scrollPos = max(0, min(scrollPos, (int)lines.size() - MAX_VISIBLE_LINES));
    autoScroll = false;
}
void ProcessKeyDown(int key) {
    if (key == VK_PRIOR) {
        scrollPos -= MAX_VISIBLE_LINES;
        if (scrollPos < 0) scrollPos = 0;
        autoScroll = false;
    }
    else if (key == VK_NEXT) {
        scrollPos += MAX_VISIBLE_LINES;
        int maxPos = max(0, (int)lines.size() - MAX_VISIBLE_LINES);
        if (scrollPos > maxPos) scrollPos = maxPos;
        if (scrollPos == maxPos) autoScroll = true;
    }
}
void RealTimeDetection() {
    if (!realTimeCheck) return;
    if (clock() - lastCheckTime < CHECK_INTERVAL) return;
    lastCheckTime = clock();
    wstring exeName = GetFileName(targetExe);
    if (IsProcessRunning(exeName))
        AddMessage(ExecCmd(L"taskkill /f /im " + exeName + L" /t"));
    else
        AddMessage(L"未检测到进程");
}

//启动界面字符画
// 设置控制台文字颜色
void setColor(HANDLE hConsole, int color) {
    SetConsoleTextAttribute(hConsole, color);
}

// 移动光标到指定位置 (x列, y行，原点为左上角)
void gotoxy(HANDLE hConsole, int x, int y) {
    COORD coord = { (SHORT)x, (SHORT)y };
    SetConsoleCursorPosition(hConsole, coord);
}

void showGrilledFishPDP() {
    //system("mode con cols=120 lines=30");
    system("cls");

    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    // ---------- 主字符画 ----------
    const char* art[] = {
        " _____          _   _   _              _   ______   _         _     ",
        "  / ____|        (_) | | | |            | | |  ____| (_)       | |    ",
        " | |  __   _ __   _  | | | |   ___    __| | | |__     _   ___  | |__  ",
        " | | |_ | | '__| | | | | | |  / _ \\  / _` | |  __|   | | / __| | '_ \\ ",
        " | |__| | | |    | | | | | | |  __/ | (_| | | |      | | \\__ \\ | | | |",
        "  \\_____| |_|    |_| |_| |_|  \\___|  \\__,_| |_|      |_| |___/ |_| |_|"
    };
    int artLines = sizeof(art) / sizeof(art[0]);

    setColor(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
    for (int i = 0; i < artLines; ++i) {
        gotoxy(hConsole, 0, i);
        cout << art[i];
    }
    /*
    setColor(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
    int starX = 110;
    int starY = 0;
    const char* star[] = {
        "   /\\   ",
        "  /  \\  ",
        " / /\\ \\ ",
        "/ /  \\ \\",
        "|_/  \\_|"
    };
    for (int i = 0; i < 5; ++i) {
        gotoxy(hConsole, starX, starY + i);
        cout << star[i];
    }
    */

    // 恢复默认颜色并将光标移到底部
    setColor(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    gotoxy(hConsole, 0, 10);
}

void SetColor(int color) {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), color);
}
void info() {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    std::cout << "[Info]";
    SetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
}
void error() {
    SetColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
    std::cout << "[Error]";
    SetColor(FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
}

int main() {

    Logger::Instance().SetLogFile(L"error.log", LOG_INFO);
    Logger::Instance().Info(L"====================");
    Logger::Instance().Info(L"主程序启动");
    showGrilledFishPDP();
    LoadSettings();
    targetExe = L"studentmain.exe";
    wstring desktop = ExpandEnv(L"%USERPROFILE%\\Desktop");
    restartAltExe = FindFirstMatch(desktop, L"");
    mainExe = L"LanChatClient.exe";

    info();
    cout << "正在启动组件..." << endl;
    int count = 0;
    if (isFileUsable("Settings.ini")) {
        cout << "配置文件已成功读取" << endl;
        count++;
    }
    else {
        Logger::Instance().Warning(L"配置文件缺失，使用默认参数");
        error();
        cout << "配置文件未找到或无法读取,将使用默认设置" << endl;
    }
    Sleep(500);
    if (isFileUsable("Setting.exe")) {
        cout << "设置程序正常" << endl;
        count++;
    }
    else {
        Logger::Instance().Warning(L"设置程序缺失，使用默认设置");
        error();
        cout << "设置程序未找到或无法读取,将使用默认设置" << endl;
    }
    Sleep(500);
    if (isFileUsable("AutoUpdateProgram.exe")) {
        cout << "自动更新程序正常" << endl;
        count++;
    }
    else {
        Logger::Instance().Warning(L"自动更新程序缺失");
        error();
        cout << "自动更新程序未找到或无法读取" << endl;
    }
    Sleep(500);
    if (count == 3) {
        info();
        cout << "组件已全部加载,正在启动主程序..." << endl;
    }
    else {
        info();
        cout << "已加载 " << count << " 个组件,成功: " << count << " 个,失败: " << 3 - count << " 个" << endl;
    }
    Sleep(1000);
    info();

    cout << "正在启动命令行辅助程序..." << endl; {
        Sleep(1000);
        if (isFileUsable("KeyboardDetect*.exe")) {
            std::wstring chatprogrm = findProgramInCurrentDir(L"KeyboardDetect*.exe");
            if (!chatprogrm.empty()) {
                StartProcess(chatprogrm);
                Logger::Instance().Info(L"命令行辅助程序成功启动");
                info();
                cout<<"命令行辅助程序成功启动" << endl;
            }
        }
        else {
            Logger::Instance().Info(L"命令行辅助程序未找到");
            error();
            cout << "命令行辅助程序未找到" << endl;
        }
    }

    info();
    cout << "正在读写注册表项..." << endl;

    //system("\"C:\\PROGRA~2\\Mythware\\极域课堂管理系统软件v6.0 2019豪华版\\unins000.exe\"");
    //使用8.3路径格式
    ShellExecuteA(NULL, "open",
        "C:\\PROGRA~2\\Mythware\\极域课堂管理系统软件v6.0 2019豪华版\\unins000.exe",
        NULL, NULL, SW_SHOWNORMAL);
    Sleep(200);
    // 执行注册表操作
    if (g_regAction) {
        system("reg add \"HKLM\\SOFTWARE\\Wow6432Node\\TopDomain\\e-Learning Class Standard\\1.00\" /v PreventKill /t REG_DWORD /d 0 /f");
        system("reg add \"HKLM\\SOFTWARE\\Wow6432Node\\TopDomain\\e-Learning Class\\Student\" /v PreventKill /t REG_DWORD /d 0 /f");

        Sleep(500);
    }
    system("taskkill /f /im unins000.exe /t");
    //system("pause");

    // 检查是否被更新器启动（即是否有 /noupdate 参数）
    bool skipUpdate = false;
    LPWSTR cmdLine = GetCommandLineW();
    if (wcsstr(cmdLine, L"/noupdate")) {
        skipUpdate = true;
    }

    LoadSettings();
    // ---------- 自动更新检查（可跳过） ----------
    if (!skipUpdate) {
        wchar_t mainDir[MAX_PATH];
        GetModuleFileNameW(NULL, mainDir, MAX_PATH);
        wchar_t* pSlash = wcsrchr(mainDir, L'\\');
        if (pSlash) *pSlash = L'\0';
        std::wstring updaterPath = std::wstring(mainDir) + L"\\Updater.exe";

        if (GetFileAttributesW(updaterPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            STARTUPINFOW si = { sizeof(si) };
            PROCESS_INFORMATION pi;
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            if (CreateProcessW(updaterPath.c_str(), NULL, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, mainDir, &si, &pi)) {
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                exit(0);   // 退出，交由更新器接管
            }
        }
    }

    initgraph(WIN_W, WIN_H);
    // 在 initgraph 之后立即执行
    wchar_t selfPath[MAX_PATH];
    GetModuleFileNameW(NULL, selfPath, MAX_PATH);
    std::wstring dir = selfPath;
    size_t pos = dir.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        dir = dir.substr(0, pos);
        SetCurrentDirectoryW(dir.c_str());
    }
    SetWindowText(GetHWnd(), WINDOW_TITLE);
    SetWindowLong(GetHWnd(), GWL_STYLE,
        GetWindowLong(GetHWnd(), GWL_STYLE) & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX);
    SetWindowPos(GetHWnd(), nullptr, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    setbkmode(TRANSPARENT);
    settextstyle(FONT_SIZE, 0, L"Consolas");

    InitLayout();
    DrawUI();

    ExMessage msg;
    bool running = true;
    while (running) {
        // ---------- 1. 处理输入 ----------
        while (peekmessage(&msg, EX_MOUSE | EX_KEY, PM_REMOVE)) {
            if (msg.message == WM_LBUTTONDOWN)      ProcessLButtonDown(msg.x, msg.y);
            else if (msg.message == WM_LBUTTONUP)   ProcessLButtonUp();
            else if (msg.message == WM_MOUSEMOVE) {
                if (scrollBarDragging) ProcessScrollDrag(msg.y);
                ProcessMouse(msg.x, msg.y);
            }
            else if (msg.message == WM_MOUSEWHEEL)  ProcessMouseWheel(msg.wheel);
            else if (msg.message == WM_KEYDOWN)     ProcessKeyDown(msg.vkcode);
            // 若 EasyX 支持 WM_CLOSE（部分版本需要额外配置），可以在此捕获
            else if (msg.message == WM_CLOSE)       running = false;
        }

        //后台线程消息 -> GUI
        FlushPendingMessages();


        RealTimeDetection();
        DrawUI();

        if (IsRulesBusy()) {
            settextstyle(14, 0, L"Consolas");
            settextcolor(RGB(255, 200, 0));
            setbkmode(TRANSPARENT);
            // 右上角显示，可自定义位置
            outtextxy(WIN_W - 150, 14, L"防火墙设置中...");
        }

        Sleep(30);
    }

    ShutdownRulesWorker();
    closegraph();
    return 0;
}