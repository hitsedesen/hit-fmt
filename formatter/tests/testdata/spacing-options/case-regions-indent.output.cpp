void a();
void b();
void c();
void regions(int value) {
   switch (value) {
    case 0:
    case 1:
       a();
       b();
       break;
    case 2:
      {
         c();
      }
       a();
       break;
    case 3:
       while (value) {
        case 4:
           c();
       }
       break;
    case 5:
       a();
       {
          b();
          {
             c();
          }
       }
       a();
       {
          c();
       }
       break;
    case 6:
      {
         a();
      }
       {
          b();
       }
       c();
       break;
    default:
       break;
   }
}
