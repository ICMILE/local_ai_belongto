#include "bili_api.h"
#include "utils.h"
#include "selection_server.h"

#include <curl/curl.h>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    curl_global_init(CURL_GLOBAL_DEFAULT);

    try {
        // 1. 登录信息
        LoginInfo login = FetchLoginInfo("http://localhost:19448/api/login");
        auto headers = BuildBiliHeaders(login);

        // 2. 拉历史
        const int maxPages = 10;
        std::cout << "fetching history (up to " << maxPages * 30 << " items)...\n";
        auto history = FetchAllHistory(headers, maxPages);
        std::cout << "fetched " << history.size() << " items\n\n";
        if (history.empty()) return 0;

        for (size_t i = 0; i < history.size(); i++) {
            std::cout << "[" << std::setw(3) << i << "] "
                      << FormatTime(history[i].viewAt) << " | "
                      << history[i].title << " | " << history[i].author << "\n";
        }

        std::cout << "\nfetched " << history.size()
                << " items, opening browser for selection...\n";

        auto sel = RunSelectionServer(history, headers, login.biliJct);

        if (!sel.submitted) {
            std::cout << "no selection submitted\n";
            return 0;
        }
        std::cout << "\ndone: " << sel.success << " deleted, " << sel.failed << " failed\n";
        return 0;

        // 3. 区间
        int a = 0, b = 0;
        std::cout << "\nEnter start index (A, newer): ";
        std::cin >> a;
        std::cout << "Enter end index (B, older): ";
        std::cin >> b;
        std::cin.ignore();

        if (a > b) std::swap(a, b);
        if (a < 0 || b >= (int)history.size()) { std::cerr << "invalid range\n"; return 1; }

        int count = b - a + 1;
        std::cout << "\nWill delete " << count << " items:\n";
        for (int i = a; i <= b; i++)
            std::cout << "  [" << i << "] " << history[i].title << "\n";

        std::cout << "\nType 'DELETE' to confirm: ";
        std::string confirm;
        std::getline(std::cin, confirm);
        if (confirm != "DELETE") { std::cout << "cancelled\n"; return 0; }

        // 4. 从后往前删
        int success = 0, failed = 0;
        std::vector<int> failedIdx;

        for (int i = b; i >= a; i--) {
            int step = b - i + 1;
            std::cout << "[" << step << "/" << count << "] deleting [" << i << "] "
                      << history[i].title << " ... ";
            std::cout.flush();

            bool ok = DeleteOne(headers, history[i], login.biliJct);
            if (!ok) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                ok = DeleteOne(headers, history[i], login.biliJct);
            }

            if (ok) { std::cout << "OK\n"; success++; }
            else    { std::cout << "FAILED\n"; failed++; failedIdx.push_back(i); }

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        std::cout << "\nDone: " << success << " deleted, " << failed << " failed\n";
        if (!failedIdx.empty()) {
            std::cout << "Failed indices:";
            for (int idx : failedIdx) std::cout << " " << idx;
            std::cout << "\n";
        }
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        curl_global_cleanup();
        return 1;
    }
}