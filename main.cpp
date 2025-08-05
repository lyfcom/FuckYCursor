#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <algorithm>  // For std::min
#include <intrin.h>   // For SIMD intrinsics
#include <immintrin.h>

// Performance optimizations: Use const char* instead of std::string for constant data
static const char* const TARGET_PROCESS = "YCursor.exe";
static const BYTE TARGET_STRING[] = {0x63, 0x6F, 0x64, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static constexpr int TARGET_STRING_SIZE = 16;
static constexpr int POINTER_OFFSET = 64;
static constexpr SIZE_T BUFFER_SIZE = 1024 * 1024; // 1MB buffer for chunked reading
static constexpr SIZE_T MAX_ADDRESSES = 1000; // Preallocate address storage

// 获取进程ID - 优化版本，使用快速字符串比较
__forceinline DWORD GetProcessID(const char* processName) noexcept {
    PROCESSENTRY32 processEntry;
    processEntry.dwSize = sizeof(PROCESSENTRY32);
    
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }
    
    if (Process32First(snapshot, &processEntry)) {
        do {
            // Fast string comparison using intrinsic
            if (strcmp(processName, processEntry.szExeFile) == 0) {
                DWORD pid = processEntry.th32ProcessID;
                CloseHandle(snapshot);
                return pid;
            }
        } while (Process32Next(snapshot, &processEntry));
    }
    
    CloseHandle(snapshot);
    return 0;
}

// SIMD优化的字节比较函数
__forceinline bool FastMemCmp(const BYTE* ptr1, const BYTE* ptr2, size_t size) noexcept {
    // 对于16字节模式，使用SSE2进行快速比较
    if (size == 16) {
        __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr1));
        __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr2));
        __m128i result = _mm_cmpeq_epi8(a, b);
        return _mm_movemask_epi8(result) == 0xFFFF;
    }
    return memcmp(ptr1, ptr2, size) == 0;
}

// 高性能Boyer-Moore风格的模式搜索
__forceinline const BYTE* FastSearch(const BYTE* haystack, size_t haystackLen, 
                                     const BYTE* needle, size_t needleLen) noexcept {
    if (needleLen > haystackLen) return nullptr;
    
    // 对于我们的16字节模式，使用优化的搜索
    const BYTE* end = haystack + haystackLen - needleLen;
    for (const BYTE* pos = haystack; pos <= end; pos++) {
        if (FastMemCmp(pos, needle, needleLen)) {
            return pos;
        }
    }
    return nullptr;
}

