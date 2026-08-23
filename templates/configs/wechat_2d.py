# Godot 4.5 WeChat Mini Game 2D profile.
#
# 这是 2026-08 实测的裁切状态，不是下一轮建议值：
# - True 表示当前保留或关闭总开关已生效。
# - False 表示当前模块已关闭，或对应 disable_* 功能仍然保留。
#
# 构建命令：
# scons platform=web target=template_release threads=no wasm_simd=no \
#   profile=templates/configs/wechat_2d.py
#
# threads 和 wasm_simd 属于微信平台兼容参数，继续放在构建命令中，不在
# 这里重复配置。命令行参数优先级高于本文件。
#
# 实测体积（Brotli wasm.br，同源码同裁切配置）：
# - 非 GLX 构建：约 4.85 MiB（5,081,135 B）
# - GLX + C++ 异常开启：约 6.05 MiB（6,341,140 B）——当前正式产物
# - GLX + C++ 异常关闭：约 4.91 MiB（5,149,758 B）
#
# 结论：GLX 静态库本身仅约 +67 KB；包体 +1.14 MiB 的差异几乎全部来自
# C++ 异常支持（-fexceptions，GLX 构建默认开启，因为微信
# libemscriptenglx.a 内部会 throw）。异常关闭时函数数从 75,064 降到
# 60,141，但 GLX 库抛异常路径会直接 abort（与仓库早期 demo 4.6.1_glx
# 构建同状态）。开/关由构建参数控制：
#   scons ... wechat_glx=yes wechat_glx_exceptions=no   # 异常关，~4.91 MiB
#   python adapter/scripts/package_wechat_glx_template.py --exceptions disabled
# 详见 adapter/WECHAT_GLX.md「包体与 C++ 异常」章节。

# -------------------- 功能总开关 --------------------

# 已关闭完整 3D。这是当前包体缩小的主要来源，并会连带关闭 3D 物理、
# 3D 导航、XR、OpenXR、WebXR、Mobile VR 及依赖 3D 的导入模块。
disable_3d = True

# 当前保留 2D 物理。改为 True 会失去 Area2D、CharacterBody2D、
# RigidBody2D、碰撞检测和 2D 物理查询。
disable_physics_2d = False

# 当前保留 2D 导航。改为 True 会失去 NavigationAgent2D、
# NavigationRegion2D、2D 寻路和避障。
disable_navigation_2d = False

# 当前保留完整 GUI，包含常用 Control、复杂控件和主题能力。
disable_advanced_gui = False

# 沿用 Godot 默认模块策略，再明确标出当前关键模块状态。
modules_enabled_by_default = True

# -------------------- 脚本与文本：当前保留 --------------------

# GDScript。关闭后 .gd 脚本不能运行。
module_gdscript_enabled = True

# 中文、复杂文本塑形、双向文本和字体回退。
module_text_server_adv_enabled = True

# TrueType/OpenType 字体加载与渲染。
module_freetype_enabled = True

# MSDF 字体。确认项目没有使用 MSDF 字体后可改为 False。
module_msdfgen_enabled = True

# SVG 矢量资源。确认项目没有 SVG 后可改为 False。
module_svg_enabled = True

# -------------------- 音频：当前保留 --------------------

# Ogg 容器与 Vorbis 音频。Vorbis 硬依赖 Ogg，应一起保留或关闭。
module_ogg_enabled = True
module_vorbis_enabled = True

# MP3。全部音频均为 Ogg 或 WAV 时可改为 False。
module_minimp3_enabled = True

# 交互式音乐、播放列表和同步音乐流。普通项目不用时可改为 False。
module_interactive_music_enabled = True

# Theora 视频。当前 4.9 MB 包仍然保留；不播放 Ogg Theora 视频时可关闭。
module_theora_enabled = True

# -------------------- 网络：当前保留 --------------------

# WebSocket。
module_websocket_enabled = True

# Godot TLS、证书、Crypto、AES 和 HMAC 等密码学能力。
module_mbedtls_enabled = True

# ENet/UDP 多人网络。当前包仍保留，但微信 Web 环境通常用不到。
module_enet_enabled = True

# WebRTC PeerConnection 与 DataChannel。当前包仍保留。
module_webrtc_enabled = True

# UPnP 端口映射。当前包仍保留，但微信环境通常不可用。
module_upnp_enabled = True

# JSON-RPC。当前包仍保留，普通游戏运行时通常用不到。
module_jsonrpc_enabled = True

# MultiplayerSpawner、MultiplayerSynchronizer 等高级场景同步。
module_multiplayer_enabled = True

# -------------------- 图片与纹理：当前保留 --------------------

# 常见图片格式。
module_bmp_enabled = True
module_jpg_enabled = True
module_webp_enabled = True
module_tga_enabled = True
module_hdr_enabled = True

# 压缩纹理、容器及编解码能力。当前包仍保留；确认资源未使用对应格式后
# 才可逐项关闭，避免导出的纹理在运行时无法加载。
module_dds_enabled = True
module_ktx_enabled = True
module_astcenc_enabled = True
module_basis_universal_enabled = True
module_bcdec_enabled = True
module_etcpak_enabled = True

# -------------------- 其他运行能力：当前保留 --------------------

# NoiseTexture、FastNoiseLite 等噪声能力。
module_noise_enabled = True

# GDScript RegEx 与 RegExMatch。
module_regex_enabled = True

# ZIPReader 与 ZIPWriter。关闭不影响 PCK，但会失去脚本 ZIP API。
module_zip_enabled = True

# -------------------- 当前已关闭的有效模块 --------------------

# 以下模块因 disable_3d=True 或 Web 平台约束而未进入当前产物。
# 显式写出 False，便于直接查看最终状态。
module_csg_enabled = False
module_fbx_enabled = False
module_gltf_enabled = False
module_gridmap_enabled = False
module_meshoptimizer_enabled = False
module_godot_physics_3d_enabled = False
module_jolt_physics_enabled = False
module_navigation_3d_enabled = False
module_mobile_vr_enabled = False
module_openxr_enabled = False
module_webxr_enabled = False
module_vhacd_enabled = False
module_raycast_enabled = False

# Web 模板不使用 Vulkan/SPIR-V 渲染路径。
module_glslang_enabled = False

# C# 模块默认关闭，当前项目使用 GDScript。
module_mono_enabled = False

# 当前使用高级文本服务器，因此 fallback 文本服务器保持默认关闭。
module_text_server_fb_enabled = False

# 以下模块只服务编辑器、资源导入或当前 Web 平台不可用，未进入最终模板。
module_betsy_enabled = False
module_camera_enabled = False
module_cvtt_enabled = False
module_lightmapper_rd_enabled = False
module_tinyexr_enabled = False
module_xatlas_unwrap_enabled = False
