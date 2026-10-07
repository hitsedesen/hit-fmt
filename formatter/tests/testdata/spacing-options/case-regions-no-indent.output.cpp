void a();
void b();
void c();
void regions(int value) {
   switch (value) {
   [[likely]] case 0:
      a();
   case 1:
      a();
      b();
      break;
   [[unlikely]] case 2:
      {
         c();
      }
      a();
      break;
   [[likely]]
   [[vendor::hint]]
   case 3:
      while (value) {
      [[unlikely]] case 4:
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
   [[likely]] case 7:
      {
         a();
      }
   [[unlikely]] case 8:
      [[likely]] if (value) {
         b();
      }
      c();
      break;
#if EXTRA_CASES
   [[likely]] case 10:
   [[unlikely]] case 11:
      b();
      break;
#endif
   case 9:
      {
         a();
      }
   [[unlikely]] default:
      break;
   }
}
