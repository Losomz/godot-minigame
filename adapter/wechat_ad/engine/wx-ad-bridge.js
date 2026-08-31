/**
 * Godot Minigame WeChat ad bridge, protocol v4.
 *
 * Owns wx ad instances and exposes JSON events on an existing GODOTSDK object.
 * Rewarded video is the only format with a show -> load -> show fallback.
 */
(function () {
    'use strict';

    const BRIDGE_VERSION = 4;
    let eventCallback = null;

    function roots() {
        const result = [];
        const append = (value) => {
            if (value && result.indexOf(value) < 0) result.push(value);
        };
        if (typeof GameGlobal !== 'undefined') append(GameGlobal);
        if (typeof globalThis !== 'undefined') append(globalThis);
        if (typeof window !== 'undefined') append(window);
        return result;
    }

    function wxApi() {
        if (typeof wx !== 'undefined' && wx) return wx;
        for (const root of roots()) if (root.wx) return root.wx;
        return null;
    }

    function windowInfo() {
        const api = wxApi();
        if (!api) return {};
        try {
            if (typeof api.getWindowInfo === 'function') return api.getWindowInfo() || {};
            if (typeof api.getSystemInfoSync === 'function') return api.getSystemInfoSync() || {};
        } catch (_err) {
            return {};
        }
        return {};
    }

    function sdkVersion() {
        const info = windowInfo();
        return info.SDKVersion ? String(info.SDKVersion) : '';
    }

    function apiName(type) {
        return {
            rewarded: 'createRewardedVideoAd',
            interstitial: 'createInterstitialAd',
            custom: 'createCustomAd'
        }[type] || '';
    }

    function normalizeError(error) {
        if (!error) return { errMsg: 'unknown error' };
        if (typeof error === 'string') return { errMsg: error };
        const result = {};
        if (error.errCode !== undefined) result.errCode = error.errCode;
        else if (error.errno !== undefined) result.errCode = error.errno;
        if (error.errMsg) result.errMsg = String(error.errMsg);
        else if (error.message) result.errMsg = String(error.message);
        else {
            try { result.errMsg = JSON.stringify(error); }
            catch (_jsonError) { result.errMsg = String(error); }
        }
        return result;
    }

    // Console output is opt-in; state always flows to GD via the event callback.
    const CONSOLE_DEBUG = false;

    function report(type, stage, ok, extra) {
        const api = wxApi();
        const method = apiName(type);
        const event = Object.assign({
            type,
            stage,
            ok,
            sdkVersion: sdkVersion(),
            hasWx: !!api,
            hasApi: !!(api && method && typeof api[method] === 'function')
        }, extra || {});
        if (CONSOLE_DEBUG) {
            const parts = ['[WxAdBridge]', type, stage];
            if (event.requestId) parts.push(`req=${event.requestId}`);
            if (event.errMsg) parts.push(event.errMsg);
            if (!ok) console.error(parts.join(' '));
            else if (stage === 'show' || stage === 'close') console.log(parts.join(' '));
        }
        if (typeof eventCallback === 'function') eventCallback(JSON.stringify(event));
        return event;
    }

    function getCreateApi(type, extra) {
        const api = wxApi();
        const method = apiName(type);
        if (!api || !method || typeof api[method] !== 'function') {
            report(type, 'api-missing', false, Object.assign({
                errMsg: `${method || 'ad api'} not available`
            }, extra || {}));
            return null;
        }
        return api[method].bind(api);
    }

    function destroyAd(ad, type, adUnitId) {
        if (!ad || typeof ad.destroy !== 'function') return;
        try {
            ad.destroy();
            report(type, 'destroy', true, { adUnitId });
        } catch (error) {
            report(type, 'destroy', false, Object.assign({ adUnitId }, normalizeError(error)));
        }
    }

    function endedValue(result) {
        return result === undefined || !!(result && result.isEnded);
    }

    const rewarded = {
        instance: null,
        adUnitId: '',
        requestId: '',
        state: 'idle',
        presentationSerial: 0,
        activePresentation: 0,

        ensure(adUnitId, requestId) {
            if (this.instance) {
                if (this.adUnitId !== adUnitId) {
                    report('rewarded', 'ad-unit-locked', false, {
                        adUnitId,
                        requestId,
                        lockedAdUnitId: this.adUnitId,
                        errMsg: `rewarded ad unit is locked to ${this.adUnitId}`
                    });
                    return null;
                }
                return this.instance;
            }
            const create = getCreateApi('rewarded', { adUnitId, requestId });
            if (!create) return null;
            let ad;
            try { ad = create({ adUnitId }); }
            catch (error) {
                report('rewarded', 'create', false, Object.assign(
                    { adUnitId, requestId }, normalizeError(error)
                ));
                return null;
            }
            if (!ad) {
                report('rewarded', 'create', false, {
                    adUnitId,
                    requestId,
                    errMsg: 'wx.createRewardedVideoAd returned an empty instance'
                });
                return null;
            }
            this.instance = ad;
            this.adUnitId = adUnitId;
            if (typeof ad.onLoad === 'function') {
                ad.onLoad((result) => report('rewarded', 'load', true, {
                    adUnitId: this.adUnitId,
                    useFallbackSharePage: !!(result && result.useFallbackSharePage)
                }));
            }
            if (typeof ad.onError === 'function') {
                ad.onError((error) => report('rewarded', 'instance-error', false, Object.assign({
                    adUnitId: this.adUnitId,
                    requestId: this.requestId
                }, normalizeError(error))));
            }
            if (typeof ad.onClose === 'function') {
                ad.onClose((result) => {
                    const requestIdAtClose = this.requestId;
                    report('rewarded', 'close', true, {
                        adUnitId: this.adUnitId,
                        requestId: requestIdAtClose,
                        isEnded: endedValue(result)
                    });
                    this.requestId = '';
                    this.state = 'idle';
                    this.activePresentation = 0;
                    this.preload();
                });
            }
            report('rewarded', 'create', true, { adUnitId, requestId });
            return ad;
        },

        preload() {
            const ad = this.instance;
            if (!ad || typeof ad.load !== 'function') return;
            try {
                Promise.resolve(ad.load()).catch((error) => report(
                    'rewarded', 'preload-error', false,
                    Object.assign({ adUnitId: this.adUnitId }, normalizeError(error))
                ));
            } catch (error) {
                report('rewarded', 'preload-error', false, Object.assign(
                    { adUnitId: this.adUnitId }, normalizeError(error)
                ));
            }
        },

        prepare(adUnitId) {
            if (!this.ensure(adUnitId, '')) return;
            report('rewarded', 'prepare', true, { adUnitId });
            this.preload();
        },

        show(adUnitId, requestId) {
            if (this.state !== 'idle') {
                report('rewarded', 'overlap', false, {
                    adUnitId,
                    requestId,
                    activeRequestId: this.requestId,
                    errMsg: 'another rewarded presentation is still active'
                });
                return;
            }
            const ad = this.ensure(adUnitId, requestId);
            if (!ad) return;
            this.requestId = requestId;
            this.state = 'showing';
            this.presentationSerial += 1;
            const presentation = this.presentationSerial;
            this.activePresentation = presentation;
            report('rewarded', 'show-request', true, { adUnitId, requestId });
            Promise.resolve()
                .then(() => {
                    if (typeof ad.show !== 'function') throw new Error('rewarded show() not available');
                    return ad.show();
                })
                .then(() => {
                    if (this.activePresentation === presentation && this.state === 'showing') {
                        this.state = 'visible';
                    }
                    report('rewarded', 'show', true, { adUnitId, requestId });
                })
                .catch((showError) => {
                    report('rewarded', 'show-fallback', true, Object.assign({
                        adUnitId,
                        requestId,
                        fallback: 'load-then-show'
                    }, normalizeError(showError)));
                    if (typeof ad.load !== 'function') throw showError;
                    return Promise.resolve(ad.load()).then(() => ad.show()).then(() => {
                        if (this.activePresentation === presentation && this.state === 'showing') {
                            this.state = 'visible';
                        }
                        report('rewarded', 'show', true, {
                            adUnitId,
                            requestId,
                            afterLoad: true
                        });
                    });
                })
                .catch((error) => {
                    report('rewarded', 'error', false, Object.assign(
                        { adUnitId, requestId }, normalizeError(error)
                    ));
                    if (this.activePresentation === presentation) {
                        this.requestId = '';
                        this.state = 'idle';
                        this.activePresentation = 0;
                    }
                });
        },

        cancel(requestId) {
            if (!this.instance || !this.requestId || requestId !== this.requestId) return;
            const cancelledRequestId = this.requestId;
            this.requestId = '';
            if (this.state !== 'idle') this.state = 'cancelled';
            report('rewarded', 'cancel', true, {
                adUnitId: this.adUnitId,
                requestId: cancelledRequestId
            });
        }
    };

    const interstitial = {
        active: null,

        destroySlot(slot) {
            if (!slot) return;
            if (this.active === slot) this.active = null;
            destroyAd(slot.ad, 'interstitial', slot.adUnitId);
        },

        settle(slot, stage, ok, extra) {
            if (!slot || slot.settled || this.active !== slot) return;
            slot.settled = true;
            report('interstitial', stage, ok, Object.assign({ adUnitId: slot.adUnitId }, extra || {}));
            this.destroySlot(slot);
        },

        show(adUnitId) {
            if (this.active) {
                report('interstitial', 'overlap', false, {
                    adUnitId,
                    activeAdUnitId: this.active.adUnitId,
                    errMsg: 'another interstitial request is still active'
                });
                return;
            }
            const create = getCreateApi('interstitial', { adUnitId });
            if (!create) return;
            let ad;
            try { ad = create({ adUnitId }); }
            catch (error) {
                report('interstitial', 'create', false, Object.assign({ adUnitId }, normalizeError(error)));
                return;
            }
            if (!ad) {
                report('interstitial', 'create', false, {
                    adUnitId,
                    errMsg: 'wx.createInterstitialAd returned an empty instance'
                });
                return;
            }
            const slot = { ad, adUnitId, settled: false };
            this.active = slot;
            if (typeof ad.onClose === 'function') ad.onClose(() => this.settle(slot, 'close', true));
            if (typeof ad.onError === 'function') {
                ad.onError((error) => this.settle(slot, 'error', false, normalizeError(error)));
            }
            report('interstitial', 'create', true, { adUnitId });
            report('interstitial', 'load-request', true, { adUnitId });
            Promise.resolve()
                .then(() => {
                    if (typeof ad.load !== 'function') throw new Error('interstitial load() not available');
                    return ad.load();
                })
                .then(() => {
                    if (slot.settled || this.active !== slot) return;
                    report('interstitial', 'load', true, { adUnitId });
                    if (typeof ad.show !== 'function') throw new Error('interstitial show() not available');
                    return ad.show();
                })
                .then(() => {
                    if (!slot.settled && this.active === slot) report('interstitial', 'show', true, { adUnitId });
                })
                .catch((error) => this.settle(slot, 'error', false, normalizeError(error)));
        },

        destroy() {
            if (!this.active) return;
            const slot = this.active;
            slot.settled = true;
            this.destroySlot(slot);
        }
    };

    function clamp(value, minimum, maximum) {
        return Math.min(Math.max(value, minimum), maximum);
    }

    function customStyle(placement) {
        const info = windowInfo();
        const windowWidth = Math.max(0, Number(info.windowWidth) || 0);
        const windowHeight = Math.max(0, Number(info.windowHeight) || 0);
        const requestedWidth = Math.max(1, Number(placement.width) || 350);
        const width = windowWidth > 0 ? Math.min(requestedWidth, windowWidth) : requestedWidth;
        const estimatedHeight = Math.max(1, Number(placement.estimatedHeight) || 120);
        const offsetX = Number(placement.offsetX) || 0;
        const offsetY = Number(placement.offsetY) || 0;
        const maxLeft = Math.max(0, windowWidth - width);
        const maxTop = Math.max(0, windowHeight - estimatedHeight);
        let left;
        let top;
        switch (String(placement.position || 'top').toLowerCase()) {
            case 'bottom': left = maxLeft / 2; top = maxTop; break;
            case 'left': left = 0; top = maxTop / 2; break;
            case 'right': left = maxLeft; top = maxTop / 2; break;
            case 'absolute': left = Number(placement.left) || 0; top = Number(placement.top) || 0; break;
            default: left = maxLeft / 2; top = 0; break;
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

    const custom = {
        slots: Object.create(null),

        current(slot) { return !!slot && this.slots[slot.adUnitId] === slot; },

        destroySlot(slot) {
            if (!this.current(slot)) return;
            delete this.slots[slot.adUnitId];
            destroyAd(slot.ad, 'custom', slot.adUnitId);
        },

        hidden(slot, source) {
            if (!this.current(slot) || slot.hiddenReported) return;
            slot.hiddenReported = true;
            slot.visible = false;
            report('custom', 'hide', true, { adUnitId: slot.adUnitId, source });
        },

        ensure(adUnitId, placement) {
            const calculated = customStyle(placement);
            const styleKey = JSON.stringify(calculated.style);
            const existing = this.slots[adUnitId];
            if (existing && existing.styleKey === styleKey) {
                report('custom', 'reuse', true, { adUnitId, style: styleKey });
                return existing;
            }
            if (existing) this.destroySlot(existing);
            const create = getCreateApi('custom', { adUnitId });
            if (!create) return null;
            let ad;
            try { ad = create({ adUnitId, adIntervals: 30, style: calculated.style }); }
            catch (error) {
                report('custom', 'create', false, Object.assign({ adUnitId }, normalizeError(error)));
                return null;
            }
            if (!ad) {
                report('custom', 'create', false, {
                    adUnitId,
                    style: styleKey,
                    errMsg: 'wx.createCustomAd returned an empty instance'
                });
                return null;
            }
            const slot = { ad, adUnitId, styleKey, visible: false, hiddenReported: true };
            this.slots[adUnitId] = slot;
            if (typeof ad.onLoad === 'function') {
                ad.onLoad(() => {
                    if (this.current(slot)) report('custom', 'load', true, { adUnitId });
                });
            }
            if (typeof ad.onError === 'function') {
                ad.onError((error) => {
                    if (!this.current(slot)) return;
                    report('custom', 'error', false, Object.assign({ adUnitId }, normalizeError(error)));
                    this.destroySlot(slot);
                });
            }
            const nativeHidden = () => this.hidden(slot, 'native');
            if (typeof ad.onHide === 'function') ad.onHide(nativeHidden);
            if (typeof ad.onClose === 'function') ad.onClose(nativeHidden);
            report('custom', 'create', true, {
                adUnitId,
                style: styleKey,
                windowWidth: calculated.windowWidth,
                windowHeight: calculated.windowHeight,
                pixelRatio: calculated.pixelRatio
            });
            return slot;
        },

        show(adUnitId, placement) {
            const slot = this.ensure(adUnitId, placement);
            if (!slot) return;
            slot.hiddenReported = false;
            report('custom', 'show-request', true, { adUnitId });
            Promise.resolve()
                .then(() => {
                    if (typeof slot.ad.show !== 'function') throw new Error('custom show() not available');
                    return slot.ad.show();
                })
                .then(() => {
                    if (!this.current(slot)) return;
                    slot.visible = !slot.hiddenReported;
                    report('custom', 'show', true, { adUnitId });
                })
                .catch((error) => {
                    if (!this.current(slot)) return;
                    report('custom', 'error', false, Object.assign({
                        adUnitId,
                        operation: 'show'
                    }, normalizeError(error)));
                    this.destroySlot(slot);
                });
        },

        hideSlot(slot) {
            if (!this.current(slot)) return;
            Promise.resolve()
                .then(() => {
                    if (typeof slot.ad.hide !== 'function') throw new Error('custom hide() not available');
                    return slot.ad.hide();
                })
                .then(() => this.hidden(slot, 'api'))
                .catch((error) => {
                    if (this.current(slot)) {
                        report('custom', 'error', false, Object.assign({
                            adUnitId: slot.adUnitId,
                            operation: 'hide'
                        }, normalizeError(error)));
                    }
                });
        },

        hide(adUnitId) {
            if (adUnitId) {
                this.hideSlot(this.slots[adUnitId]);
                return;
            }
            Object.keys(this.slots).forEach((key) => this.hideSlot(this.slots[key]));
        },

        destroyAll() {
            Object.keys(this.slots).forEach((key) => this.destroySlot(this.slots[key]));
        }
    };

    const bridge = {
        prepareRewarded: (adUnitId) => rewarded.prepare(adUnitId),
        showRewarded: (adUnitId, requestId) => rewarded.show(adUnitId, requestId),
        cancelRewarded: (requestId) => rewarded.cancel(requestId),
        showInterstitial: (adUnitId) => interstitial.show(adUnitId),
        showCustom: (adUnitId, placement) => custom.show(adUnitId, placement),
        hideCustom: (adUnitId) => custom.hide(adUnitId),
        destroyAll() {
            if (rewarded.requestId) rewarded.cancel(rewarded.requestId);
            interstitial.destroy();
            custom.destroyAll();
            report('bridge', 'destroy-all', true);
        },
        debugState() {
            report('bridge', 'state', true, {
                rewarded: {
                    adUnitId: rewarded.adUnitId,
                    requestId: rewarded.requestId,
                    state: rewarded.state,
                    created: !!rewarded.instance
                },
                interstitial: interstitial.active ? { adUnitId: interstitial.active.adUnitId } : null,
                custom: Object.keys(custom.slots).map((key) => ({
                    adUnitId: key,
                    visible: custom.slots[key].visible,
                    style: custom.slots[key].styleKey
                }))
            });
        }
    };

    function sdkHosts() {
        const result = [];
        const append = (sdk) => {
            if (sdk && (typeof sdk === 'object' || typeof sdk === 'function') && result.indexOf(sdk) < 0) {
                result.push(sdk);
            }
        };
        if (typeof GODOTSDK !== 'undefined') append(GODOTSDK);
        roots().forEach((root) => append(root.GODOTSDK));
        return result;
    }

    const hosts = sdkHosts();
    if (hosts.length === 0) {
        console.error('[WxAdBridge] sdk-attach failed: GODOTSDK not available');
        return;
    }
    hosts.forEach((sdk) => {
        sdk.dsWxAdGetBridgeVersion = () => BRIDGE_VERSION;
        sdk.dsWxAdSetEventCallback = (callback) => {
            if (typeof callback !== 'function') {
                report('bridge', 'callback', false, { errMsg: 'callback is not a function' });
                return false;
            }
            eventCallback = callback;
            report('bridge', 'callback', true);
            return true;
        };
        sdk.dsWxAdClearEventCallback = () => { eventCallback = null; return true; };
        sdk.dsWxAdDebugState = () => { bridge.debugState(); return true; };
        sdk.dsWxAdPrepareRewarded = (adUnitId) => { bridge.prepareRewarded(String(adUnitId || '')); return true; };
        sdk.dsWxAdShowRewarded = (adUnitId, requestId) => {
            bridge.showRewarded(String(adUnitId || ''), String(requestId || ''));
            return true;
        };
        sdk.dsWxAdCancelRewarded = (requestId) => { bridge.cancelRewarded(String(requestId || '')); return true; };
        sdk.dsWxAdShowInterstitial = (adUnitId) => { bridge.showInterstitial(String(adUnitId || '')); return true; };
        sdk.dsWxAdShowCustom = (adUnitId, position, width, estimatedHeight, offsetX, offsetY, left, top) => {
            bridge.showCustom(String(adUnitId || ''), {
                position: String(position || 'top'),
                width: Number(width),
                estimatedHeight: Number(estimatedHeight),
                offsetX: Number(offsetX),
                offsetY: Number(offsetY),
                left: Number(left),
                top: Number(top)
            });
            return true;
        };
        sdk.dsWxAdHideCustom = (adUnitId) => { bridge.hideCustom(String(adUnitId || '')); return true; };
        sdk.dsWxAdDestroyAll = () => { bridge.destroyAll(); return true; };
    });
    report('bridge', 'sdk-attach', true, { sdkHostCount: hosts.length, bridgeVersion: BRIDGE_VERSION });
}());
