#include "logifix.h"
#include "tty.h"
#include "config.h"
#include <regex>
#include <filesystem>
#include <future>
#include <stack>
#include <cctype>
#include <git2.h>
#include <iostream>
#include <set>
#include <tuple>
#include <mutex>
#include <nway.h>
#include <fmt/core.h>
#include <fmt/color.h>

#define PERF

using namespace std::string_literals;

extern std::vector<std::tuple<std::string, std::string, std::string>> rule_data;

namespace fs = std::filesystem;

struct options_t {
    bool accept_all;
    bool in_place;
    bool patch;
    std::set<std::string> files;
    std::set<std::string> accepted;
};

enum class key {
    down,
    left,
    ret,
    right,
    up,
    unknown
};

static const char* COLOR_BOLD       = "\033[1m";
static const char* COLOR_CYAN       = "\033[36m";
static const char* COLOR_GREEN      = "\033[32m";
static const char* COLOR_RED        = "\033[31m";
static const char* COLOR_RESET      = "\033[m";
static const char* TTY_CLEAR_TO_EOL = "\033[K";
static const char* TTY_CURSOR_UP    = "\033[A";
static const char* TTY_HIDE_CURSOR  = "\033[?25l";
static const char* TTY_SHOW_CURSOR  = "\033[?25h";

struct printer_opts {
    bool color;
    bool print_file_header;
};

bool string_has_only_whitespace(const std::string& str) {
    return std::all_of(str.begin(), str.end(), [](char c) { return std::isspace(c) != 0; });
}

std::vector<std::string> line_split(std::string str) {
    std::vector<std::string> result;
    while (!str.empty()) {
        size_t i;
        for (i = 0; i < str.size() - 1; i++) {
            if (str[i] == '\n') break;
        }
        result.emplace_back(str.substr(0, i + 1));
        str = str.substr(i + 1);
    }
    return result;
}

key get_keypress() {
    switch (getchar()) {
    case 0x0d: return key::ret;
    case  'k': return key::up;
    case  'j': return key::down;
    case  'h': return key::left;
    case  'l': return key::right;
    case 0x1b:
        switch (getchar()) {
            case 0x5b:
                switch (getchar()) {
                    case 0x41: return key::up;
                    case 0x42: return key::down;
                    case 0x44: return key::left;
                    case 0x43: return key::right;
                    default: break;
                }
                break;
            default: break;
        }
        break;
    default: break;
    }
    return key::unknown;
}

static int color_printer(const git_diff_delta* delta, const git_diff_hunk* hunk,
                         const git_diff_line* line, void* data) {


    auto* opts = (printer_opts*) data;

    /* Skip added empty lines since they will not be included in end result */
    auto* end = line->content;
    while (*end != '\0' && *end != '\n') end++;
    std::string content(line->content, end);
    if (line->origin == GIT_DIFF_LINE_ADDITION && string_has_only_whitespace(content)) {
        return 0;
    }

    if (!opts->print_file_header && line->origin == GIT_DIFF_LINE_FILE_HDR) {
        return 0;
    }

    if (!opts->print_file_header && line->origin == GIT_DIFF_LINE_HUNK_HDR) {
        return 0;
    }

    (void)delta;
    (void)hunk;

    if (opts->color) {
        switch (line->origin) {
            case GIT_DIFF_LINE_ADD_EOFNL:
            case GIT_DIFF_LINE_ADDITION:
                std::cout << COLOR_GREEN;
                break;
            case GIT_DIFF_LINE_DEL_EOFNL:
            case GIT_DIFF_LINE_DELETION:
                std::cout << COLOR_RED;
                break;
            case GIT_DIFF_LINE_FILE_HDR:
                std::cout << COLOR_BOLD;
                break;
            case GIT_DIFF_LINE_HUNK_HDR:
                std::cout << COLOR_CYAN;
                break;
            default:
                break;
        }
    }

    if (line->origin == GIT_DIFF_LINE_CONTEXT ||
        line->origin == GIT_DIFF_LINE_ADDITION ||
        line->origin == GIT_DIFF_LINE_DELETION) {
        std::cout << line->origin;
    }

    size_t i = 0;

    while (true) {
        if (line->content[i] == '\0' || line->content[i] == '\n' || line->content[i] == '\r') break;
        // render tab as four spaces
        if (line->content[i] == '\t') {
            std::cout << "    ";
        } else {
            std::cout << line->content[i];
        }
        i++;
    }

    std::cout << std::endl;

    if (opts->color) {
        std::cout << COLOR_RESET;
    }

    return 0;
}

