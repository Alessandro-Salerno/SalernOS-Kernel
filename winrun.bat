set OSNAME=SalernOS
set BUILDDIR=%0/../bin

qemu-system-x86_64 -bios \\wsl$\Ubuntu-20.04\usr\share\ovmf\OVMF.fd -cpu qemu64 -drive file=%BUILDDIR%/%OSNAME%.img -m 256M -net none
pass
