let vm_name = "fat16";
function vm_name_exists_in(cmd) {
    let fp = popen(cmd, "r");
    let found = (fp::fread()::match("\"" + vm_name + "\"") != null);
    pclose(fp);
    return found;
}
if (vm_name_exists_in("VBoxManage list vms")) {
    if (vm_name_exists_in("VBoxManage list runningvms")) {
        sleep(1);
        system(sprintf("VBoxManage controlvm %s poweroff", vm_name));
        while (vm_name_exists_in("VBoxManage list runningvms")) sleep(1);
    }
} else {
    system(sprintf("VBoxManage createvm --name %s --ostype Linux_64 --register", vm_name));
    system(sprintf("VBoxManage modifyvm %s --memory 128 --vram 8", vm_name));
    // 端口数设置为1，避免内核启动大量 "SATA link down (SStatus 0 SControl 300)"
    system(sprintf("VBoxManage storagectl %s --name SATA --add sata --portcount 1 --bootable on", vm_name));
}
let vd_path;
let fp = popen(sprintf("VBoxManage showvminfo %s", vm_name), "r");
for (;;) {
    let line = fp::freadline();
    if (line == null) break;
    let cap = line::match("Config file:\s+(.+)");
    if (cap != null) {
        vd_path = cap[1] ::dirname() + pathsep + vm_name + ".vdi";
        break;
    }
}
pclose(fp);
if (access(vd_path, F_OK)) {
    system(sprintf("VBoxManage storageattach %s --storagectl SATA --port 0 --device 0 --type hdd --medium none", vm_name));
    system(sprintf("VBoxManage closemedium disk \"%s\" --delete", vd_path));
}
system(sprintf("VBoxManage convertfromraw bin\output.raw \"%s\" --format VDI", vd_path));
system(sprintf("VBoxManage storageattach %s --storagectl SATA --port 0 --device 0 --type hdd --medium \"%s\"", vm_name, vd_path));
system(sprintf("VBoxManage startvm %s", vm_name));
