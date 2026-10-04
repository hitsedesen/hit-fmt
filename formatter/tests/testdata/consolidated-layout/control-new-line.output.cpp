void f() {
  if (ready)
  {
    first();
  } else
  {
    second();
  }
}

void g() {
  if (ready)
  {
    first();
  } else // note
  {
    second();
  }
}

void h() {
  if (ready)
  {
    first();
  } else
    second();
}

void i() {
  if (ready)
    first();
  else
  {
    second();
  }
}
