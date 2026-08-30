# 微信小游戏 EmscriptenGLX 下九宫格 UI 静默消失——问题定位与修复方案

适用环境:Godot 4.5.x GLES3 兼容渲染器(template_release)+ 微信 EmscriptenGLX(libemscriptenglx 0.1.11,Emscripten 4.0.10,录制-回放模式)。

## 一、现象

- 同一份包在不同真机上表现分裂:**部分机型上所有"九宫格路径"的 UI 全部不渲染**——`NinePatchRect` 节点、`StyleBoxTexture` 主题底、`TextureProgressBar` 九宫格拉伸模式,无一幸免;
- 同屏的**非九宫格元素完全正常**:`TextureRect`、`Label`、普通矩形、图片、文字、广告组件均正常;
- 故障设备控制台 **0 条 error**;GLX 初始化日志与正常设备逐字一致;游戏逻辑、广告、音频全程正常;
- 复现稳定:同机型反复出现,所有含九宫格的界面一致,与具体场景无关。

## 二、定位过程

1. **元素级截图对照**:逐个 UI 元素核对正常/异常设备截图——消失元素 100% 走九宫格绘制路径,幸存元素 100% 不走;无反例。范围锁定到"九宫格路径"。
2. **引擎侧机制梳理**:
   - GLES3 canvas 批处理中,`TYPE_NINEPATCH` 批与 `TYPE_RECT` 批**永不合并**(批 key 含 command_type,`rasterizer_canvas_gles3.cpp` 的批创建条件);
   - 两者的**唯一 GL 层差异**是 `USE_NINEPATCH` 着色器特化位:每个特化组合是**独立的 GL program**,首次遇到时才懒编译;Web 平台禁用 program 二进制缓存(`shader_gles3.cpp` 显式 return false),每次启动现场编译;
   - 编译失败时引擎唯一动作:`WARN_PRINT_ONCE` 一次后**整批静默跳过**(`_version_bind_shader` 返回 false → 绘制处 continue),不重试、不再报错;
   - 若驱动对 `glGetShaderiv/glGetProgramiv` **谎报成功**,则引擎完全无感知,draw call 照发但像素输出为空,零日志。
3. **日志交叉验证**:`template_release` 下着色器编译失败的 `ERR_PRINT` 是保留的(无 DEBUG 门控),而故障机日志零 error——说明故障模式更可能是"回放端编译状态与查询应答脱节/谎报",而非引擎可见的编译失败。两种模式的表现一致:静默消失。
4. **适配层自查**:C++ 侧补丁与 JS 胶水(`godot_process.js`、`patch_em_gl.js`)均不触碰着色器源文本、不拦截 `glShaderSource/glCompileShader/glGetShaderiv` 等任何编译链 API;着色器相关 API 仅由官方 `libemscriptenglx.a` 引用——排除我方胶水层。
5. **佐证**:微信官方文档 FAQ 明确承认 GLX 存在"渲染相关的表现异常"并建议联合排查;社区已有一例同型故障(GLX 下黑屏但逻辑正常,归因到 `glLinkProgram` 层,标准 WebGL2 从不复现)。

**结论**:GLX 回放端在部分机型驱动上,对 `USE_NINEPATCH` 特化变体编译/执行失败且不上报。该变体恰好是 canvas 着色器里唯一的"冷门特化"——永不与普通批合并、每处必走独立 program,片元代码包含 flat varying、inout int 副作用、整型位运算等老驱动易错构造(`canvas.glsl` 的 `map_ninepatch_axis` 一带)。这解释了"只有九宫格消失、其余正常、零报错"的全部特征。

## 三、修复方案:在命令源头消灭专用变体

失败发生在微信回放端的设备驱动上,引擎既感知不到也无法修复编译本身;但九宫格绘制在引擎里**只有一个汇聚点**,于是把"需要专用着色器的命令"从源头替换掉:

