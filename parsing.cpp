#include "stdafx.h"
#include "parsing.h"
#include "errors.h"

using std::wstring;
using std::vector;
using std::make_unique;
using std::unique_ptr;

extern bool verbose;

bool extract_version(vector<wstring> &args, wstring *version_tag);

// If the first part of the shebang line matches any of these completely, it
// will be ignored. If its prefix matches, it will be trimmed.
const wstring SHEBANG_TEMPLATES[] = {
    L"/usr/bin/env",
    L"/usr/bin/"
};


template<typename iter>
iter find_next_arg(iter begin, iter end) {
    bool inQuote = false, escaping = false;
    bool finishedArg = false;

    for (auto c = begin; c != end; ++c) {
        if (finishedArg && *c != L' ') {
            return c;
        }

        if (*c == L'"' && !escaping) {
            inQuote = !inQuote;
        } else if (*c == L'\\') {
            escaping = !escaping;
        } else if (*c == L' ' && !escaping && !inQuote) {
            finishedArg = true;
        } else {
            escaping = false;
        }
    }

    return end;
}

vector<wstring> split_args(const wstring& str) {
    auto start = str.cbegin();
    decltype(start) nextStart;
    vector<wstring> res;

    if (verbose) {
        wprintf_s(L"Parsing arguments from %ls\n", str.c_str());
    }

    while ((nextStart = find_next_arg(start, str.cend())) != str.cend()) {
        auto end = nextStart - 1;
        while (end != start && *(end - 1) == L' ') {
            --end;
        }
        if (end == start) {
            break;
        }

        if (*start == L'"') {
            --end;
            if (end == start || *end != '"') {
                ++end;
            } else {
                ++start;
            }
        }
        
        res.emplace_back(start, end);
        if (verbose) {
            wprintf_s(L"  \"%ls\"\n", res.back().c_str());
        }

        start = nextStart;
    }

    res.emplace_back(start, str.cend());
    if (verbose) {
        wprintf_s(L"  \"%ls\"\nEnd of arguments\n", res.back().c_str());
    }
    return res;
}

template<typename iter>
iter skip_shebang(iter begin, const iter& end) {
    if (begin == end || *begin++ != L'#') {
        return end;
    }
    if (begin == end || *begin != L'!') {
        return end;
    }

    while (begin != end && iswspace(*++begin)) { }
    return begin;
}

bool equal_ignore_case(const wstring& left, const wstring& right) {
    return CSTR_EQUAL == ::CompareStringW(
        LOCALE_USER_DEFAULT,
        NORM_IGNORECASE,
        left.c_str(), -1,
        right.c_str(), -1);
}

unique_ptr<wstring> read_first_line(const wstring &filename) {
    DWORD err;
    auto hFile = CreateFileW(
        filename.c_str(),
        GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        err = GetLastError();
        if (err = ERROR_FILE_NOT_FOUND) {
            return nullptr;
        }
        print_error(err, L"opening file to read first line");
        return nullptr;
    }

    char buffer[2048] = { 0 };
    DWORD bytesRead = 0;
    BOOL success = ReadFile(hFile, buffer, sizeof(buffer) - 2, &bytesRead, nullptr);
    err = GetLastError();
    CloseHandle(hFile);

    if (!success) {
        print_error(err, L"reading first line of file");
        return nullptr;
    }
    if (bytesRead < 3) {
        if (verbose) {
            wprintf_s(L"Failed to read enough characters");
        }
        return nullptr;
    }

    if (bytesRead > 2 && buffer[0] == '\xFF' && buffer[1] == '\xFE') {
        return make_unique<wstring>(reinterpret_cast<wchar_t*>(buffer + 2));

    } else if (bytesRead > 2 && !buffer[0] && buffer[1]) {
        return make_unique<wstring>(reinterpret_cast<wchar_t*>(buffer));

    } else if (bytesRead > 3 && buffer[0] == '\xEF' && buffer[1] == '\xBB' && buffer[2] == '\xBF') {
        wchar_t wbuffer[1024];
        MultiByteToWideChar(CP_UTF8, 0, &buffer[3], -1, wbuffer, 1023);
        return make_unique<wstring>(wbuffer);

    } else if (bytesRead > 0) {
        wchar_t wbuffer[1024];
        MultiByteToWideChar(CP_ACP, 0, buffer, -1, wbuffer, 1023);
        return make_unique<wstring>(wbuffer);

    } else {
        return nullptr;
    }
}

