# Linux Purification Plan (Linux 净化计划)

<https://github.com/shajunxing/linux-purification-plan>

为缓解强迫症，在长时间酝酿后，我决定对 linux 生态环境做代码级的净化。

windows 我也很讨厌，我设想如果能从 validation os 开始只搭建必要部分（网络、防病毒、 direct x ），凡涉及 .net 、 dwm 的统统干掉，自己写个经典的 shell 够用就行。但给商业版系统投入太多精力好像也没意义。

我喜欢 dos 那种纯粹，纯粹的配置文件 autoexec.bat 、纯粹的 path 环境变量，整条链路平铺直叙，没有任何花里胡哨，而 linux 发展过程，各种庸人拍脑袋决定，早已背离 kiss 原则，从内核到图形界面，各种分叉纠缠在一起，层与层的组合乘数倍增。

我的净化目标：一、像 dos 那样简洁对开发者友好；二、图形界面和必备 utilities ，对最终用户友好。

[0 - 与 chatgpt 的交谈](doc/0-talking-with-chatgpt.md)

[1 - 基于 virtualbox 的 direct boot](doc/1-virtualbox-direct-boot.md)