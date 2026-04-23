# Engine_cpp

一个基于 C++、X11 和 libpng 的像素风 2D 小型引擎实验项目，当前包含两个主要程序：

- 单文件游戏引擎 Demo：test.cpp
- 鼠标主导的地图编辑器：map_editor.cpp

项目目标不是做成完整商用引擎，而是提供一个简单、直接、可改造的原型框架，用来验证以下内容：

- PNG 贴图读取与像素级绘制
- 基于 X11 的窗口与输入处理
- 简单地图系统与摄像机
- 角色动画、特效、资源缓存、基础碰撞
- 地图编辑器的导入、导出、撤销、重做与物件摆放流程

## 功能概览

### 游戏 Demo

游戏 Demo 由 test.cpp 提供，目前包含：

- 从 JSON 加载地图底图
- 读取角色动画帧与物件贴图
- WASD 连续移动
- 摄像机跟随角色
- 简单静态碰撞体
- 基础特效系统
- 资源缓存、输入状态、物理世界等引擎骨架
- X11 窗口标题实时显示 FPS、玩家坐标、摄像机坐标、特效数量

### 地图编辑器

地图编辑器由 map_editor.cpp 提供，目前包含：

- 鼠标优先的编辑交互
- Brush、Eraser、Fill、Picker 四类基础绘制工具
- Item Place、Item Edit 两类物件编辑工具
- RGB 调色板与颜色滑条
- 地图导入/导出 JSON
- Undo / Redo 历史记录
- Item 模板自动扫描与缓存
- 启动参数、环境变量、面板路径切换三种 Item 路径来源

## 依赖环境

本项目面向 Linux + X11 环境开发与运行。

需要的依赖：

- g++
- libpng
- X11 开发库
- FreeType2
- pkg-config

Ubuntu / Debian 可参考：

```bash
sudo apt update
sudo apt install -y g++ libpng-dev libx11-dev libfreetype6-dev pkg-config
```

说明：

- 本项目依赖图形显示环境，不能在纯 headless 环境直接运行。
- 如果你在 SSH、WSL、容器或远程环境中运行，需要确保 X11 转发或本地图形会话可用。

## 快速开始

### 1. 编译游戏 Demo

```bash
g++ test.cpp -o test -O2 -lpng -lX11 $(pkg-config --cflags --libs freetype2)
```

### 2. 运行游戏 Demo

```bash
./test
```

仓库内也提供了脚本：

```bash
source run.sh
```

### 3. 编译地图编辑器

```bash
g++ map_editor.cpp -o map_editor -O2 -lpng -lX11 $(pkg-config --cflags --libs freetype2)
```

### 4. 运行地图编辑器

```bash
./map_editor
```

或使用脚本：

```bash
source map_editor.sh
```

## 项目结构

核心文件：

- AGENTS.md：仓库级开发说明
- test.cpp：单文件游戏引擎 Demo
- map_editor.cpp：地图编辑器
- json.hpp：nlohmann/json 单头文件库
- temp.json：游戏 Demo 默认读取的地图配置
- run.sh：游戏 Demo 的编译与运行脚本
- map_editor.sh：地图编辑器的编译与运行脚本

资源目录：

- main_character/：游戏 Demo 使用的角色动画帧
- items/：编辑器和 Demo 使用的物件资源目录
- 2D_pixel/：第三方像素资源包，仅作为资源参考和素材来源

## 游戏 Demo 说明

### 当前运行流程

test.cpp 启动时会：

1. 读取 temp.json
2. 从其中的 Path 字段加载底图 PNG
3. 加载 main_character/ 下的 4 帧角色动画
4. 加载 items/tree/1.png 作为场景树木
5. 加载 items/main_character/ 下的 4 帧作为演示特效
6. 初始化玩家、摄像机、碰撞体和特效系统
7. 进入主循环并在 X11 窗口中持续渲染

### 游戏操作

- W：向上移动
- A：向左移动
- S：向下移动
- D：向右移动
- E：在玩家位置生成一次残影特效
- Q：退出程序
- 关闭窗口：退出程序

### 当前 Demo 的组成

当前 Demo 更接近一个“引擎样例场景”，而不是完整游戏。场景中包含：

- 一张底图
- 一个玩家角色
- 三棵静态树木
- 基础阻挡碰撞
- 一个非循环特效示例

### 已实现的引擎模块

test.cpp 目前已经包含以下基础模块：

- Color：RGBA 像素表示，alpha 为 0 时视为透明
- Image：二维像素图像容器
- readPNG：PNG 读取与 alpha 保留
- UI / UI_system：简单 UI 贴图系统
- Item / Map：地图与地图附加物件系统
- X11Renderer：窗口创建、绘制、事件轮询、标题更新
- Effect / EffectSystem：特效模板与实例播放系统
- AABB / Collider / PhysicsWorld：基础碰撞系统
- ResourceManager：图片与动画缓存
- InputSystem：按键状态管理
- Engine：轻量引擎上下文对象

### Demo 的已知限制

当前 test.cpp 仍然是一个原型实现，存在这些限制：

- 地图数据仍以单张底图为核心，不是瓦片地图
- 树木碰撞体是代码内生成，不是从地图 JSON 自动加载
- 特效、物件和角色状态机都比较基础
- 没有音频、脚本系统、UI 框架或场景切换系统
- 没有自动化测试、CI 或跨平台窗口后端

## 地图编辑器说明

### 启动方式

编辑器支持三种 Item 资源路径来源，优先级如下：

