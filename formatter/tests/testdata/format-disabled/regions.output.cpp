void f() {
  int before=1 + 2;
  // hit-fmt off
    int untouched=3+4;  
    if(true){
      perform (  1,2 );
    }
  // hit-fmt on
  int between=5 + 6;
  /* clang-format off */
    int untouched_again=7+8;
    // This protected sentence deliberately exceeds the forty-column limit.
  /* clang-format on */
  int after=9 + 10;
  const char* text = "// hit-fmt off";
  int more=11 + 12;  // clang-format off
  // This active sentence deliberately exceeds the forty-column limit.
  // hit-fmt off
  // clang-format on
  int mixed=13 + 14;
  // hit-fmt on
  int following=15 + 16;
}
