/// @file decision_guide.cpp
/// hs_decision_guide: from sweep results, generate docs/decision_guide.md.

#include "hybrid_sched/util.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

static void usage(const char* prog) {
    std::cerr << "Usage: " << prog << " <sweep_csv> [--out docs/decision_guide.md]\n";
}

static std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> fields;
    std::string cur;
    bool in_quote = false;
    for (char c : line) {
        if (c == '"') { in_quote = !in_quote; continue; }
        if (c == ',' && !in_quote) { fields.push_back(cur); cur.clear(); continue; }
        cur += c;
    }
    fields.push_back(cur);
    return fields;
}

static double safe_double(const std::string& s) {
    if (s == "NaN" || s.empty()) return std::numeric_limits<double>::quiet_NaN();
    try { return std::stod(s); } catch (...) { return std::numeric_limits<double>::quiet_NaN(); }
}

struct BestConfig {
    std::string variant;
    double best_time{std::numeric_limits<double>::max()};
    double best_edp{std::numeric_limits<double>::max()};
    std::string best_time_variant;
    std::string best_edp_variant;
};

int main(int argc, char** argv) {
    if (argc < 2) { usage(argv[0]); return 1; }

    std::string csv_path;
    std::string out_path = "docs/decision_guide.md";

    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--out" && i + 1 < argc) out_path = argv[++i];
        else if (arg[0] != '-') csv_path = arg;
    }

    if (csv_path.empty()) { usage(argv[0]); return 1; }

    std::ifstream f(csv_path);
    if (!f.is_open()) { std::cerr << "Cannot open: " << csv_path << "\n"; return 1; }

    // Parse CSV
    std::map<std::string, int> col_idx;
    std::map<std::string, BestConfig> per_kernel;
    bool first = true;

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        const auto fields = split_csv_line(line);
        if (first) {
            for (std::size_t i = 0; i < fields.size(); ++i)
                col_idx[hs::util::trim(fields[i])] = static_cast<int>(i);
            first = false;
            continue;
        }
        auto get = [&](const std::string& name) -> std::string {
            const auto it = col_idx.find(name);
            if (it == col_idx.end()) return "";
            if (static_cast<std::size_t>(it->second) >= fields.size()) return "";
            return fields[static_cast<std::size_t>(it->second)];
        };

        const std::string kernel  = get("kernel");
        const std::string variant = get("variant");
        const double time_s       = safe_double(get("time_s"));
        const double edp          = safe_double(get("edp"));

        if (kernel.empty() || variant.empty()) continue;

        auto& bc = per_kernel[kernel];
        if (!std::isnan(time_s) && time_s < bc.best_time) {
            bc.best_time = time_s;
            bc.best_time_variant = variant;
        }
        if (!std::isnan(edp) && edp < bc.best_edp) {
            bc.best_edp = edp;
            bc.best_edp_variant = variant;
        }
    }

    // Generate markdown
    std::ostringstream md;
    md << "# Decision Guide\n\n"
       << "> **Auto-generated** by `hs_decision_guide` from sweep results.\n"
       << "> Do not edit manually — rerun the tool after collecting new data.\n\n"
       << "## Per-Kernel Best Configurations\n\n"
       << "| Kernel | Best-Time Variant | Best-EDP Variant | Note |\n"
       << "|--------|------------------|------------------|------|\n";

    std::vector<std::string> kernels;
    for (const auto& [k, _] : per_kernel) kernels.push_back(k);
    std::sort(kernels.begin(), kernels.end());

    for (const auto& k : kernels) {
        const auto& bc = per_kernel.at(k);
        const std::string note =
            (bc.best_time_variant == bc.best_edp_variant)
            ? "same config for time and EDP"
            : "time and EDP optima differ";
        md << "| " << k
           << " | " << (bc.best_time_variant.empty() ? "N/A" : bc.best_time_variant)
           << " | " << (bc.best_edp_variant.empty()  ? "N/A" : bc.best_edp_variant)
           << " | " << note << " |\n";
    }

    md << "\n## Interpretation\n\n"
       << "- **Best-Time** variant: minimises mean wall-clock time across measured reps.\n"
       << "- **Best-EDP** variant: minimises mean Energy-Delay Product (E × T).\n"
       << "- Results are data-driven from sweep CSV: `" << csv_path << "`\n"
       << "- A `*` on a CI in the analysis summary means the difference is "
          "statistically significant (CI excludes 1.0).\n\n"
       << "## Caveats\n\n"
       << "- These results are specific to the machine, OS version, governor, "
          "power mode and thermal state when the sweep was run.\n"
       << "- Do not generalise to other machines or workloads without re-running.\n"
       << "- If RAPL was unavailable (NaN energy), EDP values are not trustworthy.\n";

    const std::string result = md.str();
    std::ofstream out(out_path);
    if (out.is_open()) {
        out << result;
        std::cout << "Decision guide written to " << out_path << "\n";
    } else {
        std::cerr << "Cannot write to " << out_path << "\n";
    }
    std::cout << result;
    return 0;
}
