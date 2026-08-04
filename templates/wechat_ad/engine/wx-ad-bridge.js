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

    function getSdkVersion() {
        const wxApi = getWxApi();
        if (!wxApi || typeof wxApi.getSystemInfoSync !== 'function') {
            return '';
        }
        try {
            const info = wxApi.getSystemInfoSync();
            return info && info.SDKVersion ? String(info.SDKVersion) : '';
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
            sdk.dsWxAdSetEventCallback = function (callback) {
                eventCallback = callback;
                report('bridge', 'callback', typeof callback === 'function', {
                    errMsg: typeof callback === 'function' ? '' : 'callback is not a function'
                });
                return typeof callback === 'function';
            };
            sdk.dsWxAdDebugState = function () {
                bridgeApi.debugState();
                return true;
            };
            sdk.dsWxAdShowRewarded = function (adUnitId) {
                bridgeApi.showRewarded(String(adUnitId || ''));
                return true;
            };
            sdk.dsWxAdShowInterstitial = function (adUnitId) {
                bridgeApi.showInterstitial(String(adUnitId || ''));
                return true;
            };
            sdk.dsWxAdShowCustom = function (adUnitId, left, top, width) {
                bridgeApi.showCustom(String(adUnitId || ''), Number(left) || 0, Number(top) || 0, Number(width) || 350);
                return true;
            };
            sdk.dsWxAdHideCustom = function () {
                bridgeApi.hideCustom();
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
            '[WxAdBridge]',
            `type=${event.type}`,
            `stage=${event.stage}`,
            `ok=${event.ok}`,
            `sdkVersion=${event.sdkVersion || ''}`,
            `hasWx=${event.hasWx}`,
            `hasApi=${event.hasApi}`
        ];
        if (event.adUnitId) {
            parts.push(`adUnitId=${event.adUnitId}`);
        }
        ['rewardedApi', 'interstitialApi', 'customApi', 'rewardedReady', 'interstitialReady', 'customReady'].forEach((key) => {
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

    function getAdApi(type) {
        const wxApi = getWxApi();
        const apiName = getApiName(type);
        if (!wxApi || !apiName || typeof wxApi[apiName] !== 'function') {
            report(type, 'api-missing', false, {
                errMsg: `${apiName || 'ad api'} not available`
            });
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

    function showWithLoadFallback(ad, type, adUnitId) {
        if (!ad || typeof ad.show !== 'function') {
            report(type, 'show', false, {
                adUnitId,
                errMsg: `${type} ad instance not available`
            });
            return;
        }

        report(type, 'show-request', true, { adUnitId });
        Promise.resolve()
            .then(() => ad.show())
            .then(() => {
                report(type, 'show', true, { adUnitId });
            })
            .catch((showError) => {
                report(type, 'show-fallback', true, Object.assign({
                    adUnitId,
                    fallback: 'load-then-show'
                }, normalizeError(showError)));
                if (typeof ad.load !== 'function') {
                    throw showError;
                }
                report(type, 'load-request', true, { adUnitId });
                return ad.load()
                    .then(() => {
                        report(type, 'load', true, { adUnitId });
                        return ad.show();
                    })
                    .then(() => {
                        report(type, 'show-after-load', true, { adUnitId });
                    });
            })
            .catch((err) => {
                report(type, 'error', false, Object.assign({ adUnitId }, normalizeError(err)));
            });
    }

    const bridge = {
        rewarded: null,
        rewardedId: '',
        interstitial: null,
        interstitialId: '',
        custom: null,
        customId: '',
        customStyleKey: '',

        createRewarded(adUnitId) {
            const createAd = getAdApi('rewarded');
            if (!createAd) {
                return null;
            }
            if (this.rewarded && this.rewardedId === adUnitId) {
                report('rewarded', 'reuse', true, { adUnitId });
                return this.rewarded;
            }

            destroyAd(this.rewarded, 'rewarded', this.rewardedId);
            this.rewarded = null;
            this.rewardedId = adUnitId;
            try {
                const ad = createAd({ adUnitId });
                this.rewarded = ad;
                if (ad && typeof ad.onClose === 'function') {
                    ad.onClose((result) => {
                        report('rewarded', 'close', true, {
                            adUnitId,
                            isEnded: !!(result && result.isEnded)
                        });
                    });
                }
                if (ad && typeof ad.onError === 'function') {
                    ad.onError((err) => {
                        report('rewarded', 'error', false, Object.assign({ adUnitId }, normalizeError(err)));
                    });
                }
                report('rewarded', 'create', !!ad, {
                    adUnitId,
                    errMsg: ad ? '' : 'wx.createRewardedVideoAd returned empty instance'
                });
                return ad;
            } catch (err) {
                this.rewarded = null;
                report('rewarded', 'create', false, Object.assign({ adUnitId }, normalizeError(err)));
                return null;
            }
        },

        showRewarded(adUnitId) {
            const ad = this.createRewarded(adUnitId);
            showWithLoadFallback(ad, 'rewarded', adUnitId);
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
                    ad.onClose(() => {
                        report('interstitial', 'close', true, { adUnitId });
                    });
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
            const ad = this.createInterstitial(adUnitId);
            showWithLoadFallback(ad, 'interstitial', adUnitId);
        },

        createCustom(adUnitId, left, top, width) {
            const createAd = getAdApi('custom');
            if (!createAd) {
                return null;
            }

            const style = {
                left: Number(left) || 0,
                top: Number(top) || 0,
                width: Math.max(1, Number(width) || 350)
            };
            const styleKey = JSON.stringify(style);
            if (this.custom && this.customId === adUnitId && this.customStyleKey === styleKey) {
                report('custom', 'reuse', true, { adUnitId });
                return this.custom;
            }

            hideAd(this.custom, 'custom', this.customId);
            destroyAd(this.custom, 'custom', this.customId);
            this.custom = null;
            this.customId = adUnitId;
            this.customStyleKey = styleKey;
            try {
                const ad = createAd({ adUnitId, style });
                this.custom = ad;
                if (ad && typeof ad.onLoad === 'function') {
                    ad.onLoad(() => {
                        report('custom', 'load', true, { adUnitId });
                    });
                }
                if (ad && typeof ad.onError === 'function') {
                    ad.onError((err) => {
                        hideAd(this.custom, 'custom', adUnitId);
                        destroyAd(this.custom, 'custom', adUnitId);
                        this.custom = null;
                        this.customStyleKey = '';
                        report('custom', 'error', false, Object.assign({ adUnitId }, normalizeError(err)));
                    });
                }
                report('custom', 'create', !!ad, {
                    adUnitId,
                    style: styleKey,
                    errMsg: ad ? '' : 'wx.createCustomAd returned empty instance'
                });
                return ad;
            } catch (err) {
                this.custom = null;
                this.customStyleKey = '';
                report('custom', 'create', false, Object.assign({ adUnitId }, normalizeError(err)));
                return null;
            }
        },

        showCustom(adUnitId, left, top, width) {
            const ad = this.createCustom(adUnitId, left, top, width);
            showWithLoadFallback(ad, 'custom', adUnitId);
        },

        hideCustom() {
            hideAd(this.custom, 'custom', this.customId);
        },

        destroyCustom() {
            hideAd(this.custom, 'custom', this.customId);
            destroyAd(this.custom, 'custom', this.customId);
            this.custom = null;
            this.customStyleKey = '';
        },

        debugState() {
            report('bridge', 'debug', true, {
                rewardedReady: !!this.rewarded,
                interstitialReady: !!this.interstitial,
                customReady: !!this.custom,
                rewardedApi: !!(getWxApi() && getWxApi().createRewardedVideoAd),
                interstitialApi: !!(getWxApi() && getWxApi().createInterstitialAd),
                customApi: !!(getWxApi() && getWxApi().createCustomAd)
            });
        }
    };

    attachGodotSdkApi(bridge);
    bridge.debugState();
})();
