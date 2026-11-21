#ifndef HWS_WORKSPACE_DIFF_H
#define HWS_WORKSPACE_DIFF_H

#include <algorithm>
#include <cstddef>
#include <vector>

namespace hws_workspace {
enum class DiffKind { Equal, Remove, Add };
struct DiffLine {
    DiffKind kind;
    size_t index; // Index in before for Equal/Remove, in after for Add.
};
struct DiffResult {
    std::vector<DiffLine> lines;
    bool simplified = false;
};

// Myers shortest-edit diff: sparse edits cost O((N+M)D), not an N*M table.
// Bound work and trace memory for adversarial, completely different files.
template<class Lines>
DiffResult CompareLines(const Lines& before, const Lines& after,
    size_t maxTraceEntries = 4000000, size_t maxWork = 25000000) {
    DiffResult result;
    size_t first = 0, endBefore = before.size(), endAfter = after.size();
    while (first < endBefore && first < endAfter && before[first] == after[first])
        result.lines.push_back({DiffKind::Equal, first++});
    while (endBefore > first && endAfter > first && before[endBefore - 1] == after[endAfter - 1]) {
        --endBefore;
        --endAfter;
    }
    const int n = static_cast<int>(endBefore - first);
    const int m = static_cast<int>(endAfter - first);
    if (!n || !m) {
        for (size_t i = first; i < endBefore; ++i) result.lines.push_back({DiffKind::Remove, i});
        for (size_t i = first; i < endAfter; ++i) result.lines.push_back({DiffKind::Add, i});
    } else {
        std::vector<std::vector<int>> trace;
        size_t entries = 0, work = 0;
        bool found = false, limited = false;
        for (int d = 0; d <= n + m && !found && !limited; ++d) {
            const size_t width = static_cast<size_t>(d) * 2 + 1;
            if (width > maxTraceEntries - entries) break;
            entries += width;
            std::vector<int> row(width, -1);
            for (int k = -d; k <= d; k += 2) {
                if (++work > maxWork) { limited = true; break; }
                int x = 0;
                if (d) {
                    const auto& previous = trace.back();
                    const auto at = [&](int diagonal) { return previous[diagonal + d - 1]; };
                    x = k == -d || (k != d && at(k - 1) < at(k + 1))
                        ? at(k + 1) : at(k - 1) + 1;
                }
                int y = x - k;
                while (x < n && y < m && before[first + x] == after[first + y]) {
                    ++x; ++y;
                    if (++work > maxWork) { limited = true; break; }
                }
                if (limited) break;
                row[k + d] = x;
                if (x >= n && y >= m) { found = true; break; }
            }
            trace.push_back(std::move(row));
        }
        if (found) {
            std::vector<DiffLine> reverse;
            int x = n, y = m;
            for (int d = static_cast<int>(trace.size()) - 1; d > 0; --d) {
                const int k = x - y;
                const auto& previous = trace[d - 1];
                const auto at = [&](int diagonal) { return previous[diagonal + d - 1]; };
                const int previousK = k == -d || (k != d && at(k - 1) < at(k + 1)) ? k + 1 : k - 1;
                const int previousX = at(previousK), previousY = previousX - previousK;
                while (x > previousX && y > previousY) {
                    reverse.push_back({DiffKind::Equal, first + --x});
                    --y;
                }
                if (x == previousX) reverse.push_back({DiffKind::Add, first + --y});
                else reverse.push_back({DiffKind::Remove, first + --x});
            }
            while (x > 0 && y > 0) {
                reverse.push_back({DiffKind::Equal, first + --x});
                --y;
            }
            result.lines.insert(result.lines.end(), reverse.rbegin(), reverse.rend());
        } else {
            result.simplified = true;
            for (size_t i = first; i < endBefore; ++i) result.lines.push_back({DiffKind::Remove, i});
            for (size_t i = first; i < endAfter; ++i) result.lines.push_back({DiffKind::Add, i});
        }
    }
    for (size_t i = endBefore; i < before.size(); ++i) result.lines.push_back({DiffKind::Equal, i});
    return result;
}
}
#endif
