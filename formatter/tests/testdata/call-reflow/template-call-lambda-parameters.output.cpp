template <typename T>
void RegisterHandlers(T* router) {
    router->template SetHandler<int>([](
        int firstValue, int secondValue) {
        return firstValue + secondValue;
    });
    router->template ReinstallHandler<int>(
        [](int firstValue, int secondValue) {
            return firstValue + secondValue;
        });
    router->template SetHandler<
        TExportProgressCallback>([](
            int firstValue,
            int secondValue) {
            return firstValue + secondValue;
        });
    router->template ReinstallHandler<int>(
        Task<FirstType, SecondType>{
            firstValue, secondValue});
}
