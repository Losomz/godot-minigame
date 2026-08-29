(function () {
    const EVENT_CALLBACK_NAME = '__dsOnWechatAdEvent';
    let eventCallback = null;

    function getRoots() {
        const roots = [];
        if (typeof GameGlobal !== 'undefined' && GameGlobal) {
            roots.push(GameGlobal);
        }
        if (typeof globalThis !== 'undefined' && globalThis) {
            roots.push(globalThis);
        }
        if (typeof window !== 'undefined' && window) {
            roots.push(window);
        }
        return roots;
    }

    function getWxApi() {
        return (typeof wx !== 'undefined' && wx)
            || (typeof GameGlobal !== 'undefined' && GameGlobal.wx)
            || (typeof globalThis !== 'undefined' && globalThis.wx)
            || (typeof window !== 'undefined' && window.wx)
            || null;
    }

    function getWindowInfo() {
        const wxApi = getWxApi();
        if (!wxApi) {
            return {};
        }
        try {
            if (typeof wxApi.getWindowInfo === 'function') {
                return wxApi.getWindowInfo() || {};
            }
            if (typeof wxApi.getSystemInfoSync === 'function') {
                return wxApi.getSystemInfoSync() || {};
            }
        } catch (_err) {
            return {};
        }
        return {};
    }

    function getSdkVersion() {
        const info = getWindowInfo();
        if (info.SDKVersion) {
            return String(info.SDKVersion);
        }
        const wxApi = getWxApi();
        if (!wxApi || typeof wxApi.getSystemInfoSync !== 'function') {
            return '';
        }
        try {
            const systemInfo = wxApi.getSystemInfoSync() || {};
            return systemInfo.SDKVersion ? String(systemInfo.SDKVersion) : '';
        } catch (_err) {
            return '';
        }
    }

    function getApiName(type) {
        if (type === 'rewarded') {
            return 'createRewardedVideoAd';
        }
        if (type === 'interstitial') {
            return 'createInterstitialAd';
        }
        if (type === 'custom') {
            return 'createCustomAd';
        }
        return '';
    }

    function normalizeError(err) {
        if (!err) {
            return { errMsg: 'unknown' };
        }
        if (typeof err === 'string') {
            return { errMsg: err };
        }
        const normalized = {};
        if (typeof err.errCode !== 'undefined') {
            normalized.errCode = err.errCode;
        }
        if (typeof err.errno !== 'undefined' && typeof normalized.errCode === 'undefined') {
            normalized.errCode = err.errno;
        }
        if (err.errMsg) {
            normalized.errMsg = String(err.errMsg);
        } else if (err.message) {
            normalized.errMsg = String(err.message);
        } else {
            try {
                normalized.errMsg = JSON.stringify(err);
            } catch (_jsonErr) {
                normalized.errMsg = String(err);
            }
        }
        return normalized;
    }

    function buildEvent(type, stage, ok, extra) {
        const wxApi = getWxApi();
        const apiName = getApiName(type);
        return Object.assign({
            type,
            stage,
            ok,
            sdkVersion: getSdkVersion(),
            hasWx: !!wxApi,
            hasApi: !!(wxApi && apiName && typeof wxApi[apiName] === 'function')
        }, extra || {});
    }

    function callGodot(event) {
        const payload = JSON.stringify(event);
        if (typeof eventCallback === 'function') {
            eventCallback(payload);
            return;
        }
        const roots = getRoots();
        for (let i = 0; i < roots.length; i += 1) {
            const callback = roots[i] && roots[i][EVENT_CALLBACK_NAME];
            if (typeof callback === 'function') {
                callback(payload);
                return;
            }
        }
    }

    function getGodotSdkHosts() {
        const hosts = [];
        const seen = [];
        const roots = getRoots();
        let fallbackSdk = null;
        if (typeof GODOTSDK !== 'undefined' && GODOTSDK) {
            fallbackSdk = GODOTSDK;
        }
        for (const root of roots) {
            if (root.GODOTSDK) {
                fallbackSdk = root.GODOTSDK;
                break;
            }
        }
        if (!fallbackSdk) {
            fallbackSdk = {};
        }
        for (const root of roots) {
            if (!root.GODOTSDK) {
                root.GODOTSDK = fallbackSdk;
            }
            if (root.GODOTSDK && seen.indexOf(root.GODOTSDK) < 0) {
                seen.push(root.GODOTSDK);
                hosts.push(root.GODOTSDK);
            }
        }
        return hosts;
    }

    function attachGodotSdkApi(bridgeApi) {
        const hosts = getGodotSdkHosts();
        for (const sdk of hosts) {
            sdk.dsWxAdGetBridgeVersion = function () {
                return 2;
            };
            sdk.dsWxAdSetEventCallback = function (callback) {
                eventCallback = callback;
                report('bridge', 'callback', typeof callback === 'function', {
                    errMsg: typeof callback === 'function' ? '' : 'callback is not a function'
                });
                return typeof callback === 'function';
            };
            sdk.dsWxAdClearEventCallback = function () {
                eventCallback = null;
                return true;
            };
            sdk.dsWxAdDebugState = function () {
                bridgeApi.debugState();
                return true;
            };
            sdk.dsWxAdShowRewarded = function (adUnitId, requestId) {
                bridgeApi.showRewarded(
                    String(adUnitId || ''),
                    String(requestId || '')
                );
                return true;
            };
            sdk.dsWxAdCancelRewarded = function (requestId) {
                bridgeApi.cancelRewarded(String(requestId || ''));
                return true;
            };
            sdk.dsWxAdShowInterstitial = function (adUnitId) {
                bridgeApi.showInterstitial(String(adUnitId || ''));
                return true;
            };
            sdk.dsWxAdShowCustom = function (
                adUnitId,
                position,
                width,
                estimatedHeight,
                offsetX,
                offsetY,
                left,
                top
            ) {
                bridgeApi.showCustom(String(adUnitId || ''), {
                    position: String(position || 'top'),
                    width: Number(width) || 350,
                    estimatedHeight: Number(estimatedHeight) || 120,
                    offsetX: Number(offsetX) || 0,
                    offsetY: Number(offsetY) || 0,
                    left: Number(left) || 0,
                    top: Number(top) || 0
                });
                return true;
            };
            sdk.dsWxAdHideCustom = function (adUnitId) {
                bridgeApi.hideCustom(String(adUnitId || ''));
                return true;
            };
            sdk.dsWxAdDestroyAll = function () {
                bridgeApi.destroyAll();
                return true;
            };
        }
        report('bridge', 'sdk-attach', hosts.length > 0, {
            sdkHostCount: hosts.length,
            errMsg: hosts.length > 0 ? '' : 'GODOTSDK host not available'
        });
    }

    function logEvent(event) {
        const parts = [
            '[SumeruWxAdBridge]',
            `type=${event.type}`,
            `stage=${event.stage}`,
            `ok=${event.ok}`,
            `sdkVersion=${event.sdkVersion || ''}`,
            `hasWx=${event.hasWx}`,
            `hasApi=${event.hasApi}`
        ];
        ['adUnitId', 'requestId', 'windowWidth', 'windowHeight', 'pixelRatio', 'style'].forEach((key) => {
            if (Object.prototype.hasOwnProperty.call(event, key)) {
                parts.push(`${key}=${event[key]}`);
            }
        });
        if (typeof event.errCode !== 'undefined') {
            parts.push(`errCode=${event.errCode}`);
        }
        if (event.errMsg) {
            parts.push(`errMsg=${event.errMsg}`);
        }
        const message = parts.join(' ');
        if (event.ok) {
            console.log(message);
        } else {
            console.error(message);
        }
    }

    function report(type, stage, ok, extra) {
        const event = buildEvent(type, stage, ok, extra);
        logEvent(event);
        callGodot(event);
        return event;
    }

    function getAdApi(type, eventExtra) {
        const wxApi = getWxApi();
        const apiName = getApiName(type);
        if (!wxApi || !apiName || typeof wxApi[apiName] !== 'function') {
            report(type, 'api-missing', false, Object.assign({
                errMsg: `${apiName || 'ad api'} not available`
            }, eventExtra || {}));
            return null;
        }
        return wxApi[apiName].bind(wxApi);
    }

    function destroyAd(ad, type, adUnitId) {
        if (!ad || typeof ad.destroy !== 'function') {
            return;
        }
        try {
            ad.destroy();
            report(type, 'destroy', true, { adUnitId });
        } catch (err) {
            report(type, 'destroy', false, Object.assign({ adUnitId }, normalizeError(err)));
        }
    }

    function hideAd(ad, type, adUnitId) {
        if (!ad || typeof ad.hide !== 'function') {
            return;
        }
        try {
            ad.hide();
            report(type, 'hide', true, { adUnitId });
        } catch (err) {
            report(type, 'hide', false, Object.assign({ adUnitId }, normalizeError(err)));
        }
    }

    function showWithLoadFallback(ad, type, adUnitId, extra) {
        const eventExtra = Object.assign({ adUnitId }, extra || {});
        if (!ad || typeof ad.show !== 'function') {
            report(type, 'show', false, Object.assign({
                errMsg: `${type} ad instance not available`
            }, eventExtra));
            return;
        }
        report(type, 'show-request', true, eventExtra);
        Promise.resolve()
            .then(() => ad.show())
            .then(() => {
                report(type, 'show', true, eventExtra);
            })
            .catch((showError) => {
                report(type, 'show-fallback', true, Object.assign({
                    fallback: 'load-then-show'
                }, eventExtra, normalizeError(showError)));
                if (typeof ad.load !== 'function') {
                    throw showError;
                }
                report(type, 'load-request', true, eventExtra);
                return ad.load()
                    .then(() => {
                        report(type, 'load', true, eventExtra);
                        return ad.show();
                    })
                    .then(() => {
                        report(type, 'show-after-load', true, eventExtra);
                    });
            })
            .catch((err) => {
                report(type, 'error', false, Object.assign({}, eventExtra, normalizeError(err)));
            });
    }

    function clamp(value, minimum, maximum) {
        return Math.min(Math.max(value, minimum), maximum);
    }

    function calculateCustomStyle(rawPlacement) {
        const info = getWindowInfo();
        const windowWidth = Math.max(0, Number(info.windowWidth) || 0);
        const windowHeight = Math.max(0, Number(info.windowHeight) || 0);
        const requestedWidth = Math.max(1, Number(rawPlacement.width) || 350);
        const width = windowWidth > 0 ? Math.min(requestedWidth, windowWidth) : requestedWidth;
        const estimatedHeight = Math.max(1, Number(rawPlacement.estimatedHeight) || 120);
        const offsetX = Number(rawPlacement.offsetX) || 0;
        const offsetY = Number(rawPlacement.offsetY) || 0;
        const maxLeft = Math.max(0, windowWidth - width);
        const maxTop = Math.max(0, windowHeight - estimatedHeight);
        let left = 0;
        let top = 0;

        switch (String(rawPlacement.position || 'top').toLowerCase()) {
            case 'bottom':
                left = maxLeft / 2;
                top = maxTop;
                break;
            case 'left':
                left = 0;
                top = maxTop / 2;
                break;
            case 'right':
                left = maxLeft;
                top = maxTop / 2;
                break;
            case 'absolute':
                left = Number(rawPlacement.left) || 0;
                top = Number(rawPlacement.top) || 0;
                break;
            case 'top':
            default:
                left = maxLeft / 2;
                top = 0;
                break;
        }

        left = clamp(left + offsetX, 0, maxLeft);
        top = clamp(top + offsetY, 0, maxTop);
        return {
            style: { left, top, width },
            windowWidth,
            windowHeight,
            pixelRatio: Number(info.pixelRatio) || 1
        };
    }

    const bridge = {
        rewarded: null,
        rewardedId: '',
        rewardedRequestId: '',
        interstitial: null,
        interstitialId: '',
        customAds: {},

        createRewarded(adUnitId, requestId) {
            const eventExtra = { adUnitId, requestId };
            const createAd = getAdApi('rewarded', eventExtra);
            if (!createAd) {
                return null;
            }

            this.cancelRewarded(this.rewardedRequestId);
            this.rewardedId = adUnitId;
            this.rewardedRequestId = requestId;
            try {
                const ad = createAd({ adUnitId });
                this.rewarded = ad;
                if (ad && typeof ad.onClose === 'function') {
                    ad.onClose((result) => {
                        report('rewarded', 'close', true, {
                            adUnitId,
                            requestId,
                            isEnded: !!(result && result.isEnded)
                        });
                    });
                }
                if (ad && typeof ad.onError === 'function') {
                    ad.onError((err) => {
                        report('rewarded', 'error', false, Object.assign(
                            { adUnitId, requestId },
                            normalizeError(err)
                        ));
                    });
                }
                report('rewarded', 'create', !!ad, {
                    adUnitId,
                    requestId,
                    errMsg: ad ? '' : 'wx.createRewardedVideoAd returned empty instance'
                });
                return ad;
            } catch (err) {
                this.rewarded = null;
                this.rewardedId = '';
                this.rewardedRequestId = '';
                report('rewarded', 'create', false, Object.assign(
                    { adUnitId, requestId },
                    normalizeError(err)
                ));
                return null;
            }
        },

        showRewarded(adUnitId, requestId) {
            showWithLoadFallback(
                this.createRewarded(adUnitId, requestId),
                'rewarded',
                adUnitId,
                { requestId }
            );
        },

        cancelRewarded(requestId) {
            if (!this.rewarded || (requestId && requestId !== this.rewardedRequestId)) {
                return;
            }
            const ad = this.rewarded;
            const adUnitId = this.rewardedId;
            const activeRequestId = this.rewardedRequestId;
            this.rewarded = null;
            this.rewardedId = '';
            this.rewardedRequestId = '';
            report('rewarded', 'cancel', true, {
                adUnitId,
                requestId: activeRequestId
            });
            destroyAd(ad, 'rewarded', adUnitId);
        },

        createInterstitial(adUnitId) {
            const createAd = getAdApi('interstitial');
            if (!createAd) {
                return null;
            }
            if (this.interstitial && this.interstitialId === adUnitId) {
                report('interstitial', 'reuse', true, { adUnitId });
                return this.interstitial;
            }
            destroyAd(this.interstitial, 'interstitial', this.interstitialId);
            this.interstitial = null;
            this.interstitialId = adUnitId;
            try {
                const ad = createAd({ adUnitId });
                this.interstitial = ad;
                if (ad && typeof ad.onClose === 'function') {
                    ad.onClose(() => report('interstitial', 'close', true, { adUnitId }));
                }
                if (ad && typeof ad.onError === 'function') {
                    ad.onError((err) => {
                        report('interstitial', 'error', false, Object.assign({ adUnitId }, normalizeError(err)));
                    });
                }
                report('interstitial', 'create', !!ad, {
                    adUnitId,
                    errMsg: ad ? '' : 'wx.createInterstitialAd returned empty instance'
                });
                return ad;
            } catch (err) {
                this.interstitial = null;
                report('interstitial', 'create', false, Object.assign({ adUnitId }, normalizeError(err)));
                return null;
            }
        },

        showInterstitial(adUnitId) {
            showWithLoadFallback(this.createInterstitial(adUnitId), 'interstitial', adUnitId);
        },

        createCustom(adUnitId, placement) {
            const createAd = getAdApi('custom');
            if (!createAd) {
                return null;
            }
            const calculated = calculateCustomStyle(placement);
            const styleKey = JSON.stringify(calculated.style);
            const existing = this.customAds[adUnitId];
            if (existing && existing.ad && existing.styleKey === styleKey) {
                report('custom', 'reuse', true, { adUnitId, style: styleKey });
                return existing.ad;
            }
            if (existing) {
                hideAd(existing.ad, 'custom', adUnitId);
                destroyAd(existing.ad, 'custom', adUnitId);
                delete this.customAds[adUnitId];
            }
            try {
                const ad = createAd({
                    adUnitId,
                    adIntervals: 30,
                    style: calculated.style
                });
                this.customAds[adUnitId] = { ad, styleKey };
                if (ad && typeof ad.onLoad === 'function') {
                    ad.onLoad(() => report('custom', 'load', true, { adUnitId }));
                }
                if (ad && typeof ad.onError === 'function') {
                    ad.onError((err) => {
                        const current = this.customAds[adUnitId];
                        if (current && current.ad === ad) {
                            hideAd(ad, 'custom', adUnitId);
                            destroyAd(ad, 'custom', adUnitId);
                            delete this.customAds[adUnitId];
                        }
                        report('custom', 'error', false, Object.assign({ adUnitId }, normalizeError(err)));
                    });
                }
                report('custom', 'create', !!ad, {
                    adUnitId,
                    style: styleKey,
                    windowWidth: calculated.windowWidth,
                    windowHeight: calculated.windowHeight,
                    pixelRatio: calculated.pixelRatio,
                    errMsg: ad ? '' : 'wx.createCustomAd returned empty instance'
                });
                return ad;
            } catch (err) {
                delete this.customAds[adUnitId];
                report('custom', 'create', false, Object.assign({ adUnitId }, normalizeError(err)));
                return null;
            }
        },

        showCustom(adUnitId, placement) {
            showWithLoadFallback(this.createCustom(adUnitId, placement), 'custom', adUnitId);
        },

        hideCustom(adUnitId) {
            if (adUnitId) {
                const entry = this.customAds[adUnitId];
                if (entry) {
                    hideAd(entry.ad, 'custom', adUnitId);
                }
                return;
            }
            Object.keys(this.customAds).forEach((id) => hideAd(this.customAds[id].ad, 'custom', id));
        },

        destroyCustom(adUnitId) {
            const ids = adUnitId ? [adUnitId] : Object.keys(this.customAds);
            ids.forEach((id) => {
                const entry = this.customAds[id];
                if (!entry) {
                    return;
                }
                hideAd(entry.ad, 'custom', id);
                destroyAd(entry.ad, 'custom', id);
                delete this.customAds[id];
            });
        },

        destroyAll() {
            this.cancelRewarded(this.rewardedRequestId);
            destroyAd(this.interstitial, 'interstitial', this.interstitialId);
            this.interstitial = null;
            this.interstitialId = '';
            this.destroyCustom('');
        },

        debugState() {
            const info = getWindowInfo();
            report('bridge', 'debug', true, {
                rewardedReady: !!this.rewarded,
                interstitialReady: !!this.interstitial,
                customReady: Object.keys(this.customAds).length > 0,
                customCount: Object.keys(this.customAds).length,
                rewardedApi: !!(getWxApi() && getWxApi().createRewardedVideoAd),
                interstitialApi: !!(getWxApi() && getWxApi().createInterstitialAd),
                customApi: !!(getWxApi() && getWxApi().createCustomAd),
                windowWidth: Number(info.windowWidth) || 0,
                windowHeight: Number(info.windowHeight) || 0,
                pixelRatio: Number(info.pixelRatio) || 1
            });
        }
    };

    attachGodotSdkApi(bridge);
    bridge.debugState();
})();
