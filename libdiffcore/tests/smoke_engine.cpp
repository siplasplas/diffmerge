#include <iostream>
#include <string>
#include <vector>

#include "internal/Diff.h"

using namespace diffcore::internal;

// Smoke test - verify templated engine gives same results as the original.
int main() {
    // Test 1: same example as original main.cpp
    {
        Diff<std::string> d("ABCABBA", "CBABAC");
        auto blocks = d.walk();
        std::cout << "Test 1 (ABCABBA vs CBABAC): edit distance="
                  << d.editDistance() << " swapped=" << d.swapped() << "\n";
        for (const auto& b : blocks) {
            const char* t = b.type == EditType::Equal ? "EQ" :
                            b.type == EditType::Insert ? "IN" : "DE";
            std::cout << "  " << t << " A[" << b.startX << ".." << b.endX
                      << ") B[" << b.startY << ".." << b.endY << ")\n";
        }
    }

    // Test 2: works on vector<int> too (the whole point of templating)
    {
        std::vector<int> a = {1, 2, 3, 4, 5};
        std::vector<int> b = {1, 2, 99, 4, 5};
        Diff<std::vector<int>> d(a, b);
        auto blocks = d.walk();
        std::cout << "\nTest 2 (vector<int>): edit distance="
                  << d.editDistance() << "\n";
        for (const auto& blk : blocks) {
            const char* t = blk.type == EditType::Equal ? "EQ" :
                            blk.type == EditType::Insert ? "IN" : "DE";
            std::cout << "  " << t << " A[" << blk.startX << ".." << blk.endX
                      << ") B[" << blk.startY << ".." << blk.endY << ")\n";
        }
    }

    // Test 3: edge case - empty A
    {
        Diff<std::string> d("", "abc");
        auto blocks = d.walk();
        std::cout << "\nTest 3 (empty A): edit distance="
                  << d.editDistance() << " blocks=" << blocks.size() << "\n";
    }

    // Test 4: edge case - identical
    {
        Diff<std::string> d("hello", "hello");
        auto blocks = d.walk();
        std::cout << "Test 4 (identical): edit distance="
                  << d.editDistance() << " blocks=" << blocks.size() << "\n";
    }

    return 0;
}
