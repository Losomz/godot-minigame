/**
 * 文件用途：微信小游戏导出目录的 Godot 引擎入口，可直接覆盖 engine/game.js。
 *
 * 主要职责：
 * 1. 加载 Godot 微信运行时和 GODOTSDK。
 * 2. 挂载微信广告桥。
 * 3. 从微信文件系统读取 PCK，并复制到 Godot 虚拟文件系统。
 * 4. 启动 Godot 游戏。
 *
 * 兼容范围：Godot 4.5.x + Godot Minigame 1.0.4 的默认导出结构。
 * 配套导出要求 PCK 位于 engine/demo-pck.bin。
 */

// godot-sdk 创建 GODOTSDK；godot 提供实际的 Godot 微信运行时。
import './godot-sdk'
import './godot'

/* ===== 微信广告接入开始 =====
 * 此时 GODOTSDK 已创建，广告桥可以向它挂载 dsWxAd* 方法。
 * 这段导入必须位于 godot-sdk 之后、GODOTSDK.startGame() 之前。
 */
import './wx-ad-bridge'
/* ===== 微信广告接入结束 ===== */

// 使用默认导出名称可以直接覆盖入口文件；若修改 PCK 名称，只需同步修改 packPath。
const executablePath = '/engine/godot';
const packPath = '/engine/demo-pck.bin';


/**
 * 将微信文件系统中的 PCK 文件复制到 Godot 虚拟文件系统。
 * readFileSync 返回微信侧文件数据，copyToFS 让 Godot 引擎能够按普通资源包读取它。
 * 该方法名由 Godot Minigame 运行时约定，请勿随意改名。
 */
GODOTSDK.load_pack1 = function (path) {
    const fileSystem = wx.getFileSystemManager();
    const file = fileSystem.readFileSync(path, undefined, 0);
    const engine = GODOTSDK.engine;
    engine.rtenv.copyToFS(file.path, file.buffer);
};

// 必须在 GODOTSDK、广告桥和 PCK 加载函数都准备完成后再启动 Godot。
GODOTSDK.startGame(executablePath, packPath);
