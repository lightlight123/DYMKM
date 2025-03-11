import os
import subprocess
import sys

def get_container_pids(container_id):
    """获取容器中所有进程的 PIDs"""
    try:
        # 使用 docker top 获取容器内的所有进程
        cmd = f"docker top {container_id} -eo pid"
        result = subprocess.check_output(cmd, shell=True).decode().strip().splitlines()

        # 排除表头并返回 PID 列表
        return [int(pid) for pid in result[1:]]
    except subprocess.CalledProcessError:
        print(f"错误: 找不到容器: {container_id}")
    except Exception as e:
        print(f"获取容器 PIDs 时出错: {e}")
    return []

def get_memory_mappings(pid):
    """获取指定 PID 的内存映射"""
    mappings = []
    try:
        with open(f"/proc/{pid}/maps", 'r') as f:
            for line in f:
                mappings.append(line.strip())
    except FileNotFoundError:
        print(f"PID 为 {pid} 的进程未找到。")
    except Exception as e:
        print(f"读取内存映射时出错: {e}")
    return mappings

def get_physical_address(virtual_address, pid):
    """获取与虚拟地址对应的物理地址"""
    try:
        pagemap_path = f"/proc/{pid}/pagemap"
        page_size = 4096
        page_index = virtual_address // page_size

        with open(pagemap_path, 'rb') as f:
            f.seek(page_index * 8)  # 每个条目为 8 字节
            entry = f.read(8)
            if not entry:
                return None
            
            # 获取物理帧号 (PFN)
            pfn = int.from_bytes(entry[:8], 'little') & 0x7FFFFFFFFFFFFF
            in_memory = (entry[7] & 0x80) != 0  # 判断第 63 位是否为 1
            if not in_memory:
                return None
            
            # 计算物理地址
            physical_address = (pfn * page_size) + (virtual_address % page_size)
            return pfn, physical_address
    except Exception as e:
        print(f"获取虚拟地址 {hex(virtual_address)} 的物理地址时出错: {e}")
    return None

def main(container_id):
    # 步骤 1: 获取容器中所有进程的 PIDs
    pids = get_container_pids(container_id)
    
    if pids:
        for pid in pids:
            print(f"\nPID: {pid}")

            # 步骤 2: 获取进程的内存映射
            mappings = get_memory_mappings(pid)
            if not mappings:
                print("未找到内存映射。")
                continue
            
            # 过滤包含 'x' 的内存映射条目
            mappings_with_x = [m for m in mappings if 'x' in m]
            
            if not mappings_with_x:
                print("未找到包含 'x' 的内存映射。")
                continue
            
            # 步骤 3: 处理每个内存映射条目
            mapping = mappings_with_x[0]
            parts = mapping.split()
            address_range = parts[0]
            start_address, end_address = map(lambda x: int(x, 16), address_range.split('-'))
            print(f"Start: {hex(start_address)}, End: {hex(end_address)}")
            
            # 步骤 4: 获取虚拟地址对应的物理地址
            for address in range(start_address, end_address + 1, 4096):
                result = get_physical_address(address, pid)
                if result is not None:
                    pfn, physical_address = result
                    offset = address % 4096
                    print(f"vaddr: {hex(address)}, offset: {offset}, pfn: {pfn}, "
                          f"paddr: {hex(physical_address)}")
                else:
                    print(f"vaddr: {hex(address)} 未加载到物理内存。")
    else:
        print(f"无法获取容器 {container_id} 的 PIDs")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("用法: python integrity_measurement.py <container_id>")
        sys.exit(1)
    
    container_id = sys.argv[1]
    main(container_id)
