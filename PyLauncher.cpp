#include "stdafx.h"
#include "parsing.h"
#include "errors.h"

using std::make_unique;
using std::vector;
using std::wstring;

bool verbose = false;

struct python_version {
    wstring tag;
    wstring install_path;
    wstring exe_name;
    int major, minor, priority;

    static const python_version invalid;

    python_version() { }

    python_version(wchar_t *tag, wchar_t *install_path, wchar_t *exe_name, int priority)
        : tag(tag), install_path(install_path), exe_name(exe_name), priority(priority) {
        wchar_t *c1 = tag, *c2;
        major = std::wcstoul(c1, &c2, 10);
        if (c1 == c2) {
            major = 0;
        } else if (*c2) {
            c1 = c2 + 1;
            minor = std::wcstoul(c1, &c2, 10);
            if (c1 == c2) {
                minor = 0;
            }
        }
    }

    bool is_valid() const {
        return !tag.empty();
    }

    wstring full_path() const {
        wstring res;
        res.reserve(install_path.size() + exe_name.size() + 1);
        res.assign(install_path);
        if (res.length() && res.back() != '\\') {
            res.append(L"\\");
        }
        res.append(exe_name);
        return res;
    }

    bool operator==(const python_version& other) const {
        return major == other.major && minor == other.minor && tag == other.tag;
    }

    bool operator<(const python_version& other) const {
        if (major > other.major) {
            return true;
        } else if (major < other.major) {
            return false;
        }
        if (minor > other.minor) {
            return true;
        } else if (minor < other.minor) {
            return false;
        }
        if (priority < other.priority) {
            return true;
        } else if (priority > other.priority) {
            return false;
        }
        return tag < other.tag;
    }
};

const python_version python_version::invalid;

void enum_reg(vector<python_version> &versions, HKEY hKey, int priority, bool preferW, bool onlyX86) {
    for (DWORD i = 0; ; ++i) {
        wchar_t name[64];
        DWORD cchName = 64;
        auto res = RegEnumKeyExW(hKey, i, name, &cchName, nullptr, nullptr, nullptr, nullptr);
        if (res == ERROR_NO_MORE_ITEMS) {
            break;
        } else if (res == ERROR_MORE_DATA) {
            // Skip names that are too long
            continue;
        } else if (res != ERROR_SUCCESS) {
            auto err = GetLastError();
            print_error(err, L"enumerating registry");
            return;
        }

        wchar_t subkeyName[128];
        HKEY subkey;
        if (swprintf_s(subkeyName, L"%s\\InstallPath", name) < 0) {
            print_error(ERROR_INVALID_PARAMETER, L"formatting subkey");
            continue;
        }

        res = RegOpenKeyExW(hKey, subkeyName, 0, KEY_READ, &subkey);
        if (res != ERROR_SUCCESS) {
            if (res != ERROR_FILE_NOT_FOUND) {
                auto err = GetLastError();
                print_error(err, wstring(L"opening subkey ") + subkeyName);
            }
            continue;
        }

        wchar_t path[MAX_PATH * sizeof(wchar_t)];
        DWORD cbPath = sizeof(path);
        res = RegQueryValueExW(subkey, nullptr, nullptr, nullptr, reinterpret_cast<LPBYTE>(path), &cbPath);
        if (res != ERROR_SUCCESS) {
            if (res != ERROR_FILE_NOT_FOUND) {
                auto err = GetLastError();
                print_error(err, wstring(L"reading ") + subkeyName);
            }
            if (RegCloseKey(subkey) != ERROR_SUCCESS) {
                auto err = GetLastError();
                print_error(err, L"closing subkey");
            }
            continue;
        }

        wchar_t exe_name[MAX_PATH * sizeof(wchar_t)];
        DWORD cbExeName = sizeof(exe_name);
        res = RegQueryValueExW(subkey, preferW ? L"WExeName" : L"ExeName", nullptr, nullptr, reinterpret_cast<LPBYTE>(path), &cbPath);
        if (res == ERROR_FILE_NOT_FOUND) {
            wcscpy_s(exe_name, preferW ? L"pythonw.exe" : L"python.exe");
        } else if (res != ERROR_SUCCESS) {
            auto err = GetLastError();
            print_error(err, wstring(L"reading exe name from ") + subkeyName);
            if (RegCloseKey(subkey) != ERROR_SUCCESS) {
                auto err = GetLastError();
                print_error(err, L"closing subkey");
            }
            continue;
        }

        if (RegCloseKey(subkey) != ERROR_SUCCESS) {
            auto err = GetLastError();
            print_error(err, L"closing subkey");
        }

        auto fullPath = wstring(path) + L"\\" + exe_name;
        DWORD binaryType;
        if (!GetBinaryTypeW(fullPath.c_str(), &binaryType)) {
            if (verbose) {
                wprintf_s(L"Cannot get file at %s\n", fullPath.c_str());
            }
            continue;
        }

        if (onlyX86 && binaryType != SCS_32BIT_BINARY) {
            if (verbose) {
                wprintf_s(L"Skipping non x86 %s\n", fullPath.c_str());
            }
            continue;
        }

        versions.emplace_back(name, path, exe_name, priority);
        auto existing = std::find(versions.begin(), versions.end(), *(versions.end() - 1));
        if (existing != versions.end() && existing != versions.end() - 1) {
            versions.pop_back();
        } else {
            if (verbose) {
                wprintf_s(L"- %-16s: %s\\%s\n", name, path, exe_name);
            }
        }
    }
}

