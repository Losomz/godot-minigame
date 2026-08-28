#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <unistd.h>
#endif

namespace {

#ifdef _WIN32

bool write_file(const std::wstring &path, const std::string &content) {
	HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return false;
	}
	DWORD written = 0;
	const bool success = WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written, nullptr) && written == content.size();
	CloseHandle(file);
	return success;
}

bool file_exists(const std::wstring &path) {
	const DWORD attributes = GetFileAttributesW(path.c_str());
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring quote_argument(const std::wstring &argument) {
	if (argument.find_first_of(L" \t\"") == std::wstring::npos) {
		return argument;
	}
	std::wstring quoted = L"\"";
	size_t backslashes = 0;
	for (wchar_t character : argument) {
		if (character == L'\\') {
			backslashes++;
			continue;
		}
		if (character == L'\"') {
			quoted.append(backslashes * 2 + 1, L'\\');
			quoted.push_back(character);
		} else {
			quoted.append(backslashes, L'\\');
			quoted.push_back(character);
		}
		backslashes = 0;
	}
	quoted.append(backslashes * 2, L'\\');
	quoted.push_back(L'\"');
	return quoted;
}

void write_failure(const std::wstring &result_path, const std::wstring &version) {
	std::string version_utf8;
	if (!version.empty()) {
		const int size = WideCharToMultiByte(CP_UTF8, 0, version.data(), static_cast<int>(version.size()), nullptr, 0, nullptr, nullptr);
		if (size > 0) {
			version_utf8.resize(size);
			WideCharToMultiByte(CP_UTF8, 0, version.data(), static_cast<int>(version.size()), version_utf8.data(), size, nullptr, nullptr);
		}
	}
	write_file(result_path, "{\"success\":false,\"message\":\"Update waiter failed before installation.\",\"version\":\"" + version_utf8 + "\"}");
}

#else

bool write_file(const std::string &path, const std::string &content) {
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	file.write(content.data(), static_cast<std::streamsize>(content.size()));
	return file.good();
}

void write_failure(const std::string &result_path, const std::string &version) {
	write_file(result_path, "{\"success\":false,\"message\":\"Update waiter failed before installation.\",\"version\":\"" + version + "\"}");
}

#endif

} // namespace

#ifdef _WIN32

int wmain(int argc, wchar_t **argv) {
	if (argc != 9) {
		return 2;
	}
	const DWORD parent_pid = static_cast<DWORD>(std::wcstoul(argv[1], nullptr, 10));
	const std::wstring ready_path = argv[2];
	const std::wstring editor_path = argv[3];
	const std::wstring helper_root = argv[4];
	const std::wstring helper_script = argv[5];
	const std::wstring manifest_path = argv[6];
	const std::wstring result_path = argv[7];
	const std::wstring version = argv[8];

	HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parent_pid);
	if (!parent) {
		write_failure(result_path, version);
		return 3;
	}
	if (!write_file(ready_path, "ready")) {
		CloseHandle(parent);
		write_failure(result_path, version);
		return 4;
	}
	if (WaitForSingleObject(parent, INFINITE) != WAIT_OBJECT_0) {
		CloseHandle(parent);
		write_failure(result_path, version);
		return 5;
	}
	CloseHandle(parent);

	std::wstring command = quote_argument(editor_path) + L" --headless --path " + quote_argument(helper_root) +
			L" --script " + quote_argument(helper_script) + L" -- " + quote_argument(manifest_path);
	std::vector<wchar_t> command_buffer(command.begin(), command.end());
	command_buffer.push_back(L'\0');
	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process{};
	if (!CreateProcessW(editor_path.c_str(), command_buffer.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
		write_failure(result_path, version);
		return 6;
	}
	CloseHandle(process.hThread);
	const DWORD wait_result = WaitForSingleObject(process.hProcess, INFINITE);
	DWORD exit_code = 1;
	const bool exit_code_read = GetExitCodeProcess(process.hProcess, &exit_code) != FALSE;
	CloseHandle(process.hProcess);
	if (wait_result != WAIT_OBJECT_0 || !exit_code_read || !file_exists(result_path)) {
		write_failure(result_path, version);
		return 7;
	}
	return 0;
}

#else

int main(int argc, char **argv) {
	if (argc != 9) {
		return 2;
	}
	const pid_t parent_pid = static_cast<pid_t>(std::stol(argv[1]));
	const std::string ready_path = argv[2];
	const std::string editor_path = argv[3];
	const std::string helper_root = argv[4];
	const std::string helper_script = argv[5];
	const std::string manifest_path = argv[6];
	const std::string result_path = argv[7];
	const std::string version = argv[8];

	if (kill(parent_pid, 0) != 0 && errno != EPERM) {
		write_failure(result_path, version);
		return 3;
	}
	if (!write_file(ready_path, "ready")) {
		write_failure(result_path, version);
		return 4;
	}
	while (kill(parent_pid, 0) == 0 || errno == EPERM) {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	if (errno != ESRCH) {
		write_failure(result_path, version);
		return 5;
	}

	execl(editor_path.c_str(), editor_path.c_str(), "--headless", "--path", helper_root.c_str(), "--script", helper_script.c_str(), "--", manifest_path.c_str(), static_cast<char *>(nullptr));
	write_failure(result_path, version);
	return 6;
}

#endif
