struct C {
  NOTE(0) C(int n) : first{n}, second(n) { use(n); }
  NOTE(1) C(int n, int tag) try : first(n), second{tag} { use(n); } catch(...) { recover(); }
  int first;
  int second;
};
int tail(){return 1;}
