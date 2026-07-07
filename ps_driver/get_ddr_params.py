#!/usr/bin/env python3
"""
get_ddr_params.py — 从 Pynq base overlay 提取 DDR3 配置参数

用法 (在板子上):
    python3 get_ddr_params.py
或:
    python3 get_ddr_params.py > ddr_params.txt

输出所有 PCW_UIPARAM_DDR_* 参数, 用于校准 build_bd_systolic.tcl 的 PS7 配置。
"""
import glob
import os
import sys

# Pynq base overlay 的 .hwh 路径
HWH_CANDIDATES = [
    '/opt/python3.8/site-packages/pynq/overlays/base/base.hwh',
    '/opt/python3.7/site-packages/pynq/overlays/base/base.hwh',
    '/opt/python3.6/site-packages/pynq/overlays/base/base.hwh',
    '/usr/local/lib/python3.*/site-packages/pynq/overlays/base/base.hwh',
]

def find_hwh():
    for p in HWH_CANDIDATES:
        if os.path.exists(p):
            return p
    # glob 兜底
    for pat in HWH_CANDIDATES:
        for m in glob.glob(pat):
            if os.path.exists(m):
                return m
    # 暴力搜索
    for root in ['/opt', '/usr/local/lib']:
        for dirpath, _, files in os.walk(root):
            if 'base.hwh' in files:
                return os.path.join(dirpath, 'base.hwh')
    return None

def parse_hwh_ddr_params(hwh_path):
    """从 .hwh XML 解析 PS7 的 DDR 参数"""
    import xml.etree.ElementTree as ET
    tree = ET.parse(hwh_path)
    root = tree.getroot()

    # .hwh 格式: <module name="processing_system7_0">
    #   <parameters>
    #     <parameter name="PCW_UIPARAM_DDR_..." value="..."/>
    params = {}
    for mod in root.iter('module'):
        if 'processing_system7' in mod.get('name', ''):
            for params_node in mod.iter('parameters'):
                for p in params_node.iter('parameter'):
                    name = p.get('name', '')
                    val = p.get('value', '')
                    if 'DDR' in name or 'ddr' in name.lower():
                        params[name] = val
            break
    return params

def main():
    hwh = find_hwh()
    if not hwh:
        print('[ERROR] 找不到 base.hwh, 请手动定位:')
        print('  find / -name "base.hwh" 2>/dev/null')
        sys.exit(1)

    print(f'# base.hwh: {hwh}')
    print(f'# 提取 PS7 DDR3 参数')
    print()

    params = parse_hwh_ddr_params(hwh)
    if not params:
        print('[WARN] .hwh 里没找到 DDR 参数, 尝试备用方案')
        # 备用: 直接 grep .hwh (XML 文本)
        import re
        with open(hwh, 'r') as f:
            txt = f.read()
        for m in re.finditer(r'name="(PCW_UIPARAM_DDR_[^"]+)"\s+value="([^"]+)"', txt):
            params[m.group(1)] = m.group(2)

    # 按名字排序输出
    for k in sorted(params.keys()):
        print(f'{k} = {params[k]}')

    print()
    print('# 关键参数 (用于 build_bd_systolic.tcl):')
    key_params = [
        'PCW_UIPARAM_DDR_MEMORY_TYPE',
        'PCW_UIPARAM_DDR_PARTNO',
        'PCW_UIPARAM_DDR_BUS_WIDTH',
        'PCW_UIPARAM_DDR_CLK_FREQ',
        'PCW_UIPARAM_DDR_T_FAW',
        'PCW_UIPARAM_DDR_T_RAS_MIN',
        'PCW_UIPARAM_DDR_T_RC',
        'PCW_UIPARAM_DDR_T_RCD',
        'PCW_UIPARAM_DDR_T_RP',
        'PCW_UIPARAM_DDR_T_RFC',
        'PCW_UIPARAM_DDR_T_REFI',
        'PCW_UIPARAM_DDR_CWL',
    ]
    for k in key_params:
        if k in params:
            print(f'  {k} = {params[k]}')
        else:
            print(f'  {k} = (not set)')

if __name__ == '__main__':
    main()
