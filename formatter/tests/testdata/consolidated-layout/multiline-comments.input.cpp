void comments() {
/* Plain text
   with relative indentation
     and a deeper line.

*/
        /** Documentation
         * keeps its stars aligned.
         */
    /* Already aligned
     * stays unchanged.
     */
	/* Tab-indented
	 * keeps the extra column.
		deeper
	 */
/* An outdented continuation
text at column zero
 */
int value; /* Trailing
              continuation
           */
if (value) {
/* Nested
 * nested continuation
 */
}
const char* raw = R"(first
  raw literal indentation
last)";
}
// hit-fmt off
void disabled() {
/* Protected
 * do not shift
 */
}
// hit-fmt on
#define MULTILINE /* directive comment \
  must stay unchanged */ 1
