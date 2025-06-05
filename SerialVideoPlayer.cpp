#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dshow.h>
#include <iostream>
#include <string>
#include <map>
#include <thread>
#include <mutex>
#include <vector>
#include <shlobj.h>

#pragma comment(lib, "strmiids.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

// 窗口控件ID定义
#define IDC_COMBO_PORT      1001
#define IDC_COMBO_BAUDRATE  1002
#define IDC_BUTTON_CONNECT  1003
#define IDC_BUTTON_DISCONNECT 1004
#define IDC_BUTTON_SELECT_FOLDER 1005
#define IDC_EDIT_LOG        1006
#define IDC_LISTVIEW_MAPPING 1007
#define IDC_BUTTON_ADD_MAPPING 1008
#define IDC_BUTTON_DELETE_MAPPING 1009
#define IDC_EDIT_SIGNAL     1010
#define IDC_EDIT_VIDEO_PATH 1011
#define IDC_BUTTON_BROWSE_VIDEO 1012
#define IDC_EDIT_CURRENT_SIGNAL 1013
#define IDC_BUTTON_USE_SIGNAL   1014

class SerialVideoPlayer {
private:
    HWND hMainWnd;
    HWND hComboPort, hComboBaudRate, hBtnConnect, hBtnDisconnect;
    HWND hBtnSelectFolder, hEditLog, hListViewMapping;
    HWND hEditSignal, hEditVideoPath, hBtnBrowseVideo;
    HWND hBtnAddMapping, hBtnDeleteMapping;
    HWND hEditCurrentSignal, hBtnUseSignal;  // 新增：当前信号显示和使用按钮
    HANDLE hSerial;
    bool isConnected;
    std::map<std::string, std::string> signalVideoMap;
    std::mutex logMutex;
    std::thread serialThread;
    bool stopThread;
    std::string lastDetectedSignal;  // 新增：保存最后检测到的信号
    
    // DirectShow 接口
    IGraphBuilder* pGraph;
    IMediaControl* pControl;
    IMediaEvent* pEvent;
    IVideoWindow* pVideo;
    IBasicAudio* pAudio;
    HWND hVideoWnd;
    
public:
    SerialVideoPlayer() : hSerial(INVALID_HANDLE_VALUE), isConnected(false), 
                         stopThread(false), pGraph(nullptr), pControl(nullptr),
                         pEvent(nullptr), pVideo(nullptr), pAudio(nullptr), hVideoWnd(nullptr) {
        CoInitialize(nullptr);
    }
    
    ~SerialVideoPlayer() {
        Cleanup();
        CoUninitialize();
    }
    
    bool Initialize(HINSTANCE hInstance) {
        // 注册窗口类
        WNDCLASSEX wc = {0};
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"SerialVideoPlayer";
        wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        wc.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);
        
        if (!RegisterClassEx(&wc)) {
            return false;
        }
        
        // 创建主窗口
        hMainWnd = CreateWindowEx(
            0,
            L"SerialVideoPlayer",
            L"串口视频播放器 - 直接信号映射版",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, 900, 700,
            nullptr, nullptr, hInstance, this
        );
        
        if (!hMainWnd) {
            return false;
        }
        
        CreateControls();
        LoadSerialPorts();
        
        ShowWindow(hMainWnd, SW_SHOW);
        UpdateWindow(hMainWnd);
        
