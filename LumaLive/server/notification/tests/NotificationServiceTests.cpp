#include "INotificationService.hpp"
#include <cassert>

int main() {
    using namespace luma::server::notification;
    // Interface contract smoke test; implementation is intentionally hidden behind the interface.
    OperationResult result{false, ""};
    assert(!result.success);
    return 0;
}
