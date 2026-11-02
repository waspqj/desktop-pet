
做这个的起因很朴素：想要一个不折腾、不联网、不占资源的桌面小恐龙。

第一版写完发现三个问题：每帧渲染 83µs、内存 6MB、GDI 对象 60 个。查下来根子都在同一处——**每帧都新建 DC + DIB + 再删掉**。

改成渲染表面常驻化之后：

- 单帧渲染 **83.2µs → 8.9µs**
- 常驻内存 **6.13MB → 4.72MB**
- GDI 对象 **60 → 30**
- 拖动时 CPU **4.64% → 2.16%**（单核）

源码：https://github.com/waspqj/desktop-pet （MIT，美术 CC BY 4.0）

---

## 一、整体结构

不用任何图形库，纯 Win32 GDI：

```c
WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE
```

- `WS_EX_LAYERED` + `UpdateLayeredWindow(..., ULW_ALPHA)`：逐像素 alpha 的分层窗口，**没有窗口、没有控件**，所以不会遮挡、不会抢焦点
- `WS_EX_NOACTIVATE`：点它不会把当前输入焦点抢走
- `WS_EX_TOOLWINDOW`：不进任务栏

帧数据全部编译进 exe 的 `.rsrc`（RCDATA），运行时不读任何外部文件——所以整个程序就是一个 exe。

## 二、帧格式：为什么必须预乘 alpha

`.dat` 文件 = 8 字节头 `[u32 W][u32 H]` + 预乘 alpha 的 BGRA 像素。

```
for (i = 0; i < len; i += 4) {
    a = buf[i+3];
    if (a != 0 && a != 255) {
        buf[i+0] *= a / 255;   // B
        buf[i+1] *= a / 255;   // G
        buf[i+2] *= a / 255;   // R
    }
}
```

`UpdateLayeredWindow` 期望的是 **premultiplied** 的 BGRA；PNG 里通常是 straight alpha，所以要自己乘一遍。

## 三、坑一：GDI 缩放会把 alpha 清零

这个坑最花时间。

一开始我用 GDI 缩放贴图：

```c
StretchBlt(mem_dc, 0, 0, w, h, src_dc, 0, 0, SRCCOPY);
```

结果窗口**整个透明**，屏幕该变暗的地方永远不变暗。

原因：**`StretchBlt` / `BitBlt` 在缩放 32bpp DIB 时不会保留 alpha 字节**（1:1 复制才安全）。alpha 一被清零，`UpdateLayeredWindow` 收到的就是一张全透明的层。

结论：带逐像素 alpha 的图层，**缩放必须在 C 里按全分辨率算**，不能交给 GDI。

> 附带一提：`BILINEAR` 是 GDI+ 的常量，不是 Win32 的；`HALFTONE` 在 32bpp DIB 上官方明确说不支持。

## 四、坑二：Low 完整性级别会静默废掉键盘

现象：宠物不响应键盘、拖不动，但走路、睡觉、举牌全都正常。

第一反应当然是代码 bug。真相是 **exe 的完整性级别被设成了 Low**：Low 完整性进程里 `GetAsyncKeyState` 恒返回 0，所有键盘相关功能全废；而走路睡觉只靠定时器，自然照常。

```bat
icacls "pet.exe" /setintegritylevel Medium
```

判定方法：`SendInput` 注入后立刻 `GetAsyncKeyState` 读回——注入成功（返回 2）却读到 `0x0000`，就是完整性级别问题。

> 所以别用会改安全属性的工具传递 exe，有些压缩/网盘同步会顺手改它，用户拿到手就"坏了"，还查不到原因。

## 五、性能三板斧

### 1. 渲染表面常驻化

原来每帧：创建内存 DC → 创建设备兼容位图 → 拷贝 → 销毁。现在 DIB 只在**尺寸变化时**重建，其余时候复用。

### 2. 帧按需加载 + LRU

启动只预热 16 张，其余按需加载，LRU 上限 24 张。Private 内存 6.13MB → 4.72MB，GDI 对象 60 → 30。

### 3. 打字检测改成白名单

从全扫 247 个键改成只扫 55 个常用键，每秒少 1920 次 `GetAsyncKeyState`。

## 六、体验细节：让它"不惹人烦"

| 问题 | 做法 |
|---|---|
| 点宠物抢走输入框焦点 | `WM_MOUSEACTIVATE` 返回 `MA_NOACTIVATE` + `WS_EX_NOACTIVATE` |
| 输入法被注入到别的程序 | `ImmAssociateContext(hwnd, NULL)`，注入率实测 6/10 → 0/10 |
| 双击出现两只宠物 | 单实例互斥量 |
| 资源管理器重启后托盘图标没了 | 处理 `TaskbarCreated` 消息重建图标 |
| 右键菜单偶尔弹不出来 | `SetForegroundWindow` + `WM_NULL` |

## 七、换皮不用改代码

这是整个项目我最满意的一点：**美术与代码彻底解耦**。

1. 准备 47 张透明背景 PNG，丢进 `assets/frames/`，文件名保持一致
2. 跑 `make_frames.ps1`（缩放到 110×110 + 预乘 alpha → `.dat`）
3. `build.bat` 重新编译

`pet.rc` 里的资源 ID 是写死的，**文件名不变就不会错位**——换皮用户完全不需要碰 C 代码。

## 八、数字汇总

| 指标 | 优化前 | 优化后 |
|---|---|---|
| 单帧渲染 | 83.2 µs | **8.9 µs** |
| Private 内存 | 6.13 MB | **4.72 MB** |
| GDI 对象 | 60 | **30** |
| 拖动 CPU（单核） | 4.64% | **2.16%** |
| 输入法注入率 | 6/10 | **0/10** |

产物：单个 `pet.exe`，2.23 MB，静态链接，零运行时依赖，64 位 Windows。

代码 MIT，美术 CC BY 4.0（豆包 AI 生成，已署名）。
第一次开源，欢迎拍砖：https://github.com/waspqj/desktop-pet
