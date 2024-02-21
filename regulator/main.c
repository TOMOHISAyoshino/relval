/*####################################################################
#
# RELVAL - Limit the Flow Rate of the UNIX Pipeline Like a Relief Valve
#
# USAGE   : relval [-c|-e|-z] [-d fd|file] ratelimit [file]
# Args    : file ........ Filepath to be sent ("-" means STDIN)
#                         The file MUST be a textfile and MUST have
#                         a timestamp at the first field to make the
#                         timing of flow. The first space character
#                         <0x20> of every line will be regarded as
#                         the field delimiter.
#                         And, the string from the top of the line to
#                         the charater will be cut before outgoing to
#                         the stdout.
#           ratelimit ... Dataflow limit. You can specify it by the following
#                         two methods.
#                           1. interval time
#                              * One line will be allowed to pass through
#                                in the time you specified.
#                              * The usage is "time[unit]."
$                                - "time" is the numerical part. You can
#                                  use an integer or a decimal.
#                                - "unit" is the part of the unit of time.
#                                  You can choose one of "s," "ms," "us,"
#                                  or "ns." The default is "s."
#                              * If you set "1.24ms," this command allows
#                                up to one line of the source textdata
#                                to pass through every 1.24 milliseconds.
#                           2. number per time
#                              * Text data of a specified number of lines
#                                are allowed to pass through in a specified
#                                time.
#                              * The usage is "number/time."
#                                - "number" is the part to specify the
#                                  numner of lines. You can set only a
#                                  natural number from 1 to 65535.
#                                - "/" is the delimiter to seperate parts.
#                                  You must insert any whitespace characters
#                                  before and after this slash letter.
$                                - "time" is the part that specifies the
#                                  period. The usage is the same as
#                                  the interval time we explained above.
#                              * If you set "10/1.5," this command allows
#                                up to 10 lines to pass through every 1.5
#                                seconds.
# Options : -c,-e,-z .... Specify the format for timestamp. You can choose
#                         one of them.
#                           -c ... "YYYYMMDDhhmmss[.n]" (default)
#                                  Calendar time (standard time) in your
#                                  timezone (".n" is the digits under
#                                  second. You can specify up to nano
#                                  second.)
#                           -e ... "n[.n]"
#                                  The number of seconds since the UNIX
#                                  epoch (".n" is the same as -x)
#                           -z ... "n[.n]"
#                                  The number of seconds since this
#                                  command has startrd (".n" is the same
#                                  as -x)
#           -d fd/file .. If you set this option, the lines that will be
#                         dropped will be sent to the specified file
#                         descriptor or file.
#                         * When you set an integer, this command regards
#                           it as a file descriptor number. If you want
#                           to specify the file in the current directory
#                           that has a numerical filename, you have to
#                           add "./" before the name, like "./3."
#                         * When you set another type of string, this
#                           command regards it as a filename.
#
# Retuen  : Return 0 only when finished successfully
#
# How to compile : cc -O3 -o __CMDNAME__ __SRCNAME__ -lrt
#                  (if it doesn't work)
# How to compile : cc -O3 -o __CMDNAME__ __SRCNAME__
#
# Written by Shell-Shoccar Japan (@shellshoccarjpn) on 2024-02-09
#
# This is a public-domain software (CC0). It means that all of the
# people can use this for any purposes with no restrictions at all.
# By the way, We are fed up with the side effects which are brought
# about by the major licenses.
#
# The latest version is distributed at the following page.
# https://github.com/ShellShoccar-jpn/misc-tools
#
####################################################################*/
//  Written by TOMOHISA Yoshino on 2024/02/07.
/*####################################################################
# Initial Configuration
####################################################################*/

/*=== Initial Setting ==============================================*/

/*--- headers ------------------------------------------------------*/
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#if defined(_POSIX_PRIORITY_SCHEDULING) && !defined(__OpenBSD__) && !defined(__APPLE__)
  #include <sched.h>
  #include <sys/resource.h>
#endif

/*--- macro constants ----------------------------------------------*/
/* Buffer size for a line-string */
#define LINE_BUF         80

/*--- macro func1: Calculate additon for time(timespec) + nanosec(int64_t)
 * [in]  (struct timespec)ts : time for the augend and it will be
 *                             overwritten by the result
 *       (int64_t)i8         : nano-second time for the addend
 * [ret] none                : The result will be written into the "ts"
 *                             itself.                                 */
#define tsadd(ts,i8) ts.tv_nsec+=i8%1000000000;ts.tv_sec+=ts.tv_nsec/1000000000+i8/1000000000;ts.tv_nsec%=1000000000

