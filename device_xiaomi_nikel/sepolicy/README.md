Device SELinux policy for nikel. Empty during bring-up: the kernel command line
sets androidboot.selinux=permissive. Policy gets written from the denials in
`adb shell dmesg | grep avc` once the system boots.
