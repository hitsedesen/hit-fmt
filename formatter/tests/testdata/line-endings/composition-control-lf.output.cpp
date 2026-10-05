void flow(bool ready) {
  if (ready) {
    left();
  } else {
    right();
  }
  do {
    tick();
  } 
  while (ready);
  do {
    next();
  } while (ready);
}
