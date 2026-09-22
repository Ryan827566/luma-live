#include "ISfuClientService.hpp"
#include <cassert>

int main() {
    using namespace luma::client::sfu;
    // Interface contract smoke test; implementation is intentionally hidden behind the interface.
    OperationResult result{false, ""};
    assert(!result.success);
    return 0;
}
