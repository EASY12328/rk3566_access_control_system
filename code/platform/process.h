#pragma once
#include <functional>
#include <string>
#include <vector>

namespace platform {
using Cancel = std::function<bool()>;

struct ProcessResult {
    bool ok = false;
    std::string output;
    std::string error;
};

ProcessResult runProcess(const std::string &executable, const std::vector<std::string> &args,
                         const std::string &directory, const std::string &libraryDir,
                         const Cancel &cancel = {});
}