`servers/rendering/renderer_canvas_cull.cpp` 的 `canvas_item_add_nine_patch()`,在 `WEB_ENABLED` 门控下不再产生 NinePatch 命令,改为按 `canvas.glsl` 中 `map_ninepatch_axis` 的同一数学,在 CPU 上把九宫格展开为最多 9 次 `canvas_item_add_texture_rect_region`——即普通矩形区域绘制命令:

- STRETCH:三分支精确复刻(起边 1:1 / 中段线性拉伸 / 末边反向 1:1),margin 之和大于目标尺寸的退化情况按着色器分支优先级复刻(中段为空、起末两段相接);
- TILE / TILE_FIT:按块数 CPU 展开(每边超过 64 块的极端配置回退 STRETCH);
- `draw_center=false` 时跳过中段(对应着色器逐像素 alpha 置零,几何上等价于只跳中心块);
- 非正尺寸子块跳过;负边距钳制为 0(着色器对负边距本就未定义);
- 空 region → 取整张纹理(texel size 语义与 `_prepare_canvas_texture` 一致,纹理尺寸经 `RSG::texture_storage->texture_size_with_proxy` 获取);空纹理 RID → 1×1 白图。

效果:**`USE_NINEPATCH` 特化在 Web 构建中永远不会被编译或执行**,坏路径从变体空间中消失;桌面及其它平台因门控不受影响。

### 代码(可直接移植)

