void render(bool Alpha, bool Beta, bool Gamma, bool Delta, bool Epsilon, bool Zeta) {
  while (Alpha && Beta && Gamma && Delta &&
         Epsilon && Zeta) {
    wrong();
    assert(Alpha && Beta && Gamma && Delta &&
           Epsilon && Zeta);
    record->longMemberFieldName =
      calculateValue();
    switch (0) {
    case 1:
        work();
        break;
    case 2: {
        if (!Alpha)
          fail();
        break;
      }
    case 3:
        if (Beta)
          return;
          [[fallthrough]];
    default:
        break;
    }
  }
}
int Values[] = {100000000, 200000000, 300000000,
                400000000, 500000000, 600000000};
namespace small {
int X;
}
namespace empty {
}
namespace withComment {
int X;
} // namespace withComment
namespace larger {
int X;
int Y;
} // namespace larger
