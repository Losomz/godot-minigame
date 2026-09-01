# 微信 `InnerAudioContext` 跨音源复用导致监听器累积

## 一、现象

反复交替播放不同音源后，微信运行时持续输出以下警告：

```text
[Event] 21 listeners of event Play have been added, possibly causing memory leak.
[Event] 21 listeners of event Canplay have been added, possibly causing memory leak.
[Event] 21 listeners of event Ended have been added, possibly causing memory leak.
[Event] 21 listeners of event Error have been added, possibly causing memory leak.
[Event] 21 listeners of event Stop have been added, possibly causing memory leak.
```

警告不是单次触发。当同一个 `InnerAudioContext` 持续切换音源时，五类事件的监听器数量会继续增长，警告也会随播放次数重复出现。

## 二、根因

问题位于 `GodotAudio.WX.getContext(desiredSrc)` 的 Context 池复用策略。

旧实现会先查找 `src` 匹配的 Context，但在没有匹配项时，仍会从池中取出任意一个旧 Context：

```js
if (GodotAudio.WX.contextPool.length > 0) {
    const ctx = GodotAudio.WX.contextPool.pop();
    GodotAudio.WX.log(
        `Reusing context from pool (${GodotAudio.WX.contextPool.length} remaining)`
    );
    return ctx;
}
```

播放新音源时，该 Context 会被重新设置 `src`：

```js
ctx.src = newAudioPath;
```

微信 `InnerAudioContext` 的 `src` setter 会在内部安装一组播放事件监听器。调用 `offPlay()`、`offError()` 等公开方法只能清理外部注册的回调，不能移除因重新设置 `src` 而产生的内部监听器。

因此，一个 Context 在不同音源之间反复复用时，`Play`、`Canplay`、`Ended`、`Error` 和 `Stop` 的内部监听器会持续累积。

## 三、修复方案

只复用已经绑定到目标音源的 Context。池中不存在相同 `src` 时，创建新的 `InnerAudioContext`，不再修改旧 Context 的 `src`。

```js
getContext: function (desiredSrc) {
    if (desiredSrc) {
        const index = GodotAudio.WX.contextPool.findIndex(
            (ctx) => ctx.src === desiredSrc
        );

        if (index !== -1) {
            const ctx = GodotAudio.WX.contextPool[index];
            GodotAudio.WX.contextPool.splice(index, 1);
            GodotAudio.WX.log("Reusing pooled context with matching src");
            return ctx;
        }
    }

    const ctx = wx.createInnerAudioContext();
    GodotAudio.WX.log("Created new InnerAudioContext");
    return ctx;
}
```

Context 释放时使用最近最少使用（LRU）策略：

- 播放停止后将 Context 放回池中，供相同音源再次使用。
- 池中 Context 数量达到 `MAX_POOL_SIZE` 后，销毁最久未使用的空闲实例，再缓存刚释放的实例。
- 同一音源仍然具有 Context 复用能力。

修复位置：

```text
adapter/sources/platform/web/js/libs/library_godot_audio.js
```

## 四、回归测试

回归测试使用可控的 `FakeInnerAudioContext`，将每次 `src` 赋值模拟为一次内部监听器安装，并覆盖以下情形：

1. 不同音源不得共用同一 Context。
2. 相同音源反复播放时，必须复用原 Context。
3. 交替播放两个音源 30 次后，每个 Context 的 `src` 仍只赋值一次。
4. 池容量不得超过 `MAX_POOL_SIZE`。
5. 池满后必须淘汰最久未使用的 Context，并保留最近使用的 Context。

测试位置：

```text
adapter/sources/platform/web/js/tests/test_wechat_audio_context_single_release.js
```

## 五、实机验证结果

在相同播放流程下对比修复前后的完整日志：

| 事件 | 修复前 | 修复后 |
| --- | ---: | ---: |
| `Play` | 43 | 0 |
| `Canplay` | 43 | 0 |
| `Ended` | 43 | 0 |
| `Error` | 43 | 0 |
| `Stop` | 43 | 0 |

修复后同时确认：

- 相同音源可正常重复播放。
- 不同音源交替播放正常。
- 播放、停止及 Context 回收路径没有新增异常。
- 日志中不再出现上述五类音频事件监听器警告。

## 六、取舍

修复后，每个不同音源首次播放时可能需要创建新的 Context，因此 Context 创建数可能高于跨音源复用时的数量。

空闲 Context 缓存仍受 `MAX_POOL_SIZE` 限制；活跃实例数量由并发播放需求决定。池满时通过 LRU 淘汰旧实例，避免冷门音源长期占用缓存，也避免后续常用音源每次播放都新建 Context。相比在单个 Context 上无界累积内部监听器，按音源复用是可控且可验证的资源策略。

## 七、无关警告的识别

微信广告内部可能另外输出以下警告：

```text
[Event] 31 listeners of event LifeCycle:Show have been added, possibly causing memory leak.
[Event] 31 listeners of event LifeCycle:Hide have been added, possibly causing memory leak.
```

这类警告的调用栈位于 `createCustomAd` 和 `WAGameAd.js`，与 `InnerAudioContext` 的音频事件监听器累积不是同一问题，不应用它判定本修复是否生效。
