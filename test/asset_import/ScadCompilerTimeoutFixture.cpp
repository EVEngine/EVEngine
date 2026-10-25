// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "role": "probe",
  "default": "concurrency",
  "module": "common"
}
*/
// clang-format on

#include <chrono>
#include <thread>

// A controlled compiler process for the timeout contract; never produces geometry.
int main() {
    std::this_thread::sleep_for(std::chrono::seconds(10));
    return 1;
}
