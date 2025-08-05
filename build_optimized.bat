:: 使用utf-8编码
chcp 65001
@echo off
echo 正在编译极致优化的C++程序...

:: 设置编译选项
set COMMON_FLAGS=-std=c++17 -march=native -mtune=native -msse2 -mavx -mfma
set OPT_FLAGS=-O3 -flto -ffast-math -funroll-loops -fomit-frame-pointer -finline-functions
set LINK_FLAGS=-s -static -static-libgcc -static-libstdc++ -Wl,--gc-sections -Wl,--strip-all
set DEF_FLAGS=-DNDEBUG -D_WIN32_WINNT=0x0601

:: 准备manifest资源文件
echo 1 24 "manifest.xml" > temp_manifest.rc

:: 检查是否安装了g++编译器
g++ --version >nul 2>&1
if %errorlevel%==0 (
    echo 使用g++编译器进行极致优化编译...
    echo 编译资源文件...
    windres temp_manifest.rc -o temp_manifest.o 2>nul
    if %errorlevel%==0 (
        echo 编译主程序（包含UAC管理员权限）...
        g++ %COMMON_FLAGS% %OPT_FLAGS% %DEF_FLAGS% main.cpp temp_manifest.o -o FuckYCursor_optimized.exe %LINK_FLAGS% -lpsapi
        del temp_manifest.o
    ) else (
        echo 资源文件编译失败，使用无manifest版本...
        g++ %COMMON_FLAGS% %OPT_FLAGS% %DEF_FLAGS% main.cpp -o FuckYCursor_optimized.exe %LINK_FLAGS% -lpsapi
    )
    del temp_manifest.rc
    
    if %errorlevel%==0 (
        echo 极致优化编译成功！生成文件：FuckYCursor_optimized.exe
        echo 程序已配置为自动请求管理员权限
        
        :: 显示文件大小
        for %%I in (FuckYCursor_optimized.exe) do echo 文件大小: %%~zI 字节
        goto :end
    ) else (
        echo g++极致优化编译失败，尝试标准优化...
        g++ -std=c++11 -O3 -s -static -static-libgcc -static-libstdc++ main.cpp -o FuckYCursor_cpp.exe -lpsapi
        if %errorlevel%==0 (
            echo 标准优化编译成功！生成文件：FuckYCursor_cpp.exe
            echo 注意：此版本需要手动以管理员身份运行
            goto :end
        )
    )
)

:: 检查是否有Visual Studio编译器
where cl >nul 2>&1
if %errorlevel%==0 (
    echo 使用MSVC编译器进行极致优化编译...
    cl /EHsc /O2 /Ox /Ot /GL /Gy /MT /DNDEBUG /arch:AVX2 main.cpp /Fe:FuckYCursor_optimized.exe psapi.lib /link /LTCG /OPT:REF /OPT:ICF /MANIFEST:EMBED /MANIFESTINPUT:manifest.xml
    if %errorlevel%==0 (
        echo 极致优化编译成功！生成文件：FuckYCursor_optimized.exe
        echo 程序已配置为自动请求管理员权限
        del main.obj
        goto :end
    ) else (
        echo MSVC极致优化编译失败
    )
)

echo 错误：未找到合适的C++编译器
echo 请安装MinGW-w64或Visual Studio Build Tools

:end
echo.
echo 性能优化说明：
echo - 使用了SIMD指令集(SSE2/AVX)加速内存比较
echo - 启用了链接时优化(LTO)
echo - 使用查找表加速字符验证
echo - 分块内存读取减少内存占用
echo - 静态链接，无依赖运行
echo - 高优先级进程模式
pause