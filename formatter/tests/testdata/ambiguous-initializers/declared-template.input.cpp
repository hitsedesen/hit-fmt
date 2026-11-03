template<class T> struct Example {};
struct RecordInput {};
void Run() {
    Example<RecordInput> priceHistory(state->GetVeryLongDescriptivePriceHistory().begin(), state->GetVeryLongDescriptivePriceHistory().end());
}
