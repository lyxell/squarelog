#pragma once

#include "sjp/sjp.h"
#include <map>
#include <optional>

namespace logifix {

class program {
  private:
    std::unordered_map<std::string, std::string> source_code;
    std::unique_ptr<souffle::SouffleProgram> prog;

  public:
    program();
    void add_file(const char* filename);
    void add_string(const char* filename, const char* content);
    void run();
    std::vector<std::tuple<int, int, std::string, std::string>>
    get_possible_repairs(const char* filename);
    std::vector<std::tuple<std::string, std::string, int, int>>
    get_variables_in_scope(const char* filename);
    int get_root();
    std::tuple<std::string, int, int> get_node_properties(int id);
    std::vector<std::pair<std::string, int>> get_children(int node);
    std::vector<std::pair<std::string, std::vector<int>>>
    get_child_lists(int node);
};

} // namespace logifix
