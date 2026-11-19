#pragma once
/// @file csv_writer.hpp
/// @brief Simple CSV file writer with header management.

#include <fstream>
#include <string>
#include <vector>

namespace hs {
namespace bench {

class CsvWriter {
public:
    explicit CsvWriter(const std::string& path);
    ~CsvWriter();

    /// Write the header row (call once before any data rows).
    void write_header(const std::vector<std::string>& columns);

    /// Write one data row; values must match header column count.
    void write_row(const std::vector<std::string>& values);

    /// Flush to disk.
    void flush();

    [[nodiscard]] bool is_open() const { return file_.is_open(); }

private:
    std::ofstream file_;
    std::size_t n_cols_{0};
    bool header_written_{false};
};

} // namespace bench
} // namespace hs