1. 启动参数
2. 环境变量 MAP_EDITOR_ITEMS
3. 默认路径 ./items

示例：

```bash
./map_editor ./items
```

或：

```bash
MAP_EDITOR_ITEMS=./items ./map_editor
```

### 编辑器交互特性

编辑器是“鼠标主导”的，右侧面板可以直接点击操作。当前界面强调以下工作流：

- 颜色选择：调色板 + RGB 滑条
- 绘制模式切换：Brush / Eraser / Fill / Picker
- Item 工作流：模板导入、放置、选择、拖动、复制、删除
- 导入导出：面板按钮直接触发
- 历史记录：Undo / Redo

### 编辑器按键与退出

当前代码中明确保留的键盘行为相对少，主要是：

- Esc：退出编辑器

其余核心操作主要通过鼠标和右侧面板完成。

### 编辑器导出文件

点击 Export JSON 后，编辑器会输出两份文件：

- map_export_{timestamp}.json
- map_editor_last.json

其中：

- 带时间戳的文件用于保留历史版本
- map_editor_last.json 用于快速回读和继续编辑

程序退出前也会刷新一次 map_editor_last.json。

## 数据格式

### 游戏 Demo 的地图 JSON

游戏 Demo 使用 test.cpp 中的 Map::read_json 读取地图文件。最小可用格式如下：

```json
{
  "Path": "Dungeon_Tileset_at.png"
}
```

这表示：

- Path：地图底图 PNG 的路径

如果你需要在地图中附带物件，test.cpp 也支持更完整的结构：

```json
{
  "Path": "Dungeon_Tileset_at.png",
  "Item_names": ["tree"],
  "Items": {
    "tree": {
      "Path": "./items/tree",
      "access_able": 1,
      "tot": 1,
      "sum": 2,
      "positions": [[100, 120], [220, 180]],
      "visible": [true, true, true],
      "chosen": [0, 1, 1]
    }
  }
}
```

字段说明：

- Path：底图路径
- Item_names：启用的物件名字列表
- Items：每种物件的配置字典
- Items.<name>.Path：该物件帧图片所在目录
- tot：该物件动画或帧数量
- sum：实例数量
- positions：每个实例的位置数组
- visible：实例可见性数组
- chosen：实例当前所使用的帧编号数组

说明：

- test.cpp 当前的 Item 结构是原型设计，读写格式已经可用，但完整玩法层集成还比较初级。
- 仓库自带的 temp.json 目前只使用了最小格式。

### 地图编辑器的导出 JSON

map_editor.cpp 导出的 JSON 结构比游戏 Demo 更完整，大体包含以下部分：

- meta：导出元信息
- map：地图尺寸、单元格像素大小、摄像机位置、颜色数据
- editor：编辑器状态，例如网格显示、笔刷半径、当前颜色、当前模板等
- item_templates：模板列表
- item_instances：地图上的物件实例列表
- item_cache：编辑器当前缓存的物件记录

示意结构如下：

```json
{
  "meta": {
    "editor": "map_editor.cpp",
    "timestamp": 1710000000,
    "note": "mouse-first map editor"
  },
  "map": {
    "width": 64,
    "height": 64,
    "cell_px": 16,
    "camera_x": 0,
    "camera_y": 0,
    "data": [[0, 0, 0]]
  },
  "editor": {
    "show_grid": true,
    "brush_radius": 1,
    "item_step": 1,
    "current_color": 16777215,
    "current_template": 0,
    "current_frame": 0,
    "item_base_path": "./items"
  },
  "item_templates": [],
  "item_instances": [],
  "item_cache": []
}
```

## 资源约定

项目目前遵循这些资源组织约定：

- 动画帧按数字命名：1.png、2.png、3.png...
- 一个资源目录通常表示一个模板或动画
- main_character/ 用于角色动画
- items/ 下的每个子目录通常表示一个可放置物件模板
- 编辑器会自动扫描包含 1.png 的目录作为可用模板

## 开发建议

如果你准备继续扩展这个项目，比较自然的方向有：

1. 把 test.cpp 的场景物和碰撞改为从编辑器导出 JSON 自动加载
2. 把底图模式升级为真正的瓦片地图系统
3. 为角色增加朝向、状态机、交互逻辑和多类特效
4. 给编辑器补充快捷键帮助、选择框、多图层和碰撞层编辑
5. 增加测试、格式化脚本与 CI

## 常见问题

### 程序启动后没有窗口

通常说明：

- 当前环境没有可用的 X11 显示会话
- DISPLAY 没有正确设置
- 在远程环境中没有开启 X11 转发

### 资源加载失败

请优先检查：

- 当前工作目录是不是仓库根目录
- temp.json 中的 Path 是否正确
- main_character、items 等目录是否存在对应 PNG
- 动画帧是否从 1.png 开始连续编号

### 编辑器没有识别到 Item 模板

请检查：

- 资源目录是否正确传入
- 是否设置了 MAP_EDITOR_ITEMS
- 模板目录中是否存在 1.png

## 当前状态

项目当前适合：

- 学习 X11 + PNG 的最小游戏渲染流程
- 在单文件或轻量工程里快速试验 2D 游戏机制
- 给自定义地图编辑器和运行时做联动原型

项目当前还不适合：

- 直接作为完整游戏引擎投入大规模内容生产
- 作为成熟跨平台框架替代 SDL、SFML、Godot 之类现成方案

如果你的目标是继续把这个仓库往前推进，建议优先建立“编辑器导出 -> 游戏运行时加载”的统一数据通路，这是最能放大现有代码价值的一步。