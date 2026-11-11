from pathlib import Path
import os
import subprocess
import sys

HERE = Path(__file__).resolve().parent
# Set LLVM_BIN to the directory containing clang.exe and lld-link.exe.
LLVM = Path(os.environ.get('LLVM_BIN', r'C:\Program Files\LLVM\bin'))


def run(*args):
    subprocess.run([str(x) for x in args], cwd=HERE, check=True)


run(sys.executable, HERE / 'build_patch.py')
common = ['-target', 'x86_64-pc-windows-msvc', '-ffreestanding', '-fno-builtin',
          '-fno-stack-protector', '-mno-red-zone', '-fshort-wchar', '-O2',
          '-Wall', '-Wextra', '-Werror']
run(LLVM / 'clang.exe', *common, '-c', 'menu_probe.c', '-o', 'menu_probe.obj')
run(LLVM / 'lld-link.exe', '/subsystem:efi_application', '/entry:efi_main',
    '/nodefaultlib', '/machine:x64', '/dynamicbase', '/nxcompat', '/timestamp:0',
    '/out:GZ302EA314_MENU_PROBE.efi', 'menu_probe.obj')
run(LLVM / 'clang.exe', *common, '-DHOST_TEST', '-c', 'menu_probe.c', '-o', 'menu_probe_test.obj')
run(LLVM / 'lld-link.exe', '/dll', '/noentry', '/nodefaultlib', '/machine:x64',
    '/timestamp:0', '/out:menu_probe_test.dll', 'menu_probe_test.obj')
print('Built EFI application and native mock-test DLL.')
