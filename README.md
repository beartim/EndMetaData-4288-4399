# EndMetaData-4288-4399

iOS 13 及以上 arm64 命令行工具。到 [GitHub Actions](https://github.com/beartim/EndMetaData-4288-4399/actions/workflows/build-ios.yml) 打开成功的运行，在 Artifacts 下载 `EndMetaData-4288-4399-iOS13-arm64.zip`。解压后在越狱 iOS 终端执行。

```sh
chmod +x endmetadata-4288-4399
./endmetadata-4288-4399
./endmetadata-4288-4399 "/var/mobile/Documents/另一个存档"
```

不传路径时默认读取 `/var/mobile/Documents/My World`。传入路径时，使用解压后直接包含 `db/` 的存档目录。请在 Minecraft 退出该世界后执行。程序在临时副本上打开 LevelDB，恢复日志和清理操作均在临时目录中完成。

程序查询 LevelDB 的 `TheEnd` 键，按 Little-Endian NBT 读取 `data.LimboEntities[].EntityTagList[].Variant`。固定先打印 `4288`，再打印 `4399`。对应值至少出现一次时显示绿色，否则显示红色。缺少 `TheEnd` 或 `LimboEntities` 时，两行均为红色，退出码为 `0`。存档读取或解析失败时，两行均为红色，退出码为 `1`。参数过多时退出码为 `2`，输出同样为两行红色。

本地在 macOS 运行 `bash build.sh test` 可验证颜色和数据库读取。装有 Xcode 的 macOS 可运行 `bash build.sh ios`，生成带有 ad-hoc 签名的 iOS Mach-O 可执行文件及 ZIP。
