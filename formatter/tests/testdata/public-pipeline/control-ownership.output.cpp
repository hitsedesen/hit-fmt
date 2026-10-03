void run() {
  if (outer)
  {
  }
#if FEATURE
 if (first)
  {
  }
#else
 if (second)
  {
  } else if (third)
  {
  }
#endif
 else
  {
  }
}

void no_inner_else() {
  if (outer)
    work();
#if FEATURE
 if (first)
    act();
#else
 if (second)
    act();
#endif
 else
    finish();
}

void nested_branches() {
  if (outer)
  {
  }
#if FEATURE
#if OPTION
 if (first)
  {
  } else
  {
  }
#else
 if (second)
    act();
  else if (third)
    recover();
  else
    stop();
#endif
#else
 if (last)
  {
  } /* inner */ else
  {
  }
#endif
 /* parent */ else /* body */ {
  }
}

void dangling() {
  if (outer)
    if (inner)
      act();
    else
      recover();
}

void double_dangling() {
  if (outer)
    if (inner)
      act();
    else
      recover();
  else
    finish();
}

void braced_inner() {
  if (outer)
    if (inner)
    {
      act();
    } else
    {
      recover();
    }
  else
  {
    finish();
  }
}

void braced_outer() {
  if (outer)
  {
    if (inner)
      act();
    else
      recover();
  } else
    finish();
}

void else_chain() {
  if (first)
  {
  } else if (second)
  {
    if (inner)
    {
    } else
    {
    }
  } else
  {
  }
}
