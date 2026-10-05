#include "z.h"
#include "a.h"
void bad_before(int); // unavoidable_outside_comment_over_the_limit
void selected_bad(
int,
int) {}
void Good(bool ready) {
  if (ready)
    consume(
      "unavoidable_spilled_literal_over_the_limit");
  invoke("unavoidable_selected_literal_over_the_limit",right);
}
void bad_after(int) {}
const char* Outside = "unavoidable_outside_literal_over_the_limit";
