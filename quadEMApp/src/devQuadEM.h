/* devQuadEM.h

    Author: Mark Rivers
    Created:  April 21, 2003, based on devAiIp330Scan.cc

*/

/*  These are the commands that are sent in messages between device support and */
/*  the MPF server */
#define READ_CURRENT     0
#define READ_SUM         1
#define READ_DIFFERENCE  2
#define READ_POSITION    3
#define WRITE_GAIN       4
#define WRITE_GO         5
#define WRITE_CONVERSION 6
#define WRITE_PULSE      7
#define WRITE_PERIOD     8
#define WRITE_OFFSET     9
#define COMPUTE_AVERAGE  10
#define PING_PONG        11