auto find_python(const vector<python_version>& known, const wstring& version) -> decltype(known.begin()) {
    if (verbose) {
        wprintf_s(L"Finding match for %s\n", version.c_str());
    }
    return std::find_if(known.begin(), known.end(), [&](const python_version &pv) {
        if (verbose) {
            wprintf_s(L" considering %s\n", pv.tag.c_str());
        }
        if (pv.tag.length() < version.length()) {
            return false;
        }
        return std::equal(
            version.begin(), version.end(),
            pv.tag.begin(), pv.tag.begin() + version.length()
        );
    });

}

bool is_env_set(const wstring& name) {
    wchar_t buffer[1024];
    if (!GetEnvironmentVariableW(name.c_str(), buffer, 1024) || buffer[0] == '0' && !buffer[1]) {
        return false;
    }
    if (verbose) {
        wprintf(L"%s was set\n", name.c_str());
    }
    return true;
}

bool parse_version_from_program_name(const wstring &program, wstring *version, bool *preferW) {
    auto vstart = program.end();
    auto vend = vstart;

    for (auto c = program.begin(); c != program.end(); ++c) {
        if (*c == L'\\') {
            vstart = vend = program.end();
        } else if (vstart != program.end()) {
            if (*c == L'.') {
                vend = c;
            }
        } else if (*c == '2' || *c == '3') {
            vstart = c;
            vend = program.end();
        } else {
            *preferW = (*c == 'w' || *c == 'W');
        }
    }

    if (vstart == vend) {
        return false;
    }

    *version = { vstart, vend };
    return true;
}

