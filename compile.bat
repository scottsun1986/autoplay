@echo off
echo Compiling Serial Video Player...
echo.

cl /EHsc /MT SerialVideoPlayer.cpp /Fe:SerialVideoPlayer.exe /link user32.lib gdi32.lib comctl32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib strmiids.lib

echo.
echo Compilation completed.
echo.

if exist *.obj del *.obj

pause