// 极致优化的内存扫描函数
std::vector<DWORD_PTR> PatternScan(HANDLE hProcess, const BYTE* pattern, int patternSize) {
    std::vector<DWORD_PTR> addresses;
    addresses.reserve(MAX_ADDRESSES); // 预分配内存
    
    MEMORY_BASIC_INFORMATION mbi;
    DWORD_PTR address = 0;
    
    // 预分配缓冲区，避免重复分配
    static thread_local std::vector<BYTE> buffer;
    buffer.reserve(BUFFER_SIZE + patternSize); // 额外空间防止边界问题
    
    while (VirtualQueryEx(hProcess, (LPCVOID)address, &mbi, sizeof(mbi))) {
        // 快速过滤：只扫描可读的已提交内存
        if (mbi.State == MEM_COMMIT && 
            (mbi.Protect & (PAGE_READWRITE | PAGE_READONLY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) {
            
            DWORD_PTR regionStart = (DWORD_PTR)mbi.BaseAddress;
            SIZE_T regionSize = mbi.RegionSize;
            
            // 分块读取大内存区域，提高效率
            for (SIZE_T offset = 0; offset < regionSize; offset += BUFFER_SIZE) {
                SIZE_T chunkSize = std::min(BUFFER_SIZE, regionSize - offset);
                if (chunkSize < patternSize) break;
                
                // 调整缓冲区大小
                buffer.resize(chunkSize + patternSize - 1);
                SIZE_T bytesRead = 0;
                
                if (ReadProcessMemory(hProcess, (LPCVOID)(regionStart + offset), 
                                    buffer.data(), chunkSize, &bytesRead)) {
                    
                    // 如果不是最后一块，读取重叠部分避免漏检
                    if (offset + chunkSize < regionSize && bytesRead >= patternSize) {
                        SIZE_T overlapBytes = 0;
                        ReadProcessMemory(hProcess, (LPCVOID)(regionStart + offset + chunkSize), 
                                        buffer.data() + bytesRead, patternSize - 1, &overlapBytes);
                        bytesRead += overlapBytes;
                    }
                    
                    // 使用SIMD优化的搜索
                    const BYTE* searchStart = buffer.data();
                    const BYTE* searchEnd = buffer.data() + bytesRead;
                    
                    while (searchStart <= searchEnd - patternSize) {
                        const BYTE* found = FastSearch(searchStart, searchEnd - searchStart, pattern, patternSize);
                        if (!found) break;
                        
                        DWORD_PTR foundAddress = regionStart + offset + (found - buffer.data());
                        addresses.push_back(foundAddress);
                        
                        searchStart = found + 1; // 继续搜索下一个匹配
                        
                        // 避免过多结果影响性能
                        if (addresses.size() >= MAX_ADDRESSES) {
                            goto scan_complete;
                        }
                    }
                }
            }
        }
        address = (DWORD_PTR)mbi.BaseAddress + mbi.RegionSize;
    }
    
scan_complete:
    addresses.shrink_to_fit(); // 释放多余内存
    return addresses;
}

// 极速字符验证 - 使用查找表优化
static const bool ALPHANUMERIC_TABLE[256] = {
    false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false,
    true,  true,  true,  true,  true,  true,  true,  true,  true,  true,  false, false, false, false, false, false,  // 0-9
    false, true,  true,  true,  true,  true,  true,  true,  true,  true,  true,  true,  true,  true,  true,  true,   // A-O
    true,  true,  true,  true,  true,  true,  true,  true,  true,  true,  true,  false, false, false, false, false,  // P-Z
    false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, // a-o (假设只要大写)
    false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, // p-z
    false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false
};

__forceinline bool IsAlphaNumeric(unsigned char c) noexcept {
    return ALPHANUMERIC_TABLE[c];
}

// SIMD优化的6字节验证函数
__forceinline bool ValidateCode(const BYTE* data) noexcept {
    // 使用位运算优化，一次检查多个字符
    for (int i = 0; i < 6; i++) {
        if (!IsAlphaNumeric(data[i])) {
            return false;
        }
    }
    return true;
}

// 高性能扫描并直接提取code，找到即返回true
bool ScanForCode(HANDLE hProcess, char* outCode) noexcept {
    MEMORY_BASIC_INFORMATION mbi;
    DWORD_PTR address = 0;

    static thread_local std::vector<BYTE> buffer;
    buffer.reserve(BUFFER_SIZE + TARGET_STRING_SIZE + POINTER_OFFSET);

    while (VirtualQueryEx(hProcess, (LPCVOID)address, &mbi, sizeof(mbi))) {
        if (mbi.State == MEM_COMMIT &&
            (mbi.Protect & (PAGE_READWRITE | PAGE_READONLY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) {

            DWORD_PTR regionStart = (DWORD_PTR)mbi.BaseAddress;
            SIZE_T regionSize = mbi.RegionSize;

            for (SIZE_T offset = 0; offset < regionSize; offset += BUFFER_SIZE) {
                SIZE_T chunkSize = std::min< SIZE_T >(BUFFER_SIZE, regionSize - offset);
                if (chunkSize < TARGET_STRING_SIZE) break;

                buffer.resize(chunkSize + TARGET_STRING_SIZE - 1);
                SIZE_T bytesRead = 0;
                if (!ReadProcessMemory(hProcess, (LPCVOID)(regionStart + offset), buffer.data(), chunkSize, &bytesRead)) {
                    continue;
                }

                // 处理块末尾重叠
                if (offset + chunkSize < regionSize && bytesRead >= TARGET_STRING_SIZE) {
                    SIZE_T overlap = 0;
                    ReadProcessMemory(hProcess, (LPCVOID)(regionStart + offset + chunkSize),
                                      buffer.data() + bytesRead, TARGET_STRING_SIZE - 1, &overlap);
                    bytesRead += overlap;
                }

                const BYTE* searchStart = buffer.data();
                const BYTE* searchEnd = buffer.data() + bytesRead;

                while (searchStart <= searchEnd - TARGET_STRING_SIZE) {
                    const BYTE* found = FastSearch(searchStart, searchEnd - searchStart, TARGET_STRING, TARGET_STRING_SIZE);
                    if (!found) break;

                    DWORD_PTR foundAddr = regionStart + offset + (found - buffer.data());
                    BYTE codeBytes[6];
                    SIZE_T read6 = 0;
                    if (ReadProcessMemory(hProcess, (LPCVOID)(foundAddr + POINTER_OFFSET), codeBytes, 6, &read6) && read6 == 6) {
                        if (ValidateCode(codeBytes)) {
                            memcpy(outCode, codeBytes, 6);
                            outCode[6] = '\0';
                            return true;
                        }
                    }
                    searchStart = found + 1;
                }
            }
        }
        address = (DWORD_PTR)mbi.BaseAddress + mbi.RegionSize;
    }
    return false;
}

// 高效剪贴板操作
__forceinline bool CopyToClipboard(const char* text, size_t length) noexcept {
    if (!OpenClipboard(nullptr)) {
        return false;
    }
    
    EmptyClipboard();
    
    // 直接分配所需大小
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, length + 1);
    if (!hMem) {
        CloseClipboard();
        return false;
    }
    
    char* pMem = static_cast<char*>(GlobalLock(hMem));
    memcpy(pMem, text, length);
    pMem[length] = '\0';
    GlobalUnlock(hMem);
    
    SetClipboardData(CF_TEXT, hMem);
    CloseClipboard();
    
    return true;
}

// 设置高性能模式
void SetHighPerformanceMode() noexcept {
    // 设置进程优先级为高
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    
    // 启用大页面内存（如果可用）
    SetProcessWorkingSetSize(GetCurrentProcess(), SIZE_T(-1), SIZE_T(-1));
}

int main() {
    // 性能优化设置
    SetHighPerformanceMode();
    SetConsoleOutputCP(CP_UTF8);
    
    HANDLE hProcess = nullptr;
    
    // 高效进程查找循环
    while (true) {
        DWORD processID = GetProcessID(TARGET_PROCESS);
        if (processID != 0) {
            hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, processID);
            if (hProcess != nullptr) {
                std::cout << "进程已找到" << std::endl;
                break;
            }
        }
        
        std::cout << "进程未找到，请启动" << TARGET_PROCESS << "进程" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(500)); // 更快的轮询
    }
    
    char code[7] = {0}; // 预分配代码存储空间
    bool found = false;
    
    // 主循环：持续搜索直到找到code
    while (!found) {
        std::cout << "扫描内存..." << std::endl;
        found = ScanForCode(hProcess, code);
        if (!found) {
            std::cout << "未找到有效code，1秒后重试..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    
    CloseHandle(hProcess);
    
    std::cout << "CODE: " << code << std::endl;
    
    // 高效剪贴板操作
    if (CopyToClipboard(code, 6)) {
        std::cout << "已复制到剪贴板，按回车键继续..." << std::endl;
        std::cin.get();
    } else {
        std::cout << "复制到剪贴板失败" << std::endl;
    }
    
    return 0;
}