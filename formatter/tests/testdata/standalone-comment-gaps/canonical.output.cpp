// file note

namespace Scope {

// first
/* second */

int Step() {
    return 3;
}

/* next
 * stage */

int Finish() {
    return 4;
}

// end

}


struct Record {

    /// member documentation

    int value;
};


void Work() {

    // statement note

    int x = 1; // trailing
    call(
        // argument note
x);
    auto task = []() {

        // lambda body note

        return 2;
    };
}


void Only() {

    /* comment-only body */

}


export
// declaration header
int exported;
export
// block header
{

// exported body

int visible;
}
extern "C" {

// linkage body

int linked;
}
int mixed; /* trailing
 * continuation */

/* own line
 * interior */

// adjacent comment

int next;
#if ENABLED
// conditional body

int conditional;
#endif
// hit-fmt off
void Fixed() {
// untouched
int   value;
}
// hit-fmt on
// file end