bool parse_shebang(const wstring &filename, wstring *version_tag, vector<wstring> &allArgs) {
    if (verbose) {
        wprintf_s(L"Reading shebang from %s\n", filename.c_str());
    }

    auto pline = read_first_line(filename);
    if (!pline) {
        if (verbose) {
            wprintf_s(L"Cannot read file \"%ls\"\n", filename.c_str());
        }
        return false;
    }
    auto &line = *pline;

    if (line.length() < 2 || line[0] != '#' || line[1] != '!') {
        if (verbose) {
            wprintf_s(L"No shebang in line \"%ls\"\n", line.c_str());
        }
        return false;
    }

    const wchar_t newline[] = L"\r\n";
    auto endl = std::find(line.cbegin(), line.cend(), L'\r');
    endl = std::find(line.cbegin(), endl, L'\n');
    wstring shebang = { skip_shebang(line.cbegin(), endl), endl };
    if (verbose) {
        wprintf_s(L"  Shebang: \"%s\"\n", shebang.c_str());
    }


    auto args = split_args(shebang);

    if (args.size() == 0) {
        return false;
    }

    auto &a0 = args[0];
    bool found_template = false;
    for (const auto &prefix : SHEBANG_TEMPLATES) {
        if (std::equal(a0.cbegin(), a0.cend(), prefix.cbegin(), prefix.cend())) {
            if (verbose) {
                wprintf_s(L"Found full shebang template '%ls'\n", a0.c_str());
            }
            args.erase(args.begin(), args.begin() + 1);
            found_template = true;
            break;
        } else if (a0.length() > prefix.length() &&
            std::equal(a0.cbegin(), a0.cbegin() + prefix.length(), prefix.cbegin(), prefix.cend())) {
            if (verbose) {
                wprintf_s(L"Found prefix shebang template '%ls'\n", a0.c_str());
            }
            a0.erase(0, prefix.length());
            found_template = true;
            break;
        }
    }

    extract_version(args, version_tag);

    allArgs.erase(allArgs.cbegin(), allArgs.cbegin() + 1);
    allArgs.insert(allArgs.cbegin(), args.cbegin(), args.cend());
    return true;
}

vector<wstring> parse_args(const wstring& line, wstring *version_tag) {
    auto args = split_args(line);
    if (args.size() >= 1 && !extract_version(args, version_tag)) {
        args[0].clear();
    }
    return args;
}

bool extract_version(vector<wstring>& args, wstring *version_tag) {
    // Version may be extracted the following ways, in order of priority:
    //  1. First argument (if it starts with '-2' or '-3')
    //  2. Section of process name from first digit up to the last '.'
    //  3. Shebang in file referenced by first argument not beginning with '-'
    //  4. Propert in {processname}.ini
    //
    // The tag must start with '2' or '3', but may end with any text. A trailing
    // 'w' always selects the windowed executable if one is available.

    bool version_set = false;

    // Check first argument
    if (args.size() >= 2) {
        auto version_arg = args[1];
        if (version_arg.length() >= 1 && (version_arg[0] == L'2' || version_arg[0] == L'3')) {
            *version_tag = version_arg;
            if (verbose) {
                wprintf_s(L"Found version '%ls' in first argument\n", version_tag->c_str());
            }
            args.erase(args.cbegin(), args.cbegin() + 1);
            args[0].clear();
            version_set = true;
        }
    }

    // Check process name
    if (!version_set && args.size() >= 1) {
        const wchar_t first_digits[] = L"23";
        auto process = args[0];
        auto lastBackslash = std::find(process.crbegin(), process.crend(), L'\\');
        auto lastDot = std::find(process.crbegin(), process.crend(), L'.').base() - 1;
        if (verbose) {
            wprintf_s(L"Checking if '%ls' == '.exe'\n", wstring(lastDot, process.cend()).c_str());
        }
        if (!equal_ignore_case(L".exe", { lastDot, process.cend() })) {
            lastDot = process.cend();
        }
        auto rstart = std::find(process.crbegin(), lastBackslash, L'/').base();
        auto start = std::find_first_of(rstart, lastDot, std::cbegin(first_digits), std::cend(first_digits));
        if (start != lastDot) {
            *version_tag = { start, lastDot };
            if (verbose) {
                wprintf_s(L"Found version '%ls' in process name\n", version_tag->c_str());
            }
            args[0].clear();
            version_set = true;
        }
    }

    // Check shebang line
    if (!version_set && args.size() >= 2) {
        auto filename = std::find_if(args.cbegin() + 1, args.cend(), [](const auto& a) {
            return a.length() > 0 && a[0] != L'-';
        });
        if (filename != args.cend() && parse_shebang(*filename, version_tag, args)) {
            if (verbose) {
                wprintf_s(L"Found version '%ls' in shebang\n", version_tag->c_str());
            }
            version_set = true;
        }
    }

    if (!version_set && verbose) {
        wprintf_s(L"Did not find version\n");
    }

    return version_set;
}