/*--- macro func1: Calculate subtraction for time(timespec) - nanosec(int64_t)
 * [in]  (struct timespec)ts : time for the minuend and it will be
 *                             overwritten by the result
 *       (int64_t)i8         : nano-second time for the subtrahend
 * [ret] none                : The result will be written into the "ts"
 *                             itself.                                 */
#define tssub(ts,i8) ts.tv_nsec-=i8%1000000000;ts.tv_sec-=(ts.tv_nsec<0)+i8/1000000000;ts.tv_nsec+=(ts.tv_nsec<0)*1000000000

/*--- macro func2: Calculate modulo for time(timespec) % nanosec(int64_t)
 * [in]  (struct timespec)ts : time for the dividend
 *       (int64_t)i8         : nano-second time for the divisor
 * [ret] (int64_t)           : Division remainder by "ts % i8"
                               in nano-second                          */
#define tsmod(ts,i8) ((((ts.tv_sec%i8)*(1000000000%i8))%i8+(ts.tv_nsec)%i8)%i8)

/*--- data type definitions ----------------------------------------*/
typedef struct timespec tmsp;

/*--- prototype functions ------------------------------------------*/

/*--- global variables ---------------------------------------------*/
char*   gpszCmdname; /* The name of this command                    */
int64_t gi8Intv = -1 ; /* "interval" parameter in the arguments     */
int64_t gi8Prem =  0 ; /* "premature" parameter in the arguments    */
int64_t gi8Mini =  0 ; /* "standby" parameter in the arguments      */
int giTimeResol =  0 ; /* 0:second(def) 3:millisec 6:microsec 9:nanosec */
int giFmtType   = 'c'; /* 'c':calendar-time (default)
                      'e':UNIX-epoch-time                           */
int giVerbose   =  0 ; /* speaks more verbosely by the greater number   */
int giPrio      =  1 ; /* -p option number (default 1)                  */

typedef enum{
    F_CALENDAR_TIME, //  0：カレンダー表記（デフォルト）
    F_UNIX_TIME,     //  1：UNIX時間
    F_SEC_DESIGNED   //  2:指定時刻からの経過ナノ秒
}OP_FRM_TSTMP;
/*=== Define the functions for printing usage and error ============*/

/*--- exit with usage ----------------------------------------------*/
void print_usage_and_exit(void) {
  fprintf(stderr,
	" USAGE   : %s [-c|-e|-z] [-d fd|file] ratelimit [file]\n"
	" Args    : file ........ Filepath to be sent (\"-\" means STDIN)\n"
	"                         The file MUST be a textfile and MUST have\n"
	"                         a timestamp at the first field to make the\n"
	"                         timing of flow. The first space character\n"
	"                         <0x20> of every line will be regarded as\n"
	"                         the field delimiter.\n"
	"                         And, the string from the top of the line to\n"
	"                         the charater will be cut before outgoing to\n"
	"                         the stdout.\n"
	"           ratelimit ... Dataflow limit. You can specify it by the following\n"
	"                         two methods.\n"
	"                           1. interval time\n"
	"                              * One line will be allowed to pass through\n"
	"                                in the time you specified.\n"
	"                              * The usage is \"time[unit].\"\n"
	"                                - \"time\" is the numerical part. You can\n"
	"                                  use an integer or a decimal.\n"
	"                                - \"unit\" is the part of the unit of time.\n"
	"                                  You can choose one of \"s,\" \"ms,\" \"us,\"\n"
	"                                  or \"ns.\" The default is \"s.\"\n"
	"                              * If you set \"1.24ms,\" this command allows\n"
	"                                up to one line of the source textdata\n"
	"                                to pass through every 1.24 milliseconds.\n"
	"                           2. number per time\n"
	"                              * Text data of a specified number of lines\n"
	"                                are allowed to pass through in a specified\n"
	"                                time.\n"
	"                              * The usage is \"number/time.\"\n"
	"                                - \"number\" is the part to specify the\n"
	"                                  numner of lines. You can set only a\n"
	"                                  natural number from 1 to 65535.\n"
	"                                - \"/\" is the delimiter to seperate parts.\n"
	"                                  You must insert any whitespace characters\n"
	"                                  before and after this slash letter.\n"
	"                                - \"time\" is the part that specifies the\n"
	"                                  period. The usage is the same as\n"
	"                                  the interval time we explained above.\n"
	"                              * If you set \"10/1.5,\" this command allows\n"
	"                                up to 10 lines to pass through every 1.5\n"
	"                                seconds.\n"
	" Options : -c,-e,-z .... Specify the format for timestamp. You can choose\n"
	"                         one of them.\n"
	"                           -c ... \"YYYYMMDDhhmmss[.n]\" (default)\n"
	"                                  Calendar time (standard time) in your\n"
	"                                  timezone (\".n\" is the digits under\n"
	"                                  second. You can specify up to nano\n"
	"                                  second.)\n"
	"                           -e ... \"n[.n]\"\n"
	"                                  The number of seconds since the UNIX\n"
	"                                  epoch (\".n\" is the same as -x)\n"
	"                           -z ... \"n[.n]\"\n"
	"                                  The number of seconds since this\n"
	"                                  command has startrd (\".n\" is the same\n"
	"                                  as -x)\n"
	"           -d fd/file .. If you set this option, the lines that will be\n"
	"                         dropped will be sent to the specified file\n"
	"                         descriptor or file.\n"
	"                         * When you set an integer, this command regards\n"
	"                           it as a file descriptor number. If you want\n"
	"                           to specify the file in the current directory\n"
	"                           that has a numerical filename, you have to\n"
	"                           add \"./\" before the name, like \"./3.\"\n"
	"                         * When you set another type of string, this\n"
	"                           command regards it as a filename.\n"
	"\n"
	" Retuen  : Return 0 only when finished successfully\n"
	"\n"
	" How to compile : cc -O3 -o __CMDNAME__ __SRCNAME__ -lrt\n"
	"                  (if it doesn't work)\n"
	" How to compile : cc -O3 -o __CMDNAME__ __SRCNAME__\n"
	"\n"
	" Written by Shell-Shoccar Japan (@shellshoccarjpn) on 2024-02-14\n"
	"\n"
	" This is a public-domain software (CC0). It means that all of the\n"
	" people can use this for any purposes with no restrictions at all.\n"
	" By the way, We are fed up with the side effects which are brought\n"
	" about by the major licenses.\n"
	"\n"
	" The latest version is distributed at the following page.\n"
	" https://github.com/ShellShoccar-jpn/misc-tools\n"
    ,gpszCmdname);
  exit(1);
}