```cpp
#ifdef WEB_ENABLED
namespace {

struct NinePatchAxisSegment {
	float dst_begin = 0.0f;
	float dst_end = 0.0f;
	float uv_begin = 0.0f; // In region (source) texture pixel coordinates.
	float uv_end = 0.0f;
	bool is_middle = false;
};

// Splits one axis of a nine-patch into the linear pieces that the USE_NINEPATCH
// specialization of the GLES3 canvas shader (canvas.glsl map_ninepatch_axis)
// would render, so a whole nine-patch can be emitted as regular textured rect
// commands. WeChat's GLX command replay fails to handle that shader
// specialization on some devices (its batches are silently skipped), so on Web
// nine-patches are expanded on the CPU instead.
// Negative margins are undefined in the shader and are clamped here.
void _ninepatch_axis_segments(float p_draw_size, float p_margin_begin, float p_margin_end, float p_region_size, RS::NinePatchAxisMode p_mode, NinePatchAxisSegment *r_segments, int &r_count) {
	const float draw_size = MAX(0.0f, p_draw_size);
	const float margin_begin = MAX(0.0f, p_margin_begin);
	const float margin_end = MAX(0.0f, p_margin_end);
	r_count = 0;

	// [0, margin_begin): 1:1 from the region start (shader branch `pixel < margin_begin`).
	const float start_end = MIN(margin_begin, draw_size);
	if (start_end > CMP_EPSILON) {
		r_segments[r_count].dst_begin = 0.0f;
		r_segments[r_count].dst_end = start_end;
		r_segments[r_count].uv_begin = 0.0f;
		r_segments[r_count].uv_end = start_end;
		r_segments[r_count].is_middle = false;
		r_count++;
	}

	// The middle and end branches only apply from margin_begin onwards.
	const float middle_begin = margin_begin;
	const float middle_end = draw_size - margin_end;
	if (middle_begin < middle_end) {
		const float middle_dst = middle_end - middle_begin;
		const float middle_src = p_region_size - margin_begin - margin_end;
		bool middle_done = false;
		switch (p_mode) {
			case RS::NINE_PATCH_TILE:
			case RS::NINE_PATCH_TILE_FIT: {
				if (middle_src <= CMP_EPSILON) {
					break; // Degenerate region middle; fall back to stretch.
				}
				if (p_mode == RS::NINE_PATCH_TILE) {
					if (middle_dst / middle_src > 64.0f) {
						break; // Unrealistically many tiles; fall back to stretch.
					}
					int tiles = MAX(1, int(Math::ceil(middle_dst / middle_src)));
					// Tiles are sampled 1:1; the last one is clipped.
					float piece_begin = middle_begin;
					for (int i = 0; i < tiles; i++) {
						float piece_end = MIN(piece_begin + middle_src, middle_end);
						r_segments[r_count].dst_begin = piece_begin;
						r_segments[r_count].dst_end = piece_end;
						r_segments[r_count].uv_begin = margin_begin;
						r_segments[r_count].uv_end = margin_begin + (piece_end - piece_begin);
						r_segments[r_count].is_middle = true;
						r_count++;
						piece_begin = piece_end;
					}
				} else {
					if (middle_dst / middle_src > 64.0f) {
						break; // Scale too large; fall back to stretch.
					}
					// Tile Fit: whole region middles scaled by an integer factor.
					int tiles = MAX(1, int(Math::floor(middle_dst / middle_src + 0.5f)));
					float piece_size = middle_dst / tiles;
					for (int i = 0; i < tiles; i++) {
						float piece_begin = middle_begin + i * piece_size;
						r_segments[r_count].dst_begin = piece_begin;
						r_segments[r_count].dst_end = piece_begin + piece_size;
						r_segments[r_count].uv_begin = margin_begin;
						r_segments[r_count].uv_end = margin_begin + middle_src;
						r_segments[r_count].is_middle = true;
						r_count++;
					}
				}
				middle_done = true;
			} break;
			case RS::NINE_PATCH_STRETCH:
			default:
				break;
		}
		if (!middle_done) {
			// Stretch: linear map of the middle onto the region middle. A region
			// middle smaller than the margins produces a reversed (negative size)
			// mapping, which the rect command reproduces via its flip flags.
			r_segments[r_count].dst_begin = middle_begin;
			r_segments[r_count].dst_end = middle_end;
			r_segments[r_count].uv_begin = margin_begin;
			r_segments[r_count].uv_end = margin_begin + middle_src;
			r_segments[r_count].is_middle = true;
			r_count++;
		}
	}

	// [draw_size - margin_end, draw_size): reversed 1:1 from the region end
	// (shader branch `pixel >= draw_size - margin_end`).
	const float end_begin = MAX(middle_end, start_end);
	if (end_begin < draw_size - CMP_EPSILON) {
		r_segments[r_count].dst_begin = end_begin;
		r_segments[r_count].dst_end = draw_size;
		r_segments[r_count].uv_begin = p_region_size - (draw_size - end_begin);
		r_segments[r_count].uv_end = p_region_size;
		r_segments[r_count].is_middle = false;
		r_count++;
	}
}

} // namespace
#endif // WEB_ENABLED

void RendererCanvasCull::canvas_item_add_nine_patch(RID p_item, const Rect2 &p_rect, const Rect2 &p_source, RID p_texture, const Vector2 &p_topleft, const Vector2 &p_bottomright, RS::NinePatchAxisMode p_x_axis_mode, RS::NinePatchAxisMode p_y_axis_mode, bool p_draw_center, const Color &p_modulate) {
	Item *canvas_item = canvas_item_owner.get_or_null(p_item);
	ERR_FAIL_NULL(canvas_item);

#ifdef WEB_ENABLED
	// Expand the nine-patch into regular textured rect commands (see
	// _ninepatch_axis_segments): the USE_NINEPATCH shader specialization is
	// never compiled or executed on Web.
	if (p_rect.size.x <= 0.0f || p_rect.size.y <= 0.0f) {
		return; // The shader maps non-positive sizes in undefined ways; callers use positive ones.
	}

	Size2 region_size;
	Size2 region_offset;
	if (p_source != Rect2()) {
		region_size = p_source.size;
		region_offset = p_source.position;
	} else {
		// The shader maps the whole texture; a null texture RID falls back to
		// the 1x1 white texture, matching _prepare_canvas_texture.
		region_size = p_texture.is_valid() ? RSG::texture_storage->texture_size_with_proxy(p_texture) : Size2(1, 1);
		if (region_size.x <= 0 || region_size.y <= 0) {
			region_size = Size2(1, 1);
		}
	}

	NinePatchAxisSegment x_segments[66];
	NinePatchAxisSegment y_segments[66];
	int x_count = 0;
	int y_count = 0;
	_ninepatch_axis_segments(p_rect.size.x, p_topleft.x, p_bottomright.x, region_size.x, p_x_axis_mode, x_segments, x_count);
	_ninepatch_axis_segments(p_rect.size.y, p_topleft.y, p_bottomright.y, region_size.y, p_y_axis_mode, y_segments, y_count);

	for (int y = 0; y < y_count; y++) {
		const NinePatchAxisSegment &ys = y_segments[y];
		for (int x = 0; x < x_count; x++) {
			const NinePatchAxisSegment &xs = x_segments[x];
			if (!p_draw_center && xs.is_middle && ys.is_middle) {
				continue;
			}
			Rect2 dst_rect(p_rect.position.x + xs.dst_begin, p_rect.position.y + ys.dst_begin, xs.dst_end - xs.dst_begin, ys.dst_end - ys.dst_begin);
			Rect2 src_rect(region_offset.x + xs.uv_begin, region_offset.y + ys.uv_begin, xs.uv_end - xs.uv_begin, ys.uv_end - ys.uv_begin);
			canvas_item_add_texture_rect_region(p_item, dst_rect, p_texture, src_rect, p_modulate, false, false);
		}
	}
	return;
#endif

	Item::CommandNinePatch *style = canvas_item->alloc_command<Item::CommandNinePatch>();
	ERR_FAIL_NULL(style);

	style->texture = p_texture;

	style->rect = p_rect;
	style->source = p_source;
	style->draw_center = p_draw_center;
	style->color = p_modulate;
	style->margin[SIDE_LEFT] = p_topleft.x;
	style->margin[SIDE_TOP] = p_topleft.y;
	style->margin[SIDE_RIGHT] = p_bottomright.x;
	style->margin[SIDE_BOTTOM] = p_bottomright.y;
	style->axis_x = p_x_axis_mode;
	style->axis_y = p_y_axis_mode;
}
```

