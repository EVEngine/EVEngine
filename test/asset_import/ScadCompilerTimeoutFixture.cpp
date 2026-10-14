#include <chrono>
#include <thread>

// A controlled compiler process for the timeout contract; never produces geometry.
int main() {
    std::this_thread::sleep_for(std::chrono::seconds(10));
    return 1;
}
