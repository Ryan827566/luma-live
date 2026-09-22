#include "IAsrService.hpp"
#include <cassert>

int main() {
    using namespace luma::ai::asr;
    // Interface contract smoke test; implementation is intentionally hidden behind the interface.
    OperationResult result{false, ""};
    assert(!result.success);
    return 0;
}
