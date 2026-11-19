/// @file analyze.cpp
/// hs_analyze: aggregate CSVs into summary tables with speedup/energy/EDP ratios and CIs.

#include "hybrid_sched/stats.hpp"
#include "hybrid_sched/util.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

static void usage(const char* prog) {
    std::cerr << "Usage: " << prog << " <csv1> [csv2 ...] [--baseline VARIANT] [--out summary.md]\n"
              << "  --baseline  Prefix of the baseline variant (default: omp_static)\n"
              << "              Matches any variant whose label starts with this string.\n"
              << "  --out       Write markdown summary to this file.\n"
              << "  --help      Show this message.\n";
}

struct Row {
    std::string kernel;
    std::string variant;
    double time_s{0.0};
    double energy_j{0.0};
    double edp{0.0};
    double imbalance_pct{0.0};
};

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

int main(int argc, char** argv) {
    if (argc < 2) { usage(argv[0]); return 1; }

    std::vector<std::string> csv_paths;
    std::string baseline = "omp_static";
    std::string out_path = "";

    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--baseline" && i + 1 < argc) baseline = argv[++i];
        else if (arg == "--out" && i + 1 < argc) out_path = argv[++i];
        else if (arg == "--help" || arg == "-h") { usage(argv[0]); return 0; }
        else csv_paths.push_back(arg);
    }

    if (csv_paths.empty()) { usage(argv[0]); return 1; }

    // Parse all CSV files
    std::vector<Row> all_rows;

    for (const auto& path : csv_paths) {
        std::ifstream f(path);
        if (!f.is_open()) { std::cerr << "Cannot open: " << path << "\n"; continue; }
        std::string line;
        bool first = true;
        std::map<std::string, int> col_idx;
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
            Row r;
            r.kernel   = get("kernel");
            r.variant  = get("variant");
            r.time_s   = safe_double(get("time_s"));
            r.energy_j = safe_double(get("energy_j_gross"));
            r.edp      = safe_double(get("edp"));
            r.imbalance_pct = safe_double(get("imbalance_pct"));
            if (!r.kernel.empty() && !r.variant.empty())
                all_rows.push_back(r);
        }
    }

    if (all_rows.empty()) {
        std::cerr << "No data rows found.\n";
        return 1;
    }

    // Group by (kernel, variant)
    std::map<std::string, std::map<std::string, std::vector<Row>>> groups;
    for (const auto& r : all_rows)
        groups[r.kernel][r.variant].push_back(r);

    std::vector<std::string> kernels;
    for (const auto& [k, _] : groups) kernels.push_back(k);
    std::sort(kernels.begin(), kernels.end());

    std::ostringstream md;
    md << "# Analysis Summary\n\n";
    md << "Baseline variant: `" << baseline << "` (prefix match)\n\n";

    for (const auto& kname : kernels) {
        md << "## " << kname << "\n\n";
        md << "| variant | time_mean_s | time_CI95 | energy_mean_J | edp_mean | speedup_vs_baseline | edp_ratio_vs_baseline | imbalance_pct |\n";
        md << "|---------|------------|-----------|---------------|----------|--------------------|-----------------------|---------------|\n";

        const auto& variants_map = groups.at(kname);

        // Collect baseline times (pick the single variant that best matches the baseline prefix:
        // choose the variant whose label == baseline exactly, or if none, the first prefix match)
        std::vector<double> base_times;
        std::vector<double> base_edp;
        {
            // Try exact match first
            std::string best_match = "";
            for (const auto& [var, _] : variants_map) {
                if (var == baseline) { best_match = var; break; }
            }
            // Fall back to prefix match — but only the longest-matching variant
            if (best_match.empty()) {
                std::size_t best_len = 0;
                for (const auto& [var, _] : variants_map) {
                    if (var.rfind(baseline, 0) == 0 && var.size() > best_len) {
                        // prefer the one with the most threads (largest variant among matches)
                        // deterministic: pick the one that sorts last (highest thread count label)
                        if (best_match.empty() || var > best_match) {
                            best_match = var;
                            best_len = var.size();
                        }
                    }
                }
            }
            if (!best_match.empty() && variants_map.count(best_match)) {
                for (const auto& r : variants_map.at(best_match)) {
                    if (!std::isnan(r.time_s)) base_times.push_back(r.time_s);
                    if (!std::isnan(r.edp))    base_edp.push_back(r.edp);
                }
            }
        }

        std::vector<std::string> variant_names;
        for (const auto& [v, _] : variants_map) variant_names.push_back(v);
        std::sort(variant_names.begin(), variant_names.end());

        for (const auto& vname : variant_names) {
            const auto& rows = variants_map.at(vname);
            std::vector<double> times, energies, edps, imbs;
            for (const auto& r : rows) {
                if (!std::isnan(r.time_s))   times.push_back(r.time_s);
                if (!std::isnan(r.energy_j)) energies.push_back(r.energy_j);
                if (!std::isnan(r.edp))      edps.push_back(r.edp);
                if (!std::isnan(r.imbalance_pct)) imbs.push_back(r.imbalance_pct);
            }

            const auto t_ci  = hs::stats::ci95(times);
            const auto e_ci  = hs::stats::ci95(energies);
            const auto edp_ci = hs::stats::ci95(edps);
            const auto imb_ci = hs::stats::ci95(imbs);

            // Speedup = baseline_time / this_time (computed from time_s, not energy)
            std::string speedup_str = "N/A";
            if (!base_times.empty() && !times.empty()) {
                const auto spd_ci = hs::stats::bootstrap_ratio_ci(base_times, times, 42, 2000);
                std::ostringstream ss;
                ss << std::fixed << std::setprecision(3) << spd_ci.mean_val
                   << " [" << spd_ci.lo << ", " << spd_ci.hi << "]"
                   << (spd_ci.excludes_one() ? " *" : "");
                speedup_str = ss.str();
            }

            // EDP ratio (may be NaN if energy unavailable)
            std::string edp_ratio_str = "N/A";
            if (!base_edp.empty() && !edps.empty()) {
                const auto er_ci = hs::stats::bootstrap_ratio_ci(edps, base_edp, 42, 2000);
                std::ostringstream ss;
                ss << std::fixed << std::setprecision(3) << er_ci.mean_val
                   << " [" << er_ci.lo << ", " << er_ci.hi << "]"
                   << (er_ci.excludes_one() ? " *" : "");
                edp_ratio_str = ss.str();
            }

            auto fmt = [](double v) {
                if (std::isnan(v)) return std::string("NaN");
                std::ostringstream ss;
                ss << std::fixed << std::setprecision(4) << v;
                return ss.str();
            };

            md << "| " << vname
               << " | " << fmt(t_ci.mean_val)
               << " | ±" << fmt(t_ci.half_width)
               << " | " << fmt(e_ci.mean_val)
               << " | " << fmt(edp_ci.mean_val)
               << " | " << speedup_str
               << " | " << edp_ratio_str
               << " | " << fmt(imb_ci.mean_val)
               << " |\n";
        }
        md << "\n(*) CI excludes 1.0 — statistically significant difference from baseline.\n\n";
    }

    const std::string result = md.str();

    if (!out_path.empty()) {
        std::ofstream f(out_path);
        if (f.is_open()) {
            f << result;
            std::cout << "Summary written to " << out_path << "\n";
        } else {
            std::cerr << "Cannot write to " << out_path << "\n";
        }
    }
    std::cout << result;
    return 0;
}
