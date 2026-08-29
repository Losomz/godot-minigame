#include "core/network_proxy.h"

#include "core/plugin_state_store.h"

using namespace godot;

namespace toolkit {

namespace {

constexpr const char *NETWORK_STATE_SECTION = "network";
constexpr const char *DEFAULT_PROXY_HOST = "127.0.0.1";
constexpr int DEFAULT_PROXY_PORT = 7890;

bool valid_proxy(const String &p_host, int p_port) {
	const String host = p_host.strip_edges();
	return !host.is_empty() && !host.contains("://") && !host.contains("/") && !host.contains("\\") &&
			p_port >= 1 && p_port <= 65535;
}

Dictionary normalized_config() {
	const Dictionary state = PluginStateStore::load_section(NETWORK_STATE_SECTION);
	const String host = String(state.get("proxy_host", DEFAULT_PROXY_HOST)).strip_edges();
	const int port = int(state.get("proxy_port", DEFAULT_PROXY_PORT));

	Dictionary config;
	config["proxy_enabled"] = bool(state.get("proxy_enabled", false)) && valid_proxy(host, port);
	config["proxy_host"] = host.is_empty() ? String(DEFAULT_PROXY_HOST) : host;
	config["proxy_port"] = port >= 1 && port <= 65535 ? port : DEFAULT_PROXY_PORT;
	return config;
}

} // namespace

Dictionary NetworkProxy::get_config() {
	return normalized_config();
}

Error NetworkProxy::save_config(bool p_enabled, const String &p_host, int p_port) {
	String host = p_host.strip_edges();
	if (p_enabled && !valid_proxy(host, p_port)) {
		return ERR_INVALID_PARAMETER;
	}
	if (host.is_empty()) {
		host = DEFAULT_PROXY_HOST;
	}
	if (p_port < 1 || p_port > 65535) {
		p_port = DEFAULT_PROXY_PORT;
	}

	Dictionary state;
	state["proxy_enabled"] = p_enabled;
	state["proxy_host"] = host;
	state["proxy_port"] = p_port;
	return PluginStateStore::save_section(NETWORK_STATE_SECTION, state);
}

void NetworkProxy::apply(HTTPRequest *p_request) {
	if (!p_request) {
		return;
	}
	const Dictionary config = normalized_config();
	const bool enabled = bool(config["proxy_enabled"]);
	const String host = enabled ? String(config["proxy_host"]) : String();
	const int port = enabled ? int(config["proxy_port"]) : -1;
	p_request->set_http_proxy(host, port);
	p_request->set_https_proxy(host, port);
}

void NetworkProxy::apply(const Ref<HTTPClient> &p_client) {
	if (p_client.is_null()) {
		return;
	}
	const Dictionary config = normalized_config();
	const bool enabled = bool(config["proxy_enabled"]);
	const String host = enabled ? String(config["proxy_host"]) : String();
	const int port = enabled ? int(config["proxy_port"]) : -1;
	p_client->set_http_proxy(host, port);
	p_client->set_https_proxy(host, port);
}

} // namespace toolkit
