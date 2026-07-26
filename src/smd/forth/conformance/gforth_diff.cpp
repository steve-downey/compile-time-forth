// src/smd/forth/conformance/gforth_diff.cpp                          -*-C++-*-
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <smd/forth/conformance/gforth_diff.hpp>

#include <array>
#include <cctype>
#include <cstdio>

namespace smd::forth::conformance {

namespace {

/// Escapes @p text for inclusion inside a single-quoted POSIX shell
/// argument: each embedded `'` becomes `'\''` (close the quote, an
/// escaped literal quote, reopen the quote).
auto shell_single_quote_escape(std::string_view text) -> std::string {
    std::string escaped;
    escaped.reserve(text.size());
    for (char c : text) {
        if (c == '\'') {
            escaped += "'\\''";
        } else {
            escaped += c;
        }
    }
    return escaped;
}

/// Runs @p command via `popen` and returns everything it wrote to stdout,
/// or @c std::nullopt if the process could not be started at all.
auto capture_stdout(std::string const &command) -> std::optional<std::string> {
    FILE *pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        return std::nullopt;
    }
    std::string output;
    std::array<char, 4096> chunk{};
    std::size_t read = 0;
    while ((read = std::fread(chunk.data(), 1, chunk.size(), pipe)) > 0) {
        output.append(chunk.data(), read);
    }
    pclose(pipe);
    return output;
}

} // namespace

auto parse_dot_s_output(std::string_view text)
    -> std::optional<std::vector<gforth_cell>> {
    auto const open = text.find('<');
    if (open == std::string_view::npos) {
        return std::nullopt;
    }
    auto const close = text.find('>', open);
    if (close == std::string_view::npos) {
        return std::nullopt;
    }
    std::string_view const count_text =
        text.substr(open + 1, close - open - 1);
    if (count_text.empty()) {
        return std::nullopt;
    }
    for (char c : count_text) {
        if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
            return std::nullopt;
        }
    }
    int const count = std::atoi(std::string{count_text}.c_str());

    std::vector<gforth_cell> values;
    values.reserve(static_cast<std::size_t>(count));
    std::string_view rest = text.substr(close + 1);
    for (int i = 0; i < count; ++i) {
        // Skip leading whitespace, then read one (possibly negative)
        // decimal token.
        std::size_t pos = 0;
        while (pos < rest.size() &&
               std::isspace(static_cast<unsigned char>(rest[pos])) != 0) {
            ++pos;
        }
        std::size_t start = pos;
        if (pos < rest.size() && (rest[pos] == '-' || rest[pos] == '+')) {
            ++pos;
        }
        while (pos < rest.size() &&
               std::isdigit(static_cast<unsigned char>(rest[pos])) != 0) {
            ++pos;
        }
        if (pos == start) {
            return std::nullopt;
        }
        std::string_view const token = rest.substr(start, pos - start);
        values.push_back(
            static_cast<gforth_cell>(std::atoll(std::string{token}.c_str())));
        rest = rest.substr(pos);
    }
    return values;
}

auto gforth_version() -> std::optional<std::string> {
    // gforth writes `--version`'s own text to *stderr*, not stdout
    // (confirmed directly: `gforth --version 2>/dev/null` prints nothing;
    // `gforth --version 1>/dev/null` prints the version line) -- merge
    // stderr into the captured stream here rather than discarding it the
    // way `run_via_gforth`'s own command does.
    auto out = capture_stdout("gforth --version 2>&1");
    if (!out.has_value() || out->empty()) {
        return std::nullopt;
    }
    auto const newline = out->find('\n');
    return newline == std::string::npos ? *out : out->substr(0, newline);
}

auto run_via_gforth(std::string_view program) -> std::optional<gforth_result> {
    std::string const escaped = shell_single_quote_escape(program);
    std::string const command =
        "gforth -e '" + escaped + " .s bye' 2>/dev/null";
    auto out = capture_stdout(command);
    if (!out.has_value()) {
        return std::nullopt;
    }
    // `.s` is the last thing `<program> .s bye` ever prints (immediately
    // before `bye` exits), so its own "<n> ..." marker is the *last* `<` in
    // the captured stream -- searching from the end keeps this correct even
    // if `program`'s own ordinary output happens to contain an unrelated
    // `<` character earlier.
    auto const marker = out->rfind('<');
    if (marker == std::string::npos) {
        return std::nullopt;
    }
    auto stack = parse_dot_s_output(std::string_view{*out}.substr(marker));
    if (!stack.has_value()) {
        return std::nullopt;
    }
    return gforth_result{.output = out->substr(0, marker),
                         .stack = std::move(stack).value()};
}

} // namespace smd::forth::conformance
