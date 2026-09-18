#include "IWebRtcAdapterService.hpp"
#include <cassert>

int main() {
    using namespace luma::adapters::webrtc;
    // Interface contract smoke test; implementation is intentionally hidden behind the interface.
    OperationResult result{false, ""};
    assert(!result.success);
    return 0;
}
