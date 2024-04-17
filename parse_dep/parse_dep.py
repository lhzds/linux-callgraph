#!/usr/bin/env python3

import os
import re
import sys
import collections

CURDIR = os.path.dirname(os.path.abspath(__file__))

# 这些符号不被解析
module_symbols = set([
    'module_get',
    'modver_version_show',
    'cleanup_module',
    'do_init_module',
    'init_module',
    'load_module',
    'module_put',
    'module_put_and_exit',
    'print_modules',
    'retpoline_module_ok',
    'try_module_get'
])

def is_module_symbol(sym):
    sym_norm = re.sub("^_*", "", sym)
    if sym_norm in module_symbols or sym_norm[:5] == "llvm.":
        return True
    return False

def normalize_object_name(fname, curext='.o', newext='.o', curpath=''):
    obj = fname[:-len(curext)]
    if curpath != '':
        obj = os.path.join(curpath, obj)
    obj = re.sub('-', '_', obj)
    return obj + newext

class ObjDeps(object):
    def __init__(self, linux_build):
        # 函数依赖
        self.fdep_map = collections.defaultdict(dict)
        self.frevdep_map = collections.defaultdict(set)
        
        # 全局变量包含的所有函数
        self.gdat_cbs = collections.defaultdict(set)
        
        # 全局变量引用的外部链接
        self.gdat_lnks = collections.defaultdict(dict)
        
        # 函数中间接调用的其他函数
        self.fpref_map = collections.defaultdict(dict)
        self.fpref_revmap = collections.defaultdict(dict)

        for root,_,fs in os.walk(linux_build): # 深度遍历 build 目录中的所有文件
            for fname in fs:
                fpath = os.path.join(root, fname) 
                # 1. 解析函数依赖关系 -> fdep_map \ frevdep_map
                if fname.endswith(".o.imptab") and not fname.endswith(".mod.o.imptab"):
                    mod = normalize_object_name(fname, ".o.imptab", ".o", root)
                    with open(fpath, 'r') as fd:
                        data = fd.read()
                        for line in data.strip().split('\n'):
                            if not line:
                                continue
                            sym, call = line.strip().split(' : ')
                            if is_module_symbol(sym) or is_module_symbol(call): # linux：排除 module 函数
                                continue
                            if sym not in self.fdep_map[mod]:
                                self.fdep_map[mod][sym] = set()
                            # 解析函数依赖
                            self.fdep_map[mod][sym].add(call)
                            # 反向依赖
                            self.frevdep_map[call].add((sym, mod))                
                # 2. 解析函数中所使用的外部定义的全局变量 -> gdat_lnk（import_table 是以文件为粒度，这里以函数为粒度）
                elif fname.endswith(".o.symlnk") and not fname.endswith(".mod.o.symlnk"):
                    mod = normalize_object_name(fname, ".o.symlnk", ".o", root)
                    with open(fpath, 'r') as fd:
                        data = fd.read()
                        for line in data.strip().split('\n'):
                            if not line:
                                continue
                            func, gdat = line.strip().split(' : ')
                            if is_module_symbol(func) or is_module_symbol(gdat):
                                continue
                            if func not in self.gdat_lnks[mod]:
                                self.gdat_lnks[mod][func] = set()
                            self.gdat_lnks[mod][func].add(gdat)
                # 3. 解析全局变量中嵌套包含的所有函数/函数指针 _> gdat_cbs
                elif fname.endswith(".o.symcbs") and not fname.endswith(".mod.o.symcbs"):
                    mod = normalize_object_name(fname, ".o.symcbs", ".o", root)
                    with open(fpath, 'r') as fd:
                        data = fd.read()
                        for line in data.strip().split('\n'):
                            if not line:
                                continue
                            gdat, cb = line.strip().split(' : ')
                            if is_module_symbol(gdat) or is_module_symbol(cb):
                                continue
                            self.gdat_cbs[gdat].add(cb)
                # 4. 解析函数间接的所有函数 -> fpref_map、fpref_revmap
                elif fname.endswith(".o.fptref") and not fname.endswith(".mod.o.fptref"):
                    mod = normalize_object_name(fname, ".o.fptref", ".o", root)
                    with open(fpath, 'r') as fd:
                        data = fd.read()
                        for line in data.strip().split('\n'):
                            if not line:
                                continue
                            sym, fpref = line.strip().split(' : ')
                            if is_module_symbol(sym) or is_module_symbol(fpref):
                                continue
                            if sym not in self.fpref_map[mod]:
                                self.fpref_map[mod][sym] = set()
                            if fpref not in self.fpref_revmap[mod]:
                                self.fpref_revmap[mod][fpref] = set()
                            self.fpref_map[mod][sym].add(fpref)
                            self.fpref_revmap[mod][fpref].add(sym)
        
        # link function through global variables
        # 5. gdat_lnks 以全局变量作为媒介完善模块与模块间的依赖
        for mod in self.gdat_lnks:
            for func in self.gdat_lnks[mod]:
                for gv in self.gdat_lnks[mod][func]:
                    if gv not in self.gdat_cbs:
                        continue
                    for cb in self.gdat_cbs[gv]:
                        if func not in self.fdep_map[mod]:
                            self.fdep_map[mod][func] = set()
                        self.fdep_map[mod][func].add(cb)
                        self.frevdep_map[cb].add((func, mod))


if __name__ == "__main__":
    linux_build = sys.argv[1]
    obj_dep = ObjDeps(linux_build)
    for mod in obj_dep.fdep_map:
        mod_path = mod[len(os.path.abspath(linux_build)) + 1:]
        path = os.path.join("deps", mod_path)
        dirname = os.path.dirname(path)
        if not os.path.exists(dirname):
            os.makedirs(dirname)
            
        with open(path, "w") as f:
            f.write(mod_path)
            f.write("\n")
            for sym in obj_dep.fdep_map[mod]:
                f.write(sym + " -> (")
                f.write(", ".join(list(obj_dep.fdep_map[mod][sym])))
                f.write(")\n")
