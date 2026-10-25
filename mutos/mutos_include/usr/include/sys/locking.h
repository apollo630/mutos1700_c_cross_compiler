/*
 *   Flag values for locking system call.
 */

#define LK_UNLK   0       /* unlock request       */
#define LK_LOCK   1       /* lock request         */
#define LK_NBLCK  2       /* non-blocking lock request    */
#define LK_RLCK   3       /* read permitted only lock request       */
#define LK_NBRLCK 4       /* non-blocking read only lock request  */
