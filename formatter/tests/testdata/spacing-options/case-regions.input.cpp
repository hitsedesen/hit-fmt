void a();
void b();
void c();
void regions(int value) {
 switch(value) {
case 0:
   case 1: a(); b(); break;
  case 2: { c(); } a(); break;
    case 3: while(value) {
 case 4: c();
 } break;
default: break;
 }
}
