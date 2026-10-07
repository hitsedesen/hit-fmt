void OpaqueBlocks() {
DEFER {
Consume();
};
if (Ready()) {
ON_EXIT {
  Reset();
  if (Ready()) {
    Notify();
  }
};
}
#if FEATURE
DEFER {
Consume();
};
#endif
ON_EXIT {
if (Ready()) {
  Consume();
}
};
ON_EXIT {
return;
};
ON_EXIT {
{
  Consume();
}
};
Consume(Value{1, 2});
Consume(Value{[] { Consume(); }});
// hit-fmt off
DEFER {
Consume();
};
// hit-fmt on
auto callback = [] {
Consume();
};
}
void AlreadyIndented() {
        DEFER {
            Consume();
        };
}
