namespace Example {
    /*
     * Documentation
     */
    void Run();
}
/* Neighbor
   documentation */
namespace Outer {
    namespace Inner {
        /* Nested
         * documentation */
        void Run();
    }
    /* Following
     * documentation */
}
namespace {
	/* Anonymous
	 * documentation */
	int value;
} // namespace Wrong
namespace Protected {
// hit-fmt off
    /* Preserved
     * documentation */
// hit-fmt on
    int value;
}
