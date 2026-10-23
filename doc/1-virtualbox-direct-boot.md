# 1 - 基于 virtualbox 的 direct boot

那么先得打造一套趁手的环境。 qemu 能够直接启动内核跟内存文件系统，但我讨厌 qemu ，因为它太零乱，我的另一台 debian 笔记本被 qemu 搞得一团糟，卸载时候却不禁用内核模块，搞得 virtualbox 无法启动。开源世界的庸人们做不好大部分事情，他们只爱自我陶醉。

我还讨厌 initrd/initramdisk ，为什么那些庸人直路不走走弯路？非得额外增加多少步骤，彰显自己技术高超？ linux 内核已经够臃肿了，还在意几个文件系统驱动占的空间？

我喜欢 virtualbox ，小巧稳健跨平台，要让它能以类似 qemu direct boot 的方式工作。经过大量调研，可行的思路是：找一个内核能识别的最简文件系统，非 fat16 莫属（貌似还有个romfs，但不确定能否写入硬盘分区），然后把编译好的内核、 init 等文件以及引导代码写成 .raw 格式，用 virtualbox 工具转成 .vdi 格式启动，把这些步骤写成自动化脚本。

没有现成的 fat16 制作工具，那就自己写一个： `raw.c` ， super floopy 格式，只有一个分区、没有主引导记录，因为 fat 分区有 bootstrap 部分，所以能正常引导。把文件按顺序写入根目录、连续扇区，文件名严格按照 8.3 规范，并在写入时候转换为大写。第一个文件被视作内核，由引导代码加载。引导代码 `boot.s` 修改自著名的 <https://sebastian-plotz.blogspot.com/> （收录在内核文档里），由 nasm 编译后再用 `bin2c.c` 转成文本格式 include 进 `raw.c` ，以上步骤参见批处理 `make.bat` 。

那么接下来，编译内核，在我的 debian 虚拟机里，先 make allnoconfig 或 make tinyconfig 得到最小配置，然后添加 tty/vt 、 msdos vfat 、 elf 等支持（随附 4.19.325 内核的 `.config` 。为什么用这个版本？因为我厌恶 c++ ，我让 chatgpt 帮我查能够用纯 c 自举的 gcc 最高版本是 4.7.4 ，那么相对应得到能够编译内核的最高版本，就拿这个做实验）。同时写一个简单的 `init.c` ,用 gcc -static -o init init.c 静态编译。

材料齐备，执行 raw -o output.raw bzImage init 生成镜像文件。执行 VBoxManage convertfromraw output.raw fat16.vdi --format VDI 转换格式，然后添加进 linux 64 位虚拟机，控制器选择 sata/ahci ，启动，成功。以上步骤可做成自动化脚本 `start.js` （基于 <https://github.com/shajunxing/sjx-script> ）。

![](1-0.png)

