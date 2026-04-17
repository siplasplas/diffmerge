#include <iostream>

int add(int a, int b) {
    return a + b;
}

int main() {
    int x = 1;
    int y = 2;
    std::cout << add(x, y) << std::endl;
    return 0;
}
