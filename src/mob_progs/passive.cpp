#include "passive.h"

#include <algorithm>
#include <cstdio>

namespace {

std::vector<std::string> wrap_words(const std::string& text, size_t width)
{
    std::vector<std::string> chunks;
    std::string current;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t space = text.find(' ', pos);
        std::string word = text.substr(pos, space == std::string::npos ? std::string::npos : space - pos);
        pos = space == std::string::npos ? text.size() : space + 1;
        while (word.size() > width) { /* a single over-long word is cut */
            if (!current.empty()) {
                chunks.push_back(current);
                current.clear();
            }
            chunks.push_back(word.substr(0, width));
            word = word.substr(width);
        }
        if (current.empty())
            current = word;
        else if (current.size() + 1 + word.size() <= width)
            current += " " + word;
        else {
            chunks.push_back(current);
            current = word;
        }
    }
    if (!current.empty() || chunks.empty())
        chunks.push_back(current);
    return chunks;
}

} // namespace

std::string format_vendor_list(const std::vector<vendor_list_row>& rows)
{
    std::vector<std::string> names;
    size_t width = 0;
    for (const vendor_list_row& row : rows) {
        std::string name = row.name;
        if (row.left >= 0)
            name += " (" + std::to_string(row.left) + " left)";
        width = std::max(width, std::min(name.size(), VENDOR_LIST_NAME_COLUMN_MAX));
        names.push_back(name);
    }
    std::string out;
    for (size_t i = 0; i < rows.size(); ++i) {
        std::vector<std::string> chunks = wrap_words(names[i], VENDOR_LIST_NAME_COLUMN_MAX);
        size_t lines = std::max(chunks.size(), rows[i].costs.size());
        for (size_t l = 0; l < lines; ++l) {
            char number[8];
            snprintf(number, sizeof(number), "%2d. ", (int)(i + 1));
            std::string line = l == 0 ? number : "    ";
            std::string chunk = l < chunks.size() ? chunks[l] : "";
            line += chunk;
            line.append(width - chunk.size() + 2, ' ');
            if (l < rows[i].costs.size())
                line += std::to_string(rows[i].costs[l].qty) + " x " + rows[i].costs[l].name;
            line.erase(line.find_last_not_of(' ') + 1);
            out += line + "\n\r";
        }
    }
    return out;
}

std::vector<vendor_shortfall> vendor_shortfalls(const std::vector<vendor_cost>& costs,
    const std::function<int(int)>& have_count)
{
    std::vector<vendor_shortfall> short_of;
    for (const vendor_cost& cost : costs) {
        int have = have_count(cost.obj_vnum);
        if (have < cost.qty)
            short_of.push_back({ cost.obj_vnum, cost.qty, have });
    }
    return short_of;
}
