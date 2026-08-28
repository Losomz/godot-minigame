#pragma once

#include <godot_cpp/classes/http_client.hpp>
#include <godot_cpp/classes/http_request.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace toolkit {

class NetworkProxy {
public:
	static godot::Dictionary get_config();
	static godot::Error save_config(bool p_enabled, const godot::String &p_host, int p_port);
	static void apply(godot::HTTPRequest *p_request);
	static void apply(const godot::Ref<godot::HTTPClient> &p_client);
};

} // namespace toolkit