## 四、方案性质与代价

- **像素级等价**:CPU 数学与着色器数学逐项一致,九块的位置与 UV 完全相同;实测多机型无可见差异、无接缝;
- **性能不变**:九块同贴图合并为同一 draw call(同批实例数 1→9),绘制次数不变;且 NinePatch 批从此可并入普通矩形批,批合并行为更优;
- **游戏项目零改动**:节点/资源/场景/主题全部不动——改的是组件绘制实现在引擎内的翻译层,对外接口透明;
- **影响面锁死**:`WEB_ENABLED` 门控,仅 Web 构建的九宫格路径;桌面编辑器与其它平台编译产物不变;渲染器其余部分零改动。

## 五、验证

- 此前必现掉图的机型:修复后 **10 台设备全部正常显示九宫格 UI,0 复现**;
- 正常机型回归无差异;开发者工具(非 GLX 环境)行为不变。

## 六、给同类问题的排查提示

1. 遇到"特定绘制内容在部分 GLX 机型静默消失"时,先做**元素级对照**,把范围从"界面"缩小到"绘制命令类型 → 着色器特化变体";
2. 检查该变体是否为"独立 program、懒编译、Web 无缓存"的冷门特化——这类最容易被低端驱动打穿,且失败是静默的(引擎侧 `WARN_PRINT_ONCE` 一次,驱动谎报则零日志);
3. 修复优先考虑**在命令层降级为已被全机型验证的基线路径**(绕开),而不是试图感知/修复回放端的编译失败(无法感知);
4. 已知相关事实:官方 FAQ 承认 GLX 存在渲染异常类问题;时间戳查询、OFFSCREEN_FRAMEBUFFER 等标准 GL 能力在 GLX 下也有绕开先例——本例属于同一模式在着色器特化粒度上的体现。
