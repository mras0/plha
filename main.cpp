#include <print>
#include <stdexcept>


int main()
{
    try {
        std::println("TODO!");

    } catch (const std::exception& e) {
        std::println("{}", e.what());
        return 1;
    }
}