/*--- print warning message ----------------------------------------*/
void warning(const char* szFormat, ...) {
  va_list va;
  va_start(va, szFormat);
  fprintf(stderr,"%s: ",gpszCmdname);
  vfprintf(stderr,szFormat,va);
  va_end(va);
  return;
}

/*--- exit with error message --------------------------------------*/
void error_exit(int iErrno, const char* szFormat, ...) {
  va_list va;
  va_start(va, szFormat);
  fprintf(stderr,"%s: ",gpszCmdname);
  vfprintf(stderr,szFormat,va);
  va_end(va);
  exit(iErrno);
}

/*=== Initialization ===============================================*/
int main(int argc, const char * argv[]) {
    /*--- Variables ----------------------------------------------------*/
    int          i;               /* all-purpose int                    */
    int64_t      i8;              /* all-purpose int64_t                */
    char*        psz;             /* all-purpose char*                  */
    OP_FRM_TSTMP nsCalendar = F_CALENDAR_TIME; /* Integer means format of timestamp */
    char*        fdOut;         /* File or Descriptor to Output   */
//    tmsp       tsT0;            /* Time this command booted ~ exiting */
//    tmsp       tsRep;           /* Time to report at exiting          */
//    char*      pszArg;          /* String to parsr an argument        */
//    struct tm* ptm;             /* a pointer of "tm" structure        */
//    char       szTs[LINE_BUF];  /* timestamp to be reported           */
//    char       szTim[LINE_BUF]; /* timestamp (year - sec)             */
//    char       szDec[LINE_BUF]; /* timestamp (under sec)              */
//    char       szTmz[LINE_BUF]; /* timestamp (timezone)               */

    /*--- Initialize ---------------------------------------------------*/
    if (clock_gettime(CLOCK_REALTIME,&tsT0) != 0) {
      error_exit(errno,"clock_gettime() at initialize: %s\n",strerror(errno));
    }
    
    /*--- Parse options which start by "-" -----------------------------*/
    while ((i=getopt(argc, argv, "cezd")) != -1) {
      switch (i) {
        case 'c': nsCalendar = F_CALENDAR_TIME; break;
        case 'e': nsCalendar = F_UNIX_TIME;     break;
        case 'z': nsCalendar = F_SEC_DESIGNED;  break;
        case 'd': fdOut      = ;                 break;
        default : print_usage_and_exit();
      }
    }
    
    gpszCmdname = argv[0];
    for (i=0; *(gpszCmdname+i)!='\0'; i++) {
      if (*(gpszCmdname+i)=='/') {gpszCmdname=gpszCmdname+i+1;}
    }
    if (setenv("POSIXLY_CORRECT","1",1) < 0) {
      error_exit(errno,"setenv() at initialization: \n", strerror(errno));
    }
    setlocale(LC_CTYPE, "");


    // insert code here...
    printf("Hello, World!\n");
    return 0;
}