python_version find_suitable_version(wstring version) {
    vector<python_version> pythons;
    bool preferW = false;
    bool onlyX86 = false;

    if (version.length() >= 1) {
        preferW = version.back() == L'w' || version.back() == L'W';
        if (preferW) {
            version.pop_back();
            if (verbose) {
                wprintf_s(L"Preferring windowed interpreters\n");
            }
        }
    }

    if (version.length() >= 3) {
        onlyX86 = std::equal(version.end() - 3, version.end(), L"-32");
        if (verbose && onlyX86) {
            wprintf_s(L"Only including 32-bit interpreters\n");
        }
    }

    HKEY key;
    LRESULT res;

    if (verbose) {
        wprintf_s(L"Searching HKCU\\Software\\Python\\PythonCore\n");
    }
    res = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Python\\PythonCore", 0, KEY_READ, &key);
    if (res == ERROR_SUCCESS) {
        enum_reg(pythons, key, 1, preferW, onlyX86);
        if (RegCloseKey(key) != ERROR_SUCCESS) {
            auto err = GetLastError();
            print_error(err, L"closing HKEY_CURRENT_USER search");
        }
    } else {
        auto err = GetLastError();
        print_error(err, L"scanning HKEY_CURRENT_USER");
    }

    if (verbose) {
        wprintf_s(L"Searching HKLM\\Software\\Python\\PythonCore (64-bit)\n");
    }
    res = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Python\\PythonCore", 0, KEY_READ | KEY_WOW64_64KEY, &key);
    if (res == ERROR_SUCCESS) {
        enum_reg(pythons, key, 2, preferW, onlyX86);
        if (RegCloseKey(key) != ERROR_SUCCESS) {
            auto err = GetLastError();
            print_error(err, L"closing HKEY_LOCAL_MACHINE (64-bit) search");
        }
    } else {
        auto err = GetLastError();
        print_error(err, L"scanning HKEY_LOCAL_MACHINE (64-bit)");
    }

    if (verbose) {
        wprintf_s(L"Searching HKCU\\Software\\Python\\PythonCore (32-bit)\n");
    }
    res = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Python\\PythonCore", 0, KEY_READ | KEY_WOW64_32KEY, &key);
    if (res == ERROR_SUCCESS) {
        enum_reg(pythons, key, 3, preferW, onlyX86);
        if (RegCloseKey(key) != ERROR_SUCCESS) {
            auto err = GetLastError();
            print_error(err, L"closing HKEY_LOCAL_MACHINE (32-bit) search");
        }
    } else {
        auto err = GetLastError();
        print_error(err, L"scanning HKEY_LOCAL_MACHINE (32-bit)");
    }

    std::sort(pythons.begin(), pythons.end());

    auto selected = pythons.cbegin();

    if (version.length()) {
        auto vb = version.begin(), ve = version.end();
        selected = find_python(pythons, { vb, ve });

        if (selected == pythons.end() && onlyX86 && version.length() > 3) {
            ve -= 3;
            selected = find_python(pythons, { vb, ve });
        }
    }

    if (selected == pythons.end()) {
        if (verbose) {
            wprintf_s(L"No suitable interpreter found\n");
        }
        // TODO: Download and install Python
        return python_version::invalid;
    }

    return *selected;
}

template<typename iter>
wstring join_args(iter start, iter end) {
    std::wostringstream message;
    bool first = true;
    for (; start != end; ++start) {
        if (!first) {
            message << L' ';
        }
        first = false;

        auto &a = *start;
        if (a.empty()) {
            message << L'"' << L'"';
        } else if (std::find(a.cbegin(), a.cend(), L' ') != a.cend()) {
            message << L'"' << a;
            if (a.back() == L'\\') {
                message << L'\\';
            }
            message << L'"';
        } else {
            message << a;
        }
    }

    return message.str();
}

int main() {
    wstring version;

    verbose = is_env_set(L"PYLAUNCHER_VERBOSE");
    auto noLaunch = is_env_set(L"PYLAUNCHER_NOLAUNCH");

    auto args = parse_args(GetCommandLineW(), &version);

    if (args.size() == 0) {
        wprintf_s(L"Invalid arguments!");
        return -1;
    }

    if (verbose) {
        auto message = join_args(args.cbegin(), args.cend());
        wprintf_s(L"Args: %s\n", message.c_str());
    }

    if (args[0].empty()) {
        if (verbose) {
            wprintf_s(L"Found version: %s\n", version.c_str());
        }

        auto python = find_suitable_version(version);
        if (!python.is_valid()) {
            return -1;
        }
        args[0] = python.full_path();
    }

    if (noLaunch || verbose) {
        auto message = join_args(args.cbegin(), args.cend());
        wprintf_s(L"Selected: %s\n", message.c_str());
    }

    if (noLaunch) {
        return 0;
    }

    // TODO: Launch

    return 0;
}

