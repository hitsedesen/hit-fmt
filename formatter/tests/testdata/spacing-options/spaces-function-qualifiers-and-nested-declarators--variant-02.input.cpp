struct S { int value; void f()const&; void g()&&; void h()volatile; };
int (*callback)(double);
int (S::*method)(double);
int S::*member;
