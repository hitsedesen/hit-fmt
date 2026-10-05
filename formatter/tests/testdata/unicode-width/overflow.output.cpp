void Demo() {
    Sink("Привет",
         value);
    Sink("abcdef",
         value);
    Sink("日本🙂", value);
    Sink("abc", value);
    Sink("é", value);
    Sink("ab", value);
    Sink(U'🙂', value);
    Sink(R"(日本🙂)",
         value);
    Приём("日本",
        first_argument,
        second_argument);
    Sink(
        first_argument,
         Содержимое);
    Outer("🙂", Inner(
        "é", left,
        right));
    Sink("Привет",
         value);
    Sink(value); // 日本🙂
    // hit-fmt off
    Sink("ПРИВЕТ",    value);
    // hit-fmt on
}
void Приём(int first,
           int second);
void ОбработкаДанных(
    int a, int b);
using Имя = Wrapper<
    Тип, ДругойТип>;
