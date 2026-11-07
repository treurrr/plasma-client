#include "client/perfreport.hpp"

#include "mcpp/perf.hpp"

#include <filesystem>
#include <format>
#include <iostream>

namespace client {

int perfreport(const std::vector<std::string>& arguments) {
    std::string file;
    std::string out;
    bool graph = true;
    for (const auto& argument : arguments) {
        if (argument == "--no-graph") {
            graph = false;
        } else if (file.empty()) {
            file = argument;
        } else {
            out = argument;
        }
    }
    if (file.empty()) {
        std::cout << "usage: plasma-client perfreport <file.plperf> [out.png] [--no-graph]\n"
                     "\nA recording is made while playing: the frame recorder's key in the\n"
                     "Plasma profile, its line in the feature menu, or PLASMA_PERF=<file> to\n"
                     "record a session from the start. docs/LIVEPERF.md says how to compare a\n"
                     "number from here with vanilla's own, and what not to claim.\n";
        return 2;
    }
    const auto recording = mcpp::perf::read(file);
    if (!recording) {
        std::cerr << std::format("{}: not a plasma frame recording\n", file);
        return 1;
    }
    std::cout << std::format("plasma frame recording: {}\n\n", file);
    std::cout << mcpp::perf::report(*recording);
    if (!graph) return 0;
    std::filesystem::path image(out.empty() ? file : out);
    if (out.empty()) image.replace_extension(".png");
    if (!mcpp::perf::write_graph(image, *recording)) {
        std::cerr << "the graph was not written\n";
        return 1;
    }
    std::cout << std::format("\nframe time graph: {}\n", image.string());
    return 0;
}

} // namespace client
