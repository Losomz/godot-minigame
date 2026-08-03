(function (root) {
  const gameGlobal = root.GameGlobal || (root.GameGlobal = {});
  const windowObject = root.window;

  const bridge = {
    ping() {
      const payload = "pong";
      console.log("[WeChatBridgeTest] ping ->", payload);
      return payload;
    },
    getStoreJsonString() {
      const payload = JSON.stringify({
        ok: true,
        source: "wechat-bridge-test",
        hasGlobalThisBridge: typeof root.WeChatBridge !== "undefined",
        hasGameGlobalBridge: typeof gameGlobal.WeChatBridge !== "undefined",
        windowEqualsGlobalThis: !!windowObject && windowObject === root,
      });
      console.log("[WeChatBridgeTest] getStoreJsonString ->", payload);
      return payload;
    },
  };

  root.WeChatBridge = bridge;
  gameGlobal.WeChatBridge = bridge;

  if (windowObject && windowObject !== root) {
    try {
      delete windowObject.WeChatBridge;
    } catch (e) {
      try {
        windowObject.WeChatBridge = undefined;
      } catch (_) {
      }
    }
  }

  console.log("[WeChatBridgeTest] installed", {
    hasGlobalThisBridge: typeof root.WeChatBridge !== "undefined",
    hasGameGlobalBridge: typeof gameGlobal.WeChatBridge !== "undefined",
    hasWindowBridge: !!windowObject && typeof windowObject.WeChatBridge !== "undefined",
    windowEqualsGlobalThis: !!windowObject && windowObject === root,
  });
})(typeof globalThis !== "undefined" ? globalThis : this);
