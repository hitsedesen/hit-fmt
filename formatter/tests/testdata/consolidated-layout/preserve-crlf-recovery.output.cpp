#define RAW(x) x ## x  
namespace X {
	void f( int x,
	  int y ) {
// hit-fmt off
   broken(, @);
// hit-fmt on
		call(  x, y ); // keep 
	}
}
