## 格式化SD卡命令

```shell
diskutil list
```
### 找到你的 SD 卡对应的盘，比如 /dev/disk2

```shell
diskutil unmountDisk /dev/disk2
```
```shell
diskutil eraseDisk FAT32 SDCARD MBRFormat /dev/disk2
```