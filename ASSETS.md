# 美术素材授权 / Artwork License

本文件说明仓库内**美术素材**的来源与授权。代码授权见 [LICENSE](LICENSE)（MIT）。

## 覆盖范围

| 路径 | 内容 |
|---|---|
| `assets/frames/*.png` | 47 张宠物动作帧（待机 / 走动 / 表情 / 拖拽 / 举牌 / 睡觉 / 打字 / 咆哮） |
| `assets/dino_icon.ico` | 应用图标 |
| `docs/*.png` | 由上述素材程序化拼合而成的预览图与横幅 |

## 来源

**这些图由 AI 生成工具「豆包（Doubao）」生成**，之后经过人工抠图（去背景）与缩放处理，
再由 `make_frames.ps1` 转成运行时使用的 `.dat` 帧文件。

- 生成方式：文生图（text-to-image），提示词由作者编写
- 后处理：透明背景抠图 → 缩放到 110×110 → 预乘 alpha 写入 `.dat`
- 仓库中提供的 PNG 是 2048×2048 原始尺寸的 **512×512 压缩副本**（控制仓库体积），
  足以重新生成与官方版本等效的 `.dat`

## 授权

除另有说明外，`assets/` 目录下的素材以 **[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)**
（署名 4.0 国际）许可发布：

- **允许**：自由使用、修改、改编、商用、再发布（含收费分发）
- **要求**：署名 —— 请注明「美术素材：AI 生成（豆包 Doubao），作者 waspqj，CC BY 4.0」
- **不允许**：暗示作者为素材背书，或在未标明 AI 生成的情况下使用

## 换成你自己的皮（推荐做法）

本项目刻意把美术与代码解耦：**你不需要改一行 C 代码就能换掉整只恐龙。**

1. 准备 47 张 PNG，放进 `assets/frames/`，文件名与现有的一致
   （`idle_01.png`、`walk_right_01.png`、`sign_01.png` …）
   尺寸任意，透明背景 RGBA 即可
2. 跑一次转换：

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File make_frames.ps1
   ```

3. 重新编译：

   ```bat
   build.bat
   ```

或者更省事：**只替换 `assets/frames/` 里的同名 PNG，然后跑 `make_frames.ps1` + `build.bat` 即可**，
不需要动 `pet.rc`（资源 ID 是写死的，文件名不变就不会错位）。

> 小提示：帧是逐像素预乘 alpha 的透明图层，抠图边缘如果有白边/黑边会很明显，
> 建议先用抠图工具处理干净再导入。