        return true;
    }
    
    void CreateControls() {
        HINSTANCE hInst = GetModuleHandle(nullptr);
        
        // 串口设置组
        CreateWindow(L"BUTTON", L"串口设置", WS_VISIBLE | WS_CHILD | BS_GROUPBOX,
                    10, 10, 400, 80, hMainWnd, nullptr, hInst, nullptr);
        
        // 串口选择
        CreateWindow(L"STATIC", L"串口:", WS_VISIBLE | WS_CHILD,
                    20, 35, 50, 20, hMainWnd, nullptr, hInst, nullptr);
        
        hComboPort = CreateWindow(L"COMBOBOX", nullptr,
                                 WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST,
                                 80, 33, 100, 200, hMainWnd, (HMENU)IDC_COMBO_PORT, hInst, nullptr);
        
        // 波特率选择
        CreateWindow(L"STATIC", L"波特率:", WS_VISIBLE | WS_CHILD,
                    190, 35, 60, 20, hMainWnd, nullptr, hInst, nullptr);
        
        hComboBaudRate = CreateWindow(L"COMBOBOX", nullptr,
                                     WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST,
                                     260, 33, 80, 200, hMainWnd, (HMENU)IDC_COMBO_BAUDRATE, hInst, nullptr);
        
        // 添加波特率选项
        SendMessage(hComboBaudRate, CB_ADDSTRING, 0, (LPARAM)L"9600");
        SendMessage(hComboBaudRate, CB_ADDSTRING, 0, (LPARAM)L"19200");
        SendMessage(hComboBaudRate, CB_ADDSTRING, 0, (LPARAM)L"38400");
        SendMessage(hComboBaudRate, CB_ADDSTRING, 0, (LPARAM)L"57600");
        SendMessage(hComboBaudRate, CB_ADDSTRING, 0, (LPARAM)L"115200");
        SendMessage(hComboBaudRate, CB_SETCURSEL, 0, 0); // 默认选择9600
        
        // 连接按钮
        hBtnConnect = CreateWindow(L"BUTTON", L"连接",
                                  WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                                  20, 60, 80, 25, hMainWnd, (HMENU)IDC_BUTTON_CONNECT, hInst, nullptr);
        
        hBtnDisconnect = CreateWindow(L"BUTTON", L"断开",
                                     WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                                     110, 60, 80, 25, hMainWnd, (HMENU)IDC_BUTTON_DISCONNECT, hInst, nullptr);
        EnableWindow(hBtnDisconnect, FALSE);
        
        // 信号检测显示组
        CreateWindow(L"BUTTON", L"当前检测信号", WS_VISIBLE | WS_CHILD | BS_GROUPBOX,
                    10, 100, 400, 60, hMainWnd, nullptr, hInst, nullptr);
        
        // 当前信号显示
        CreateWindow(L"STATIC", L"检测到:", WS_VISIBLE | WS_CHILD,
                    20, 125, 60, 20, hMainWnd, nullptr, hInst, nullptr);
        
        hEditCurrentSignal = CreateWindow(L"EDIT", L"[等待信号...]",
                                         WS_VISIBLE | WS_CHILD | WS_BORDER | ES_READONLY | ES_AUTOHSCROLL,
                                         90, 123, 200, 22, hMainWnd, (HMENU)IDC_EDIT_CURRENT_SIGNAL, hInst, nullptr);
        
        hBtnUseSignal = CreateWindow(L"BUTTON", L"使用此信号",
                                    WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                                    300, 122, 80, 24, hMainWnd, (HMENU)IDC_BUTTON_USE_SIGNAL, hInst, nullptr);
        EnableWindow(hBtnUseSignal, FALSE);  // 初始状态禁用
        
        // 信号映射设置组
        CreateWindow(L"BUTTON", L"信号映射设置", WS_VISIBLE | WS_CHILD | BS_GROUPBOX,
                    420, 10, 460, 150, hMainWnd, nullptr, hInst, nullptr);
        
        // 信号输入
        CreateWindow(L"STATIC", L"信号数据:", WS_VISIBLE | WS_CHILD,
                    430, 35, 80, 20, hMainWnd, nullptr, hInst, nullptr);
        
        hEditSignal = CreateWindow(L"EDIT", nullptr,
                                  WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                                  520, 33, 150, 22, hMainWnd, (HMENU)IDC_EDIT_SIGNAL, hInst, nullptr);
        
        // 视频文件路径
        CreateWindow(L"STATIC", L"视频文件:", WS_VISIBLE | WS_CHILD,
                    430, 65, 80, 20, hMainWnd, nullptr, hInst, nullptr);
        
        hEditVideoPath = CreateWindow(L"EDIT", nullptr,
                                     WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
                                     520, 63, 250, 22, hMainWnd, (HMENU)IDC_EDIT_VIDEO_PATH, hInst, nullptr);
        
        hBtnBrowseVideo = CreateWindow(L"BUTTON", L"浏览",
                                      WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                                      780, 62, 50, 24, hMainWnd, (HMENU)IDC_BUTTON_BROWSE_VIDEO, hInst, nullptr);
        
        // 添加和删除按钮
        hBtnAddMapping = CreateWindow(L"BUTTON", L"添加映射",
                                     WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                                     430, 95, 80, 25, hMainWnd, (HMENU)IDC_BUTTON_ADD_MAPPING, hInst, nullptr);
        
        hBtnDeleteMapping = CreateWindow(L"BUTTON", L"删除映射",
                                        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                                        520, 95, 80, 25, hMainWnd, (HMENU)IDC_BUTTON_DELETE_MAPPING, hInst, nullptr);
        
        // 批量导入按钮
        hBtnSelectFolder = CreateWindow(L"BUTTON", L"批量导入视频文件夹",
                                       WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                                       610, 125, 140, 25, hMainWnd, (HMENU)IDC_BUTTON_SELECT_FOLDER, hInst, nullptr);
        
        // 信号映射列表
        CreateWindow(L"STATIC", L"当前信号映射列表:", WS_VISIBLE | WS_CHILD,
                    10, 170, 150, 20, hMainWnd, nullptr, hInst, nullptr);
        
        hListViewMapping = CreateWindow(WC_LISTVIEW, nullptr,
                                       WS_VISIBLE | WS_CHILD | LVS_REPORT | LVS_SINGLESEL | WS_BORDER,
                                       10, 195, 870, 180, hMainWnd, (HMENU)IDC_LISTVIEW_MAPPING, hInst, nullptr);
        
        // 设置列表视图列
        LVCOLUMN lvc = {0};
        lvc.mask = LVCF_TEXT | LVCF_WIDTH;
        lvc.cx = 200;
        lvc.pszText = (LPWSTR)L"信号数据";
        ListView_InsertColumn(hListViewMapping, 0, &lvc);
        
        lvc.cx = 650;
        lvc.pszText = (LPWSTR)L"视频文件路径";
        ListView_InsertColumn(hListViewMapping, 1, &lvc);
        
        // 启用整行选择和网格线
        ListView_SetExtendedListViewStyle(hListViewMapping, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        
        // 日志文本框
        CreateWindow(L"STATIC", L"接收日志:", WS_VISIBLE | WS_CHILD,
                    10, 385, 80, 20, hMainWnd, nullptr, hInst, nullptr);
        
        hEditLog = CreateWindow(L"EDIT", nullptr,
                               WS_VISIBLE | WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_READONLY | WS_BORDER,
                               10, 410, 870, 245, hMainWnd, (HMENU)IDC_EDIT_LOG, hInst, nullptr);
        
        // 设置字体
        HFONT hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"微软雅黑");
        
        SendMessage(hEditLog, WM_SETFONT, (WPARAM)hFont, TRUE);
    }
    
    void LoadSerialPorts() {
        SendMessage(hComboPort, CB_RESETCONTENT, 0, 0);
        
        for (int i = 1; i <= 256; i++) {
            wchar_t portName[10];
            swprintf_s(portName, L"COM%d", i);
            
            HANDLE hPort = CreateFile(portName, GENERIC_READ | GENERIC_WRITE,
                                     0, nullptr, OPEN_EXISTING, 0, nullptr);
            
            if (hPort != INVALID_HANDLE_VALUE) {
                SendMessage(hComboPort, CB_ADDSTRING, 0, (LPARAM)portName);
                CloseHandle(hPort);
            }
        }
        
        if (SendMessage(hComboPort, CB_GETCOUNT, 0, 0) > 0) {
            SendMessage(hComboPort, CB_SETCURSEL, 0, 0);
        }
    }
    
    bool ConnectSerial() {
        int portIndex = SendMessage(hComboPort, CB_GETCURSEL, 0, 0);
        int baudIndex = SendMessage(hComboBaudRate, CB_GETCURSEL, 0, 0);
        
        if (portIndex == CB_ERR || baudIndex == CB_ERR) {
            MessageBox(hMainWnd, L"请选择串口和波特率!", L"错误", MB_OK | MB_ICONWARNING);
            return false;
        }
        
        wchar_t portName[20];
        SendMessage(hComboPort, CB_GETLBTEXT, portIndex, (LPARAM)portName);
        
        hSerial = CreateFile(portName, GENERIC_READ | GENERIC_WRITE,
                            0, nullptr, OPEN_EXISTING, 0, nullptr);
        
        if (hSerial == INVALID_HANDLE_VALUE) {
            MessageBox(hMainWnd, L"无法打开串口!", L"错误", MB_OK | MB_ICONERROR);
            return false;
        }
        
        // 配置串口参数
        DCB dcb = {0};
        dcb.DCBlength = sizeof(DCB);
        GetCommState(hSerial, &dcb);
        
        int baudRates[] = {9600, 19200, 38400, 57600, 115200};
        dcb.BaudRate = baudRates[baudIndex];
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        
        if (!SetCommState(hSerial, &dcb)) {
            CloseHandle(hSerial);
            hSerial = INVALID_HANDLE_VALUE;
            MessageBox(hMainWnd, L"配置串口失败!", L"错误", MB_OK | MB_ICONERROR);
            return false;
        }
        
        // 设置超时
        COMMTIMEOUTS timeouts = {0};
        timeouts.ReadIntervalTimeout = 50;
        timeouts.ReadTotalTimeoutConstant = 50;
        timeouts.ReadTotalTimeoutMultiplier = 10;
        SetCommTimeouts(hSerial, &timeouts);
        
        isConnected = true;
        EnableWindow(hBtnConnect, FALSE);
        EnableWindow(hBtnDisconnect, TRUE);
        
        // 启动串口读取线程
        stopThread = false;
        serialThread = std::thread(&SerialVideoPlayer::SerialReadThread, this);
        
        LogMessage(L"串口连接成功: " + std::wstring(portName));
        return true;
    }
    
    void DisconnectSerial() {
        if (hSerial != INVALID_HANDLE_VALUE) {
            stopThread = true;
            if (serialThread.joinable()) {
                serialThread.join();
            }
            
            CloseHandle(hSerial);
            hSerial = INVALID_HANDLE_VALUE;
        }
        
        isConnected = false;
        EnableWindow(hBtnConnect, TRUE);
        EnableWindow(hBtnDisconnect, FALSE);
        
        // 重置信号检测显示
        lastDetectedSignal.clear();
        SetWindowText(hEditCurrentSignal, L"[等待信号...]");
        EnableWindow(hBtnUseSignal, FALSE);
        
        LogMessage(L"串口已断开连接");
    }
    
    void SerialReadThread() {
        char buffer[256];
        DWORD bytesRead;
        std::string receivedData;
        
        while (!stopThread && hSerial != INVALID_HANDLE_VALUE) {
            if (ReadFile(hSerial, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) && bytesRead > 0) {
                buffer[bytesRead] = '\0';
                receivedData += buffer;
                
                // 检查是否有完整的行（以换行符结束）
                size_t pos;
                while ((pos = receivedData.find('\n')) != std::string::npos) {
                    std::string line = receivedData.substr(0, pos);
                    receivedData.erase(0, pos + 1);
                    
                    // 移除回车符
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    
                    // 处理接收到的数据
                    if (!line.empty()) {
                        ProcessReceivedData(line);
                    }
                }
            }
            Sleep(10);
        }
    }
    
    void ProcessReceivedData(const std::string& data) {
        // 转换为宽字符
        std::wstring wdata(data.begin(), data.end());
        LogMessage(L"接收到数据: [" + wdata + L"]");
        
        // 更新当前信号显示
        lastDetectedSignal = data;
        SetWindowText(hEditCurrentSignal, wdata.c_str());
        EnableWindow(hBtnUseSignal, TRUE);  // 启用"使用此信号"按钮
        
        // 设置当前信号显示框的背景色为浅绿色，表示有新信号
        InvalidateRect(hEditCurrentSignal, nullptr, TRUE);
        
        // 直接使用接收到的完整数据作为信号进行映射
        auto it = signalVideoMap.find(data);
        if (it != signalVideoMap.end()) {
            std::wstring wvideoPath(it->second.begin(), it->second.end());
            LogMessage(L"找到匹配信号，播放视频: " + wvideoPath);
            PlayVideo(wvideoPath);
        } else {
            LogMessage(L"未找到信号 [" + wdata + L"] 对应的视频文件");
        }
    }
    
    void PlayVideo(const std::wstring& videoPath) {
        // 清理之前的DirectShow对象
        CleanupDirectShow();
        
        // 创建DirectShow图形
        HRESULT hr = CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER,
                                     IID_IGraphBuilder, (void**)&pGraph);
        if (FAILED(hr)) {
            LogMessage(L"创建DirectShow图形失败");
            return;
        }
        
        // 获取接口
        pGraph->QueryInterface(IID_IMediaControl, (void**)&pControl);
        pGraph->QueryInterface(IID_IMediaEvent, (void**)&pEvent);
        pGraph->QueryInterface(IID_IVideoWindow, (void**)&pVideo);
        pGraph->QueryInterface(IID_IBasicAudio, (void**)&pAudio);
        
        // 渲染文件
        hr = pGraph->RenderFile(videoPath.c_str(), nullptr);
        if (FAILED(hr)) {
            LogMessage(L"无法加载视频文件: " + videoPath);
            CleanupDirectShow();
            return;
        }
        
        // 创建全屏视频窗口
        CreateVideoWindow();
        
        // 设置视频窗口
        if (pVideo) {
            pVideo->put_Owner((OAHWND)hVideoWnd);
            pVideo->put_WindowStyle(WS_CHILD | WS_CLIPSIBLINGS);
            
            RECT rc;
            GetClientRect(hVideoWnd, &rc);
            pVideo->SetWindowPosition(0, 0, rc.right, rc.bottom);
        }
        
        // 播放视频
        if (pControl) {
            pControl->Run();
            LogMessage(L"开始播放视频");
        }
        
        // 监听播放完成事件
        std::thread([this]() {
            if (pEvent) {
                long eventCode, param1, param2;
                while (pEvent->WaitForCompletion(100, &eventCode) == S_FALSE) {
                    if (stopThread) break;
                }
                
                // 播放完成，关闭视频窗口
                PostMessage(hVideoWnd, WM_CLOSE, 0, 0);
                LogMessage(L"视频播放完成");
            }
        }).detach();
    }
    
    void CreateVideoWindow() {
        if (hVideoWnd) {
            DestroyWindow(hVideoWnd);
        }
        
        WNDCLASS wc = {0};
        wc.lpfnWndProc = VideoWindowProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = L"VideoWindow";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        
        RegisterClass(&wc);
        
        // 创建全屏窗口
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        
        hVideoWnd = CreateWindowEx(
            WS_EX_TOPMOST,
            L"VideoWindow",
            L"视频播放",
            WS_POPUP,
            0, 0, screenWidth, screenHeight,
            nullptr, nullptr, GetModuleHandle(nullptr), this
        );
        
        ShowWindow(hVideoWnd, SW_SHOW);
        SetForegroundWindow(hVideoWnd);
    }
    
    void CleanupDirectShow() {
        if (pControl) {
            pControl->Stop();
            pControl->Release();
            pControl = nullptr;
        }
        
        if (pVideo) {
            pVideo->Release();
            pVideo = nullptr;
        }
        
        if (pAudio) {
            pAudio->Release();
            pAudio = nullptr;
        }
        
        if (pEvent) {
            pEvent->Release();
            pEvent = nullptr;
        }
        
        if (pGraph) {
            pGraph->Release();
            pGraph = nullptr;
        }
        
        if (hVideoWnd) {
            DestroyWindow(hVideoWnd);
            hVideoWnd = nullptr;
        }
    }
    
    void AddMapping() {
        wchar_t signal[256], videoPath[MAX_PATH];
        GetWindowText(hEditSignal, signal, 256);
        GetWindowText(hEditVideoPath, videoPath, MAX_PATH);
        
        if (wcslen(signal) == 0 || wcslen(videoPath) == 0) {
            MessageBox(hMainWnd, L"请输入信号数据和视频文件路径!", L"提示", MB_OK | MB_ICONWARNING);
            return;
        }
        
        // 检查文件是否存在
        if (GetFileAttributes(videoPath) == INVALID_FILE_ATTRIBUTES) {
            MessageBox(hMainWnd, L"视频文件不存在!", L"错误", MB_OK | MB_ICONERROR);
            return;
        }
        
        // 转换为string
        std::string signalStr(signal, signal + wcslen(signal));
        std::string videoPathStr(videoPath, videoPath + wcslen(videoPath));
        
        // 添加到映射
        signalVideoMap[signalStr] = videoPathStr;
        
        // 添加到列表视图
        LVITEM lvi = {0};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = ListView_GetItemCount(hListViewMapping);
        lvi.pszText = signal;
        ListView_InsertItem(hListViewMapping, &lvi);
        
        ListView_SetItemText(hListViewMapping, lvi.iItem, 1, videoPath);
        
        // 清空输入框
        SetWindowText(hEditSignal, L"");
        SetWindowText(hEditVideoPath, L"");
        
        LogMessage(L"添加映射: [" + std::wstring(signal) + L"] -> " + std::wstring(videoPath));
    }
    
    void UseCurrentSignal() {
        if (lastDetectedSignal.empty()) {
            MessageBox(hMainWnd, L"当前没有检测到信号!", L"提示", MB_OK | MB_ICONWARNING);
            return;
        }
        
        // 将当前检测到的信号填入信号输入框
        std::wstring wsignal(lastDetectedSignal.begin(), lastDetectedSignal.end());
        SetWindowText(hEditSignal, wsignal.c_str());
        
        // 将焦点设置到视频文件路径输入框，方便用户继续操作
        SetFocus(hEditVideoPath);
        
        LogMessage(L"已将信号 [" + wsignal + L"] 填入映射设置");
    }
    
    void DeleteMapping() {
        int selectedIndex = ListView_GetNextItem(hListViewMapping, -1, LVNI_SELECTED);
        if (selectedIndex == -1) {
            MessageBox(hMainWnd, L"请选择要删除的映射项!", L"提示", MB_OK | MB_ICONWARNING);
            return;
        }
        
        // 获取信号数据
        wchar_t signal[256];
        ListView_GetItemText(hListViewMapping, selectedIndex, 0, signal, 256);
        
        // 从映射中删除
        std::string signalStr(signal, signal + wcslen(signal));
        signalVideoMap.erase(signalStr);
        
        // 从列表视图中删除
        ListView_DeleteItem(hListViewMapping, selectedIndex);
        
        LogMessage(L"删除映射: [" + std::wstring(signal) + L"]");
    }
    
    void BrowseVideoFile() {
        OPENFILENAME ofn = {0};
        wchar_t fileName[MAX_PATH] = L"";
        
        ofn.lStructSize = sizeof(OPENFILENAME);
        ofn.hwndOwner = hMainWnd;
        ofn.lpstrFile = fileName;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrFilter = L"视频文件\0*.mp4;*.avi;*.mkv;*.wmv;*.mov;*.flv;*.webm\0所有文件\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.lpstrTitle = L"选择视频文件";
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
        
        if (GetOpenFileName(&ofn)) {
            SetWindowText(hEditVideoPath, fileName);
        }
    }
    
    void SelectVideoFolder() {
        BROWSEINFO bi = {0};
        bi.hwndOwner = hMainWnd;
        bi.lpszTitle = L"选择包含视频文件的文件夹（将自动生成数字信号映射）";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        
        LPITEMIDLIST pidl = SHBrowseForFolder(&bi);
        if (pidl) {
            wchar_t folderPath[MAX_PATH];
            if (SHGetPathFromIDList(pidl, folderPath)) {
                LoadVideoFiles(folderPath);
            }
            CoTaskMemFree(pidl);
        }
    }
    
    void LoadVideoFiles(const std::wstring& folderPath) {
        // 清空现有映射
        ListView_DeleteAllItems(hListViewMapping);
        signalVideoMap.clear();
        
        // 搜索视频文件
        std::wstring searchPath = folderPath + L"\\*.*";
        WIN32_FIND_DATA findData;
        HANDLE hFind = FindFirstFile(searchPath.c_str(), &findData);
        
        int signalIndex = 1;
        
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    std::wstring fileName = findData.cFileName;
                    std::wstring ext = fileName.substr(fileName.find_last_of(L'.'));
                    
                    // 转换为小写进行比较
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
                    
                    // 检查是否为支持的视频格式
                    if (ext == L".mp4" || ext == L".avi" || ext == L".mkv" || 
                        ext == L".wmv" || ext == L".mov" || ext == L".flv" || ext == L".webm") {
                        
                        std::wstring fullPath = folderPath + L"\\" + fileName;
                        std::string signal = std::to_string(signalIndex++); // 使用数字作为信号
                        std::string fullPathA(fullPath.begin(), fullPath.end());
                        
                        signalVideoMap[signal] = fullPathA;
                        
                        // 添加到列表视图
                        LVITEM lvi = {0};
                        lvi.mask = LVIF_TEXT;
                        lvi.iItem = ListView_GetItemCount(hListViewMapping);
                        std::wstring wsignal(signal.begin(), signal.end());
                        lvi.pszText = (LPWSTR)wsignal.c_str();
                        ListView_InsertItem(hListViewMapping, &lvi);
                        
                        ListView_SetItemText(hListViewMapping, lvi.iItem, 1, (LPWSTR)fullPath.c_str());
                    }
                }
            } while (FindNextFile(hFind, &findData));
            
            FindClose(hFind);
        }
        
        LogMessage(L"批量导入完成，共加载 " + std::to_wstring(signalVideoMap.size()) + L" 个视频文件");
        LogMessage(L"信号格式: 1, 2, 3, ... (纯数字)");
    }
    
    void LogMessage(const std::wstring& message) {
        std::lock_guard<std::mutex> lock(logMutex);
        
        SYSTEMTIME st;
        GetLocalTime(&st);
        
        wchar_t timestamp[32];
        swprintf_s(timestamp, L"[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
        
        std::wstring logEntry = timestamp + message + L"\r\n";
        
        // 添加到日志文本框
        int textLength = GetWindowTextLength(hEditLog);
        SendMessage(hEditLog, EM_SETSEL, textLength, textLength);
        SendMessage(hEditLog, EM_REPLACESEL, FALSE, (LPARAM)logEntry.c_str());
        SendMessage(hEditLog, EM_SCROLLCARET, 0, 0);
    }
    
    void Cleanup() {
        DisconnectSerial();
        CleanupDirectShow();
    }
    
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        SerialVideoPlayer* pThis = nullptr;
        
        if (uMsg == WM_NCCREATE) {
            CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
            pThis = (SerialVideoPlayer*)pCreate->lpCreateParams;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
        } else {
            pThis = (SerialVideoPlayer*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        }
        
        if (pThis) {
            return pThis->HandleMessage(hwnd, uMsg, wParam, lParam);
        }
        
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    
    static LRESULT CALLBACK VideoWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE || wParam == VK_RETURN || wParam == VK_SPACE) {
                DestroyWindow(hwnd);
            }
            break;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
            DestroyWindow(hwnd);
            break;
        case WM_DESTROY:
            return 0;
        }
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    
    LRESULT HandleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        switch (uMsg) {
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
            case IDC_BUTTON_CONNECT:
                ConnectSerial();
                break;
            case IDC_BUTTON_DISCONNECT:
                DisconnectSerial();
                break;
            case IDC_BUTTON_ADD_MAPPING:
                AddMapping();
                break;
            case IDC_BUTTON_DELETE_MAPPING:
                DeleteMapping();
                break;
            case IDC_BUTTON_BROWSE_VIDEO:
                BrowseVideoFile();
                break;
            case IDC_BUTTON_SELECT_FOLDER:
                SelectVideoFolder();
                break;
            case IDC_BUTTON_USE_SIGNAL:
                UseCurrentSignal();
                break;
            }
            break;
        case WM_DESTROY:
            Cleanup();
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }
        return 0;
    }
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    InitCommonControls();
    
    SerialVideoPlayer player;
    if (!player.Initialize(hInstance)) {
        MessageBox(nullptr, L"初始化失败!", L"错误", MB_OK | MB_ICONERROR);
        return 1;
    }
    
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    return (int)msg.wParam;
}