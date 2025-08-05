from time import sleep
from pymem import Pymem
from pyperclip import copy
from re import findall
TARGET_PROCESS = "YCursor.exe"
TARGET_STRING = bytes.fromhex("63 6F 64 65 00 00 00 00 00 00 00 00 00 00 00 00")
#地址偏移量
POINTER_OFFSET = 64



def main():
    while True:
        try:
            pm = Pymem(TARGET_PROCESS,exact_match=True)
            print("进程已找到")
            break
        except Exception as e:
            if e.args[0] == f"Could not find process: {TARGET_PROCESS}":
                print(f"进程未找到，请启动{TARGET_PROCESS}进程")
            else:
                print(f"错误: {e}")
            sleep(1)
    while True:
        try:
            found_address = pm.pattern_scan_all(TARGET_STRING, return_multiple=True)
            if not found_address or len(found_address) == 0:
                raise Exception("未找到目标字符串地址")
            print(f"找到地址数量: {len(found_address)}")
            break
        except Exception as e:
            print(f"错误: {e}")
            sleep(1)
    code = None  # 修复未定义变量
    for address in found_address:
        try:
            # 读取字节
            memory_chunk = pm.read_bytes(address + POINTER_OFFSET , 6)
            matches = findall(b"[A-Z0-9]{6}", memory_chunk)
            if matches:
                code = matches[0]
                break  # 找到后跳出循环
        except Exception as e:
            print(f"错误: {e}")
    if code:
        print("CODE:", code.decode("ascii"))  # 字节转字符串
        #复制到剪贴板
        copy(code)
        input("已复制到剪贴板，按回车键继续...")
    else:
        print("未找到code，重试中...")
        sleep(1)
        main()
        

if __name__ == "__main__":
    main()






