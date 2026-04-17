#include <iostream>
#include <string>

int add(int a, int b) {
    return a + b;
}

int multiply(int a, int b) {
    return a * b;
}

int main() {
    int x = 1;
    int y = 2;
    std::cout << "sum: " << add(x, y) << std::endl;
    std::cout << "product: " << multiply(x, y) << std::endl;
    return 0;
}