std::string read_file(std::string_view path) {
    constexpr auto read_size = std::size_t {4096};
    auto stream = std::ifstream {path.data()};
    stream.exceptions(std::ios_base::badbit);
    auto out = std::string {};
    auto buf = std::string(read_size, '\0');
    while (stream.read(& buf[0], read_size)) {
        out.append(buf, 0, stream.gcount());
    }
    out.append(buf, 0, stream.gcount());
    return out;
}

void print_patch(std::string filename, std::string before, std::string after, printer_opts opts) {
    git_patch* patch = nullptr;
    git_patch_from_buffers(&patch, before.c_str(), before.size(),
                           filename.c_str(), after.c_str(),
                           after.size(), filename.c_str(), nullptr);
    git_patch_print(patch, color_printer, &opts);
    git_patch_free(patch);
}

void print_version_and_exit() {
    std::cout << PROJECT_VERSION << std::endl;
    std::exit(0);
}

options_t parse_options(int argc, char** argv) {

    options_t options = {
        .accept_all     = false,
        .in_place       = false,
        .patch          = false,
        .files          = {},
        .accepted       = {},
    };

    std::vector<std::tuple<std::string,std::function<void(std::string)>,std::string>> opts;

    auto print_usage = []() {
        fmt::print(fmt::emphasis::bold, "USAGE\n");
        fmt::print("  {} [flags] path [path ...]\n\n", PROJECT_NAME);
    };

    auto print_flags = [&opts]() {
        fmt::print(fmt::emphasis::bold, "FLAGS\n");
        for (const auto& [option, _, description] : opts) {
            std::cout << " " << std::setw(19) << std::left << option << description << std::endl;
        }
        std::cout << std::endl;
    };

    auto print_examples = []() {
        fmt::print(fmt::emphasis::bold, "EXAMPLES\n\n");
        fmt::print("  {} src/main src/test\n\n", PROJECT_NAME);
        fmt::print("  {} src/main --in-place --accept=1125,1155 Test.java\n\n", PROJECT_NAME);
    };

    auto parse_accepted = [&](std::string str) {
        std::stringstream ss(str);
        while (ss.good()) {
            std::string substr;
            std::getline(ss, substr, ',');
            options.accepted.emplace(substr);
        }
    };

    opts = {
        {"--accept-all",      [&](std::string str) { options.accept_all = true; },   "Accept all rewrites without asking"},
        {"--accept=<rules>",  [&](std::string str) { parse_accepted(str);  },        "Comma-separated list of rules to accept"},
        {"--in-place",        [&](std::string str) { options.in_place = true; },        "Disable interaction, rewrite files on disk"},
        {"--patch",           [&](std::string str) { options.patch = true; },        "Disable interaction, output a patch to stdout"},
        {"--help",            [&](std::string str) { print_usage(); print_flags(); print_examples(); std::exit(0); },     "Print this information and exit"},
        {"--version",         [&](std::string str) { print_version_and_exit(); },    "Print version information and exit"},
    };

    std::vector<std::string> arguments;
    for (auto i = 1; i < argc; i++) {
        arguments.emplace_back(argv[i]);
    }
    std::reverse(arguments.begin(), arguments.end());

    while (arguments.size()) {
        auto argument = arguments.back();
        if (argument.size() < 2 || argument.substr(0, 2) != "--") break;
        auto found = false;
        for (const auto [option, fn, description] : opts) {
            if (option.find("=") != std::string::npos) {
                auto opt_part = option.substr(0, option.find("=") + 1);
                if (argument.size() >= opt_part.size() && argument.substr(0, opt_part.size()) == opt_part) {
                    fn(argument.substr(opt_part.size()));
                    found = true;
                    break;
                }
            } else {
                if (argument.substr(0, option.size()) == option) {
                    fn("");
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            fmt::print("Error: Found invalid flag '{}'\n\n", argument);
            print_usage();
            print_flags();
            std::exit(1);
        }
        arguments.pop_back();
    }

    if (arguments.empty()) {
        print_usage();
        print_flags();
        print_examples();
        std::exit(0);
    }

    while (arguments.size()) {
        std::cmatch m;
        auto argument = arguments.back();
        if (!fs::exists(argument)) {
            fmt::print("Error: Path '{}' does not exist\n\n", argument);
            print_usage();
            std::exit(1);
        }
        if (fs::is_directory(argument)) {
            for (const auto& entry : fs::recursive_directory_iterator(argument)) {
                if (!entry.is_regular_file()) continue;
                if (entry.path().extension() != ".java") continue;
                std::string s = entry.path().lexically_normal();
                options.files.emplace(entry.path().lexically_normal());
            }
        } else {
            options.files.emplace(fs::path(std::move(argument)).lexically_normal());
        }
        arguments.pop_back();
    }

    return options;
}

int multi_choice(std::string question, std::vector<std::string> alternatives, bool exit_on_left = false) {
    tty_enable_cbreak_mode();
    std::cout << TTY_HIDE_CURSOR;
    fmt::print(fg(fmt::terminal_color::green) | fmt::emphasis::bold, "?");
    fmt::print(fmt::emphasis::bold, " {} ", question);
    if (exit_on_left) {
        fmt::print("[Use arrows to move, left to finish]");
    } else {
        fmt::print("[Use arrows to move]");
    }
    int cursor = 0;
    size_t scroll = 0;
    size_t height = 15;
    bool found = false;
    while (true) {
        if (cursor != -1 && cursor < scroll) {
            scroll = cursor;
        } else if (cursor != -1 && cursor == scroll + height) {
            scroll = cursor - height + 1;
        }
        for (auto i = scroll; i < std::min(alternatives.size(), scroll + height); i++) {
            if (cursor == i) {
                fmt::print("\n> {}", alternatives[i]);
            } else {
                fmt::print("\n  {}", alternatives[i]);
            }
            std::cout << TTY_CLEAR_TO_EOL;
        }
        if (found) {
            std::cout << std::endl;
            std::cout << std::endl;
            std::cout << TTY_SHOW_CURSOR;
            tty_disable_cbreak_mode();
            return cursor;
        }
        for (auto i = scroll; i < std::min(alternatives.size(), scroll + height); i++) {
            std::cout << TTY_CURSOR_UP;
        }
        auto key_press = get_keypress();
        if (key_press == key::left && exit_on_left) {
            cursor = -1;
            found = true;
        } else if (key_press == key::up && cursor > 0) {
            cursor--;
        } else if (key_press == key::down && cursor + 1 < alternatives.size()) {
            cursor++;
        } else if (key_press == key::ret || key_press == key::right) {
            found = true;
        }
    }
    return 0;
}

int main(int argc, char** argv) {

    options_t options = parse_options(argc, argv);

    git_libgit2_init();

    std::mutex thread_mutex;
    std::vector<std::thread> thread_pool;
    std::vector<std::tuple<std::string,int,std::string, bool>> rewrites;

    std::vector<std::string> file_stack(options.files.begin(), options.files.end());

#ifdef PERF
    std::vector<std::pair<double, std::string>> file_time;
    using std::chrono::high_resolution_clock;
    using std::chrono::duration_cast;
    using std::chrono::milliseconds;
#endif

    for (size_t i = 0; i < std::thread::hardware_concurrency(); i++) {
        thread_pool.emplace_back(
            std::thread([&] {
                while (true) {
                    std::string file;
                    {
                        std::lock_guard<std::mutex> lock(thread_mutex);
                        if (file_stack.empty()) return;
                        file = file_stack.back();
                        file_stack.pop_back();
                        std::cerr << "\r" << options.files.size() - file_stack.size() << "/" <<  options.files.size();
                    }
#ifdef PERF
                    std::chrono::time_point<std::chrono::high_resolution_clock> start = std::chrono::high_resolution_clock::now();
#endif
                    std::unique_ptr<logifix::program> program = std::make_unique<logifix::program>();
                    std::string input = read_file(file);
                    auto result = program->run(input, {});
                    program = nullptr;
#ifdef PERF
                    std::chrono::time_point<std::chrono::high_resolution_clock> end = std::chrono::high_resolution_clock::now();
                    auto diff = duration_cast<milliseconds>(end - start).count();
#endif
                    std::lock_guard<std::mutex> lock(thread_mutex);
#ifdef PERF
                    file_time.emplace_back(double(diff) / 1000.0f, file);
#endif
                    if (result.size() == 0) continue;
                    for (auto [output, rule] : result) {
                        if (output != input) {
                            rewrites.emplace_back(file, rule, output, false);
                        }
                    }
                }
            }));
    }

    for (auto& f : thread_pool) {
        f.join();
    }

#ifdef PERF
    std::sort(file_time.begin(), file_time.end());
    for (auto [t, f] : file_time) {
        printf("%.3f ", t);
        std::cerr << f << std::endl;
    }
#endif


    /* Sort rewrites by filename */
    std::sort(rewrites.begin(), rewrites.end(), [](const auto& a, const auto& b) -> bool {
        return std::get<0>(a) < std::get<0>(b);
    });

    auto review = [](auto& rw, size_t curr, size_t total) {
        auto& [filename, rule, rewrite, accepted] = rw;
        std::cout << "-----------------------------------------------------------" << std::endl;
        fmt::print(fmt::emphasis::bold, "\nRewrite {}/{} • {}\n\n", curr, total, filename);
        std::string input = read_file(filename);
        print_patch(filename, input, rewrite, {true, false});
        std::cout << std::endl;
        auto choice = multi_choice("What would you like to do?", {
            "Accept this rewrite",
            "Reject this rewrite"
        }, true);
        if (choice == -1) {
            return false;
        } else if (choice == 0) {
            accepted = true;
        } else if (choice == 1) {
            accepted = false;
        }
        return true;
    };

    if (!options.patch && !options.in_place) {

        while (true) {

            size_t selected_rewrites = std::count_if(rewrites.begin(), rewrites.end(), [](const auto& rewrite) {
                return std::get<3>(rewrite);
            });

            if (selected_rewrites > 0) {
                fmt::print(fmt::emphasis::bold,
                    "\nSelected {}/{} rewrites\n\n",
                    selected_rewrites,
                    rewrites.size());
            } else {
                fmt::print(fmt::emphasis::bold,
                    "\nFound {} rewrites\n\n",
                    rewrites.size());
            }

            auto selection = multi_choice("What would you like to do?", {
                "Review rewrites by rule",
                "Review rewrites by file",
                "Exit without doing anything",
            });

            if (selection == 2) break;

            while (true) {

                if (selection == 0) {
                    std::map<int, decltype(rewrites)> rules;
                    for (auto& rewrite : rewrites) {
                        rules[std::get<1>(rewrite)].emplace_back(rewrite);
                    }
                    std::vector<int> keys;
                    std::vector<std::string> options;
                    for (auto& [rule, rws] : rules) {
                        size_t accepted = 0;
                        for (auto rw : rws) {
                            if (std::get<3>(rw)) accepted++;
                        }
                        keys.emplace_back(rule);
                        std::string description = std::to_string(rule);
                        for (auto [squid, pmdid, desc] : rule_data) {
                            if (squid == "S" + std::to_string(rule)) {
                                description = fmt::format("{} • {}", desc, squid);
                                break;
                            }
                        }
                        std::string status;
                        if (accepted > 0) {
                            status = fmt::format(fg(fmt::terminal_color::green), " ({}/{})", accepted, rws.size());
                        } else {
                            status = fmt::format(" ({}/{})", accepted, rws.size());
                        }
                        options.emplace_back(description + status);
                    }
                    auto rule_selection = multi_choice("Which rule would you like to review?", options, true);
                    if (rule_selection == -1) break;
                    auto rule = keys[rule_selection];
                    auto curr = 1;
                    auto total = rules[rule].size();
                    for (auto& rw : rewrites) {
                        if (std::get<1>(rw) == rule) {
                            if (!review(rw, curr, total)) break;
                            curr++;
                        }
                    }
                } else if (selection == 1) {
                    /*std::map<std::string, decltype(rewrites)> files;
                    for (auto rewrite : rewrites) {
                        files[std::get<0>(rewrite)].emplace_back(rewrite);
                    }
                    std::vector<std::string> keys;
                    std::vector<std::string> options;
                    for (auto& [fn, rws] : files) {
                        keys.emplace_back(fn);
                        options.emplace_back(fn + " (" + std::to_string(rws.size()) + ")");
                    }
                    auto file_selection = multi_choice("Which file would you like to review?", options, true);
                    if (file_selection == -1) break;
                    auto file = keys[file_selection];
                    review(files[file]);*/
                } else {
                    break;
                }

            }
        }

    } else {
        if (options.accept_all) {
            for (auto& rw : rewrites) {
                std::get<3>(rw) = true;
            }
        }
        if (!options.accepted.empty()) {
            for (auto& rw : rewrites) {
                if (options.accepted.find(std::to_string(std::get<1>(rw))) != options.accepted.end()) {
                    std::get<3>(rw) = true;
                }
            }
        }
    }

    std::map<std::string, std::vector<std::string>> result;

    for (auto& [filename, rule, rewrite, accepted] : rewrites) {
        if (accepted) {
            result[filename].emplace_back(rewrite);
        }
    }

    for (auto [filename, rewrites] : result) {
        std::string before = read_file(filename);
        auto diff = nway::diff(before, rewrites);
        assert(!nway::has_conflict(diff));
        auto after = nway::merge(diff);
        /* post-processing, remove introduced empty lines */
        auto after_lines = line_split(after);
        auto before_lines = line_split(before);
        auto lcs = nway::longest_common_subsequence(after_lines, before_lines);
        std::string processed;
        for (size_t i = 0; i < after_lines.size(); i++) {
            if (lcs.find(i) == lcs.end() && string_has_only_whitespace(after_lines[i])) {
                continue;
            }
            processed += after_lines[i];
        }
        if (options.in_place) {
            std::ofstream f(filename);
            f << processed;
            f.close();
        } else if (options.patch) {
            print_patch(filename, before, processed, {false, true});
        } else {
            assert(false);
        }
    }

    return 0;
}
