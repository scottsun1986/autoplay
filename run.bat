@echo off
echo 串口视频播放器 - 直接信号映射版
echo =====================================
echo.

:: 检查可执行文件是否存在
if exist "SerialVideoPlayer.exe" (
    echo 启动程序...
    start "" "SerialVideoPlayer.exe"
    echo 程序已启动!
) else (
    echo 错误: 未找到 SerialVideoPlayer.exe
    echo 请先运行 compile.bat 编译程序
    echo.
    pause
)