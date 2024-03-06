//
//  relval_test.c
//  
//
//  Created by 吉野 智恒 on 2024/02/29.
//

#include <stdio.h>
#include <fcntl.h>
typedef long long int64_t;
typedef char*     string;

int main(int argc, const char* argv[]){
    /*--- Variables ----------------------*/
    int i=0;       /* all-purpose int */
    int64_t i64=0; /* all-purpose int */

    char*   sCmdName = argv[0]; /* name of this Command */
    printf("Command name : %s\n", sCmdName);
    
    /*--- read any argment ---------------*/
    for(i=1; i<argc; i++){
        printf("argv[%d] : %s\n", i, argv[i]);
    }
    
    return 0;
        　
}
