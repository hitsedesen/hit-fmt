namespace Outer {
    enum class Role {
        Paragraph,
          Heading = 2,
        Caption
    };
    enum Plain {
      First,
      Second
    };
    void Work() {
        Invoke(value);
    }
    struct Record {
        int field;
    };
    namespace Inner {
        enum class Nested {
          One,
            Two
        };
    }
    enum class AlreadyIndented {
    Item
};
}
