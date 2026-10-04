void f() { use(alpha +
        beta, gamma, delta); }

void g() { auto x = (Type)(alpha, beta, gamma, delta); }

void h() { use(alpha, /* note */ beta, gamma); }
